# Licence check: nano
- Name:         GNU nano (the text editor, `/bin/nano`)
- Upstream URL: <https://ftp.gnu.org/gnu/nano/nano-8.7.1.tar.xz> (the release tarball; the git tree <https://git.savannah.gnu.org/git/nano.git> has no
  `configure` and its `autogen.sh` fetches gnulib, which is not imported here)
- Tag:          release 8.7.1 (2026-02-04), the latest 8.x. SHA-256 of the tarball
  `76f0dcb248f2e2f1251d4ecd20fd30fb400a360a3a37c6c340e0a52c2d1cdedf`: the same from <https://ftp.gnu.org/gnu/nano/> and from
  <https://www.nano-editor.org/dist/v8/> (two hosts). The GPG signature `nano-8.7.1.tar.xz.sig` could not be checked (no gpg on the build host).
  The tarball is unpacked, unmodified, into `third_party/nano/` (a directory of the repository, not a submodule); `make` does not need to download anything.
- Licence:      GPL-3.0-or-later (`third_party/nano/COPYING`: GNU GPL version 3; the files say "either version 3 of the License, or (at your option) any later version").
  The gnulib files in `lib/` are GPL-3.0-or-later or LGPL-2.1-or-later.
- Why it is compatible with GPLv3: it is GPLv3-or-later, the licence of this project, like GNU readline. The documentation `COPYING.DOC` (GFDL) is in the tree, not used.
- Linux code in it? No.
- Patches:      none. Our own files: the rules in `arm64/libc/Makefile` (`build/nano.elf`), `arm64/etc/nanorc`, `arm64/tools/nano_test.py`, the chapter `arm64/lit/nano.nw`;
  the terminal settings (termios) that nano asks for are in the kernel (`lit/console.nw`, `lit/libc.nw`).
- Built with: the wide-character ncurses of `arm64/libc/Makefile`; `--disable-nls --disable-libmagic --disable-speller`. Checked 2026-11-21.
