#!/usr/bin/env python3
"""Copy the files and directories of one Minix v2 image into another (empty) one.

usage: minix_copy.py SOURCE-IMAGE DEST-IMAGE

The root disk of the x86 Ulix has 1.4 MByte and 480 inodes. A bigger program (the Python interpreter is 6 MByte) and its
library need a bigger file system, and the zone map of an existing image cannot grow. So DEST-IMAGE is made first with
minix_mkfs.py (any size), and this tool copies the tree into it: names, modes, owners and dates stay, the inode numbers are new.
Hard links become separate files (the x86 image has none that matter).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from minix_check import Minix
from minix_add import Fs

def copy(src, dst):
    done = {}
    def walk(n, path):
        for k, name in src.readdir(n):
            if name in ('.', '..'): continue
            ino, p = src.inode(k), path + '/' + name
            full = src.d[src.ist + (k - 1) * 64:src.ist + k * 64]
            uid, gid = int.from_bytes(full[4:6], 'little'), int.from_bytes(full[6:8], 'little')
            times = tuple(int.from_bytes(full[12 + 4 * i:16 + 4 * i], 'little') for i in range(3))
            if ino['mode'] & 0o170000 == 0o040000:
                dst.mkdir(p, ino['mode'], uid, gid, times)
                walk(k, p)
            else:
                dst.add_file(p, src.read(k), ino['mode'], uid, gid, times)
    walk(1, '')

if len(sys.argv) != 3: sys.exit(__doc__)
fs = Fs(sys.argv[2])
copy(Minix(sys.argv[1]), fs)
fs.save(sys.argv[2])
