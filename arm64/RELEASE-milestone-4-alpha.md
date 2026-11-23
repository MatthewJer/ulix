# Ulix ARM64, milestone 4 (alpha)

Ulix on AArch64: the teaching operating system of Hans-Georg Esser (GPLv3), here with its ARM64 port for qemu and the Raspberry Pi 4B. This is an **alpha**: it runs, it has been tried on a board, and it has rough edges (below).

## What is new since the Raspberry Pi bring-up
- **HDMI console** (mailbox, frame buffer, Terminus bitmap fonts, a small xterm-compatible terminal): the number of lines and columns follows the resolution (1080p: 240x67, 1440p: 320x90, 4K: 320x90 with bigger letters); `video=` and `fbfont=` on the command line.
- **USB** on the Pi 4B: PCIe root complex, xHCI driver, hubs (all sockets of the Pi are behind a hub on the board), keyboard (US/German layout), mouse events, USB mass storage.
- **FAT32 and exFAT, read and write**, mounted like on macOS under `/Volumes/LABEL` (`mount`, `umount`, `eject`); checked against `fsck_msdos` and `fsck_exfat` of macOS.
- **ssh and telnet** pass the window size of the client (and tell the programs when it changes).
- `/dev/kmsg` (the boot messages without a serial line), `/dev/cpustat` (load of every CPU), a CPU test (`arm64/examples/cpu_test.py`) that logs the load of the cores to a CSV file on a stick.
- The documentation is the book of the ARM64 part (`make -C arm64/lit`), with chapters on all of this, and `ARM64-PORT.md`.

## What is in the downloads
| file | what it is |
|---|---|
| `ulix-qemu-virt-*.tar.xz` | kernel and disk for qemu's generic ARM machine: `sh run-qemu-virt.sh` (ssh on port 2222, telnet 2323) |
| `ulix-qemu-raspi4b-*.tar.xz` | the kernel of the board and the SD card image for qemu's Raspberry Pi 4B machine: `sh run-qemu-raspi4b.sh` |
| `ulix-rpi4-sd-*.img.xz` | the SD card image for the real **Raspberry Pi 4B**, without the firmware (see below) |
| `add_pi_firmware.sh` | puts the Raspberry Pi firmware into the SD card image (see below) |
| `SHA256SUMS-*.txt` | checksums |

## Try it in qemu (no hardware needed)
You need `qemu-system-aarch64` (macOS: `brew install qemu`; Debian/Ubuntu: `apt install qemu-system-arm`).

**The generic ARM machine** (`ulix-qemu-virt-*.tar.xz`):
```sh
tar xf ulix-qemu-virt-*.tar.xz && cd virt
sh run-qemu-virt.sh
```
You are logged in as `esser` on the console after a few seconds (`ls`, `mount`, `/musl/python`, `nano`). Leave qemu with `Ctrl-A X`. From a second terminal:
`ssh -p 2222 esser@127.0.0.1` or `telnet 127.0.0.1 2323` (password `xyz`). A USB stick image (FAT32 or exFAT, with a partition table or without) is attached with
`sh run-qemu-virt.sh -drive file=stick.img,format=raw,if=none,id=stick -device usb-storage,drive=stick`; inside Ulix `mount` finds it and `ls /Volumes` shows it.

**The Raspberry Pi 4B machine of qemu** (`ulix-qemu-raspi4b-*.tar.xz`): the same kernel as on the board, without USB and Ethernet (qemu has neither of the Pi):
```sh
tar xf ulix-qemu-raspi4b-*.tar.xz && cd raspi4b
sh run-qemu-raspi4b.sh
```

### Raspberry Pi 4B
Full instructions (macOS and Linux, serial console, USB sticks, troubleshooting): [`arm64/INSTALL-RPI4.md`](https://github.com/MatthewJer/ulix/blob/milestone-4-alpha/arm64/INSTALL-RPI4.md). In short:

The Raspberry Pi firmware (`start4.elf`, `fixup4.dat`, the device tree) belongs to Broadcom and has its own licence, so it is not part of the image. Put it in with one command (needs `curl` and `mtools`; it takes the files from the official repository):

```sh
xz -d ulix-rpi4-sd-*.img.xz
sh add_pi_firmware.sh ulix-rpi4-sd-*.img        # tools/add_pi_firmware.sh of the repository
diskutil list external                           # find the card (macOS); on Linux: lsblk
diskutil unmountDisk /dev/diskN
sudo dd if=ulix-rpi4-sd-*.img of=/dev/rdiskN bs=4m
```
Login: automatic as `esser` on the console; `esser` and `root` have the password `xyz` for telnet and ssh. A USB stick is mounted at the start or by `mount`; `eject usb0` before it is pulled out.

## Known limits (alpha)
- A keyboard or mouse behind the hub on the Pi has not been tried on the board yet; the translator for slow devices is only checked in qemu. No hot plug (the command `mount` looks for new devices).
- exFAT and FAT32 only (no FAT12/FAT16, no GPT), names of 63 bytes at most, files up to 4 GByte.
- HDMI: tried in qemu only (no monitor was connected to the board yet).
- Hubs: high speed hubs only.
