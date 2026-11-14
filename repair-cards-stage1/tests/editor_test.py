"""Browser integration test. Requires Playwright, Chromium and openpyxl only in the test environment."""
import io
import json
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from openpyxl import load_workbook
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(sys.argv[1]).resolve()
CHROMIUM = sys.argv[2] if len(sys.argv) > 2 else '/usr/bin/chromium'
with socket.socket() as s:
    s.bind(('127.0.0.1', 0)); PORT = s.getsockname()[1]
BASE = f'http://127.0.0.1:{PORT}'


def get(path, raw=False):
    with urllib.request.urlopen(BASE + path, timeout=10) as r:
        body = r.read()
    return body if raw else json.loads(body)


def cell_center(page, row, col):
    value = page.evaluate('''([r,c])=>{
      const d=repairSheet.sheet.data,rect=d.cellRect(r,c);
      return {x:d.cols.indexWidth+rect.left+Math.min(rect.width/2,20),
              y:d.rows.height+rect.top+Math.min(rect.height/2,12)};
    }''', [row, col])
    box = page.locator('.x-spreadsheet-overlayer').bounding_box()
    return box['x'] + value['x'], box['y'] + value['y']


def choose(page, row, col, double=False):
    x, y = cell_center(page, row, col)
    if double:
        page.mouse.dblclick(x, y)
    else:
        page.mouse.click(x, y)


