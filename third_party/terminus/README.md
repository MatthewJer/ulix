# Terminus Font 4.49.1 (the three bitmap fonts of the HDMI console)

Unmodified files from `terminus-font-4.49.1.tar.gz` (<https://sourceforge.net/projects/terminus-font/>, downloaded 2026-11-23, 648 KByte):
`ter-u16n.bdf` (8x16), `ter-u24n.bdf` (12x24), `ter-u28n.bdf` (14x28), `ter-u32n.bdf` (16x32), `OFL.TXT`, `AUTHORS`.
Copyright (C) 2020 Dimitar Toshkov Zhekov, SIL Open Font License 1.1 (`OFL.TXT`). `arm64/tools/bdf2c.py` turns them into a C table at build
time (`arm64/gen/font.c`); see `LICENCE-CHECK-terminus.md` and the chapter "Screen and keyboard" of the port's book.
