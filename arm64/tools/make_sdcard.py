#!/usr/bin/env python3
"""Builds the image of an SD card for the Raspberry Pi 4B (or for qemu's raspi4b machine).

usage: make_sdcard.py [--boot DIR] OUT.img HDA.img [HDB.img [FD1.img]]

Layout (sector = 512 bytes, partitions start on multiples of 1 MByte like every partition tool does):

    sector 0           the MBR with the partition table
    partition 1        type 0x0c (FAT32), 64 MByte: the firmware, config.txt, cmdline.txt and the kernel go here (on the board;
                       not needed in qemu). With --boot DIR it is formatted and every file of DIR is copied into it
                       (needs mtools: mformat and mcopy); without it the partition stays empty.
    partition 2, 3, 4  type 0x81 (Minix), one per disk image: /dev/hda, /dev/hdb and the stand-in for /dev/fd1

The size of the file is rounded up to a power of two: qemu only accepts such sizes for an SD card.
"""
import os, struct, subprocess, sys

ALIGN = 2048                      # sectors: 1 MByte
BOOT_SECTORS = 131072             # 64 MByte (FAT32 needs at least 65525 clusters; with 512-byte clusters that is about 33 MByte)


def chs(lba):                     # the old CHS fields are not used any more: the maximum value means "use the LBA fields"
    return b"\xfe\xff\xff"


def entry(ptype, start, count):
    return b"\x00" + chs(start) + bytes([ptype]) + chs(start + count - 1) + struct.pack("<II", start, count)


def fill_boot_partition(image, start, boot_dir):
    """Format partition 1 as FAT32 and copy the files of boot_dir into it (the tools of mtools can work inside an image file:
    image@@offset means 'the file system that starts at this byte')."""
    target = "%s@@%d" % (image, 512 * start)
    subprocess.run(["mformat", "-i", target, "-F", "-c", "1", "-T", str(BOOT_SECTORS), "-v", "ULIXBOOT", "::"], check=True)
    for name in sorted(os.listdir(boot_dir)):
        subprocess.run(["mcopy", "-i", target, "-s", os.path.join(boot_dir, name), "::" + name], check=True)


def main():
    args = sys.argv[1:]
    boot_dir = None
    if args and args[0] == "--boot":
        boot_dir, args = args[1], args[2:]
    if len(args) < 2:
        sys.exit(__doc__)
    out, disks = args[0], args[1:4]
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
    if boot_dir:
        fill_boot_partition(out, ALIGN, boot_dir)
    print("%s: %d MByte, %d partitions" % (out, total >> 20, len(parts)))
    for i, (t, s, c, _) in enumerate(parts):
        print("  partition %d: type 0x%02x, first sector %d, %d sectors (%d KByte)" % (i + 1, t, s, c, c // 2))


main()
