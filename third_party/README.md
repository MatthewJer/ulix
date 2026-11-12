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
