all: tools bin

pdf: ulix-book.nw
	make -C tex-build

# the same with tectonic instead of xelatex (see tex-build/tectonic.sh)
pdf-tectonic: ulix-book.nw
	make -C tex-build pdf-tectonic

bin: ulix-book.nw
	make -C bin-build

run:
	make -C bin-build run

runs:
	make -C bin-build runs

tools: ulix-book.nw
	make -C lib-build

clean:
	make -C lib-build clean
	make -C bin-build clean
	make -C tex-build clean
	make -C libtex-build clean

mkfs:
	ssh -p 2222 localhost "/sbin/mkfs.minix -2 bin-build/minixdata.img; sync"

