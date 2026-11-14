"""Package a built release and standalone EXEs. Python standard library only."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,sys,zipfile
p=argparse.ArgumentParser();p.add_argument('--source-dir',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]);p.add_argument('--build-dir',type=pathlib.Path,required=True);p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args()
src=a.source_dir.resolve();build=a.build_dir.resolve();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
version=re.search(r'project\(RepairCards VERSION ([0-9.]+)',(src/'CMakeLists.txt').read_text()).group(1)
stage=out/('stage-'+version)
if stage.exists():shutil.rmtree(stage)
project=stage/'RepairCards-stage1';project.mkdir(parents=True)
for path in sorted(src.rglob('*')):
    rel=path.relative_to(src)
    if any(n in ('build','data','backups','.git','.idea','__pycache__','.updates','ready-windows','clients') or n.startswith('cmake-build-') for n in rel.parts):continue
    if path.name in ('config.local.json','.clients.lock') or path.suffix in ('.tmp','.sqlite3','.db','.pyc') or path.name.endswith(('-wal','-shm','.server.lock')):continue
    if path.is_file():dest=project/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
ready=project/'ready-windows';ready.mkdir();clients=project/'clients';clients.mkdir()
names=['repair_server.exe','repair_backup.exe','Creator.exe','Reader.exe','Repair.exe','Updater.exe','RepairClient.dll','WebView2Loader.dll']
for name in names:shutil.copy2(build/name,ready/name)
for name in ['installation.json','theme.json']:shutil.copy2(build/name,ready/name)
assert json.loads((ready/'installation.json').read_text())['version']==version
for role in ['Creator','Reader','Repair']:shutil.copy2(build/'portable'/(role+'.exe'),clients/(role+'.exe'))
shutil.copytree(src/'labels',ready/'labels');shutil.copytree(src/'docs',ready/'docs');shutil.copy2(src/'VALIDATION.md',ready/'VALIDATION.md')
config={'host':'0.0.0.0','port':8080,'database':'../data/repair.sqlite3','template':'../templates/blank.xlsx','web':'../web','public_url':''}
(ready/'config.example.json').write_text(json.dumps(config,indent=2)+'\n')
(ready/'START-SERVER.cmd').write_text('@echo off\ncd /d "%~dp0"\nif exist config.local.json (\n  repair_server.exe --config config.local.json\n) else (\n  repair_server.exe --config config.example.json\n)\npause\n',newline='\r\n')
quick=f'''REPAIR CARDS {version} — WINDOWS x64

Полная инструкция: docs/USER_GUIDE.md

СЕРВЕР: ready-windows/START-SERVER.cmd.
До обновления сохраните бланки, остановите прежний сервер и сделайте копию data.
Для прежней базы следуйте разделу 3 инструкции; не запускайте пустую базу вместо нее.
В ready-windows создайте config.local.json из config.example.json.
Проверьте путь ../data/repair.sqlite3, шаблон, web и прежний порт.
При запуске новой версии схема обновится до 5.

ПОЛЬЗОВАТЕЛЯМ: передайте ОДИН нужный EXE из папки clients:
Creator.exe — учет/редактирование/QR; Reader.exe — просмотр; Repair.exe — ремонт.
DLL встроены и извлекаются в LocalAppData автоматически.
Несколько экземпляров каждого EXE могут работать одновременно.
Для встроенного бланка один раз нужен Microsoft WebView2 Runtime Evergreen x64.
Клиентам нужна связь с сервером; базы на их ПК нет.

Сохранения идут в очередь. Разные ячейки объединяются, одинаковая получает
последнее значение. Предыдущие версии — кнопка «История».
Открытые бланки получают обновления примерно раз в секунду.

Для BarTender нужны поля базы QRUrl и OneCCode в рабочем макете.
Печать проверяется на вашей Zebra, настройки — в штатном диалоге BarTender.

Будущее обновление: заменить один EXE либо установить ZIP-патч DLL через Updater.
Для запуска Updater без открытия клиента: Repair.exe --update (аналогично другим).
Папка кэша: Repair.exe --files.
Подробные шаги/копирование/откат: docs/USER_GUIDE.md.
'''
(project/'НАЧАТЬ ЗДЕСЬ.txt').write_text(quick,encoding='utf-8-sig',newline='\r\n')
(clients/'ПОЛЬЗОВАТЕЛЮ.txt').write_text(quick,encoding='utf-8-sig',newline='\r\n')
licenses=ready/'licenses';licenses.mkdir()
for path in (src/'third_party').glob('*LICENSE*'):
    if path.is_file():shutil.copy2(path,licenses/path.name)
shutil.copy2(src/'third_party/webview2/LICENSE.txt',licenses/'WEBVIEW2-LICENSE.txt');shutil.copytree(src/'third_party/runtime-licenses',licenses/'runtime-licenses')
(licenses/'NAYUKI-LICENSE.txt').write_text((src/'third_party/qrcodegen/qrcodegen.hpp').read_text().split('*/',1)[0]+'*/\n')
meta={'product':'RepairCards','version':version,'architecture':'x64','target':'Windows 10 x64','client_abi':1,'database_schema':5,'toolchain':'LLVM-MinGW 20261104 / Clang 23.1.5','files':{n:hashlib.sha256((ready/n).read_bytes()).hexdigest() for n in names},'standalone':{n.name:hashlib.sha256(n.read_bytes()).hexdigest() for n in clients.glob('*.exe')},'physical_zebra_print_tested_here':False,'validation':'VALIDATION.md'}
(ready/'BUILD_INFO.json').write_text(json.dumps(meta,indent=2)+'\n')
# A separate all-client ZIP is convenient for the administrator; users still copy one EXE.
clientkit=stage/('RepairCards-clients-'+version);shutil.copytree(clients,clientkit);shutil.copytree(src/'docs',clientkit/'docs');shutil.copy2(src/'VALIDATION.md',clientkit/'VALIDATION.md')
def checksum(path):
    digest=hashlib.sha256(path.read_bytes()).hexdigest();path.with_suffix(path.suffix+'.sha256').write_text(digest+'  '+path.name+'\n');return digest
for name,directory in [(f'RepairCards-{version}-full.zip',project),(f'RepairCards-{version}-clients.zip',clientkit)]:
    target=out/name
    with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                info=zipfile.ZipInfo(path.relative_to(stage).as_posix(),date_time=(2026,11,14,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o644<<16;z.writestr(info,path.read_bytes(),compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
    with zipfile.ZipFile(target) as z:assert z.testzip() is None
    print(target.name,target.stat().st_size,checksum(target))
standalone=out/version;standalone.mkdir(exist_ok=True)
for file in clients.glob('*.exe'):
    target=standalone/file.name;shutil.copy2(file,target);print(target.relative_to(out),target.stat().st_size,checksum(target))
subprocess.run([sys.executable,str(src/'scripts/make_patch.py'),'--client-dir',str(build),'--from-version','0.5.0','--version',version,'--output',str(out/f'RepairCards-0.5.0-to-{version}-client-patch.zip')],check=True)
