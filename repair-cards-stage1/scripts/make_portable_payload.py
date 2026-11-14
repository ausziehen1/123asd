"""Build an embedded client payload. Standard Python; no runtime dependency."""
import argparse, hashlib, json, pathlib, zipfile
p = argparse.ArgumentParser()
p.add_argument('--client-dir', type=pathlib.Path, required=True)
p.add_argument('--source-dir', type=pathlib.Path, required=True)
p.add_argument('--role', choices=['Creator', 'Reader', 'Repair'], required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
a = p.parse_args()
files = {'VALIDATION.md':(a.source_dir/'VALIDATION.md').read_bytes()}
for name in [a.role+'.exe', 'RepairClient.dll', 'WebView2Loader.dll', 'Updater.exe', 'installation.json', 'theme.json']:
    files[name] = (a.client_dir/name).read_bytes()
for path in (a.source_dir/'docs').glob('*.md'):
    files['docs/'+path.name] = path.read_bytes()
files['licenses/WEBVIEW2-LICENSE.txt'] = (a.source_dir/'third_party/webview2/LICENSE.txt').read_bytes()
for path in (a.source_dir/'third_party').glob('*LICENSE*'):
    if path.is_file(): files['licenses/'+path.name] = path.read_bytes()
for path in (a.source_dir/'third_party/runtime-licenses').glob('*'):
    if path.is_file(): files['licenses/runtime/'+path.name] = path.read_bytes()
files['licenses/NAYUKI-LICENSE.txt'] = ((a.source_dir/'third_party/qrcodegen/qrcodegen.hpp').read_text().split('*/',1)[0]+'*/\n').encode()
if a.role == 'Creator':
    for path in (a.source_dir/'labels').glob('*'):
        if path.is_file(): files['labels/'+path.name] = path.read_bytes()
version = json.loads(files['installation.json'])['version']
manifest = {'product':'RepairCardsPortable', 'role':a.role, 'abi':1, 'architecture':'x64', 'version':version,
            'files':[{'path':n, 'sha256':hashlib.sha256(b).hexdigest()} for n,b in sorted(files.items())]}
files['payload.json'] = json.dumps(manifest, ensure_ascii=False, indent=2).encode()
a.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(a.output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name, data in sorted(files.items()):
        info = zipfile.ZipInfo(name, date_time=(2026,11,14,0,0,0)); info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
rc = a.output.with_suffix('.rc')
rc.write_text('201 RCDATA "'+a.output.resolve().as_posix()+'"\n')
print(a.role, version, len(files), 'embedded files')
