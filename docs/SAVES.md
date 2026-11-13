# Save files

Saves are in `/data/nfsu2/PPSA99027/save/UserData/4541005a/<profile>/`, laid out
like on the Xbox: `SaveMeta.xbx`, `saveimage.xbx`, and the profile file named after
the profile.

## Profile file format

| Offset | Size | Contents |
|---|---|---|
| `0x00` | 4 | magic `20CM` |
| `0x04` | 4 | total file size |
| `0x08` | 4 | size of part A (8 in the saves seen so far) |
| `0x0C` | 4 | size of part B |
| `0x10` | 20 | signature of part A |
| `0x24` | 20 | signature of part B |
| `0x38` | 20 | signature of bytes `0x00..0x37` |
| `0x4C` | A + B | part A, then part B |

Each signature is the XAPI roaming save signature (`XCalculateSignatureBegin`
with flags 0): HMAC-SHA1 keyed with the kernel export `XboxSignatureKey`. There is
no HDD-bound (no-roam) signature. The port leaves that key at 16 zero bytes
(`recomp/xboxrecomp/src/kernel/kernel_bridge.c`), so the key is `00 * 16`.

The game checks all three on load (`sub_00256117`, `repe cmpsd` against the
stored signatures) and reports the save as damaged on a mismatch. Saves are
written by `sub_00255B37`.

To edit a save, change its data, then run `tools/save-resign.py <file> --write`.
A save from a real Xbox has the same format but is signed with the console key, so
it will not load until it is re-signed the same way.

FTP cannot overwrite these files at first, because the game created them (uid 1,
`rw-r--r--`): run `SITE CHMOD 777` on the folder and `SITE CHMOD 666` on the
file first.

## Validation

2026-11-06, PS5, PPSA99027, profile `MVP` (53 868 bytes, written by the port):
all three stored signatures matched HMAC-SHA1 with a zero key. One byte in part B
(offset `0x834`, filler `d` → `e`) was changed, the file was re-signed with
`tools/save-resign.py`, uploaded over FTP, and the game loaded the profile
normally. This was not checked: whether the data has an EA checksum covering
other bytes, and loading a save from a real Xbox.
