"""Check or re-sign an NFSU2 (Xbox) save file for reUndr2: HMAC-SHA1, key = 16 zero bytes.
usage: save-resign.py SAVEFILE [--write]"""
import hmac,hashlib,struct,sys
p=sys.argv[1]; d=bytearray(open(p,'rb').read())
h=lambda b: hmac.new(bytes(16),bytes(b),hashlib.sha1).digest()
magic,total,a,b=struct.unpack_from('<4sIII',d,0)
assert magic==b'20CM' and total==len(d)==0x4C+a+b, 'not an NFSU2 save'
new=[h(d[0x4C:0x4C+a]),h(d[0x4C+a:])]
ok=[new[0]==d[0x10:0x24],new[1]==d[0x24:0x38]]
d[0x10:0x24],d[0x24:0x38]=new
ok.append(h(d[:0x38])==d[0x38:0x4C]); d[0x38:0x4C]=h(d[:0x38])
print('signatures valid' if all(ok) else 'signatures differ', ok)
if '--write' in sys.argv: open(p,'wb').write(d); print('re-signed', p)
