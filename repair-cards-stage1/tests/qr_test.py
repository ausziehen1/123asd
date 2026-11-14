"""Independent QR decoder and HTTP integration checks; zxing-cpp and Pillow are test-only dependencies."""
import csv
import io
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw
import zxingcpp

ROOT=Path(__file__).resolve().parents[1]
SERVER=Path(sys.argv[1]).resolve()
LABEL_TESTS=Path(sys.argv[2]).resolve()
PUBLIC='http://192.168.10.20:8080'
with socket.socket() as s:
    s.bind(('127.0.0.1',0));PORT=s.getsockname()[1]
BASE=f'http://127.0.0.1:{PORT}'


def decode(svg):
    root=ET.fromstring(svg)
    extent=int(root.attrib['viewBox'].split()[3])
    image=Image.new('RGB',(extent*8,extent*8),'white');draw=ImageDraw.Draw(image)
    path=root.find('{http://www.w3.org/2000/svg}path').attrib['d']
    coordinates=[(int(x),int(y)) for x,y in re.findall(r'M(\d+),(\d+)h1v1h-1z',path)]
    assert coordinates and min(x for x,y in coordinates)>=4 and min(y for x,y in coordinates)>=4
    assert max(x for x,y in coordinates)<extent-4 and max(y for x,y in coordinates)<extent-4
    for x,y in coordinates: draw.rectangle((x*8,y*8,x*8+7,y*8+7),fill='black')
    result=zxingcpp.read_barcode(image)
    assert result is not None,'Independent decoder could not read QR'
    return result.text


def call(path,body=None,method=None,status=200,raw=False):
    data=None if body is None else json.dumps(body).encode()
    req=urllib.request.Request(BASE+path,data=data,method=method,headers={'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=10) as r: code,payload=r.status,r.read()
    except urllib.error.HTTPError as e:code,payload=e.code,e.read()
    assert code==status,(path,code,payload)
    return payload if raw else json.loads(payload)


with tempfile.TemporaryDirectory(prefix='repair-qr-') as temp:
    root=Path(temp)
    result=subprocess.run([str(LABEL_TESTS),'--emit',str(root/'jobs')],check=True,capture_output=True,text=True)
    job=Path(result.stdout.strip())
    rows=list(csv.DictReader(io.StringIO((job/'labels.csv').read_text(encoding='utf-8-sig'))))
    assert len(rows)==2 and rows[0]['ProductID']==rows[1]['ProductID']=='V00000024'
    for i,row in enumerate(rows,1):
        assert decode((job/f'qr-{i}.svg').read_text())==row['QRUrl']==f'{PUBLIC}/cards/{i}'
    assert (job/'preview.html').exists() and len(json.loads((job/'manifest.json').read_text()))==2
    config=root/'config.json'
    settings={'host':'0.0.0.0','port':PORT,'database':'data/db.sqlite3','template':str(ROOT/'templates/blank.xlsx'),'web':str(ROOT/'web')}
    config.write_text(json.dumps(settings))
    log=(root/'server.log').open('w+');process=None
    def start():
        child=subprocess.Popen([str(SERVER),'--config',str(config)],stdout=log,stderr=log)
        for _ in range(100):
            if child.poll() is not None:log.seek(0);raise AssertionError(log.read())
            try:call('/api/health');return child
            except (urllib.error.URLError,TimeoutError):time.sleep(.05)
        child.terminate();child.wait(timeout=5);raise AssertionError('No server response')
    try:
        process=start()
        # A second process must not share the port and serve another database.
        duplicate=subprocess.run([str(SERVER),'--config',str(config)],capture_output=True,timeout=10)
        assert duplicate.returncode!=0 and b'Cannot listen' in duplicate.stdout+duplicate.stderr
        assert b'Listen:' not in duplicate.stdout
        info=call('/api/server-info');assert info['public_url']=='' and info['local_only'] is False
        original_id=info['server_id'];assert original_id
        platform=call('/api/platforms',{'name':'Focusline 16'},status=201)
        call('/api/batches',{'platform_type_id':platform['id'],'first_product_id':'V00000024','count':2},status=201)
        for bad in ['http://127.0.0.1:8080','http://localhost:8080','file:///x','http://server:65536']:
            call('/api/settings/public-url',{'public_url':bad},method='PUT',status=400)
        call('/api/settings/public-url',{'public_url':PUBLIC},method='PUT')
        for card_id in [1,2]:
            svg=call(f'/api/cards/{card_id}/qr.svg',raw=True).decode()
            assert decode(svg)==f'{PUBLIC}/cards/{card_id}'
            page=call(f'/cards/{card_id}',raw=True).decode()
            assert 'data-mode="read"' in page
        selected=call('/api/labels/cards',{'ids':[2,1]})
        assert [x['id'] for x in selected]==[2,1]
        call('/api/labels/cards',{'ids':[1,999]},status=404)
        call('/api/labels/cards',{'ids':[]},status=400)
        call('/api/cards/999/qr.svg',status=404)
        process.terminate();process.wait(timeout=10);process=start()
        assert call('/api/server-info')['server_id']==original_id
        assert call('/api/server-info')['public_url']==PUBLIC
        assert len(call('/api/cards'))==2
        # Loopback-only mode must not produce misleading network settings.
        process.terminate();process.wait(timeout=10);settings['host']='127.0.0.1';config.write_text(json.dumps(settings));process=start()
        assert call('/api/server-info')['local_only'] is True
        call('/api/settings/public-url',{'public_url':PUBLIC},method='PUT',status=400)
        print('QR checks passed: independent decoding, unique links, CSV, selection, persistence, read-only routes and network settings')
    finally:
        if process is not None and process.poll() is None:process.terminate();process.wait(timeout=10)
        log.close()
