"""Real HTTP concurrency and online backup; Python standard library only."""
import base64, concurrent.futures, json, pathlib, socket, sqlite3, subprocess, sys, tempfile, threading, time, urllib.error, urllib.request,io,zipfile,xml.etree.ElementTree as ET
from contextlib import closing
ROOT=pathlib.Path(__file__).resolve().parents[1]
EXE=pathlib.Path(sys.argv[1]).resolve();BACKUP=pathlib.Path(sys.argv[2]).resolve()
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
with tempfile.TemporaryDirectory(prefix='repair-multi-user-') as temp:
    root=pathlib.Path(temp);db=root/'данные'/'repair.sqlite3';config=root/'сервер.json';base=f'http://127.0.0.1:{port()}'
    settings={'host':'127.0.0.1','port':int(base.rsplit(':',1)[1]),'database':str(db),'template':str(ROOT/'templates/blank.xlsx'),'web':str(ROOT/'web')}
    config.write_text(json.dumps(settings),encoding='utf-8');log=(root/'server.log').open('w+');process=None
    def call(path,body=None,method=None):
        request=urllib.request.Request(base+path,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'},method=method)
        try:
            with urllib.request.urlopen(request,timeout=30) as r:return r.status,json.loads(r.read())
        except urllib.error.HTTPError as e:return e.code,json.loads(e.read())
    def parallel(actions):
        barrier=threading.Barrier(len(actions))
        def run(action):barrier.wait(timeout=10);return action()
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(actions)) as pool:return list(pool.map(run,actions))
    def start():
        child=subprocess.Popen([str(EXE),'--config',str(config)],stdout=log,stderr=log)
        for _ in range(200):
            if child.poll() is not None:log.seek(0);raise AssertionError(log.read())
            try:
                if call('/api/health')[0]==200:return child
            except urllib.error.URLError:pass
            time.sleep(.05)
        child.terminate();child.wait(timeout=5);raise AssertionError('Server startup timeout')
    try:
        process=start();status,p=call('/api/platforms',{'name':'Concurrent platform'});assert status==201
        revision=p['allocation_revision'];body={'platform_type_id':p['id'],'first_product_id':'V00001','count':10,'expected_allocation_revision':revision}
        responses=parallel([lambda i=i:call('/api/batches',{**body,'request_id':f'creator-{i}'}) for i in range(2)])
        assert sorted(s for s,_ in responses)==[201,409],responses
        winner=next(result for status,result in responses if status==201)
        assert len(call('/api/cards')[1])==10
        # A lost reply can be recovered by resending the SAME request token.
        winner_body={**body,'request_id':winner['request_id']}
        responses=parallel([lambda:call('/api/batches',winner_body) for _ in range(12)])
        assert all(status==201 and result==winner for status,result in responses),responses
        assert call('/api/batches',{**winner_body,'count':11})[0]==409
        assert len(call('/api/cards')[1])==10
        # Independent platforms can reuse the same external numbers.
        other=call('/api/platforms',{'name':'Other platform'})[1]
        responses=parallel([lambda:call('/api/batches',{'platform_type_id':other['id'],'first_product_id':'V00001','count':10,'expected_allocation_revision':0,'request_id':'other'}),lambda:call('/api/cards')])
        assert responses[0][0]==201 and responses[1][0]==200
        xlsx=base64.b64encode((ROOT/'templates/blank.xlsx').read_bytes()).decode()
        def document(text):return {'format':'x-data-spreadsheet','version':1,'sheets':[{'name':'Repair','rows':{'len':20,'0':{'cells':{'0':{'text':text}}}},'cols':{'len':10}}]}
        def save(card,text,revision=0):return call(f'/api/cards/{card}/document',{'revision':revision,'document':document(text),'workbook_base64':xlsx},'PUT')
        responses=parallel([lambda i=i:save(i,f'independent-{i}') for i in range(1,11)])
        assert all(status==200 for status,_ in responses),responses
        responses=parallel([lambda i=i:save(11,f'writer-{i}') for i in range(12)]+[lambda:call('/api/cards/11/document') for _ in range(12)])
        writes=responses[:12];assert [status for status,_ in writes].count(200)==1 and [status for status,_ in writes].count(409)==11,writes
        assert all(status==200 for status,_ in responses[12:])
        stored=call('/api/cards/11/document')[1];assert stored['revision']==1
        # The label assignment and editable form protect the same revision.
        label={'cards':[{'id':12,'revision':0}],'groups':[{'count':1,'code':'00123'}]}
        responses=parallel([lambda:save(12,'repair-writer'),lambda:call('/api/labels/assign',label)])
        assert sorted(s for s,_ in responses)==[200,409],responses
        # New clients use queued saves: ALL writers succeed, and a later edit
        # to one cell does not discard another user's edits in other cells.
        blank={'format':'x-data-spreadsheet','version':1,'sheets':[{'name':'Queue','rows':{'len':40},'cols':{'len':20},'styles':[],'merges':[]}]}
        queued=[]
        for i in range(12):
            state=json.loads(json.dumps(blank));state['sheets'][0]['rows']['0']={'cells':{str(i):{'text':f'client-{i}'}}};state['sheets'][0]['rows']['1']={'cells':{'0':{'text':f'shared-{i}'}}}
            queued.append({'revision':0,'base_document':blank,'document':state,'request_id':f'queued-{i}'})
        results=parallel([lambda body=body:call('/api/cards/13/save',body) for body in queued])
        assert all(status==200 for status,_ in results),results
        assert sorted(result['revision'] for _,result in results)==list(range(1,13))
        latest=call('/api/cards/13/document')[1];cells=latest['document']['sheets'][0]['rows']['0']['cells']
        assert all(cells[str(i)]['text']==f'client-{i}' for i in range(12)),cells
        last_index=max(range(12),key=lambda i:results[i][1]['revision']);assert latest['document']['sheets'][0]['rows']['1']['cells']['0']['text']==f'shared-{last_index}'
        assert len(call('/api/cards/13/history')[1])==13
        assert call('/api/cards/13/save',queued[0])[1]['revision']==results[0][1]['revision']
        assert call('/api/cards/13/document')[1]['revision']==12
        with urllib.request.urlopen(base+'/api/cards/13/workbook') as response:current_book=response.read()
        with zipfile.ZipFile(io.BytesIO(current_book)) as z:
            sheet=ET.fromstring(z.read('xl/worksheets/sheet1.xml'));ns={'m':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
            values={c.attrib['r']:''.join(c.itertext()) for c in sheet.findall('.//m:c',ns)}
            assert values['A2']==f'shared-{last_index}' and values['L1']=='client-11',values
        # New Creator requests also queue, retaining the exact entered IDs.
        r=call('/api/platforms')[1][0]['allocation_revision']
        requests=[{**body,'count':1,'queued':True,'first_product_id':'Q00001','expected_allocation_revision':r,'request_id':f'queued-creator-{i}'} for i in range(2)]
        created=parallel([lambda request=request:call('/api/batches',request) for request in requests])
        assert all(status==201 for status,_ in created),created
        assert len({result['cards'][0]['id'] for _,result in created})==2
        assert call('/api/cards/13/document?after=12')[1]=={'changed':False,'revision':12}
        # A second server cannot own the database even on another port.
        second=root/'second.json';second.write_text(json.dumps({**settings,'port':port()}))
        duplicate=subprocess.run([str(EXE),'--config',str(second)],capture_output=True,timeout=10)
        assert duplicate.returncode!=0 and b'already owned' in duplicate.stderr,duplicate.stderr
        assert call('/api/health')[0]==200
        # Backup reads the live WAL through sqlite3_backup, never a raw file copy.
        stop=threading.Event();failures=[]
        def writer():
            while not stop.is_set():
                try:
                    current=call('/api/cards/20/document')[1]['revision']
                    if save(20,f'background-{current}',current)[0]!=200:failures.append('background save')
                except Exception as e:failures.append(str(e));break
        thread=threading.Thread(target=writer);thread.start();backup=root/'backup.sqlite3'
        try:
            outcome=subprocess.run([str(BACKUP),'--database',str(db),'--output',str(backup)],capture_output=True,timeout=70)
            assert outcome.returncode==0,(outcome.stdout,outcome.stderr)
        finally:stop.set();thread.join(timeout=30)
        assert not failures,failures
        with closing(sqlite3.connect(backup)) as c:
            assert c.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
            assert c.execute('SELECT count(*) FROM cards').fetchone()[0]==22
            assert c.execute('PRAGMA user_version').fetchone()[0]==5
        assert subprocess.run([str(BACKUP),'--database',str(db),'--output',str(backup)],capture_output=True,timeout=10).returncode!=0
        # Abrupt termination/restart keeps committed documents and allocation tokens.
        process.kill();process.wait(timeout=10);process=start()
        assert call('/api/cards/11/document')[1]==stored
        assert call('/api/batches',winner_body)[1]==winner
        with closing(sqlite3.connect(db)) as c:assert c.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
        print('Multi-user checks passed: legacy compatibility, idempotent Creators, 12 queued writers all accepted, independent cells retained, last cell edit wins, canonical XLSX/history/live revisions, single database owner, online WAL backup and abrupt restart')
    finally:
        if process and process.poll() is None:process.terminate();process.wait(timeout=10)
        log.close()
