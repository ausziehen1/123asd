"""Build a local client update ZIP; standard Python only, never touches working data."""
import argparse,hashlib,json,re,zipfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--client-dir',required=True,type=Path)
p.add_argument('--from-version',required=True)
p.add_argument('--version',required=True)
p.add_argument('--output',required=True,type=Path)
p.add_argument('--file',action='append',dest='files',help='Relative client file; default RepairClient.dll')
a=p.parse_args()
def version(s):
 if not re.fullmatch(r'\d+\.\d+\.\d+',s):p.error('Version must be x.y.z')
 return tuple(map(int,s.split('.')))
if version(a.version)<=version(a.from_version):p.error('New version must be greater than base version')
metadata=json.loads((a.client_dir/'installation.json').read_text(encoding='utf-8-sig'))
if metadata.get('product')!='RepairCardsClients' or metadata.get('abi')!=1 or metadata.get('version')!=a.version:p.error('Build the new DLL/version first: installation.json must match --version')
files=a.files or ['RepairClient.dll']
allowed={'RepairClient.dll','WebView2Loader.dll','Creator.exe','Reader.exe','Repair.exe','theme.json'}
entries=[];payload={}
for name in files:
 if name not in allowed and not (re.fullmatch(r'ui/[A-Za-z0-9_./-]+\.(css|js|json)',name) and '..' not in name):p.error('Unsupported client file: '+name)
 path=a.client_dir/name
 if not path.is_file() or path.is_symlink():p.error('Not a regular file: '+name)
 if name in payload:p.error('Duplicate file: '+name)
 data=path.read_bytes();payload[name]=data;entries.append({'path':name,'sha256':hashlib.sha256(data).hexdigest()})
manifest={'product':'RepairCardsClients','architecture':'x64','abi':1,'from_version':a.from_version,'version':a.version,'files':entries}
a.output.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(a.output,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 z.writestr('manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2))
 for name,data in payload.items():z.writestr(name,data)
checksum=hashlib.sha256(a.output.read_bytes()).hexdigest()
a.output.with_suffix(a.output.suffix+'.sha256').write_text(checksum+'  '+a.output.name+'\n')
print(a.output,checksum)
