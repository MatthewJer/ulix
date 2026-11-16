> **Notice: this is an unofficial fork.** It adds a port of Ulix to 64-bit ARM (AArch64) in the directory [`arm64/`](arm64/), a few
> changes in `ulix-book.nw` that the port needs (mostly behind `#if` branches, see [`arm64/README.md`](arm64/README.md)) and one
> additional appendix. **The ARM64 port and these changes were not written by the authors of Ulix** (Hans-Georg Eßer and
> Felix Freiling). The authors have nothing to do with this fork, did not review or endorse it, and are not responsible for it.
> The original project is at <https://github.com/hgesser/ulix>; please report problems with the ARM64 port to this fork, not to the
> original authors. The software stays under the GNU General Public License, version 3 (see [`LICENSE`](LICENSE)); the copyright
> notices of the original are kept. The text below is the original README.

# Ulix, the Literate Unix
Ulix (Literate Unix) is a Unix-like operating system that was developed at University of Erlangen-Nuremberg. We used Donald E. Knuth's concept of Literate Programming for the implementation and documentation. The intention was to create a fully working system which can be used in operating system courses to show students how OS concepts (such as paging and scheduling) can be implemented. Literate programs are very accessible because they can be read like a book; the order of presentation is not enforced by program logic or compiler restrictions, but instead is guided by the implementer's creative process.

Ulix was written in C and Assembler for the Intel x86 architecture; for literate programming we used Norman Ramsey's noweb tool.

Ulix has already been used twice in operating system courses at TH Nürnberg (Nuremberg Institute of Technology); lecture slides and videos in German language are online (see below).

Your options are ...
* [Ulix Development Blog](http://hgesser.de/ulix/)
* Ulix Source Code (released under the GPL, version 3, on this GitHub page)
* The [Wiki](https://github.com/hgesser/ulix/wiki) (here on GitHub)
* [The Ulix Book](http://ulixos.org/doc/ulix-book-0.13.pdf) (Hans-Georg Eßer and Felix Freiling: The Design and Implementation of the Ulix Operating System, 708 pages, September 2015)
* [Development VM](https://www.dropbox.com/s/3x3iwzvygecivf6/Ulix-0.13-Debian.ova?dl=1) (OVA format, tested with VirtualBox, Debian-based, 1 GByte, expands to ca. 3 GByte)
* the full list of publications about Ulix (on the [http://ulixos.org/](http://ulixos.org/) page)
* lecture notes of Ulix-based courses (on the [http://ulixos.org/](http://ulixos.org/) page) 

Note: ulix-book.nw is the literate document.

## ARM64 port
Ulix also runs on 64-bit ARM (AArch64) in `qemu-system-aarch64`, for example on Apple-silicon Macs. The x86 version still builds as before.
See [`arm64/README.md`](arm64/README.md) (build, run, tests, and how the port reuses the code of this book) and
[`ARM64-PORT.md`](ARM64-PORT.md) (work log).

## Getting the sources (fork)
The foreign code (musl, BearSSL, CPython and the libraries behind it) is not copied into this repository; it comes as unmodified git submodules in
`third_party/` (list: [`THIRD-PARTY.md`](THIRD-PARTY.md), rules: [`third_party/README.md`](third_party/README.md)). Clone with the submodules:

    git clone --recursive https://github.com/MatthewJer/ulix.git        # shallow submodules: add --shallow-submodules
    # or, in a clone that was made without --recursive:
    git submodule update --init                                         # everything (CPython alone is about 700 MByte; --depth 1 makes it 100)
    git submodule update --init --depth 1 third_party/musl third_party/bearssl     # the minimum for `make PYTHON=0 ARCHIVE=0`

`make` in `arm64/` first runs `make check-submodules`. A missing submodule stops the build at once with the list of directories and the command that
fetches them, instead of failing later on a missing header. `make TLS=0`, `make ARCHIVE=0` and `make PYTHON=0` leave out the features that need
BearSSL, libarchive or CPython and their libraries, and then the check does not ask for these submodules either.

## Branches and releases (fork; issue #15)
This is how the repository is used today, written down so that it stays the same with more people:

* **`rpi4` is the stable line** and the default branch of the fork. It builds, `make pcheck` is green on it, and what it contains has run on the
  board or in qemu. Nobody works directly on it except for documentation fixes and the merge of finished topics.
* **`master` follows the original project** (`origin`, Hans-Georg Eßer and Felix Freiling, including pull requests that others made there). It is only
  updated from `origin`, never with port work. `arm64-port` is the older line of the port without the Pi parts; it is kept for reference.
* **One topic branch per issue or group of issues**, branched from `rpi4` and named after the subject (`libc-musl`, `tls-support`,
  `daemons-pipes`, `userspace-servers`, `python-prereqs`; from now on with the milestone as prefix, for example `m2-futex-threads`). Commits are small and say
  what and why; the issue number is in the message (`#124`).
* **Merging:** a topic branch is first rebased onto the current `rpi4` (so that conflicts are solved on the topic, and the history of `rpi4` stays
  straight), `make pcheck` runs on the result, and then it goes into `rpi4` as a fast-forward (a merge commit only for a topic that several people worked
  on, as for the spin table). No force push on `rpi4`; topic branches may be rebased as long as nobody else has them.
* **A shared `develop` branch is not needed** while one maintainer merges. When more people work in parallel, a `develop` branch takes the topics first,
  and `rpi4` receives `develop` only when `make pcheck` is green and the maintainer has run the changes on the board.
* **Releases** (a proposal; no such tag exists yet) are tags on `rpi4`: `ulix-arm64-MAJOR.MINOR` (one per finished milestone M1, M2, ...; the original release is tagged `v0.13`). A tag comes with
  the rebuilt `ulix-book.pdf` and the section of `ARM64-PORT.md` that describes the milestone. The SD card image is built from the tag
  (see "Reproducible run on the Raspberry Pi", section 45 of `ARM64-PORT.md`).
* **Remotes:** `origin` is the original project and is never pushed to; `fork` is the GitHub repository of this fork (`MatthewJer/ulix`); `backup` is the private copy.
