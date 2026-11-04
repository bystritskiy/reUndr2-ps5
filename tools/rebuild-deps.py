#!/usr/bin/env python3
"""Rebuild the pinned public PS5 dependencies; generated/downloaded files stay in build/."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,tarfile,tempfile
ROOT=Path(__file__).resolve().parent.parent
DEPS=ROOT/'build/deps'
LOCK=json.loads((ROOT/'ps5/dependencies.json').read_text())

def run(args,**kwargs):
 print('+',subprocess.list2cmdline([str(x) for x in args]),flush=True)
 subprocess.run([str(x) for x in args],cwd=ROOT,check=True,**kwargs)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--llvm-bin',type=Path,default=Path('/opt/homebrew/opt/llvm@18/bin'));p.add_argument('--jobs',type=int,default=8);a=p.parse_args()
 DEPS.mkdir(parents=True,exist_ok=True)
 for name,(url,rev) in LOCK['sources'].items():
  dest=DEPS/name
  if not dest.exists():
   run(['git','clone','--filter=blob:none','--no-checkout',url,dest]);run(['git','-C',dest,'checkout','--detach',rev])
  actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
  if actual!=rev:raise SystemExit(name+': pin mismatch; preserve local changes before switching')
 mesa=DEPS/'PS5_Vulkan/.deps/work/radv-src';marker=mesa/'.revision';revision=LOCK['sources']['PS5_Mesa'][1]
 if not marker.exists():
  if mesa.exists() and any(mesa.iterdir()):raise SystemExit('Existing Mesa export needs review')
  mesa.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryFile() as f:
   run(['git','-C',DEPS/'PS5_Mesa','archive',revision],stdout=f);f.seek(0)
   with tarfile.open(fileobj=f,mode='r|') as tf:tf.extractall(mesa,filter='data')
  marker.write_text(revision+'\n')
 elif marker.read_text().strip()!=revision:raise SystemExit('Mesa export revision mismatch')
 run(['docker','build','-t','reundr2-ps5-builder:local','-f','ps5/Dockerfile','ps5'])
 def container(command):
  run(['docker','run','--rm','-v',str(ROOT)+':'+str(ROOT),'-w',ROOT,'-e','BUILD_JOBS='+str(a.jobs),'reundr2-ps5-builder:local','bash','-lc',command])
 import shlex
 q=shlex.quote
 container('set -e; python3 -m venv --system-site-packages build/deps/linux-tools; build/deps/linux-tools/bin/pip install meson==1.9.0; '
  'export PATH='+q(str(DEPS/'linux-tools/bin'))+':/usr/lib/llvm-18/bin:$PATH; cd build/deps/PS5_Vulkan; bash tools/setup-native-dependencies.sh; '
  'MESON='+q(str(DEPS/'linux-tools/bin/meson'))+' NINJA=/usr/bin/ninja bash tools/build-radv.sh release; CXX=clang++-18 bash tools/rebuild-libc.sh')
 crt=DEPS/'compiler-rt';crt.mkdir(exist_ok=True);builtin=crt/'libclang_rt.builtins-x86_64.a'
 if not builtin.exists():
  run(['docker','run','--rm','--platform','linux/amd64','-v',str(crt)+':/out','ubuntu:24.04','bash','-lc',
   'set -e; apt-get update -qq; cd /tmp; apt-get download libclang-rt-18-dev=1:18.1.3-1ubuntu1; dpkg-deb -x libclang-rt-18-dev*.deb extracted; cp extracted/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a /out/'])
 if hashlib.sha256(builtin.read_bytes()).hexdigest()!=LOCK['compiler_rt']['archive_sha256']:raise SystemExit('compiler-rt hash mismatch')
 # Only SDL video/WSI is needed: audio and input use native PS5 APIs.
 sdl=DEPS/'sdl3';shutil.copy2(ROOT/'ps5/sdl/SDL_ps5vulkan.c',sdl/'src/video/offscreen/SDL_offscreenvulkan.c')
 sdk=DEPS/'PS5_Vulkan/.deps/native/ps5-payload-sdk'
 options=['SDL_STATIC=ON','SDL_SHARED=OFF','SDL_UNIX_CONSOLE_BUILD=ON','SDL_X11=OFF','SDL_WAYLAND=OFF','SDL_KMSDRM=OFF','SDL_OPENGL=OFF','SDL_OPENGLES=OFF','SDL_VULKAN=ON','SDL_OFFSCREEN=ON','SDL_HIDAPI=OFF','SDL_JOYSTICK=OFF','SDL_AUDIO=OFF','SDL_HAPTIC=OFF','SDL_SENSOR=OFF','SDL_OSS=OFF','SDL_TEST_LIBRARY=OFF','SDL_TESTS=OFF','SDL_EXAMPLES=OFF','LIBC_HAS_WCSNLEN=OFF','LIBC_HAS_WCSLCPY=OFF','LIBC_HAS_WCSLCAT=OFF']
 common=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'ps5/cmake/ps5.cmake'),'-DCMAKE_C_COMPILER='+str(a.llvm_bin/'clang'),'-DCMAKE_CXX_COMPILER='+str(a.llvm_bin/'clang++'),'-DPS5_PAYLOAD_SDK='+str(sdk),'-DCMAKE_BUILD_TYPE=Release']
 run(['cmake','-S',sdl,'-B',ROOT/'build/sdl3','-G','Ninja',*common,*['-D'+x for x in options]]);run(['cmake','--build',ROOT/'build/sdl3','-j',a.jobs])
 run(['cmake','-S',ROOT/'ps5/shaders','-B',ROOT/'build/shader-compiler','-G','Ninja',*common]);run(['cmake','--build',ROOT/'build/shader-compiler','--target','glslang','glslang-default-resource-limits','SPIRV-Tools-opt','SPIRV-Tools-static','-j',a.jobs])
if __name__=='__main__':main()
