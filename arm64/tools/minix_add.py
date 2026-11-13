#!/usr/bin/env python3
"""Add files to a Minix v2 image (14- or 30-character names).

usage: minix_add.py IMAGE [--grow=ZONES] [--mkdir=PATH ...] [--chmod=PATH:OCTAL ...] PATH=HOSTFILE [PATH=HOSTFILE ...]

PATH may name a directory ("/bin/ls"); the directory must exist. If PATH already exists, the old
file is removed first and its inode and zones are freed. --grow enlarges the file system to ZONES
zones first (more room for write tests). Needed because mkfs.minix and mounting a Minix image are
not available on macOS. Files up to 7 direct + 256 indirect + 256 * 256 double indirect zones (64 MByte) are supported.
"""
import struct, sys, time

ZONE = 1024

class Fs:
    def __init__(self, image, grow=0):
        self.d = bytearray(open(image, 'rb').read())
        (self.ninodes, nz16, self.imap_b, self.zmap_b, self.first, logz, _, magic, _, self.nzones) = \
            struct.unpack('<HHHHHHIHHI', self.d[1024:1048])
        if magic not in (0x2468, 0x2478) or logz != 0:
            sys.exit('not a Minix v2 image with 1 KByte zones (magic 0x%x)' % magic)
        self.dsize, self.nlen = (32, 30) if magic == 0x2478 else (16, 14)
        self.zhint = 1
        self.imap_off, self.zmap_off = 2 * ZONE, (2 + self.imap_b) * ZONE
        self.ino_off = (2 + self.imap_b + self.zmap_b) * ZONE
        if grow: self.grow(grow)

    def grow(self, zones):
        if zones < self.nzones or zones - self.first + 1 > self.zmap_b * ZONE * 8:
            sys.exit('cannot grow to %d zones' % zones)
        self.d.extend(bytes(zones * ZONE - len(self.d)))
        for z in range(self.nzones, zones):                 # new zones are free: clear their bitmap bits
            self.clearbit(self.zmap_off, z - self.first + 1)
        struct.pack_into('<I', self.d, 1024 + 20, zones)
        print('grown from %d to %d zones' % (self.nzones, zones))
        self.nzones = zones

    def bit(self, base, n): return self.d[base + n // 8] >> (n % 8) & 1
    def setbit(self, base, n): self.d[base + n // 8] |= 1 << (n % 8)
    def clearbit(self, base, n): self.d[base + n // 8] &= ~(1 << (n % 8))

    def alloc_inode(self):
        for n in range(1, self.ninodes + 1):
            if not self.bit(self.imap_off, n): self.setbit(self.imap_off, n); return n
        sys.exit('no free inode')

    def alloc_zone(self):
        for n in range(self.zhint, self.nzones - self.first + 1):   # (zones below zhint are known to be in use)
            if not self.bit(self.zmap_off, n):
                self.setbit(self.zmap_off, n); self.zhint = n + 1; return self.first + n - 1
        sys.exit('no free zone')

    def free_zone(self, z):
        self.clearbit(self.zmap_off, z - self.first + 1)
        self.zhint = min(self.zhint, z - self.first + 1)

    def inode_at(self, n): return self.ino_off + (n - 1) * 64
    def get_inode(self, n):
        o = self.inode_at(n)
        mode, nl, uid, gid, size = struct.unpack('<HHHHI', self.d[o:o + 12])
        return dict(mode=mode, nlinks=nl, uid=uid, gid=gid, size=size,
                    times=struct.unpack('<III', self.d[o + 12:o + 24]),
                    zones=list(struct.unpack('<10I', self.d[o + 24:o + 64])))
    def put_inode(self, n, ino):
        o = self.inode_at(n)
        self.d[o:o + 64] = struct.pack('<HHHHIIII10I', ino['mode'], ino['nlinks'], ino['uid'], ino['gid'],
                                       ino['size'], *ino['times'], *ino['zones'])

    def zone_data(self, z): return self.d[z * ZONE:(z + 1) * ZONE]
    def put_zone(self, z, data): self.d[z * ZONE:z * ZONE + len(data)] = data

    def all_zones(self, ino):
        """every zone used by an inode: data zones and indirect blocks"""
        z, out = ino['zones'], [x for x in ino['zones'][:7] if x]
        if z[7]:
            out.append(z[7]); out += [x for x in struct.unpack('<256I', self.zone_data(z[7])) if x]
        if z[8]:
            out.append(z[8])
            for ind in struct.unpack('<256I', self.zone_data(z[8])):
                if ind: out.append(ind); out += [x for x in struct.unpack('<256I', self.zone_data(ind)) if x]
        return out

    def free_file(self, n):
        for z in self.all_zones(self.get_inode(n)): self.free_zone(z)
        self.clearbit(self.imap_off, n)

    # ---- directories ----
    def dir_entries(self, n):
        ino = self.get_inode(n)
        for off in range(0, ino['size'], self.dsize):
            e = self.zone_data(ino['zones'][off // ZONE])[off % ZONE:off % ZONE + self.dsize]
            k, = struct.unpack('<H', e[:2])
            yield off, k, e[2:].split(b'\0')[0].decode()

    def lookup(self, n, name):
        for off, k, nm in self.dir_entries(n):
            if k and nm == name: return off, k
        return None, None

    def resolve_dir(self, path):
        cur = 1
        for part in [p for p in path.split('/') if p]:
            off, k = self.lookup(cur, part)
            if k is None or self.get_inode(k)['mode'] & 0o170000 != 0o040000:
                sys.exit('directory not found: ' + path)
            cur = k
        return cur

    def set_entry(self, dirnr, off, nr, name):
        ino = self.get_inode(dirnr)
        z = ino['zones'][off // ZONE]
        blk = bytearray(self.zone_data(z))
        blk[off % ZONE:off % ZONE + self.dsize] = struct.pack('<H', nr) + name.encode().ljust(self.dsize - 2, b'\0')
        self.put_zone(z, blk)

    def add_entry(self, dirnr, nr, name):
        ino = self.get_inode(dirnr)
        for off, k, nm in self.dir_entries(dirnr):         # reuse an unused slot
            if k == 0: self.set_entry(dirnr, off, nr, name); return
        off = ino['size']
        if off // ZONE >= 7: sys.exit('directory full')
        if off % ZONE == 0 and not ino['zones'][off // ZONE]:
            ino['zones'][off // ZONE] = self.alloc_zone()
        ino['size'] = off + self.dsize
        self.put_inode(dirnr, ino)
        self.set_entry(dirnr, off, nr, name)

    # ---- files ----
    def add_file(self, path, data, mode=0o100755, uid=0, gid=0, times=None):
        dirpath, _, name = path.rpartition('/')
        if len(name) > self.nlen: sys.exit('name too long: ' + name)
        dirnr = self.resolve_dir(dirpath or '/')
        nz = (len(data) + ZONE - 1) // ZONE
        if nz > 7 + 256 + 256 * 256: sys.exit('file too large: ' + path)
        off, old = self.lookup(dirnr, name)
        if old: self.free_file(old)                        # replace: free inode and zones of the old file
        nr = self.alloc_inode()
        zl = [self.alloc_zone() for _ in range(nz)]
        for i, z in enumerate(zl):
            self.put_zone(z, data[i * ZONE:(i + 1) * ZONE].ljust(ZONE, b'\0'))
        zones = zl[:7] + [0] * (7 - len(zl[:7]))
        zones += [0, 0, 0]
        if nz > 7:                                         # one indirect block: 256 more zones
            part = zl[7:7 + 256]
            ind = self.alloc_zone()
            self.put_zone(ind, struct.pack('<256I', *(part + [0] * (256 - len(part)))))
            zones[7] = ind
        if nz > 7 + 256:                                   # a double indirect block: up to 256 blocks of 256 zones
            dbl, blocks = self.alloc_zone(), []
            for i in range(7 + 256, nz, 256):
                part = zl[i:i + 256]
                b = self.alloc_zone()
                self.put_zone(b, struct.pack('<256I', *(part + [0] * (256 - len(part)))))
                blocks.append(b)
            self.put_zone(dbl, struct.pack('<256I', *(blocks + [0] * (256 - len(blocks)))))
            zones[8] = dbl
        t = int(time.time())
        self.put_inode(nr, dict(mode=mode, nlinks=1, uid=uid, gid=gid, size=len(data), times=times or (t, t, t), zones=zones))
        if old: self.set_entry(dirnr, off, nr, name)
        else:   self.add_entry(dirnr, nr, name)
        print('%s %-22s inode %3d, %6d bytes, %3d zones' % ('replaced' if old else 'added   ', path, nr, len(data), nz))

    def mkdir(self, path, mode=0o040755, uid=0, gid=0, times=None):
        """a new empty directory (with . and ..); the directory above it must exist; nothing happens if it exists"""
        dirpath, _, name = path.rstrip('/').rpartition('/')
        parent = self.resolve_dir(dirpath or '/')
        if self.lookup(parent, name)[1] is not None: return
        nr, z = self.alloc_inode(), self.alloc_zone()
        blk = bytearray(ZONE)
        blk[0:self.dsize] = struct.pack('<H', nr) + b'.'.ljust(self.dsize - 2, b'\0')
        blk[self.dsize:2 * self.dsize] = struct.pack('<H', parent) + b'..'.ljust(self.dsize - 2, b'\0')
        self.put_zone(z, blk)
        t = int(time.time())
        self.put_inode(nr, dict(mode=mode, nlinks=2, uid=uid, gid=gid, size=2 * self.dsize, times=times or (t, t, t), zones=[z] + [0] * 9))
        self.add_entry(parent, nr, name)
        up = self.get_inode(parent)
        up['nlinks'] += 1                                  # the .. of the new directory
        self.put_inode(parent, up)
        print('mkdir    %-22s inode %3d' % (path, nr))

    def chmod(self, path, mode):
        dirpath, _, name = path.rstrip('/').rpartition('/')
        off, nr = self.lookup(self.resolve_dir(dirpath or '/'), name)
        if nr is None: sys.exit('chmod: not found: ' + path)
        ino = self.get_inode(nr)
        ino['mode'] = (ino['mode'] & 0o170000) | mode
        self.put_inode(nr, ino)
        print('chmod    %-22s %o' % (path, mode))

    def save(self, image): open(image, 'wb').write(self.d)

def main(image, specs):
    grow = 0
    if specs and specs[0].startswith('--grow='):
        grow = int(specs[0][7:]); specs = specs[1:]
    fs = Fs(image, grow)
    for d in [s for s in specs if s.startswith('--mkdir=')]:
        fs.mkdir(d[8:])
    chmods = [s for s in specs if s.startswith('--chmod=')]
    specs = [s for s in specs if not s.startswith('--chmod=') and not s.startswith('--mkdir=')]
    for spec in specs:
        path, host = spec.split('=', 1)
        if not path.startswith('/'): path = '/' + path
        fs.add_file(path, open(host, 'rb').read())
    for c in chmods:
        path, mode = c[8:].split(':')
        fs.chmod(path, int(mode, 8))
    fs.save(image)

if __name__ == '__main__':
    if len(sys.argv) < 3: sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2:])
