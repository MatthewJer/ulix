#!/bin/sh
# Collects what a release contains from the build (run "make" first so that the images are there): usage: sh tools/release.sh TAG
#   ulix-qemu-virt-TAG.tar.xz   the kernel and the disk for qemu's generic ARM machine (run-qemu-virt.sh)
#   ulix-qemu-raspi4b-TAG.tar.xz  the Raspberry Pi 4B kernel and SD card image for qemu's raspi4b machine (run-qemu-raspi4b.sh)
#   ulix-rpi4-sd-TAG.img.xz     the SD card image for the real board, WITHOUT the firmware (tools/add_pi_firmware.sh adds it)
set -e
tag=${1:?usage: release.sh TAG}
cd "$(dirname "$0")/.."
out=dist/$tag
rm -rf dist/$tag; mkdir -p $out/virt $out/raspi4b
cp ulix-shell.bin disk.img $out/virt/
cat > $out/virt/run-qemu-virt.sh <<'EOS'
#!/bin/sh
# Ulix on qemu's generic ARM machine (needs qemu-system-aarch64). Ctrl-A X leaves qemu. After the login (auto login: user esser) try: ls, mount, python.
# ssh: port 2222, telnet: 2323, a web server in Ulix: 8080. Changes to the disk are thrown away at the end (remove -snapshot to keep them).
cd "$(dirname "$0")"
exec qemu-system-aarch64 -M virt -cpu cortex-a72 -smp 4 -m 256 -nographic -kernel ulix-shell.bin -snapshot \
  -drive file=disk.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 -device virtio-rng-device \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:8080-10.0.2.15:8080,hostfwd=tcp:127.0.0.1:2323-10.0.2.15:23,hostfwd=tcp:127.0.0.1:2222-10.0.2.15:22 \
  -device virtio-net-device,netdev=n0 \
  -device qemu-xhci -device usb-kbd -device usb-mouse "$@"
# a USB stick image (FAT32 or exFAT) can be attached with:  -drive file=stick.img,format=raw,if=none,id=stick -device usb-storage,drive=stick
EOS
cp kernel8-shell.img rpi4-qemu.dtb sd.img $out/raspi4b/
cat > $out/raspi4b/run-qemu-raspi4b.sh <<'EOS'
#!/bin/sh
# Ulix on qemu's Raspberry Pi 4B machine (the same kernel as on the board; qemu has no PCIe and no Ethernet of the Pi, so no USB sticks here).
# Ctrl-A X leaves qemu. Changes to the card are thrown away at the end (remove -snapshot to keep them).
cd "$(dirname "$0")"
exec qemu-system-aarch64 -M raspi4b -dtb rpi4-qemu.dtb -serial stdio -display none -kernel kernel8-shell.img -snapshot \
  -drive file=sd.img,if=sd,format=raw "$@"
EOS
chmod +x $out/virt/run-qemu-virt.sh $out/raspi4b/run-qemu-raspi4b.sh
tar -C $out -cJf dist/ulix-qemu-virt-$tag.tar.xz virt
tar -C $out -cJf dist/ulix-qemu-raspi4b-$tag.tar.xz raspi4b
# the SD card image of the board without the firmware (what sd-pi.img does, but with an empty firmware directory)
rm -rf $out/sd-boot && mkdir $out/sd-boot
cp rpi4-config.txt $out/sd-boot/config.txt
cp rpi4-cmdline.txt $out/sd-boot/cmdline.txt
cp kernel8-shell.img $out/sd-boot/kernel8.img
python3 tools/make_sdcard.py --boot $out/sd-boot $out/ulix-rpi4-sd.img disk.img disk2.img disk3.img
xz -9 -c $out/ulix-rpi4-sd.img > dist/ulix-rpi4-sd-$tag.img.xz
( cd dist && shasum -a 256 ulix-*"$tag"* > SHA256SUMS-$tag.txt )
ls -la dist
