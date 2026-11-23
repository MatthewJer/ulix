#!/bin/sh
# Puts the firmware of the Raspberry Pi into the boot partition of a Ulix SD card image (the release image does not contain it: the files are
# Broadcom's, under their own licence, which asks that its text goes with them; so you fetch them yourself, from the official source).
# usage: sh tools/add_pi_firmware.sh ulix-rpi4-sd.img        (needs curl and mtools: brew install mtools)
set -e
img=${1:?usage: add_pi_firmware.sh IMAGE}
base=${FIRMWARE_BASE:-https://raw.githubusercontent.com/raspberrypi/firmware/stable/boot}      # (FIRMWARE_BASE: another place, for tests)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
for f in start4.elf fixup4.dat bcm2711-rpi-4-b.dtb LICENCE.broadcom overlays/disable-bt.dtbo; do
  echo "fetching $f"
  mkdir -p "$tmp/$(dirname $f)"
  curl -fsSL -o "$tmp/$f" "$base/$f"
done
part="$img@@1M"                                  # the boot partition starts at sector 2048
mmd -i "$part" ::/overlays 2>/dev/null || true
for f in start4.elf fixup4.dat bcm2711-rpi-4-b.dtb LICENCE.broadcom; do mcopy -o -i "$part" "$tmp/$f" ::/; done
mcopy -o -i "$part" "$tmp/overlays/disable-bt.dtbo" ::/overlays/
echo "done: the boot partition of $img now has the firmware. Write it to the card with dd (see the release notes)."
