#!/usr/bin/env python3
"""Make an empty Minix file system (1 KByte zones) in a new image file.

usage: minix_mkfs.py IMAGE ZONES [--inodes=N] [--v2]

Like mkfs.minix -3 (default) or mkfs.minix -2 -n 30 (--v2), which are not available on macOS. The image gets an empty root directory.
Version 3 (magic 0x4D5A, the layout of Linux) is the extended format of the ARM64 port (section 45 of ARM64-PORT.md): 32-bit inode numbers,
64-byte directory entries with names of 60 characters, directories that grow through indirect zones. Version 2 has 65535 inodes at most, names
of 30 characters and (with the book's old directory code) 224 entries per directory; it stays for the small serial disk and for tests.
--inodes sets the number of inodes (default: a quarter of the zones). The first data zone follows from it (superblock, bitmaps and the
inode table come first): the 1.4 MByte image of the book has 464 inodes and starts its data at zone 34, the root disk of the ARM64 port has
2048 inodes and starts at zone 135. The book reads the first data zone from the superblock (never assumes 34), so any number of inodes works.
Also used for the second disk of the ARM64 port (/dev/hdb, mounted on /tmp like on the x86).
"""
import struct, sys, time

ZONE = 1024
opts = [a for a in sys.argv[3:] if a.startswith('--inodes=')]
v2 = '--v2' in sys.argv[3:]
if len(sys.argv) < 3 or len(sys.argv) - 3 != len(opts) + v2: sys.exit(__doc__)
image, nzones = sys.argv[1], int(sys.argv[2])

ninodes = int(opts[0][9:]) if opts else nzones // 4
imap_b = (ninodes + 1 + 8 * ZONE - 1) // (8 * ZONE)
inode_blocks = (ninodes * 64 + ZONE - 1) // ZONE
# the zone map needs one bit per data zone (bit 0 of the map is unused); first is the number of the first data zone
zmap_b = 1
while True:
    first = 2 + imap_b + zmap_b + inode_blocks
    need = (nzones - first + 1 + 8 * ZONE - 1) // (8 * ZONE)
    if need <= zmap_b: break
    zmap_b = need
first = 2 + imap_b + zmap_b + inode_blocks

d = bytearray(nzones * ZONE)
if v2:
    struct.pack_into('<HHHHHHIHHI', d, 1024, ninodes, 0, imap_b, zmap_b, first, 0, 0x7fffffff, 0x2478, 0, nzones)
else:                                                        # the superblock of version 3 (struct minix3_super_block of Linux)
    struct.pack_into('<IHHHHHHIIHHHB', d, 1024, ninodes, 0, imap_b, zmap_b, first, 0, 0, 0x7fffffff, nzones, 0x4D5A, 0, ZONE, 3)
    assert struct.unpack_from('<H', d, 1024 + 24)[0] == 0x4D5A

def setbit(base, n): d[base + n // 8] |= 1 << (n % 8)
imap, zmap = 2 * ZONE, (2 + imap_b) * ZONE
for n in range(0, imap_b * 8 * ZONE):                       # inode 0 does not exist, the root directory is inode 1;
    if n in (0, 1) or n > ninodes: setbit(imap, n)          # bits past the last inode are marked as used
for n in range(0, zmap_b * 8 * ZONE):                       # bit 0 is unused, bit 1 is the first data zone (the root directory)
    if n in (0, 1) or n > nzones - first: setbit(zmap, n)

nlen = 30 if v2 else 60
ib = 2 if v2 else 4
entries = (1).to_bytes(ib, 'little') + b'.'.ljust(nlen, b'\0') + (1).to_bytes(ib, 'little') + b'..'.ljust(nlen, b'\0')
now = int(time.time())
ino_off = (2 + imap_b + zmap_b) * ZONE
struct.pack_into('<HHHHIIII10I', d, ino_off, 0o041777, 2, 0, 0, len(entries), now, now, now, first, *([0] * 9))
d[first * ZONE:first * ZONE + len(entries)] = entries

open(image, 'wb').write(d)
print('%s: Minix v%d, %d zones (%d KByte), %d inodes, first data zone %d' % (image, 2 if v2 else 3, nzones, nzones, ninodes, first))
