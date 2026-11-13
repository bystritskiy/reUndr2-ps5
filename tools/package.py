#!/usr/bin/env python3
"""Sign and stage reUndr 2 for PS5; optionally build and roundtrip-verify an FFPFSC,
or with --release a public ZIP holding nothing from the game."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parent.parent
# With licenses/ and sce_sys/, all a public release may hold.
RELEASE_FILES={'eboot.bin','sce_module/libc.prx','README.txt','hd/manifest.bin'}

def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1<<20),b''):h.update(block)
 return h.hexdigest()
def inventory(path):
 return {str(p.relative_to(path)):{'size':p.stat().st_size,'sha256':digest(p)} for p in sorted(path.rglob('*')) if p.is_file() and p.name!='.DS_Store'}
def run(args,log):
 result=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 log.write_text(result.stdout);result.check_returncode();return result.stdout

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path);p.add_argument('--pack',action='store_true');p.add_argument('--release',action='store_true',help='public ZIP: the program and own artwork, nothing from the game');a=p.parse_args()
 if a.release and (a.game or a.pack):raise SystemExit('--release takes no game data and no image')
 work=ROOT/'build/linked';sdk=ROOT/'build/deps/PS5_Vulkan/.deps/native/ps5-payload-sdk';native=ROOT/'build/ps5-native-tool'
 param=json.loads((ROOT/'ps5/sce_sys/param.json').read_text())
 if not re.fullmatch(r'[A-Z]{2}[0-9]{4}-PPSA99027_00-[A-Z0-9]{16}',param['contentId']):raise SystemExit('Invalid contentId')
 if param['contentVersion']!=json.loads((ROOT/'ps5/dependencies.json').read_text())['ps5_content_version']:raise SystemExit('Content version mismatch')
 name='game' if a.game else 'release' if a.release else 'app';app=ROOT/'dist'/name/'PPSA99027'
 if a.release:shutil.rmtree(app,ignore_errors=True)
 app.mkdir(parents=True,exist_ok=True)
 if any(p.is_symlink() for p in app.rglob('*')):raise SystemExit('Staging has symlinks')
 dynamic=run(['/opt/homebrew/opt/llvm@18/bin/llvm-readelf','--dyn-syms','--wide',work/'llvm-pie.elf'],work/'imports.txt')
 if any(re.search(r'\b(?:GLOBAL|WEAK)\b',s) and ' UND ' not in s for s in dynamic.splitlines()):raise SystemExit('Unexpected native exports')
 run([native,'link','--in',work/'llvm-pie.elf','--out',work/'eboot.elf','--stub-dir',sdk/'target/lib','--stub',work/'stubs/libSceAgc.so','--stub',work/'stubs/libSceAgcDriver.so','--module-sdk','0x02000009','--companion-sdk','0x08050001','--file-name','eboot.elf'],work/'convert.log')
 run([native,'self','--sign','--in',work/'eboot.elf','--out',app/'eboot.bin','--magic','0x1D3D154F'],work/'sign.log')
 modules=app/'sce_module';modules.mkdir(exist_ok=True)
 libc=ROOT/'build/deps/PS5_Vulkan/runtime/libc.prx'
 if digest(libc)!='e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036':raise SystemExit('Unexpected libc.prx hash')
 shutil.copy2(libc,modules/'libc.prx')
 for file in [app/'eboot.bin',modules/'libc.prx']:
  inspection=run([native,'self','--inspect','--file',file],work/(file.name+'-inspect.txt'))
  if 'integrity: valid' not in inspection:raise SystemExit('FSELF integrity failed')
 shutil.copytree(ROOT/'ps5/sce_sys',app/'sce_sys',dirs_exist_ok=True)
 if a.game:
  game=a.game.resolve()
  if any(p.is_symlink() for p in game.rglob('*')):raise SystemExit('Game has symlinks')
  expected=re.search(r'"([A-F0-9]{64})"',(ROOT/'ps5/src/game_revision.h').read_text()).group(1)
  if digest(game/'default.xbe').upper()!=expected:raise SystemExit('Game XBE differs from compiled revision')
  for folder in ['B3','NFSUNDER']:
   if not (game/folder).is_dir():raise SystemExit('Missing game folder: '+folder)
  source_inv=inventory(game);staged=app/'game';staged.mkdir(exist_ok=True)
  if set(inventory(staged))-set(source_inv):raise SystemExit('Unexpected staged game files')
  for file in source_inv:
   src=game/file;dest=staged/file;dest.parent.mkdir(parents=True,exist_ok=True)
   if dest.exists():
    if digest(dest)!=source_inv[file]['sha256']:raise SystemExit('Staged data differs: '+file)
   else:os.link(src,dest)
  if inventory(staged)!=source_inv:raise SystemExit('Game staging verification failed')
 hd=ROOT/'build/hd'
 if not (hd/'manifest.bin').exists():raise SystemExit('Run tools/build-hd-ui.py first')
 shutil.rmtree(app/'hd',ignore_errors=True);(app/'hd').mkdir()
 for file in [hd/'manifest.bin',*hd.glob('*.rgba')]:shutil.copy2(file,app/'hd'/file.name)
 notices=app/'licenses';notices.mkdir(exist_ok=True)
 for label,source in {
  'PS5-host-MIT.txt':'LICENSE','PS5-port-notice.txt':'NOTICE.md','dependencies.json':'ps5/dependencies.json',
  'xboxrecomp-MIT.txt':'recomp/xboxrecomp/LICENSE','xboxrecomp-NOTICE.txt':'recomp/xboxrecomp/NOTICE',
  'xemu-LGPL.txt':'recomp/xboxrecomp/LICENSES/LGPL-2.1.txt','SDL3-zlib.txt':'build/deps/sdl3/LICENSE.txt',
  'glslang.txt':'build/deps/glslang/LICENSE.txt','SPIRV-Tools-Apache.txt':'build/deps/spirv-tools/LICENSE',
  'SPIRV-Headers.txt':'build/deps/spirv-headers/LICENSE','dav1d-BSD.txt':'build/deps/dav1d/COPYING','PS5-Vulkan-GPL.txt':'build/deps/PS5_Vulkan/LICENSE',
  'PS5-Vulkan-NOTICE.txt':'build/deps/PS5_Vulkan/NOTICE.md','PS5-PayloadSDK.txt':'build/deps/PS5_PayloadSDK/LICENSE',
  'Mesa-license.txt':'build/deps/PS5_Mesa/docs/license.rst',
  'DearImGui-MIT.txt':'ps5/third_party/imgui/LICENSE.txt','Roboto-Apache-2.0.txt':'ps5/third_party/fonts/Roboto-LICENSE.txt'}.items():
  shutil.copy2(ROOT/source,notices/label)
 shutil.copytree(ROOT/'build/deps/PS5_Mesa/licenses',notices/'Mesa',dirs_exist_ok=True)
 platform=ROOT/'build/deps/PS5_PayloadSDK/platform'
 for source in platform.rglob('*'):
  if source.is_file() and any(word in source.name.lower() for word in ['license','copying','notice']):
   dest=notices/'PS5-platform'/source.relative_to(platform);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
 shutil.copy2(ROOT/('ps5/release/README.txt' if a.release else 'README.md'),app/'README.txt')
 inv=inventory(app)
 if a.release:
  stray=[f for f in inv if f not in RELEASE_FILES and not f.startswith(('licenses/','sce_sys/')) and not re.fullmatch(r'hd/[0-9a-f]{32}\.rgba',f)]
  if stray:raise SystemExit('Not allowed in a release: '+', '.join(stray))
 receipt={'title_id':'PPSA99027','content_version':param['contentVersion'],'console_tested':False,'game_linked':True,'game_included':bool(a.game),'fself_integrity':'valid','files':inv,'source_commit':subprocess.run(['git','-C',str(ROOT),'rev-parse','HEAD'],capture_output=True,text=True).stdout.strip()}
 report_path=ROOT/'docs/console-validation.json'
 if report_path.is_file():
  report=json.loads(report_path.read_text())
  if report.get('title_id')=='PPSA99027' and report.get('eboot_sha256')==inv['eboot.bin']['sha256']:
   receipt['console_tested']=True
   receipt['console_validation']=report
 if a.release:
  archive=ROOT/'dist/release'/f"PPSA99027-{param['contentVersion']}.zip"
  archive.unlink(missing_ok=True)
  with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
   for file in sorted(inv):z.write(app/file,'PPSA99027/'+file)
  with zipfile.ZipFile(archive) as z:
   if z.testzip() or {n for n in z.namelist()}!={'PPSA99027/'+f for f in inv}:raise SystemExit('ZIP differs from staged app')
  receipt['zip']={'path':str(archive),'size':archive.stat().st_size,'sha256':digest(archive)}
 if a.pack:
  image=ROOT/'dist'/('reUndr2-PS5-PPSA99027'+('' if a.game else '-app')+'.ffpfsc')
  if image.exists():raise SystemExit('Image already exists; preserve it before rebuilding: '+str(image))
  run([sys.executable,'-m','mkpfs','pack','folder',app,image,'--compression-backend','zlib','--compression-level','6','--cpu-count','4','--require-game-files','--verify'],work/(name+'-pack.log'))
  extract=ROOT/'build'/('roundtrip-'+name)
  if extract.exists():raise SystemExit('Roundtrip folder already exists')
  run([sys.executable,'-m','mkpfs','unpack',image,extract,'--deep','--no-progress'],work/(name+'-unpack.log'))
  if inventory(extract)!=inv:raise SystemExit('Roundtrip differs from staged app')
  receipt['image']={'path':str(image),'size':image.stat().st_size,'sha256':digest(image),'roundtrip_verified':True}
 (ROOT/'dist'/(name+'-verification.json')).write_text(json.dumps(receipt,indent=2)+'\n')
 print(json.dumps({k:v for k,v in receipt.items() if k!='files'},indent=2))
if __name__=='__main__':main()
