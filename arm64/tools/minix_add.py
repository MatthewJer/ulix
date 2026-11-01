#!/usr/bin/env python3
"""Add files to the root directory of a Minix v2 image (14- or 30-character names).

usage: minix_add.py IMAGE NAME=HOSTFILE [NAME=HOSTFILE ...]

Needed because mkfs.minix / a Minix mount are not available on macOS.  Supports files up to
7 direct + 256 indirect zones (263 KByte), which is plenty for the AArch64 user programs.
"""
import struct, sys, time

ZONE = 1024

def main(image, specs):
    d = bytearray(open(image, 'rb').read())
    ninodes, nz16, imap_b, zmap_b, first, logz, maxsize, magic, state, nzones = \
        struct.unpack('<HHHHHHIHHI', d[1024:1048])
    if magic not in (0x2468, 0x2478) or logz != 0:
        sys.exit('not a Minix v2 image with 1 KByte zones (magic 0x%x)' % magic)
    dsize, nlen = (32, 30) if magic == 0x2478 else (16, 14)
    imap_off, zmap_off = 2 * ZONE, (2 + imap_b) * ZONE
    ino_off = (2 + imap_b + zmap_b) * ZONE

    def bit(base, n): return d[base + n // 8] >> (n % 8) & 1
    def setbit(base, n): d[base + n // 8] |= 1 << (n % 8)

    def alloc_inode():
        for n in range(1, ninodes + 1):
            if not bit(imap_off, n): setbit(imap_off, n); return n
        sys.exit('no free inode')

    def alloc_zone():
        for n in range(1, nzones - first + 1):
            if not bit(zmap_off, n): setbit(zmap_off, n); return first + n - 1
        sys.exit('no free zone')

    def inode_at(n): return ino_off + (n - 1) * 64
    def put_inode(n, mode, size, zones):
        t = int(time.time())
        z = list(zones) + [0] * (10 - len(zones))
        d[inode_at(n):inode_at(n) + 64] = struct.pack('<HHHHIIII10I', mode, 1, 0, 0, size, t, t, t, *z)

    def zone_data(z): return d[z * ZONE:(z + 1) * ZONE]
    def put_zone(z, data): d[z * ZONE:z * ZONE + len(data)] = data

    # root directory (inode 1) must have a single direct zone with room for new entries
    ro = inode_at(1)
    rmode, rn, ru, rg, rsize = struct.unpack('<HHHHI', d[ro:ro + 12])
    rzones = list(struct.unpack('<10I', d[ro + 24:ro + 64]))
    existing = {}
    for off in range(0, rsize, dsize):
        z = rzones[off // ZONE]
        e = zone_data(z)[off % ZONE:off % ZONE + dsize]
        k, = struct.unpack('<H', e[:2])
        if k: existing[e[2:].split(b'\0')[0].decode()] = (k, off)

    for spec in specs:
        name, host = spec.split('=', 1)
        if len(name) > nlen: sys.exit('name too long: ' + name)
        data = open(host, 'rb').read()
        nz = (len(data) + ZONE - 1) // ZONE
        if nz > 7 + 256: sys.exit('file too large: ' + host)
        if name in existing:                   # replace: keep the directory entry, new inode
            nr = existing[name][0]
            print('replacing existing', name, '(old zones are leaked, use a fresh image copy)')
        else:
            nr = alloc_inode()
        zl = [alloc_zone() for _ in range(nz)]
        for i, z in enumerate(zl):
            put_zone(z, data[i * ZONE:(i + 1) * ZONE].ljust(ZONE, b'\0'))
        direct = zl[:7]
        zones = direct
        if nz > 7:
            ind = alloc_zone()
            put_zone(ind, struct.pack('<256I', *(zl[7:] + [0] * (256 - (nz - 7)))))
            zones = direct + [0] * (7 - len(direct)) + [ind]
        put_inode(nr, 0o100755, len(data), zones)
        if name not in existing:
            off = rsize
            if off + dsize > ZONE * 7 or off // ZONE >= 1 and not rzones[off // ZONE]:
                if off // ZONE >= 7: sys.exit('root directory full')
                rzones[off // ZONE] = alloc_zone()
            entry = struct.pack('<H', nr) + name.encode().ljust(dsize - 2, b'\0')
            z = rzones[off // ZONE]
            blk = bytearray(zone_data(z)); blk[off % ZONE:off % ZONE + dsize] = entry; put_zone(z, blk)
            rsize += dsize
        print('added %-12s inode %3d, %6d bytes, %3d zones' % (name, nr, len(data), nz))

    d[ro:ro + 64] = struct.pack('<HHHHIIII10I', rmode, rn, ru, rg, rsize,
                                *struct.unpack('<III', d[ro + 12:ro + 24]), *rzones)
    open(image, 'wb').write(d)

if len(sys.argv) < 3: sys.exit(__doc__)
main(sys.argv[1], sys.argv[2:])