with tempfile.TemporaryDirectory(prefix='repair-editor-') as directory:
    directory = Path(directory)
    database = directory / 'old.sqlite3'
    with sqlite3.connect(database) as db:
        db.executescript('''
          CREATE TABLE platform_types(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL UNIQUE,
            next_product_id TEXT,created_at TEXT NOT NULL DEFAULT 'old');
          CREATE TABLE cards(id INTEGER PRIMARY KEY AUTOINCREMENT,product_id TEXT NOT NULL,
            platform_type_id INTEGER NOT NULL REFERENCES platform_types(id),workbook BLOB NOT NULL,
            created_at TEXT NOT NULL DEFAULT 'old');
          INSERT INTO platform_types(name,next_product_id) VALUES('Focusline 16','V00000026');
          PRAGMA user_version=1;
        ''')
        for number in [24, 25]:
            db.execute('INSERT INTO cards(product_id,platform_type_id,workbook) VALUES(?,1,?)',
                       (f'V{number:08}', (ROOT / 'templates/blank.xlsx').read_bytes()))
    config = directory / 'config.json'
    config.write_text(json.dumps({'host':'127.0.0.1','port':PORT,'database':str(database),
        'template':str(ROOT/'templates/blank.xlsx'),'web':str(ROOT/'web')}), encoding='utf-8')
    log = (directory / 'server.log').open('w+')
    process = None

    def start():
        child = subprocess.Popen([str(EXE),'--config',str(config)],stdout=log,stderr=log)
        for _ in range(100):
            if child.poll() is not None:
                log.seek(0); raise AssertionError(log.read())
            try:
                get('/api/health'); return child
            except (urllib.error.URLError, TimeoutError): time.sleep(.05)
        child.terminate();child.wait(timeout=5);raise AssertionError('Server not ready')

    try:
        process = start()
        with sync_playwright() as pw:
            browser = pw.chromium.launch(executable_path=CHROMIUM, headless=True, args=['--no-sandbox'])
            context = browser.new_context(viewport={'width':1366,'height':1000})
            page = context.new_page(); page_errors=[]; external=[]
            page.on('pageerror',lambda error:page_errors.append(str(error)))
            page.on('request',lambda request:external.append(request.url) if not request.url.startswith((BASE,'data:')) else None)
            page.goto(BASE+'/edit/1')
            expect(page.locator('#state')).to_contain_text('Бланк открыт')
            expect(page.locator('#title')).to_contain_text('V00000024')
            assert page.evaluate('repairSheet.cell(2,1).text') == '#ID V00000024'
            assert 'B1:L1' in page.evaluate('repairSheet.getData()[0].merges')
            assert get('/api/cards/1/document')['revision'] == 0
            assert get('/api/cards/2/document')['document'] is None
            # A real double-click, text input and save without Enter in the cell editor.
            choose(page,8,5,double=True)
            editor=page.locator('.x-spreadsheet-editor textarea')
            expect(editor).to_be_visible()
            editor.fill('Не включается. Проверка питания.')
            expect(page.locator('#save')).to_be_enabled()
            editor.press('Control+s')
            expect(page.locator('#state')).to_have_text('Бланк сохранен.')
            assert get('/api/cards/1/document')['revision'] == 1
            assert get('/api/cards/2/document')['revision'] == 0
            book=load_workbook(io.BytesIO(get('/api/cards/1/workbook',raw=True)))
            assert book.active['F9'].value == 'Не включается. Проверка питания.'
            assert book.active['B3'].value == '#ID V00000024'
            page.reload();expect(page.locator('#state')).to_contain_text('Бланк открыт')
            assert page.evaluate('repairSheet.cell(8,5).text') == 'Не включается. Проверка питания.'
            # Structural edits must move data and be saved with the same formatting.
            choose(page,8,5)
            page.locator('#insert-column').click()
            page.locator('#insert-row').click()
            assert page.evaluate('repairSheet.cell(9,6).text') == 'Не включается. Проверка питания.'
            assert page.evaluate('repairSheet.getData()[0].rows.len') == 41
            assert page.evaluate('repairSheet.getData()[0].cols.len') == 17
            choose(page,9,6)
            page.locator('.x-spreadsheet-toolbar-btn:has(.x-spreadsheet-icon-img.font-bold)').click()
            expect(page.locator('#save')).to_be_enabled();page.locator('#save').click()
            expect(page.locator('#state')).to_have_text('Бланк сохранен.')
            book=load_workbook(io.BytesIO(get('/api/cards/1/workbook',raw=True)))
            assert book.active['G10'].value == 'Не включается. Проверка питания.'
            assert book.active['G10'].font.bold is True
            stored=get('/api/cards/1/document'); assert stored['revision']==2
            # Two editors: later queued changes to the same cell win automatically.
            stale=context.new_page();stale.goto(BASE+'/edit/1')
            expect(stale.locator('#state')).to_contain_text('Бланк открыт')
            choose(page,10,6,double=True);page.locator('.x-spreadsheet-editor textarea').fill('Проверка завершена')
            expect(page.locator('#save')).to_be_enabled();page.locator('#save').click()
            expect(page.locator('#state')).to_have_text('Бланк сохранен.')
            choose(stale,10,6,double=True);stale.locator('.x-spreadsheet-editor textarea').fill('Устаревшая правка')
            expect(stale.locator('#save')).to_be_enabled();stale.locator('#save').click()
            expect(stale.locator('#state')).to_have_text('Бланк сохранен.')
            assert get('/api/cards/1/document')['document']['sheets'][0]['rows']['10']['cells']['6']['text']=='Устаревшая правка'
            assert stale.evaluate('repairSheet.cell(10,6).text')=='Устаревшая правка'
            # Read-only view uses the saved document and has no edit controls.
            reader=context.new_page();reader.goto(BASE+'/cards/1')
            expect(reader.locator('#state')).to_contain_text('только для просмотра')
            expect(reader.locator('#save')).not_to_be_visible()
            expect(reader.locator('#insert-row')).not_to_be_visible()
            assert reader.evaluate('repairSheet.sheet.data.settings.mode')=='read'
            assert reader.evaluate('repairSheet.cell(10,6).text')=='Устаревшая правка'
            choose(reader,10,6,double=True)
            expect(reader.locator('.x-spreadsheet-editor textarea')).not_to_be_visible()
            reader.set_viewport_size({'width':390,'height':844})
            reader.reload();expect(reader.locator('#state')).to_contain_text('только для просмотра')
            assert reader.locator('canvas').is_visible()
            assert not external,external
            assert not page_errors,page_errors
            browser.close()
        process.terminate();process.wait(timeout=10);process=start()
        assert get('/api/cards/1/document')['revision']==4
        book=load_workbook(io.BytesIO(get('/api/cards/1/workbook',raw=True)))
        assert book.active['G11'].value=='Устаревшая правка'
        assert len(get('/api/cards'))==2
        print('Editor browser tests passed: migration, real input, row/column insertion, formatting, XLSX, queued overwrite, read-only, mobile, restart')
    finally:
        if process is not None and process.poll() is None: process.terminate();process.wait(timeout=10)
        log.close()
