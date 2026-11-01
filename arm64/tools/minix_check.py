#!/usr/bin/env python3
"""Read-only consistency check and dump for a Minix v2 image (independent of the kernel code).

usage: minix_check.py IMAGE [--files] [--cat PATH]

Walks the directory tree from the root inode, collects the inodes and zones in use and compares
them with the bitmaps and the link counts.  Prints the number of problems; exit status 1 if any.
--files lists every file with size and byte sum, --cat PATH writes one file to stdout.
"""
import struct, sys

class Minix:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        (self.ninodes, _, self.imap_b, self.zmap_b, self.first, self.logz, _,
         self.magic, _, self.nzones) = struct.unpack('<HHHHHHIHHI', self.d[1024:1048])
        assert self.magic in (0x2468, 0x2478) and self.logz == 0, 'unsupported file system'
        self.dsize, self.nlen = (32, 30) if self.magic == 0x2478 else (16, 14)
        self.ist = (2 + self.imap_b + self.zmap_b) * 1024
    def blk(self, z): return self.d[z * 1024:(z + 1) * 1024]
    def inode(self, n):
        o = self.ist + (n - 1) * 64
        mode, nl, uid, gid, size, at, mt, ct = struct.unpack('<HHHHIIII', self.d[o:o + 24])
        return dict(mode=mode, nlinks=nl, size=size, zones=list(struct.unpack('<10I', self.d[o + 24:o + 64])))
    def zones_of(self, ino):
        """(data zones in file order, metadata zones)"""
        z, meta, data = ino['zones'], [], []
        data += [x for x in z[:7]]
        if z[7]:
            meta.append(z[7]); data += list(struct.unpack('<256I', self.blk(z[7])))
        if z[8]:
            meta.append(z[8])
            for ind in struct.unpack('<256I', self.blk(z[8])):
                if ind:
                    meta.append(ind); data += list(struct.unpack('<256I', self.blk(ind)))
                else: data += [0] * 256
        return data, meta
    def read(self, n):
        ino = self.inode(n); data, _ = self.zones_of(ino)
        nblocks = (ino['size'] + 1023) // 1024
        return b''.join(self.blk(z) if z else b'\0' * 1024 for z in data[:nblocks])[:ino['size']]
    def readdir(self, n):
        raw, out = self.read(n), []
        for i in range(0, len(raw), self.dsize):
            k, = struct.unpack('<H', raw[i:i + 2])
            if k: out.append((k, raw[i + 2:i + self.dsize].split(b'\0')[0].decode()))
        return out
    def bit(self, base, n): return self.d[base + n // 8] >> (n % 8) & 1
    def lookup(self, path):
        cur = 1
        for part in [p for p in path.split('/') if p]:
            cur = dict((nm, k) for k, nm in self.readdir(cur)).get(part)
            if cur is None: return None
        return cur

def check(fs):
    problems, seen_inodes, zones, links = [], {}, {}, {}
    def walk(n, path):
        if n in seen_inodes: return
        ino = fs.inode(n); seen_inodes[n] = path
        data, meta = fs.zones_of(ino)
        nblocks = (ino['size'] + 1023) // 1024
        for z in data[:nblocks] + meta:
            if z:
                if z in zones: problems.append('zone %d used by %s and %s' % (z, zones[z], path))
                zones[z] = path
                if z < fs.first or z >= fs.nzones: problems.append('zone %d of %s out of range' % (z, path))
        if ino['mode'] & 0o170000 == 0o040000:
            for k, nm in fs.readdir(n):
                links[k] = links.get(k, 0) + 1
                if nm not in ('.', '..'): walk(k, path.rstrip('/') + '/' + nm)
    walk(1, '/')
    for n, path in seen_inodes.items():
        ino = fs.inode(n)
        if ino['nlinks'] != links.get(n, 0) and n != 1:
            problems.append('inode %d (%s): link count %d, but %d directory entries' % (n, path, ino['nlinks'], links.get(n, 0)))
        if not fs.bit(2 * 1024, n): problems.append('inode %d (%s) in use but free in the inode bitmap' % (n, path))
    zmap = (2 + fs.imap_b) * 1024
    for z, path in zones.items():
        if not fs.bit(zmap, z - fs.first + 1): problems.append('zone %d (%s) in use but free in the zone bitmap' % (z, path))
    leaked_z = [z for z in range(fs.first, fs.nzones) if fs.bit(zmap, z - fs.first + 1) and z not in zones]
    leaked_i = [n for n in range(1, fs.ninodes + 1) if fs.bit(2 * 1024, n) and n not in seen_inodes]
    return problems, leaked_z, leaked_i, seen_inodes, zones

if __name__ == '__main__':
    fs = Minix(sys.argv[1])
    if '--cat' in sys.argv:
        n = fs.lookup(sys.argv[sys.argv.index('--cat') + 1])
        sys.stdout.buffer.write(fs.read(n)); sys.exit(0)
    problems, lz, li, inodes, zones = check(fs)
    if '--files' in sys.argv:
        for n, path in sorted(inodes.items(), key=lambda x: x[1]):
            ino = fs.inode(n)
            kind = {0o040000: 'dir', 0o100000: 'file', 0o120000: 'link'}.get(ino['mode'] & 0o170000, '?')
            print('%-5s %-28s inode %3d nlinks %d size %7d sum %d' % (
                kind, path, n, ino['nlinks'], ino['size'], sum(fs.read(n)) if kind != 'dir' else 0))
    free = sum(1 for z in range(fs.first, fs.nzones) if not fs.bit((2 + fs.imap_b) * 1024, z - fs.first + 1))
    print('inodes in use %d, zones in use %d, free zones %d' % (len(inodes), len(zones), free))
    print('leaked zones (marked used, not reachable): %d, leaked inodes: %d' % (len(lz), len(li)))
    for p in problems: print('PROBLEM:', p)
    print('PROBLEMS: %d' % len(problems))
    sys.exit(1 if problems else 0)
