"""1C HTTP/XLSX and embedded editor checks. Test-only deps: openpyxl, Playwright."""
import io,json,socket,sqlite3,subprocess,sys,tempfile,time,urllib.request,urllib.error
from pathlib import Path
from openpyxl import load_workbook
from playwright.sync_api import sync_playwright,expect
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(sys.argv[1]).resolve();CHROMIUM=sys.argv[2] if len(sys.argv)>2 else '/usr/bin/chromium'
with socket.socket() as s:s.bind(('127.0.0.1',0));PORT=s.getsockname()[1]
BASE=f'http://127.0.0.1:{PORT}'
def call(path,body=None,status=200,raw=False):
 req=urllib.request.Request(BASE+path,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'})
 try:
  with urllib.request.urlopen(req,timeout=10) as r:code,data=r.status,r.read()
 except urllib.error.HTTPError as e:code,data=e.code,e.read()
 assert code==status,(path,code,data)
 return data if raw else json.loads(data)
def choose(page,row,col,double=False):
 rect=page.evaluate('([r,c])=>{const d=repairSheet.sheet.data,v=d.cellRect(r,c);return {x:d.cols.indexWidth+v.left+Math.min(v.width/2,20),y:d.rows.height+v.top+Math.min(v.height/2,12)}}',[row,col])
 box=page.locator('.x-spreadsheet-overlayer').bounding_box();getattr(page.mouse,'dblclick' if double else 'click')(box['x']+rect['x'],box['y']+rect['y'])
with tempfile.TemporaryDirectory(prefix='repair-onec-browser-') as temp:
 root=Path(temp);db=root/'old.db';book=load_workbook(ROOT/'templates/blank.xlsx');book.active['J5']='Код 1С: 0007';buffer=io.BytesIO();book.save(buffer)
 with sqlite3.connect(db) as connection:
  connection.executescript("CREATE TABLE platform_types(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT UNIQUE,next_product_id TEXT,created_at TEXT DEFAULT 'old');CREATE TABLE cards(id INTEGER PRIMARY KEY AUTOINCREMENT,product_id TEXT,platform_type_id INTEGER,workbook BLOB,created_at TEXT DEFAULT 'old',document_json TEXT,document_revision INTEGER DEFAULT 0,document_updated_at TEXT);CREATE TABLE app_settings(key TEXT PRIMARY KEY,value TEXT);INSERT INTO platform_types(name,next_product_id)VALUES('Test','V00001');PRAGMA user_version=3;")
  connection.execute("INSERT INTO cards(product_id,platform_type_id,workbook)VALUES('old',1,?)",(buffer.getvalue(),))
 config=root/'config.json';config.write_text(json.dumps({'host':'127.0.0.1','port':PORT,'database':str(db),'template':str(ROOT/'templates/blank.xlsx'),'web':str(ROOT/'web')}))
 log=(root/'server.log').open('w+');process=subprocess.Popen([str(EXE),'--config',str(config)],stdout=log,stderr=log)
 try:
  for _ in range(100):
   if process.poll() is not None:log.seek(0);raise AssertionError(log.read())
   try:call('/api/health');break
   except urllib.error.URLError:time.sleep(.05)
  assert call('/api/cards/1')['one_c_code']=='0007'
  with sqlite3.connect(db) as connection:assert connection.execute('PRAGMA user_version').fetchone()[0]==4
  call('/api/batches',{'platform_type_id':1,'first_product_id':'V00001','count':10},status=201)
  selected=[{'id':n,'revision':0} for n in range(2,12)]
  groups=[{'count':2,'code':'123'},{'count':7,'code':'1234'},{'count':1,'code':'12345'}]
  assigned=call('/api/labels/assign',{'cards':selected,'groups':groups})
  assert [c['one_c_code'] for c in assigned]==['123']*2+['1234']*7+['12345']
  call('/api/labels/assign',{'cards':selected,'groups':groups},status=409)
  before=call('/api/cards/2')
  call('/api/labels/assign',{'cards':[{'id':2,'revision':1},{'id':999,'revision':0}],'groups':[{'count':2,'code':'bad'}]},status=404)
  assert call('/api/cards/2')==before
  with sync_playwright() as p:
   browser=p.chromium.launch(executable_path=CHROMIUM,headless=True,args=['--no-sandbox']);context=browser.new_context(viewport={'width':1500,'height':1000})
   context.add_init_script('window.__hostMessages=[];window.chrome=window.chrome||{};window.chrome.webview={postMessage:x=>window.__hostMessages.push(x)}')
   page=context.new_page();errors=[];page.on('pageerror',lambda e:errors.append(str(e)));page.goto(BASE+'/edit/2?embedded=1');expect(page.locator('#state')).to_contain_text('Бланк открыт')
   assert page.locator('body').evaluate('e=>e.classList.contains("embedded")')
   assert page.evaluate('repairSheet.cell(4,9).repairField')=='one_c_code'
   assert page.evaluate('repairSheet.cell(4,9).text')=='Код 1С: 123'
   choose(page,4,9);page.locator('#insert-column').click();page.locator('#insert-row').click()
   assert page.evaluate('repairSheet.cell(5,10).repairField')=='one_c_code'
   choose(page,5,10,True);editor=page.locator('.x-spreadsheet-editor textarea');editor.fill('Код 1С: 00099');editor.press('Control+s');expect(page.locator('#state')).to_have_text('Бланк сохранен.')
   assert call('/api/cards/2')['one_c_code']=='00099'
   book=load_workbook(io.BytesIO(call('/api/cards/2/workbook',raw=True)));assert book.active['K6'].value=='Код 1С: 00099'
   name=book.defined_names['RepairCards_OneCCode'];assert '$K$6' in name.attr_text,name.attr_text
   assert page.evaluate('window.__hostMessages.some(x=>x.type==="state"&&x.dirty)')
   assert page.evaluate('window.__hostMessages.at(-1).dirty') is False
   current=call('/api/cards/2');call('/api/labels/assign',{'cards':[{'id':2,'revision':current['document_revision']}],'groups':[{'count':1,'code':'555'}]})
   stored=call('/api/cards/2/document')['document'];assert stored['sheets'][0]['rows']['5']['cells']['10']['text']=='Код 1С: 555'
   book=load_workbook(io.BytesIO(call('/api/cards/2/workbook',raw=True)));assert book.active['K6'].value=='Код 1С: 555'
   page.reload();expect(page.locator('#state')).to_contain_text('Бланк открыт');choose(page,5,10,True);editor=page.locator('.x-spreadsheet-editor textarea');editor.fill('Код 1С: 556');editor.press('Enter');page.evaluate('window.repairSaveAndClose()');page.wait_for_function('window.__hostMessages.some(x=>x.type==="save-close"&&x.ok)')
   reader=context.new_page();reader.goto(BASE+'/cards/2?embedded=1');expect(reader.locator('#state')).to_contain_text('только для просмотра');expect(reader.locator('#save')).not_to_be_visible();assert reader.evaluate('repairSheet.cell(5,10).text')=='Код 1С: 556'
   assert not errors,errors;browser.close()
  print('1C browser checks passed: v3 migration/backfill, 2/7/1 HTTP allocation, rollback, moved cell binding, named XLSX field, SQLite sync, embedded dirty/save-close and Reader view')
 finally:
  if process.poll() is None:process.terminate();process.wait(timeout=10)
  log.close()
