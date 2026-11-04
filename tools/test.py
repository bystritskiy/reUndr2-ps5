#!/usr/bin/env python3
from pathlib import Path
import subprocess,shutil
root=Path(__file__).resolve().parent.parent
out=root/'build/tests';out.mkdir(parents=True,exist_ok=True)
headers=out/'include';headers.mkdir(exist_ok=True)
api=headers/'ps5platform'
if not api.exists(): api.symlink_to((root/'build/deps/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/include/ps5platform').resolve())
common=[shutil.which('clang'),'-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-DNFSU2_PS5=1','-I'+str(root/'ps5/compat'),'-I'+str(root/'ps5/src'),'-I'+str(root/'upstream/xboxrecomp/src'),'-I'+str(headers)]
for name,sources in [('path',['upstream/xboxrecomp/src/kernel/kernel_path.c']),('xbe_check',['ps5/src/xbe_check.c','ps5/src/sha256.c']),('disc_image',['ps5/src/disc_image.c']),('hd',['ps5/src/hd_textures.c']),('pad',['ps5/src/pad_map.c']),('input',['ps5/src/input_ps5.c','ps5/src/pad_map.c']),('audio',['ps5/src/audio_ps5.c']),('memory',['ps5/src/memory_ps5.c','upstream/xboxrecomp/src/kernel/guest_vmem.c'])]:
    binary=out/name
    extra=[]
    if name=='hd':
        fixture=out/'hd-fixture';fixture.mkdir(exist_ok=True)
        extra=['-DNFS_HD_ROOT="'+str(fixture)+'"']
    subprocess.run(common+extra+[str(root/'ps5/tests'/f'{name}_test.c')]+[str(root/s) for s in sources]+['-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=25)
    (out/f'{name}.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='');result.check_returncode()
    if name=='hd':
        result=subprocess.run([str(binary),'invalid'],capture_output=True,text=True,timeout=25)
        print(result.stdout+result.stderr,end='');result.check_returncode()
