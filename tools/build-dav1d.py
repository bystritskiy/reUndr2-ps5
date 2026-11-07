#!/usr/bin/env python3
"""Cross-compile the pinned dav1d for PS5 into build/dav1d, for tools/link.sh.

Meson links small programs to probe the target, and a PS5 program cannot link on
its own, so the compiler it gets only compiles for real. A link "succeeds", which
makes every function look present: the ones the SDK lacks are switched off below.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT/'build/deps/dav1d'
OUT = ROOT/'build/dav1d'
SDK = ROOT/'build/deps/PS5_Vulkan/.deps/native/ps5-payload-sdk'
MISSING = ('HAVE_ALIGNED_ALLOC', 'HAVE_MEMALIGN', 'HAVE_PTHREAD_SETNAME_NP')

WRAPPER = '''#!/bin/bash
flags=(-target x86_64-sie-ps5 --sysroot={sdk} -isystem {sdk}/target/include -fno-stack-protector
 -fno-plt -femulated-tls -fdenormal-fp-math=ieee -fPIC -ffunction-sections -fdata-sections)
for a in "$@"; do case "$a" in -c|-E|-S) exec {llvm}/clang "${{flags[@]}}" "$@";; esac; done
case "$*" in
 *-Wl,--version*) echo 'LLD 18.1.0 (compatible with GNU linkers)'; exit 0;;
 *--version*|*-dumpversion*|*-v) exec {llvm}/clang "$@";;
esac
while [ $# -gt 0 ]; do [ "$1" = -o ] && {{ : > "$2"; chmod +x "$2"; }}; shift; done
'''
CROSS = '''[binaries]
c = '{wrapper}'
ar = '{llvm}/llvm-ar'
strip = '{llvm}/llvm-strip'
nasm = 'nasm'

[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
needs_exe_wrapper = true
'''


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--llvm-bin', type=Path, default=Path('/opt/homebrew/opt/llvm@18/bin'))
    a = p.parse_args()
    url, revision = json.loads((ROOT/'ps5/dependencies.json').read_text())['sources']['dav1d']
    if not SOURCE.exists():
        run('git', 'clone', '--filter=blob:none', '--no-checkout', url, SOURCE)
        run('git', '-C', SOURCE, 'checkout', '--detach', revision)
    head = subprocess.check_output(['git', '-C', str(SOURCE), 'rev-parse', 'HEAD'], text=True).strip()
    if head != revision:
        raise SystemExit('dav1d: pin mismatch')
    OUT.mkdir(parents=True, exist_ok=True)
    wrapper = OUT/'ps5cc'
    wrapper.write_text(WRAPPER.format(sdk=SDK, llvm=a.llvm_bin))
    wrapper.chmod(0o755)
    cross = OUT/'cross.ini'
    cross.write_text(CROSS.format(wrapper=wrapper, llvm=a.llvm_bin))
    build = OUT/'obj'
    run('meson', 'setup', *(['--wipe'] if build.exists() else []), build, SOURCE, '--cross-file', cross,
        '--buildtype=release', '-Ddefault_library=static', '-Denable_tools=false', '-Denable_tests=false',
        '-Dbitdepths=["8"]', '-Dlogging=false', '-Denable_asm=true')
    config = build/'config.h'
    text = config.read_text()
    for name in MISSING:
        if f'#define {name} 1' not in text:
            raise SystemExit(f'{name}: expected Meson to report it')
        text = text.replace(f'#define {name} 1', f'#define {name} 0')
    config.write_text(text)
    run('ninja', '-C', build)


if __name__ == '__main__':
    main()
