# Third-party code in this repository

One line per package: what it is, where it comes from, which version, under which licence, where Ulix uses it. The rules (unmodified
submodule pinned to a tag, own changes next to it, licence check before the import) are in [`third_party/README.md`](third_party/README.md);
the checks themselves are the files `third_party/LICENCE-CHECK-*.md`. Ulix itself is GPLv3 ([`LICENSE`](LICENSE)); every licence below is
compatible with it. Never copy Linux code (GPL-2.0-only).

## Git submodules (`third_party/`, all unmodified)

| package | upstream | tag | licence | where Ulix uses it |
|---|---|---|---|---|
| musl libc | <https://git.musl-libc.org/git/musl> | `v1.2.6` | MIT (`COPYRIGHT`; some BSD/ISC-style parts) | the C library of the user space, `arm64/libc/Makefile`, system-call layer `linux_abi.c` (chapter "The C library") |
| BearSSL | <https://bearssl.org/git/BearSSL> | `v0.6` | MIT | TLS client `https` (`userland/https.c`), crypto of `sshd` (`userland/sshcrypto.c`); not with `make TLS=0` |
| CPython | <https://github.com/python/cpython> | `v3.12.7` | PSF-2.0 | `/musl/python` (`make PYTHON=1`), modules in `libc/python-setup.local` |
| zlib | <https://github.com/madler/zlib> | `v1.3.1` | zlib | Python `zlib`, `gzip`, `zipfile`, `tarfile`; libarchive |
| libffi | <https://github.com/libffi/libffi> | `v3.4.6` | MIT | Python `ctypes` |
| libarchive | <https://github.com/libarchive/libarchive> | `v3.7.7` | New BSD (a few files public domain, BSD-2) | `/bin/tar` (bsdtar), `/bin/unzip` (bsdunzip); not with `make ARCHIVE=0` |
| SQLite | <https://github.com/sqlite/sqlite> | `version-3.46.1` | public domain | Python `sqlite3` |
| LLVM libc++, libc++abi, libunwind | <https://github.com/llvm/llvm-project> | `llvmorg-21.1.8` | Apache-2.0 with LLVM exception (`libcxx/LICENSE.TXT`) | the C++ standard library (issue #43), sparse checkout, see `third_party/LICENCE-CHECK-llvm.md` |
| ncurses | <https://github.com/ThomasDickey/ncurses-snapshots> | `v6_5` | MIT-style | Python `curses` |
| bzip2 | <https://sourceware.org/git/bzip2.git> | `bzip2-1.0.8` | BSD-style | Python `bz2`; libarchive (`tar -j`) |
| xz (liblzma only) | <https://github.com/tukaani-project/xz> | `v5.4.7` (never 5.6.0 or 5.6.1) | 0BSD (liblzma) | Python `lzma`; libarchive (`tar -J`) |
| GNU readline | <https://git.savannah.gnu.org/git/readline.git> | `readline-8.2` | GPL-3.0-or-later | Python `readline` |
| OpenSSL | <https://github.com/openssl/openssl> | `openssl-3.0.22` | Apache-2.0 | Python `ssl` and `_hashlib` |

| GNU nano (release tarball, unpacked unmodified in `third_party/nano`, not a submodule: its git tree needs gnulib to make `configure`) | <https://ftp.gnu.org/gnu/nano/nano-8.7.1.tar.xz> | `8.7.1` | GPL-3.0-or-later | `/bin/nano` (`make NANO=0` leaves it out), syntax files in `/usr/share/nano`; see `third_party/LICENCE-CHECK-nano.md` |

Source of the tags: `third_party/LICENCE-CHECK-musl.md`, `LICENCE-CHECK-cpython.md`, `LICENCE-CHECK-libs-for-python.md` and the table in
`third_party/README.md`; `git submodule status` shows the commits. A tag that disagrees with the commit is a bug in this list.

## Imported data and adapted code (in the tree)

| item | upstream | version | licence | where Ulix uses it |
|---|---|---|---|---|
| `arm64/etc/ssl/cacert.pem` | <https://curl.se/ca/cacert.pem> (extract of Mozilla's root store) | dated `Thu Nov  5 04:12:01 2026 GMT`, updated with `arm64/tools/update_cacert.sh` | MPL-2.0 (data; unmodified, header kept) | root certificates for `https` (`/etc/ssl/cacert.pem` on the disk) |
| `arm64/genet.c` (chapter `lit/network.nw`) | FreeBSD `sys/arm64/broadcom/genet/if_genet.c`, `if_genetreg.h` (derived from NetBSD `bcmgenet.c`) | register names, layout and order of the set-up steps only; the code is written new | BSD-2-Clause, (c) 2020 Michael J Karels, (c) 2016, 2020 Jared McNeill; text kept in the file header | Ethernet of the Raspberry Pi 4B |
| `arm64/sdcard.c` (chapter `lit/disks.nw`) | FreeBSD `sys/dev/sdhci/sdhci.h` | register names only, nothing copied | BSD-2-Clause | SD card controller (EMMC2) |
| original Ulix, `ulix-book.nw` and `lib-build/`, `bin-build/` | <https://github.com/hgesser/ulix> | release `v0.13` and later upstream commits | GPLv3 | the kernel logic that the ARM64 port reuses (`arm64/port.manifest`) |

Not imported: Linux code (none anywhere); the Raspberry Pi firmware files (`start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`) are downloaded by
the person who builds the SD card and are not part of the repository (licence: Broadcom, redistributable but not free; see section 46 of
[`ARM64-PORT.md`](ARM64-PORT.md)).

## Adding a package

1. Write `third_party/LICENCE-CHECK-<name>.md` (template in `third_party/README.md`) and a row in `third_party/README.md`.
2. `git submodule add` it, pinned to a tag, shallow if big.
3. Add one line to the table above and add its directory to `SUBMODULES_NEEDED` in `arm64/Makefile` (so that a clone without the submodule gets a clear message).
