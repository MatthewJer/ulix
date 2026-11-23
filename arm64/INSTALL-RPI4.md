# Ulix on the Raspberry Pi 4B: writing the SD card

Two ways: use the **release image** (download, add the firmware, write), or **build it yourself** (`make sd-pi.img`).

You need: a Raspberry Pi 4B, a microSD card of at least 256 MByte (everything on it is overwritten), a USB-C power supply, and for a console either the network
(the Pi asks for an address with DHCP; ssh and telnet), a monitor (micro HDMI, the socket next to the USB-C power socket), or a 3.3 V USB serial adapter (below).

## 1. Get the image
### The release (no compiler needed)
Download from the release page: `ulix-rpi4-sd-<tag>.img.xz`, and `add_pi_firmware.sh` (it is in `arm64/tools/` of the repository).
```sh
xz -d ulix-rpi4-sd-<tag>.img.xz
```
### Or build it
```sh
brew install qemu noweb aarch64-elf-gcc aarch64-elf-binutils python@3.12 mtools        # see arm64/README.md for the sources of the submodules
cd arm64 && make sd-pi.img                                                              # the image is arm64/sd-pi.img (256 MByte)
```

## 2. The firmware of the Pi (release image only)
The files `start4.elf`, `fixup4.dat`, the device tree and the licence text of Broadcom are not part of the image (they have their own licence). One command fetches them from the official
repository (https://github.com/raspberrypi/firmware, branch `stable`) and writes them into the boot partition of the image (needs `curl` and `mtools`):
```sh
sh arm64/tools/add_pi_firmware.sh ulix-rpi4-sd-<tag>.img
```
(`make sd-pi.img` uses the directory `arm64/firmware/` instead, if it exists: put these files there.)

## 3. Write the card
**macOS** (the disk number can change: look every time, and take the card, not another disk):
```sh
diskutil list external                     # find the card: the size tells (for example /dev/disk4, 8.1 GB)
diskutil unmountDisk /dev/disk4
sudo dd if=ulix-rpi4-sd-<tag>.img of=/dev/rdisk4 bs=4m       # rdisk, not disk: much faster. No progress output: Ctrl-T shows the state
diskutil eject /dev/disk4
```
**Linux:**
```sh
lsblk                                      # find the card, for example /dev/sdb
sudo umount /dev/sdb* 2>/dev/null
sudo dd if=ulix-rpi4-sd-<tag>.img of=/dev/sdb bs=4M conv=fsync status=progress
sync
```
Everything on the device that you name is lost. A card of 8 GByte or more shows two partitions less than its size: the image has 256 MByte, the rest stays empty.

Only the kernel changed (you built a new one)? Copy it onto the first partition of the card instead of writing everything again:
`cp arm64/kernel8-shell.img /Volumes/ULIXBOOT/kernel8.img` (the partition is called `ULIXBOOT`).

## 4. Start
Put the card in, connect network (and monitor, serial adapter, USB devices), then the power. After about 20 seconds the system is up. Find the address:
the router's list of devices (the name is `ulix`), or the console (`ifconfig`). Then
```sh
ssh esser@<address>            # or: telnet <address>
```
User `esser` (and `root`) have the password `xyz`. The console logs in as `esser` by itself.

**Serial console** (GPIO 14 and 15, 115200 baud 8N1; 3.3 V only, never 5 V): adapter GND to pin 6, adapter RX to pin 8 (TXD), adapter TX to pin 10 (RXD); on macOS
`screen /dev/cu.usbserial-XXXX 115200` (leave with Ctrl-A K). Without a serial line, `cat /dev/kmsg` shows the boot messages.

## 5. USB sticks
A stick (FAT32 or exFAT, partition table of the old kind) is mounted at the start, or by `mount` if it is plugged in later:
```
mount                          # looks for new sticks, lists what is mounted
ls /Volumes/                   # a stick is at /Volumes/LABEL
cd /Volumes/LABEL
eject usb0                     # always, before it is pulled out
```
A CPU test that writes the load of the cores into a CSV file: copy `arm64/examples/cpu_test.py` onto the stick, then `cd /Volumes/LABEL` and `/musl/python cpu_test.py`.

## If something does not work
- **No sign of life, no network:** is the firmware in the boot partition (`start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`, `overlays/disable-bt.dtbo`, `config.txt`, `cmdline.txt`, `kernel8.img`)? Look at the green LED and at the serial console.
- **The stick does not appear:** `cat /dev/kmsg | grep -i usb` and `grep -i pci`: they say what the controller saw. A hub (a keyboard hub, a docking station) behind a stick is not supported if it is a SuperSpeed hub.
- **A reboot after a shutdown that hung** (`BKL: CPU 2 waits for the lock` at `poweroff`) can leave pid files of the services behind: `rm /var/run/*.pid` and `service -a start`.
