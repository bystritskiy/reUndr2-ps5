#!/usr/bin/env python3
"""Deploy reUndr 2 as a ShadowMountPlus folder, with param.json sent last.

Files use .part then rename; game data, hd/ and licenses/ are sent only when
the recorded source changes. Close PPSA99027 before using this tool. Never
overwrite saves.
"""
import argparse
import ftplib
import json
import os
import urllib.parse
import urllib.request
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
TITLE = 'PPSA99027'
# eboot.bin (signed with --magic 0x1D3D154F) and the clean-room libc.prx.
SELF_MAGICS = (bytes.fromhex('4f153d1d'), bytes.fromhex('5414f5ee'))


def listing(ftp, path, base=''):
    files = {}
    lines = []
    ftp.retrlines('LIST '+path, lines.append)
    for line in lines:
        fields = line.split(None, 8)
        if len(fields) < 9 or fields[8] in ('.', '..'):
            continue
        name = base+fields[8]
        if line.startswith('d'):
            files.update(listing(ftp, path+'/'+fields[8], name+'/'))
        else:
            files[name] = int(fields[4])
    return files


def running(engine, addr, manager_port):
    """True while a game holds a process on the console.

    The ps5upload engine names the title; without its helper the manager on the
    console answers instead, naming processes only, so any eboot.bin stops us.
    """
    query = urllib.parse.urlencode({'addr': addr})
    processes = None
    try:
        with urllib.request.urlopen(engine+'/api/ps5/process/list?'+query, timeout=10) as response:
            processes = json.load(response).get('processes')
    except (OSError, ValueError):
        pass
    if processes:
        return any(p.get('title_id') == TITLE and p.get('name') == 'eboot.bin' for p in processes)
    url = 'http://%s:%d/processes_list' % (addr, manager_port)
    with urllib.request.urlopen(url, timeout=10) as response:
        listed = json.load(response)
    processes = listed.get('processes', []) if isinstance(listed, dict) else listed
    return any(p.get('name') == 'eboot.bin' for p in processes)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--addr', required=True, help='console address (ftpsrv)')
    p.add_argument('--port', type=int, default=2121)
    p.add_argument('--engine', default=os.environ.get('PS5UPLOAD_ENGINE', 'http://127.0.0.1:19113'))
    p.add_argument('--manager-port', type=int, default=int(os.environ.get('PS5_MANAGER_PORT', 8084)),
                   help='payload manager on the console, asked when the engine helper is absent')
    p.add_argument('--dest', default='/data/homebrew/'+TITLE)
    # Staged by tools/package.py --game.
    p.add_argument('--app', type=Path, default=ROOT/'dist/game'/TITLE)
    args = p.parse_args()
    app = args.app
    if not (app/'eboot.bin').is_file() or not (app/'sce_sys/param.json').is_file():
        raise SystemExit('No staged app folder: '+str(app))
    param = json.loads((app/'sce_sys/param.json').read_text())
    if param.get('titleId') != TITLE:
        raise SystemExit('Unexpected staged title ID')
    if running(args.engine, args.addr, args.manager_port):
        raise SystemExit('PPSA99027 is running; close it before replacing files')
    stats = {x.relative_to(app).as_posix(): x.stat() for x in app.rglob('*')
             if x.is_file() and x.name.lower() != '.ds_store'}
    local = {name: st.st_size for name, st in stats.items()}
    stamp = {name: [st.st_size, st.st_mtime_ns, st.st_ino] for name, st in stats.items()}
    record = ROOT/'build/ps5'/('deployed-'+args.dest.strip('/').replace('/', '_')+'.json')
    sent_before = json.loads(record.read_text()) if record.exists() else {}
    ftp = ftplib.FTP()
    ftp.connect(args.addr, args.port, timeout=60)
    ftp.login()
    ftp.voidcmd('TYPE I')
    try:
        remote = listing(ftp, args.dest)
    except ftplib.error_perm:
        remote = {}
    for name in sorted(remote.keys()-local.keys()):
        print('only on console:', name)
    bulky = ('game/', 'licenses/', 'hd/')
    send = [n for n in local if n not in remote or remote[n] != local[n] or not n.startswith(bulky)
            or sent_before.get(n) != stamp[n]]
    send.sort(key=lambda n: (n == 'sce_sys/param.json', not n.startswith('game/'), n))
    made = set()
    total, started = sum(local[n] for n in send), time.time()
    for name in send:
        parent = ''
        for part in [args.dest]+name.split('/')[:-1]:
            parent = parent+'/'+part if parent else part
            if parent not in made:
                try:
                    ftp.mkd(parent)
                except ftplib.error_perm:
                    pass
                made.add(parent)
        target = args.dest+'/'+name
        with open(app/name, 'rb') as fh:
            signed = fh.read(4) in SELF_MAGICS
            fh.seek(0)
            ftp.storbinary('STOR '+target+'.part', fh, blocksize=4 << 20)
        # ftpsrv exposes the decrypted ELF for SELF files. Verify loadable
        # segments separately against build/linked/eboot.elf; raw hashes differ.
        if not signed and ftp.size(target+'.part') != local[name]:
            raise SystemExit('Size mismatch, left as '+target+'.part')
        if name in remote:
            # ftpsrv answers DELE with 226, which ftplib reports as an error.
            try:
                ftp.delete(target)
            except ftplib.error_reply:
                pass
        ftp.rename(target+'.part', target)
        sent_before[name] = stamp[name]
        record.write_text(json.dumps(sent_before, indent=1)+'\n')
        print('sent', name, local[name], flush=True)
    elapsed = time.time()-started
    print('%d of %d files, %.1f MB in %.0f s' % (len(send), len(local), total/1e6, elapsed))
    ftp.quit()


if __name__ == '__main__':
    main()
