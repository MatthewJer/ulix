# Licence check: the libraries behind the Python modules and the tar/zip program
One file for the nine libraries that were added together (the whole standard library, and `tar`/`unzip`). All of them are unmodified,
shallow clones pinned to a release tag (`git clone --depth 1 --branch TAG`, listed here and in `README.md`); our own files are
`arm64/libc/Makefile` (the rules), `arm64/libc/python-setup.local`, `arm64/libc/fficonfig.h`, `arm64/libc/dlshim.c`, `arm64/libc/gen_dlsyms.py`,
`arm64/libc/musl.cmake`. There are no patches. Checked 2026-11-14.

| library | upstream, tag | licence (file) | used for |
|---|---|---|---|
| zlib | <https://github.com/madler/zlib>, `v1.3.1` | zlib licence (`zlib/LICENSE`) | Python `zlib`, `gzip`, `zipfile`, `tarfile`; libarchive |
| libffi | <https://github.com/libffi/libffi>, `v3.4.6` | MIT (`libffi/LICENSE`) | Python `ctypes` |
| libarchive | <https://github.com/libarchive/libarchive>, `v3.7.7` | New BSD, a few files public domain or BSD-2 (`libarchive/COPYING`) | `bsdtar` (installed as `/bin/tar`), `bsdunzip` (`/bin/unzip`) |
| SQLite | <https://github.com/sqlite/sqlite>, `version-3.46.1` | public domain (`sqlite/LICENSE.md`) | Python `sqlite3` |
| ncurses | <https://github.com/ThomasDickey/ncurses-snapshots>, `v6_5` | MIT-style (`ncurses/COPYING`) | Python `curses` |
| bzip2 | <https://sourceware.org/git/bzip2.git>, `bzip2-1.0.8` | BSD-style (`bzip2/LICENSE`) | Python `bz2`; libarchive (`tar -j`) |
| xz (liblzma only) | <https://github.com/tukaani-project/xz>, `v5.4.7` (never 5.6.0 or 5.6.1, the backdoored releases) | 0BSD for liblzma (`xz/COPYING`) | Python `lzma`; libarchive (`tar -J`) |
| GNU readline | <https://git.savannah.gnu.org/git/readline.git>, `readline-8.2` | GPL-3.0-or-later (`readline/COPYING`), the licence of this project | Python `readline` |
| OpenSSL | <https://github.com/openssl/openssl>, `openssl-3.0.22` | Apache-2.0 (`openssl/LICENSE.txt`) | Python `ssl` and `_hashlib` |

Why they are compatible with GPLv3: zlib, MIT, BSD and public domain are permissive (the notices must stay with the code); Apache-2.0 is
compatible with GPLv3 (not with GPLv2, which does not matter here). Linux code in any of them? No. Build-time only (not linked into
anything): the host `tic` and `infocmp` of ncurses, `tclsh` for SQLite's amalgamation.
What it costs: the interpreter grew from 8.3 to 12.9 MByte (OpenSSL 3 MByte, SQLite 0.8, libffi and ncurses little, all of libc.a 0.5);
`bsdtar` 0.9 MByte, `bsdunzip` 0.25 MByte; the root disk is 64 MByte now.
