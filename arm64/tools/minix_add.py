#!/usr/bin/env python3
"""Add files to a Minix v2 image (14- or 30-character names) or a Minix v3 image (60-character names, 32-bit inode numbers).

usage: minix_add.py IMAGE [--grow=ZONES] [--mkdir=PATH ...] [--chmod=PATH:OCTAL ...] [--exclude=NAME,NAME ...]
                          [--tree=PATH=HOSTDIR ...] [--hi-inode=PATH=HOSTFILE ...] PATH=HOSTFILE [PATH=HOSTFILE ...]

PATH may name a directory ("/bin/ls"); the directory must exist. If PATH already exists, the old
file is removed first and its inode and zones are freed. --grow enlarges the file system to ZONES
zones first (more room for write tests). Needed because mkfs.minix and mounting a Minix image are
not available on macOS. Files up to 7 direct + 256 indirect + 256 * 256 double indirect zones (64 MByte) are supported.

--hi-inode=PATH=HOSTFILE adds a file whose inode number is at least 65536 (a test of 32-bit inode numbers; the image needs more inodes).

--tree=PATH=HOSTDIR copies the Python files (*.py) of a directory tree of the host, with all its subdirectories (the Python
standard library, see the Makefile). Directories and files whose name is in an --exclude list (any depth) are left out. A name
longer than the file system allows (30 or 60 characters), or a directory of more than 224 entries on a v2 image (the old directory code
of the book reads seven direct zones of 32 entries each), stops the tool: nothing is cut off silently. A directory of a v3 image grows
through indirect zones like any file.
"""
import os, struct, sys, time

ZONE = 1024

