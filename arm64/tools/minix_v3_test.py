#!/usr/bin/env python3
"""Host-side test of the extended file system format (Minix v3) and of the host tools (issue #141).

usage: minix_v3_test.py

Makes images with minix_mkfs.py, fills them with minix_add.py (as a library) and reads them with the independent parser of
minix_check.py: more than 65535 inodes, a directory of 600 entries (indirect zones), names of 60 characters, a name like CPython's
__pycache__ files, a file of 3 MByte (double indirect zones), a deep tree, and the old v2 format next to it. Every check prints ok or FAIL.
"""
import os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from minix_check import Minix, check
from minix_add import Fs

fails = 0
def ok(cond, what):
    global fails
    print('  %s: %s' % ('ok  ' if cond else 'FAIL', what))
    if not cond: fails += 1

def mkfs(path, zones, *args):
    subprocess.run([sys.executable, os.path.join(HERE, 'minix_mkfs.py'), path, str(zones), *args], check=True, stdout=subprocess.DEVNULL)

def refused(fs, path):
    try: fs.add_file(path, b''); return False
    except SystemExit: return True

tmp = tempfile.mkdtemp()
img = os.path.join(tmp, 'v3.img')
mkfs(img, 40000, '--inodes=70000')                       # more inodes than a 16-bit number can count
fs = Fs(img)
ok(fs.version == 3 and fs.dsize == 64 and fs.nlen == 60 and fs.ninodes == 70000, 'v3 image: 70000 inodes, 64-byte entries, 60-character names')

pyc = '/__pycache__/_collections_abc.cpython-312.pyc'
fs.mkdir('/__pycache__')
fs.add_file(pyc, b'pyc' * 1000)
long60 = 'n' * 60
fs.add_file('/' + long60, b'sixty')
fs.mkdir('/many')
for i in range(600):                                     # 600 entries: 38 blocks of 16, the 8th and later through the indirect zone
    fs.add_file('/many/file%04d' % i, b'x%d' % i)
big = bytes((i * 7 + i // 251) & 255 for i in range(3 * 1024 * 1024))
fs.add_file('/big.bin', big)
fs.mkdir_p('/a/b/c/d/e')
fs.add_file('/a/b/c/d/e/' + 'z' * 57 + '.py', b'deep')
ok(refused(fs, '/' + 'q' * 61), 'a name of 61 characters is refused')
fs.min_inode = 65536                                     # inode numbers that do not fit into 16 bits
fs.mkdir('/hi')
fs.add_file('/hi/high.txt', b'inode number above 65535')
fs.min_inode = 1
fs.save(img)

m = Minix(img)
problems, lz, li, inodes, zones = check(m)
ok(not problems and not lz and len(li) == 0, 'minix_check: no problems, no leaked zones or inodes (%d inodes in use)' % len(inodes))
ok(m.read(m.lookup(pyc)) == b'pyc' * 1000, '__pycache__/_collections_abc.cpython-312.pyc (43 characters) is found and reads back')
ok(m.read(m.lookup('/' + long60)) == b'sixty', 'a name of 60 characters')
names = [nm for k, nm in m.readdir(m.lookup('/many')) if nm not in ('.', '..')]
ok(len(names) == 600 and m.read(m.lookup('/many/file0599')) == b'x599' and m.read(m.lookup('/many/file0000')) == b'x0',
   'a directory of 600 entries (it needs indirect zones): all there, first and last read back')
ok(m.inode(m.lookup('/many'))['size'] == 602 * 64 and m.inode(m.lookup('/many'))['zones'][7] != 0, 'its inode has a size of 602 entries and an indirect zone')
ok(m.read(m.lookup('/big.bin')) == big and m.inode(m.lookup('/big.bin'))['zones'][8] != 0, 'a file of 3 MByte through the double indirect zone')
ok(m.lookup('/hi/high.txt') > 65535 and m.read(m.lookup('/hi/high.txt')) == b'inode number above 65535' and m.lookup('/hi') > 65535,
   'a file and a directory with inode numbers above 65535: found through the directory entries (4-byte numbers), content reads back')
ok(m.lookup('/a/b/c/d/e/' + 'z' * 57 + '.py') is not None, 'a deep path')

img2 = os.path.join(tmp, 'v2.img')                       # the old format still works with the same tools
mkfs(img2, 2000, '--v2')
fs = Fs(img2)
fs.add_file('/old.txt', b'old format')
fs.add_file('/big', big[:400000])
ok(refused(fs, '/' + 'q' * 31), 'v2: a name of 31 characters is refused')
fs.save(img2)
m = Minix(img2)
problems, lz, li, inodes, zones = check(m)
ok(m.version == 2 and not problems and m.read(m.lookup('/old.txt')) == b'old format' and m.read(m.lookup('/big')) == big[:400000],
   'v2 image: consistent, files read back (a file of 400000 bytes)')
print('minix_v3_test: %d failed' % fails)
sys.exit(1 if fails else 0)
