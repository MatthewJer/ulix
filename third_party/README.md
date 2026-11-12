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
| what we add | `userland/https.c` and the glue to the system calls (chapter "TLS", `lit/tls.nw`); a configuration is not needed |
| what it costs | about 2.3 MByte of source, 60 KByte of code for a client with TLS 1.2, no `malloc`, needs only `memcpy`, `memmove`, `memset`, `memcmp`, `strlen` |
