# third_party: code that is not ours

Foreign code (libc, GUI toolkits, interpreters, ...) is **not** copied into the book. The rules (issue #16):

1. **Unmodified git submodule** in `third_party/<name>`, pinned to a release **tag** (never a branch). `git submodule status`
   shows the commit; the tag is written in the licence check below.
2. **Our changes** are either patch files in `patches/<name>/` (applied by the build, in file name order) or our own files
   next to the submodule (a `config.h`, the system-call layer). The submodule directory itself stays clean.
3. **Only our adaptation becomes part of the book** (a chapter of `arm64/lit/`), not the foreign code.
4. **Licence check before the import**, one file `third_party/LICENCE-CHECK-<name>.md` per package, copied from the template
   below. BSD, MIT, ISC and Apache-2.0 are fine (this project is GPLv3). Never copy Linux code (GPL-2.0-only is not compatible
   with GPLv3); reading Linux for facts such as register layouts is fine.
5. A package is listed in the top-level `README.md` (third-party code).

## Template: licence check

```
# Licence check: <name>
- Name:         <name>
- Upstream URL: <git or tarball URL>
- Tag:          <release tag the submodule is pinned to>
- Licence:      <SPDX id, and the file in the upstream tree that says so>
- Why it is compatible with GPLv3: <one or two sentences; also name parts with other licences>
- Linux code in it? <no / which parts>
- Patches:      <patches/<name>/..., or none>
```

After a fresh clone: `git submodule update --init`. The licence-check template is also repeated in
[`arm64/PRINCIPLES.md`](../arm64/PRINCIPLES.md), section "Third-party libraries".

## Packages

### musl

See `LICENCE-CHECK-musl.md`.

### cpython

| field | content |
|---|---|
| upstream, tag | <https://github.com/python/cpython>, tag `v3.12.7`; the submodule is shallow (`git submodule update --init third_party/cpython`: about 100 MByte instead of 700) |
| licence | PSF License Version 2 (`third_party/cpython/LICENSE`, "2.2 and above: GPL-compatible"), checked 2026-11-13; see `LICENCE-CHECK-cpython.md` |
| why compatible with GPLv3 | the FSF lists the PSF licence as GPL-compatible; the notice and the licence text must stay with the code |
| what we add | `arm64/libc/musl-cc` (compiler wrapper for configure), `arm64/libc/python-setup.local` (the modules that are built in), rules in `arm64/libc/Makefile`, `arm64/tools/python_test.py`, `arm64/libc/hello.py`; the chapter "Python" in `lit/libc.nw`; no patches |
| what it costs | the stripped interpreter is 7.4 MByte (static, with the modules of `python-setup.local`); a start needs about 7 MByte of RAM for the image plus 3 to 4 MByte of heap; the build needs a Python 3.12 on the host (`BUILD_PYTHON`); it is switched off with `make PYTHON=0` and then not even compiled |

### bearssl

| field | content |
|---|---|
| upstream, tag | <https://bearssl.org/git/BearSSL>, tag `v0.6` (the last release; commit 8ef7680) |
| licence | MIT (`third_party/bearssl/LICENSE.txt`, Copyright (c) 2016 Thomas Pornin), checked 2026-11-12 |
| why compatible with GPLv3 | permissive: only the copyright and permission notice must stay with the code |
| what we add | `arm64/userland/https.c`, `arm64/userland/tls/` (glue, `string.h`) and `arm64/sshcrypto.c` (chapters `lit/tls.nw`, `lit/tlsuser.nw`, `lit/ssh.nw`); no configuration, no patches |
| what it costs | 2.3 MByte of source; linked into `https`: 110 KByte of code, 192 KByte of static data (128 KByte of it for the certificate authorities); client context 3.7 KByte, record buffer 16.7 KByte; no `malloc`; needs only `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`; TLS 1.2 at most |

## etc/ssl/cacert.pem (data, not a submodule)

| field | content |
|---|---|
| upstream | <https://curl.se/ca/cacert.pem> (the curl project's extract of Mozilla's `certdata.txt`), fetched 2026-11-12 with the date `Thu Nov  5 04:12:01 2026 GMT` in its header; SHA-256 of the file in the repository: `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285` |
| licence | Mozilla Public License 2.0 (the data of the Mozilla root store) |
| why compatible with GPLv3 | the MPL 2.0 lists the GNU GPL (version 2.0 and later versions) as a "Secondary License": the data may be combined with GPL code; the file is unmodified and keeps its header |
| update | `arm64/tools/update_cacert.sh`, then commit; there is no automatic update on the system |

### zlib, bzip2, xz, readline, libffi, libarchive, sqlite, ncurses, openssl (the whole Python standard library, and tar/zip)

Nine submodules (all shallow, each pinned to a tag), checked 2026-11-14 in one file: `LICENCE-CHECK-libs-for-python.md` (tags, licences, what each
one is used for). Unmodified; the rules that build them are in `arm64/libc/Makefile` (zlib, SQLite, libffi, ncurses, OpenSSL into the sysroot
`arm64/libc/build/sysroot`; libarchive with cmake), the Python modules that use them in `arm64/libc/python-setup.local`.
`make ARCHIVE=0` leaves out `tar` and `unzip`. Fetch: `git submodule update --init --depth 1 third_party/zlib third_party/libffi third_party/libarchive
third_party/sqlite third_party/ncurses third_party/openssl third_party/bzip2 third_party/xz third_party/readline`. Build needs on the host: cmake (libarchive), perl (OpenSSL), tclsh (SQLite) and a C compiler (ncurses'
`tic`).
