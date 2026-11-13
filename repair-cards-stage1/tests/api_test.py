"""Integration checks; Python is needed for development tests only, not to run the server."""
import concurrent.futures
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(sys.argv[1]).resolve()
with socket.socket() as s:
    s.bind(('127.0.0.1', 0))
    PORT = s.getsockname()[1]
BASE = f'http://127.0.0.1:{PORT}'


def call(path, body=None, expected=200, raw=False):
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode('utf-8')
    req = urllib.request.Request(BASE + path, data=data, headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            status, payload = response.status, response.read()
    except urllib.error.HTTPError as error:
        status, payload = error.code, error.read()
    assert status == expected, (path, status, expected, payload)
    return payload if raw else json.loads(payload)


with tempfile.TemporaryDirectory(prefix='repair-api-') as directory:
    path = Path(directory)
    config = path / 'config.json'
    config.write_text(json.dumps({'host': '127.0.0.1', 'port': PORT,
        'database': 'данные/repair.sqlite3', 'template': str(ROOT / 'templates/blank.xlsx')},
        ensure_ascii=False), encoding='utf-8')
    log = (path / 'server.log').open('w+')
    process = None

    def start():
        child = subprocess.Popen([str(EXE), '--config', str(config)], stdout=log, stderr=log)
        for _ in range(100):
            if child.poll() is not None:
                log.seek(0)
                raise AssertionError(log.read())
            try:
                call('/api/health')
                return child
            except (urllib.error.URLError, TimeoutError):
                time.sleep(0.05)
        child.terminate()
        child.wait(timeout=5)
        raise AssertionError('Server did not become ready')

    try:
        process = start()
        assert call('/api/cards') == []
        name = 'Ноутбук 16.5 <script>alert(1)</script>'
        platform = call('/api/platforms', {'name': name}, 201)
        other = call('/api/platforms', {'name': 'Планшет'}, 201)
        preview = call('/api/batches/preview', {'first_product_id': 'V00001', 'count': 5})
        assert preview['last_product_id'] == 'V00005'
        assert call('/api/cards') == [], 'Preview must not register cards'
        body = {'platform_type_id': platform['id'], 'first_product_id': 'V00001', 'count': 5}
        batch = call('/api/batches', body, 201)
        assert batch['next_product_id'] == 'V00006'
        assert [c['product_id'] for c in batch['cards']] == [f'V{i:05}' for i in range(1, 6)]
        call('/api/batches', body, 201)  # Same product IDs are deliberately allowed.
        call('/api/batches', {**body, 'platform_type_id': other['id']}, 201)
        assert len(call(f'/api/cards?platform_type_id={platform["id"]}')) == 10
        assert len(call('/api/cards?limit=2&after=1')) == 2
        original = (ROOT / 'templates/blank.xlsx').read_bytes()
        assert call('/api/cards/1/workbook', raw=True) == original
        html = call('/cards/1', raw=True).decode()
        assert 'V00001' in html and '&lt;script&gt;' in html and '<script>' not in html
        assert 'contenteditable' not in html
        for value in [0, -1, 1001, 1.5, True, '5', 2**64 - 1]:
            call('/api/batches', {**body, 'count': value}, 400)
        call('/api/batches', {**body, 'platform_type_id': 99999}, 404)
        call('/api/batches', {**body, 'first_product_id': 'bad'}, 400)
        call('/api/cards?limit=99999999999999999999', expected=400)
        call('/api/cards?after=-1', expected=400)
        call('/api/cards/9999999999999999999999', expected=400)
        call('/api/cards/99999', expected=404)
        call('/api/cards/99999/workbook', expected=404)
        call('/missing', expected=404)
        before = len(call('/api/cards'))
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            results = list(pool.map(lambda i: call('/api/batches', {
                **body, 'first_product_id': f'C{i:03}', 'count': 3}, 201), range(12)))
        internal = [card['id'] for batch in results for card in batch['cards']]
        assert len(set(internal)) == 36
        assert len(call('/api/cards')) == before + 36
        process.terminate()
        process.wait(timeout=10)
        process = start()
        assert len(call('/api/cards')) == before + 36
        assert call('/api/cards/1/workbook', raw=True) == original
        assert call('/api/platforms')[0]['name'] == name
        print('API tests passed: batches, duplicates, filters, binary templates, validation, concurrency, restart')
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        log.close()
