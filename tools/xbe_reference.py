#!/usr/bin/env python3
"""Write ps5/src/game_revision.h from the pinned default.xbe.

Besides the whole-file hash, it lists every section's raw size and the SHA-1
digest stored in the section header, which region, media and code patches
leave as they are. Every section except .text also gets the SHA-256 of its
data: the port runs .text recompiled, the rest it maps as the game's data."""
import argparse,hashlib,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent

def sections(data):
    if data[:4]!=b'XBEH':raise SystemExit('not an XBE')
    base,=struct.unpack_from('<I',data,0x104);count,at=struct.unpack_from('<II',data,0x11C)
    for i in range(count):
        o=at-base+i*56
        raw,size,name=struct.unpack_from('<III',data,o+12)
        yield data[name-base:data.index(b'\0',name-base)].decode(),size,data[o+36:o+56],data[raw:raw+size]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('xbe',type=Path);a=p.parse_args()
    data=a.xbe.read_bytes();rows=[]
    for name,size,digest,body in sections(data):
        sha='NULL' if name=='.text' else '"'+hashlib.sha256(body).hexdigest().upper()+'"'
        rows.append(f' {{{size},{{{",".join("0x%02x"%b for b in digest)}}},{sha}}}, /* {name} */')
    out=('/* Made by tools/xbe_reference.py from the pinned default.xbe. */\n'
         f'#define NFSU2_XBE_SHA256 "{hashlib.sha256(data).hexdigest().upper()}"\n'
         '#define NFSU2_XBE_SECTIONS {\\\n'+'\\\n'.join(rows)+'\\\n}\n')
    (ROOT/'ps5/src/game_revision.h').write_text(out)

if __name__=='__main__':main()
