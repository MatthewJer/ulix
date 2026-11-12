# Licence check: musl
- Name:         musl libc (the C library of the AArch64 user space, issues #3, #27, #28)
- Upstream URL: https://git.musl-libc.org/git/musl
- Tag:          v1.2.6 (submodule `third_party/musl`, unmodified)
- Licence:      MIT, see `third_party/musl/COPYRIGHT` (that file also lists the few parts with other permissive licences:
                the BSD-style code of `src/regex` (TRE) and BSD/ISC-style code of some math functions and `crypt`)
- Why it is compatible with GPLv3: the MIT licence only asks that the copyright notice is kept; MIT code may be combined
  with GPLv3 code, and the programs linked with musl are covered by GPLv3 as a whole.
- Linux code in it? No. musl is written independently; it only uses the Linux system-call interface (an interface, not
  copied code).
- Patches:      none
