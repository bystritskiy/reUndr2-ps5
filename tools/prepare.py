#!/usr/bin/env python3
"""Regenerate the lifted game using the pinned Xbox disc; keep source input untouched."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys
r=Path(__file__).resolve().parent.parent
source=r/'source/Need for Speed Underground 2/default.xbe'
expected=re.search(r'"([A-F0-9]{64})"',(r/'ps5/src/game_revision.h').read_text()).group(1)
if hashlib.sha256(source.read_bytes()).hexdigest().upper()!=expected:raise SystemExit('Unsupported XBE: manual hooks require the pinned revision')
lock=json.loads((r/'ps5/dependencies.json').read_text())
if not (r/'upstream').exists():
 subprocess.run(['git','clone',lock['upstream_url'],str(r/'upstream')],check=True)
 subprocess.run(['git','-C',str(r/'upstream'),'checkout','--detach',lock['upstream']],check=True)
 subprocess.run(['git','-C',str(r/'upstream'),'apply',str(r/'ps5/upstream.patch')],check=True)
rev=subprocess.check_output(['git','-C',str(r/'upstream'),'rev-parse','HEAD'],text=True).strip()
if rev!=lock['upstream']:raise SystemExit('Upstream pin mismatch')
dest=r/'build/input/default.xbe';dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
subprocess.run([sys.executable,'-m','tools.xbe_parser',str(dest),'--json',str(dest.with_name('default_analysis.json'))],cwd=r/'upstream/xboxrecomp',check=True)
env=dict(os.environ,PATH=str(Path(sys.executable).parent)+os.pathsep+os.environ['PATH'],NFSU2_XBE=str(dest),NFSU2_GEN_DIR=str(r/'build/gen'))
subprocess.run(['bash',str(r/'upstream/tools/regen.sh')],cwd=r,env=env,check=True)
