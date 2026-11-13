#!/usr/bin/env python3
from pathlib import Path
import subprocess,shutil
root=Path(__file__).resolve().parent.parent
out=root/'build/tests';out.mkdir(parents=True,exist_ok=True)
headers=out/'include';headers.mkdir(exist_ok=True)
api=headers/'ps5platform'
if not api.exists(): api.symlink_to((root/'build/deps/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/include/ps5platform').resolve())
common=[shutil.which('clang'),'-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-DNFSU2_PS5=1','-I'+str(root/'ps5/compat'),'-I'+str(root/'ps5/src'),'-I'+str(root/'recomp/xboxrecomp/src'),'-I'+str(headers)]
for name,sources in [('path',['recomp/xboxrecomp/src/kernel/kernel_path.c']),('xbe_check',['ps5/src/xbe_check.c','ps5/src/sha256.c']),('disc_image',['ps5/src/disc_image.c']),('hd',['ps5/src/hd_textures.c']),('fmv',['ps5/src/fmv_replace.c','ps5/src/fmv_video.c']),('pad',['ps5/src/pad_map.c']),('input',['ps5/src/input_ps5.c','ps5/src/pad_map.c']),('audio',['ps5/src/audio_ps5.c']),('audio_surround',['ps5/src/audio_ps5.c']),('audio_native',['ps5/src/audio_native.c']),('audio_enhance',[]),('apu_surround',['recomp/xboxrecomp/src/apu/apu_dsp.c']),('memory',['ps5/src/memory_ps5.c','recomp/xboxrecomp/src/kernel/guest_vmem.c'])]:
    binary=out/name
    extra=[]
    if name in ('hd','fmv'):
        fixture=out/f'{name}-fixture';fixture.mkdir(exist_ok=True)
        extra=['-DNFS_HD_ROOT="'+str(fixture)+'"']
    if name=='fmv':
        dav1d=Path(subprocess.check_output(['brew','--prefix','dav1d'],text=True).strip())
        extra+=['-I'+str(dav1d/'include'),'-L'+str(dav1d/'lib'),'-ldav1d','-Wl,-rpath,'+str(dav1d/'lib')]
        subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','color=gray:s=64x64:r=30,geq=lum=30+N+100*gte(Y\\,32):cb=128:cr=128',
                        '-frames:v','120','-pix_fmt','yuv420p','-c:v','libsvtav1','-preset','12','-crf','4','-g','120',
                        str(fixture/'test.ivf')],check=True,stderr=subprocess.DEVNULL)
    subprocess.run(common+extra+[str(root/'ps5/tests'/f'{name}_test.c')]+[str(root/s) for s in sources]+['-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=25)
    (out/f'{name}.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='');result.check_returncode()
    if name=='audio_surround':
        result=subprocess.run([str(binary),'fallback'],capture_output=True,text=True,timeout=25)
        print(result.stdout+result.stderr,end='');result.check_returncode()
    if name in ('hd','fmv'):
        result=subprocess.run([str(binary),'invalid'],capture_output=True,text=True,timeout=25)
        print(result.stdout+result.stderr,end='');result.check_returncode()
