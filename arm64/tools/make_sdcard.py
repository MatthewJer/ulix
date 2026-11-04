#!/usr/bin/env python3
"""Builds the image of an SD card for the Raspberry Pi 4B (or for qemu's raspi4b machine).

usage: make_sdcard.py OUT.img HDA.img [HDB.img [FD1.img]]

Layout (sector = 512 bytes, partitions start on multiples of 1 MByte like every partition tool does):

    sector 0           the MBR with the partition table
    partition 1        type 0x0c (FAT32), 32 MByte: the firmware and the kernel go here (on the board; not needed in qemu,
                       so it is left empty here)
    partition 2, 3, 4  type 0x81 (Minix), one per disk image: /dev/hda, /dev/hdb and the stand-in for /dev/fd1

The size of the file is rounded up to a power of two: qemu only accepts such sizes for an SD card.
"""
import struct, sys

ALIGN = 2048                      # sectors: 1 MByte
BOOT_SECTORS = 65536              # 32 MByte


def chs(lba):                     # the old CHS fields are not used any more: the maximum value means "use the LBA fields"
    return b"\xfe\xff\xff"


def entry(ptype, start, count):
    return b"\x00" + chs(start) + bytes([ptype]) + chs(start + count - 1) + struct.pack("<II", start, count)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, disks = sys.argv[1], sys.argv[2:5]
    parts = [(0x0c, ALIGN, BOOT_SECTORS, None)]
    start = ALIGN + BOOT_SECTORS
    for path in disks:
        data = open(path, "rb").read()
        count = (len(data) + 511) // 512
        parts.append((0x81, start, count, data))
        start += (count + ALIGN - 1) // ALIGN * ALIGN
    size = 512 * start
    total = 1 << (size - 1).bit_length()
    image = bytearray(total)
    table = b"".join(entry(t, s, c) for t, s, c, _ in parts)
    image[446:446 + len(table)] = table
    image[510:512] = b"\x55\xaa"
    for t, s, c, data in parts:
        if data:
            image[512 * s:512 * s + len(data)] = data
    open(out, "wb").write(image)
    print("%s: %d MByte, %d partitions" % (out, total >> 20, len(parts)))
    for i, (t, s, c, _) in enumerate(parts):
        print("  partition %d: type 0x%02x, first sector %d, %d sectors (%d KByte)" % (i + 1, t, s, c, c // 2))


main()
