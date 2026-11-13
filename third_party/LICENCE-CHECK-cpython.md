# Licence check: CPython
- Name:         CPython, the reference implementation of the Python language (issues #7, #57, #59)
- Upstream URL: https://github.com/python/cpython
- Tag:          v3.12.7 (submodule `third_party/cpython`, unmodified; fetched shallow: `git submodule update --init --depth 1 third_party/cpython`)
- Licence:      Python Software Foundation License Version 2 (PSF-2.0), see `third_party/cpython/LICENSE`. That file lists the releases and says
                that 2.2 and above are GPL-compatible. It also lists the licences of the parts that come with the source and are not built here
                (expat, libmpdec, the HACL* code, ...). What we build into the interpreter is the core (`Python/`, `Objects/`, `Parser/`) and
                the modules of `libc/python-setup.local`; `Python/dtoa.c` is David Gay's code (a permissive licence, notice in the file).
- Why it is compatible with GPLv3: the FSF lists the PSF licence as a GPL-compatible free software licence; it only asks that the
  copyright notice and the licence text stay with the code, and that the PSF's name is not used to promote derived products.
- Linux code in it? No (CPython is written independently; it uses the POSIX interface of the C library).
- Patches:      none (the build is configured with `--host=aarch64-linux-musl`; `libc/musl-cc` is the compiler wrapper, `libc/python-setup.local`
                the list of modules; see the chapter "Python" in `arm64/lit/libc.nw`)
