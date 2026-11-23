# Licence check: the BCM2711 PCI Express bring-up (adapted from FreeBSD)
- Name:         `sys/arm/broadcom/bcm2835/bcm2838_pci.c` of FreeBSD (the PCIe root complex of the Raspberry Pi 4B)
- Upstream URL: https://github.com/freebsd/freebsd-src/blob/main/sys/arm/broadcom/bcm2835/bcm2838_pci.c (read on 2026-11-23, not copied as a file)
- Licence:      ISC (`SPDX-License-Identifier: ISC`), Copyright (c) 2020 Dr Robert Harvey Crowston <crowston@protonmail.com>
- What Ulix takes: the register offsets, the values that are written and the order of the steps (reset of the bridge, inbound DMA window, wait for the link,
  outbound window, class code), written anew in `arm64/lit/usb.nw` (`pci.c: the Raspberry Pi 4 host`) for a small polled driver, with the copyright line in the source.
- Why it is compatible with GPLv3: ISC is a permissive licence (like the two-clause BSD licence) and may be used in GPLv3 software; the notice below stays.
- Linux code in it? No (Linux's `pcie-brcmstb.c` is GPL-2.0-only and was not used).
- The firmware call that loads the program of the VL805 USB chip (mailbox tag 0x00030058, argument: the PCI address of the device) is an interface of the Raspberry Pi
  firmware, not of any driver's code.
- Not tested on a board.

The permission notice of the ISC licence:

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
