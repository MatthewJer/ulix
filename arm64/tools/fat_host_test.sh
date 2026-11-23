#!/bin/sh
# Tries fat.c (FAT32 and exFAT) on the host, without the kernel: a disk image is made (mtools for FAT32, hdiutil for exFAT on a Mac), the
# program tools/fat_host.c (fat.c with a file as the disk) does a few hundred operations (tools/fat_host_stress.sh), and the file system checks of the host
# (fsck_msdos, fsck_exfat) look at the result. usage: sh tools/fat_host_test.sh    (needs noweb, mtools; exFAT: macOS)
set -e
cd "$(dirname "$0")/.."
W=${TMPDIR:-/tmp}/ulix-fat-test.$$
mkdir -p $W/inc && trap 'rm -rf $W' EXIT
echo 'typedef unsigned char u8; typedef unsigned int u32; typedef unsigned long u64;' > $W/inc/ulix.h
notangle -Rfat.c lit/fat.nw > $W/fat.c; notangle -Rfat.h lit/fat.nw > $W/inc/fat.h
cc -Wall -Wno-unused-function -I$W/inc -o $W/fhost tools/fat_host.c $W/fat.c
cp tools/fat_host_stress.sh $W/stress.sh
cd $W
dd if=/dev/zero of=f32.img bs=1048576 count=64 2>/dev/null; mformat -i f32.img -F -v FATSTICK ::
echo "== FAT32"; sh stress.sh f32.img
fsck_msdos -n f32.img 2>&1 | grep -iv "FSInfo\|volume label\|^Fix\|^Remove\|^: no\|^\*\*" || true
if command -v hdiutil >/dev/null; then
  hdiutil create -size 64m -fs ExFAT -volname EXSTICK -layout NONE -type UDIF x.dmg >/dev/null 2>&1; hdiutil convert x.dmg -format UDTO -o x >/dev/null 2>&1; mv x.cdr e.img
  echo "== exFAT"; sh stress.sh e.img
  DEV=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage e.img 2>/dev/null | head -1 | awk '{print $1}')
  fsck_exfat -n $(echo $DEV | sed 's/disk/rdisk/') 2>&1 | tail -2; hdiutil detach $DEV >/dev/null 2>&1
fi