class Fs:
    def __init__(self, image, grow=0):
        self.d = bytearray(open(image, 'rb').read())
        if struct.unpack('<H', self.d[1024 + 24:1024 + 26])[0] == 0x4D5A:           # version 3
            (self.ninodes, _, self.imap_b, self.zmap_b, self.first, logz, _, _, self.nzones, magic) = \
                struct.unpack('<IHHHHHHIIH', self.d[1024:1050])
            self.dsize, self.nlen, self.version = 64, 60, 3
        else:
            (self.ninodes, nz16, self.imap_b, self.zmap_b, self.first, logz, _, magic, _, self.nzones) = \
                struct.unpack('<HHHHHHIHHI', self.d[1024:1048])
            if magic not in (0x2468, 0x2478): sys.exit('not a Minix v2 or v3 image (magic 0x%x)' % magic)
            self.dsize, self.nlen = (32, 30) if magic == 0x2478 else (16, 14)
            self.version = 2
        if logz != 0: sys.exit('not a file system with 1 KByte zones')
        self.zhint = 1
        self.min_inode = 1                                 # --hi-inode: allocate from here on
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
        for n in range(self.min_inode, self.ninodes + 1):
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

    # ---- zones of a file: logical block -> zone, and the other way (also used for directories, which are files) ----
    def zone_of(self, ino, n):
        z = ino['zones']
        if n < 7: return z[n]
        n -= 7
        if n < 256: return struct.unpack_from('<I', self.d, z[7] * ZONE + 4 * n)[0] if z[7] else 0
        n -= 256
        if not z[8]: return 0
        ind = struct.unpack_from('<I', self.d, z[8] * ZONE + 4 * (n // 256))[0]
        return struct.unpack_from('<I', self.d, ind * ZONE + 4 * (n % 256))[0] if ind else 0

    def new_zone(self):
        z = self.alloc_zone()
        self.put_zone(z, bytes(ZONE))
        return z

    def map_zone(self, ino, n):
        """a new, empty zone for logical block n of the inode (the pointers in the inode and the indirect zones are set; the caller writes the inode)"""
        z, new = ino['zones'], self.new_zone()
        if n < 7:
            z[n] = new
        elif n < 7 + 256:
            if not z[7]: z[7] = self.new_zone()
            struct.pack_into('<I', self.d, z[7] * ZONE + 4 * (n - 7), new)
        else:
            m = n - 7 - 256
            if not z[8]: z[8] = self.new_zone()
            slot = z[8] * ZONE + 4 * (m // 256)
            ind = struct.unpack_from('<I', self.d, slot)[0]
            if not ind:
                ind = self.new_zone()
                struct.pack_into('<I', self.d, slot, ind)
            struct.pack_into('<I', self.d, ind * ZONE + 4 * (m % 256), new)
        return new

    # ---- directories ----
    def pack_entry(self, nr, name):
        ib = self.dsize - self.nlen                        # bytes of the inode number: 2 or 4
        return nr.to_bytes(ib, 'little') + name.encode().ljust(self.nlen, b'\0')

    def dir_entries(self, n):
        ino = self.get_inode(n)
        ib = self.dsize - self.nlen
        for off in range(0, ino['size'], self.dsize):
            z = self.zone_of(ino, off // ZONE)
            if not z: yield off, 0, ''; continue
            e = self.d[z * ZONE + off % ZONE:z * ZONE + off % ZONE + self.dsize]
            yield off, int.from_bytes(e[:ib], 'little'), e[ib:].split(b'\0')[0].decode()

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
        z = self.zone_of(self.get_inode(dirnr), off // ZONE)
        o = z * ZONE + off % ZONE
        self.d[o:o + self.dsize] = self.pack_entry(nr, name)

    def add_entry(self, dirnr, nr, name):
        ino = self.get_inode(dirnr)
        for off, k, nm in self.dir_entries(dirnr):         # reuse an unused slot
            if k == 0 and self.zone_of(ino, off // ZONE): self.set_entry(dirnr, off, nr, name); return
        off = ino['size']
        if self.version == 2 and off // ZONE >= 7: sys.exit('directory full (version 2: 224 entries)')
        if not self.zone_of(ino, off // ZONE): self.map_zone(ino, off // ZONE)
        ino['size'] = off + self.dsize
        self.put_inode(dirnr, ino)
        self.set_entry(dirnr, off, nr, name)

    # ---- files ----
    def add_file(self, path, data, mode=0o100755, uid=0, gid=0, times=None):
        dirpath, _, name = path.rpartition('/')
        if len(name.encode()) > self.nlen: sys.exit('name too long (%d characters at most): %s' % (self.nlen, name))
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
        blk[0:self.dsize] = self.pack_entry(nr, '.')
        blk[self.dsize:2 * self.dsize] = self.pack_entry(parent, '..')
        self.put_zone(z, blk)
        t = int(time.time())
        self.put_inode(nr, dict(mode=mode, nlinks=2, uid=uid, gid=gid, size=2 * self.dsize, times=times or (t, t, t), zones=[z] + [0] * 9))
        self.add_entry(parent, nr, name)
        up = self.get_inode(parent)
        up['nlinks'] += 1                                  # the .. of the new directory
        self.put_inode(parent, up)
        print('mkdir    %-22s inode %3d' % (path, nr))

    def mkdir_p(self, path):
        """mkdir for every directory on the way (the directories that exist stay as they are)"""
        cur = ''
        for part in [p for p in path.split('/') if p]:
            cur += '/' + part
            self.mkdir(cur)

    def add_tree(self, dest, src, excluded, suffix='.py'):
        """the files of the host directory SRC with the given suffix, and the directories on the way, below the image path DEST"""
        self.mkdir_p(dest)
        count = 0
        for root, dirs, files in os.walk(src):
            dirs[:] = sorted(d for d in dirs if d not in excluded)
            rel = os.path.relpath(root, src)
            target = dest if rel == '.' else dest + '/' + rel
            self.mkdir_p(target)
            for f in sorted(files):
                if f.endswith(suffix) and f not in excluded:
                    self.add_file(target + '/' + f, open(os.path.join(root, f), 'rb').read())
                    count += 1
        print('tree     %-22s %d files from %s' % (dest, count, src))

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
    excluded = set(','.join(s[10:] for s in specs if s.startswith('--exclude=')).split(','))
    for t in [s for s in specs if s.startswith('--tree=')]:
        path, host = t[7:].split('=', 1)
        fs.add_tree(path, host, excluded)
    for h in [s for s in specs if s.startswith('--hi-inode=')]:       # a file whose inode number is above 65535 (needs a v3 image with that many inodes)
        path, host = h[11:].split('=', 1)
        if fs.ninodes <= 65536: sys.exit('--hi-inode needs more than 65536 inodes')
        fs.min_inode = 65536
        fs.add_file(path, open(host, 'rb').read())
        fs.min_inode = 1
    specs = [s for s in specs if not s.startswith(('--chmod=', '--mkdir=', '--exclude=', '--tree=', '--hi-inode='))]
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
