"""Actual editor/Reader windows: queued merging, live updates, drafts and history."""
import io,json,pathlib,socket,subprocess,sys,tempfile,time,urllib.request,urllib.error,re
from openpyxl import load_workbook
from playwright.sync_api import sync_playwright,expect
ROOT=pathlib.Path(__file__).resolve().parents[1];EXE=pathlib.Path(sys.argv[1]).resolve();CHROMIUM=sys.argv[2] if len(sys.argv)>2 else '/usr/bin/chromium'
with socket.socket() as s:s.bind(('127.0.0.1',0));PORT=s.getsockname()[1]
BASE=f'http://127.0.0.1:{PORT}'
def call(path,body=None):
    request=urllib.request.Request(BASE+path,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(request,timeout=10) as r:return json.loads(r.read())
def edit(page,row,col,value,commit=True):
    rect=page.evaluate('([r,c])=>{const d=repairSheet.sheet.data,v=d.cellRect(r,c);return {x:d.cols.indexWidth+v.left+20,y:d.rows.height+v.top+12}}',[row,col])
    box=page.locator('.x-spreadsheet-overlayer').bounding_box();page.mouse.dblclick(box['x']+rect['x'],box['y']+rect['y'])
    field=page.locator('.x-spreadsheet-editor textarea');field.fill(value)
    if commit:field.press('Enter')
def saved(page):
    page.locator('#save').click();expect(page.locator('#state')).to_have_text('Бланк сохранен.')
def value(page,row,col,text):
    page.wait_for_function('([r,c,text])=>window.repairSheet&&repairSheet.cell(r,c)?.text===text',arg=[row,col,text])
with tempfile.TemporaryDirectory(prefix='repair-multi-editor-') as temp:
    root=pathlib.Path(temp);config=root/'config.json';config.write_text(json.dumps({'host':'127.0.0.1','port':PORT,'database':str(root/'repair.sqlite3'),'template':str(ROOT/'templates/blank.xlsx'),'web':str(ROOT/'web')}))
    log=(root/'server.log').open('w+');process=subprocess.Popen([str(EXE),'--config',str(config)],stdout=log,stderr=log)
    try:
        for _ in range(100):
            if process.poll() is not None:log.seek(0);raise AssertionError(log.read())
            try:call('/api/health');break
            except urllib.error.URLError:time.sleep(.05)
        platform=call('/api/platforms',{'name':'Multi editor'});call('/api/batches',{'platform_type_id':platform['id'],'first_product_id':'V00001','count':1})
        with sync_playwright() as pw:
            browser=pw.chromium.launch(executable_path=CHROMIUM,headless=True,args=['--no-sandbox'])
            first=browser.new_context(viewport={'width':1400,'height':1000});second=browser.new_context(viewport={'width':1400,'height':1000},accept_downloads=True);view=browser.new_context(viewport={'width':1400,'height':1000})
            a=first.new_page();b=second.new_page();reader=view.new_page();errors=[]
            for page in [a,b,reader]:page.on('pageerror',lambda error:errors.append(str(error)))
            for page in [a,b]:page.goto(BASE+'/edit/1?embedded=1');expect(page.locator('#state')).to_contain_text('Бланк открыт')
            reader.goto(BASE+'/cards/1?embedded=1');expect(reader.locator('#state')).to_contain_text('только для просмотра')
            # Force stale state in one editor, proving the server merges it.
            b.route('**/api/cards/1/document?after=*',lambda route:route.fulfill(json={'changed':False,'revision':0}))
            edit(a,8,5,'First user saved');edit(b,9,5,'Second user other cell');saved(a);saved(b)
            assert b.evaluate('repairSheet.cell(8,5).text')=='First user saved'
            stored=call('/api/cards/1/document');assert stored['revision']==2
            assert stored['document']['sheets'][0]['rows']['8']['cells']['5']['text']=='First user saved'
            assert stored['document']['sheets'][0]['rows']['9']['cells']['5']['text']=='Second user other cell'
            reader.bring_to_front();value(reader,9,5,'Second user other cell')
            # Same cell: the later queued save wins, with no conflict dialog.
            edit(a,8,5,'Earlier queued same cell');saved(a)
            edit(b,8,5,'Later queued same cell');saved(b)
            assert call('/api/cards/1/document')['document']['sheets'][0]['rows']['8']['cells']['5']['text']=='Later queued same cell'
            b.unroute('**/api/cards/1/document?after=*');a.bring_to_front();value(a,8,5,'Later queued same cell')
            # Keep a textarea open while a colleague saves another cell.
            edit(b,10,5,'Still typing local draft',commit=False)
            edit(a,11,5,'Remote update while typing');saved(a)
            b.bring_to_front();b.wait_for_timeout(1500)
            expect(b.locator('.x-spreadsheet-editor textarea')).to_have_value('Still typing local draft')
            expect(b.locator('.x-spreadsheet-editor textarea')).to_be_visible()
            b.locator('.x-spreadsheet-editor textarea').press('Enter');value(b,11,5,'Remote update while typing')
            assert b.evaluate('repairSheet.cell(10,5).text')=='Still typing local draft'
            assert call('/api/cards/1/document')['document']['sheets'][0]['rows'].get('10',{}).get('cells',{}).get('5',{}).get('text')!='Still typing local draft'
            with b.expect_download() as download:b.locator('#export-draft').click()
            saved_file=root/'my-edits.xlsx';download.value.save_as(saved_file);assert load_workbook(saved_file).active['F11'].value=='Still typing local draft'
            saved(b);reader.bring_to_front();value(reader,10,5,'Still typing local draft')
            # Simulate a committed save whose HTTP reply is lost.
            edit(b,12,5,'Lost reply edit')
            before=call('/api/cards/1/document')['revision']
            def lost_reply(route):route.fetch();route.abort('failed')
            b.route('**/api/cards/1/save',lost_reply);b.locator('#save').click();expect(b.locator('#state')).to_contain_text('Нет ответа сервера')
            b.unroute('**/api/cards/1/save');b.evaluate('window.repairSaveAndClose()')
            b.wait_for_function('document.getElementById("save").disabled')
            assert call('/api/cards/1/document')['revision']==before+1
            # Historical XLSX and restoration are real, not a UI-only list.
            versions=call('/api/cards/1/history');assert len(versions)==before+2
            b.on('dialog',lambda dialog:dialog.accept());b.locator('#history').click()
            initial=b.locator('.history-row').filter(has_text=re.compile(r'Версия 0 ·'))
            with b.expect_download() as download:initial.get_by_text('Скачать XLSX').click()
            original=root/'original.xlsx';download.value.save_as(original);assert load_workbook(original).active['F9'].value!='First user saved'
            target=b.locator('.history-row').filter(has_text=re.compile(r'Версия 1 ·'))
            with b.expect_response(lambda response:response.url.endswith('/api/cards/1/restore') and response.request.method=='POST') as restored:target.get_by_text('Восстановить',exact=True).click()
            assert restored.value.status==200
            expect(b.locator('#state')).to_contain_text('Бланк открыт')
            assert b.evaluate('repairSheet.cell(8,5).text')=='First user saved'
            assert call('/api/cards/1/document')['revision']==before+2
            reader.bring_to_front();value(reader,8,5,'First user saved')
            # Renderer kept worksheet formatting and the explicit code field.
            with urllib.request.urlopen(BASE+'/api/cards/1/workbook') as response:book=load_workbook(io.BytesIO(response.read()))
            assert book.active['F9'].value=='First user saved' and 'B1:L1' in book.active.merged_cells
            assert 'RepairCards_OneCCode' in book.defined_names
            # Initial history has the original XLSX but no saved JSON yet.
            # Await the POST, not an already-visible pre-navigation status.
            b.locator('#history').click()
            initial=b.locator('.history-row').filter(has_text=re.compile(r'Версия 0 ·'))
            with b.expect_response(lambda response:response.url.endswith('/api/cards/1/restore') and response.request.method=='POST') as restored:initial.get_by_text('Восстановить',exact=True).click()
            assert restored.value.status==200
            b.wait_for_function('()=>window.repairSheet&&(repairSheet.cell(8,5)?.text||"")!=="First user saved"')
            assert call('/api/cards/1/document')['revision']==before+3
            with urllib.request.urlopen(BASE+'/api/cards/1/workbook') as response:initial_book=load_workbook(io.BytesIO(response.read()))
            assert initial_book.active['B3'].value=='#ID V00001' and initial_book.active['F9'].value!='First user saved'
            assert 'RepairCards_OneCCode' in initial_book.defined_names
            assert not errors,errors
            first.close();second.close();view.close();browser.close()
        print('Live multi-editor checks passed: stale snapshots merge, later shared cell wins, Reader/editor refresh, active typing/draft preserved, XLSX export, lost reply idempotency, history download/restore and formatting')
    finally:
        if process.poll() is None:process.terminate();process.wait(timeout=10)
        log.close()
