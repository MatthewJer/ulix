# The ARM64 part of Ulix as a book: weave the chapters with noweave, typeset with tectonic (or xelatex).
CHAPTERS = $(addprefix ,$(addsuffix .nw,$(shell cat CHAPTERS)))
TEX = $(shell command -v tectonic >/dev/null && echo "tectonic --keep-logs -o build" || echo "xelatex -interaction=nonstopmode -output-directory=build")

all: build/ulix-arm64.pdf

build/body.tex: $(CHAPTERS) CHAPTERS escape-names.pl
	mkdir -p build
	noweave -n -latex $(CHAPTERS) | perl escape-names.pl > $@

noweb.sty: ../../tex-build/noweb.sty
	cp $< $@

build/ulix-arm64.pdf: ulix-arm64.tex build/body.tex noweb.sty
	$(TEX) ulix-arm64.tex
	$(TEX) ulix-arm64.tex

clean:
	rm -rf build noweb.sty
