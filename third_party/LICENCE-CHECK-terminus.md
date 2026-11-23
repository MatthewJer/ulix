# Licence check: Terminus Font (bitmap fonts of the HDMI console)
- Name:         Terminus Font 4.49.1, the files `ter-u16n.bdf`, `ter-u24n.bdf`, `ter-u28n.bdf`, `ter-u32n.bdf` (BDF text files)
- Upstream URL: https://sourceforge.net/projects/terminus-font/ (tarball `terminus-font-4.49.1.tar.gz`, unpacked, three files copied)
- Licence:      SIL Open Font License 1.1 (`third_party/terminus/OFL.TXT`), Copyright (C) 2020 Dimitar Toshkov Zhekov
- Why it is compatible with GPLv3: the OFL allows fonts to be bundled and embedded in software of any licence as long as the font is not sold on its
  own; the reserved font name "Terminus Font" is not used for a derivative (the table made from the BDF files carries no name, it is a build product
  and not tracked). The licence text stays with the files. FreeBSD ships Terminus-derived console fonts, too.
- Linux code in it? No.
- Patches:      none; `arm64/tools/bdf2c.py` only reads the glyph bitmaps.
