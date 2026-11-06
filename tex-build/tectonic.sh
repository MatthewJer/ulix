#!/bin/sh
# Builds ../ulix-book.pdf-like output with tectonic instead of xelatex (no TeX Live needed): "make pdf-tectonic".
# Same preprocessing as the Makefile, then a few fixes for a current LaTeX (the book is from 2016):
#   - the Linux Libertine / Biolinum fonts have no " O" in their names in the Homebrew cask (brew install --cask font-linux-libertine font-linux-biolinum)
#   - scrpage2 is gone (scrlayer-scrpage), \tt \bf ... are not defined by KOMA-Script any more, inputenc is not for xetex
#   - Menlo is not usable by tectonic's sandbox on every setup: use JetBrains Mono if Menlo is missing in fc-list
#   - tectonic cannot include EPS: the one gnuplot figure (burst-distribution-nehmer) is left out
# The result is written to ulix-book-tectonic.pdf (the pdf in the repository is not touched). Not run: makeindex, so the index
# pages stay as in the last full build.
set -e
cd "$(dirname "$0")"
OUT=ulix-book-tectonic
grep -v "REMOVE_DEBUGGING_CODE" ../ulix-book.nw > tmp.nw
LANGSET=cee; [ -f "$(dirname "$(command -v nodefs)")/../lib/autodefs.cee" ] || LANGSET=c
nodefs -auto $LANGSET tmp.nw | sort -u > noweb.defs
grep -v -f grep-patterns noweb.defs > noweb.filtered.defs
noweave -indexfrom noweb.filtered.defs -delay tmp.nw > tmp.tex.in
python3 filter-uses.py < tmp.tex.in > tmp.tex
sed -i.bak "s/SCRIPTBUILDDATE/`LANG=C date`/" tmp.tex
sed -i.bak 's/<=/≤/g; s/>=/≥/g; s/<<module.c>≥/<<module.c>>=/; s/<<module.h>≥/<<module.h>>=/' tmp.tex
for i in 1 2 3; do sed -i.bak 's|\(.*//.*\)\([a-zA-Z]*\)_ \(.*\)|\1\2 \3|' tmp.tex; done
sed -i.bak 's|\(.*//.*\)\([a-zA-Z]*\)_}|\1\2}|' tmp.tex
sed -i.bak 's/Linux Libertine O/Linux Libertine/g; s/Linux Biolinum O/Linux Biolinum/g' tmp.tex
fc-list | grep -qi menlo || sed -i.bak 's/{Menlo}/{JetBrains Mono}/' tmp.tex
sed -i.bak 's/\\usepackage{scrpage2}/\\usepackage{scrlayer-scrpage}/; s/\\setheadsepline{[^}]*}/\\KOMAoptions{headsepline}/' tmp.tex
sed -i.bak '/usepackage\[latin1\]{inputenc}/d; /^\\blowUp{paper=x0.9}/d' tmp.tex
sed -i.bak 's|\\input{pics/burst-distribution-nehmer}|\\fbox{(EPS figure omitted)}|' tmp.tex
python3 - <<'PY'
s = open('tmp.tex').read()
i = s.index('\\documentclass'); j = s.index('\n', s.index('\\usepackage', i))
old = r'''\makeatletter
\DeclareOldFontCommand{\tt}{\normalfont\ttfamily}{\mathtt}
\DeclareOldFontCommand{\bf}{\normalfont\bfseries}{\mathbf}
\DeclareOldFontCommand{\it}{\normalfont\itshape}{\mathit}
\DeclareOldFontCommand{\rm}{\normalfont\rmfamily}{\mathrm}
\DeclareOldFontCommand{\sf}{\normalfont\sffamily}{\mathsf}
\DeclareOldFontCommand{\sc}{\normalfont\scshape}{\@nomath\sc}
\DeclareOldFontCommand{\sl}{\normalfont\slshape}{\@nomath\sl}
\makeatother
'''
open('tmp.tex', 'w').write(s[:j+1] + old + s[j+1:])
PY
tectonic --keep-logs --keep-intermediates -Z shell-escape tmp.tex
mv tmp.pdf $OUT.pdf
rm -f tmp.tex.bak
