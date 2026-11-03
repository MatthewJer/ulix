#!/usr/bin/env python3
"""Make an empty Minix v2 file system (1 KByte zones, 30 character names) in a new image file.

usage: minix_mkfs.py IMAGE ZONES

Like mkfs.minix -2 -n 30, which is not available on macOS. The image gets an empty root directory.
Used for the second disk of the ARM64 port (/dev/hdb, mounted on /tmp like on the x86).
"""
import struct, sys, time

ZONE = 1024
if len(sys.argv) != 3: sys.exit(__doc__)
image, nzones = sys.argv[1], int(sys.argv[2])

ninodes = nzones // 4
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
struct.pack_into('<HHHHHHIHHI', d, 1024, ninodes, 0, imap_b, zmap_b, first, 0, 0x7fffffff, 0x2478, 0, nzones)

def setbit(base, n): d[base + n // 8] |= 1 << (n % 8)
imap, zmap = 2 * ZONE, (2 + imap_b) * ZONE
for n in range(0, imap_b * 8 * ZONE):                       # inode 0 does not exist, the root directory is inode 1;
    if n in (0, 1) or n > ninodes: setbit(imap, n)          # bits past the last inode are marked as used
for n in range(0, zmap_b * 8 * ZONE):                       # bit 0 is unused, bit 1 is the first data zone (the root directory)
    if n in (0, 1) or n > nzones - first: setbit(zmap, n)

now = int(time.time())
ino_off = (2 + imap_b + zmap_b) * ZONE
struct.pack_into('<HHHHIIII10I', d, ino_off, 0o041777, 2, 0, 0, 64, now, now, now, first, *([0] * 9))
entries = struct.pack('<H', 1) + b'.'.ljust(30, b'\0') + struct.pack('<H', 1) + b'..'.ljust(30, b'\0')
d[first * ZONE:first * ZONE + len(entries)] = entries

open(image, 'wb').write(d)
print('%s: %d zones (%d KByte), %d inodes, first data zone %d' % (image, nzones, nzones, ninodes, first))
