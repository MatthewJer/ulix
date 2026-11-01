# Ulix → ARM64: Bestandsaufnahme

Stand: Branch `arm64-port`. Es wurde noch kein Code portiert. Dies ist die Analyse, die vor dem Port nötig ist.
Alle Zeilennummern beziehen sich auf `ulix-book.nw` (32 557 Zeilen). Das Buch ist die einzige Quelle: `ulix.c`, `start.asm` und `ulix.ld` werden per `notangle` daraus erzeugt.

## 1. Warum das kein "Befehle übersetzen" ist

Ulix ist kein Programm, das nur x86-Instruktionen enthält, sondern ein Kernel für die **PC-Plattform**. Ein ARM64-Port bedeutet deshalb, die Hardware-Schicht neu zu schreiben:

| Bereich | x86 (Ulix heute) | ARM64-Entsprechung | Aufwand |
|---|---|---|---|
| Boot | GRUB Legacy von Floppy, Multiboot, `start.asm` (NASM) | UEFI/Device-Tree-Boot (z. B. qemu `-kernel`), neuer Startcode in GNU-as-Syntax | hoch |
| Segmentierung (GDT, 127 Treffer) | Flat-Segmente, Ring 0/3 über Selektoren (0x08/0x10/0x18/0x20) | entfällt; Privilegien über EL1/EL0 | mittel |
| Interrupts (IDT, 226 Treffer; PIC, 424 Treffer) | IDT + 8259-PIC, `iret` | Exception-Vektortabelle (`VBAR_EL1`) + GICv2/v3, `eret` | hoch |
| Syscalls | `int 0x80` (16 Treffer) | `svc #0` | niedrig bis mittel |
| TSS (65 Treffer) | Kernel-Stack-Wechsel bei Ring-Übergang | `SP_EL1`/`SP_EL0`, braucht kein TSS | mittel |
| Paging (~281 Treffer) | 2-stufig, 4-KB-Seiten, 32 Bit, `cr0`/`cr3` (33 Treffer) | 4-stufig (48 Bit), `TTBR0/1_EL1`, `TCR_EL1`, `MAIR_EL1` | **sehr hoch** |
| Prozesswechsel | Register + Stack-Frame von `iret` | neuer Kontext (x0–x30, SP, PC, PSTATE) | hoch |
| Port-I/O (`inportb`/`outportb`, 90 Treffer) | `in`/`out` | gibt es nicht; Geräte sind memory-mapped (MMIO) | siehe 2 |
| Inline-ASM | 61 `asm`-Blöcke | alle neu schreiben | mittel |
| Compiler | `i586-elf-gcc`, `-m32 -march=i586` | `aarch64-elf-gcc`; Ulix nutzt `__sync_*`-Builtins, die dort funktionieren | niedrig |
| Wortbreite | `int`/Zeiger = 32 Bit, harte Casts wie `(unsigned)fdc_buf` | 64-Bit-Zeiger; jeder Zeiger-nach-int-Cast und alle Strukturgrößen müssen geprüft werden | hoch |
| Userland | **fertige x86-Maschinencode-Bytes** im Buch (Zeile ~12758, Hexdump von `test`), ELF-Loader erwartet i386-ELF | muss neu kompiliert werden, der Loader auf ELF64/AArch64 umgestellt | mittel |

### Geräte, für die es auf ARM kein direktes Gegenstück gibt

Das Buch ist didaktisch um PC-Hardware herum gebaut. Auf dem ARM64-`virt`-Board (qemu) gilt:

| Ulix-Treiber | Port/Adresse heute | Ersatz |
|---|---|---|
| VGA-Textmodus (Zeile 16472, `0xB8000`) | Textspeicher + CRTC-Ports 0x3D4/0x3D5 | PL011-UART als Konsole oder ein Framebuffer (ramfb/virtio-gpu) |
| Tastatur (8042, Ports 0x60/0x64, Zeile 8146) | PS/2 | PL011-UART-Eingabe oder virtio-input |
| Serieller Port (16550, COM1 0x3F8) | Port-I/O | PL011 (`0x09000000` auf `virt`) |
| PIT 8253 (Ports 0x40/0x43, Zeile 16991) | 100 Hz Timer | Generic Timer (`CNTP_*`-Register), PPI-Interrupt |
| CMOS-RTC (0x70/0x71, Zeile 17015) | BCD-Uhrzeit | PL031-RTC (MMIO) |
| PIC (0x20/0x21/0xA0/0xA1, Zeile 7130) | 8259 | GIC |
| IDE/ATA-PIO (Ports 0x1F0–0x1F7, 0x3F6, Zeile 25941) | Festplatten | virtio-blk (MMIO); das ATA-Protokoll selbst entfällt komplett |
| Floppy-Controller + ISA-DMA (Zeile 26504, 26833) | NEC 765 | entfällt; virtio-blk als Ersatz |
| Speichergröße | hartkodiert 64 MB (`MEM_SIZE`, Zeile 5998) | RAM-Layout aus dem Device Tree lesen |

Praktisch ist damit **jeder Treiber** (Konsole, Eingabe, Timer, Uhr, Plattenzugriff, Interrupts) neu zu schreiben. Das Buch bräuchte für den Port neue Kapitel, nicht nur geänderte Zeilen. Der Kern (Scheduler, Prozess-/Dateisystem-Logik, Minix-FS, Syscall-Tabellen) bleibt weitgehend wiederverwendbar.

## 2. Wichtig zu VirtualBox und ARM

- VirtualBox emuliert auf ARM-Hosts (Apple Silicon) nach meinem Kenntnisstand **kein x86**, es startet dort nur ARM-Gäste (bitte gegen die aktuelle VirtualBox-Doku prüfen). Wer den Port will, um Ulix auf einem M-Mac zu starten, startet ihn mit **qemu `-M virt -cpu cortex-a72`** (oder UTM). VirtualBox ist dafür das falsche Werkzeug.
- Die Bochs- und qemu-Zeilen im Makefile müssen durch `qemu-system-aarch64` ersetzt werden.

## 3. Was unter VirtualBox (x86) offensichtlich Probleme macht

Das Buch sagt selbst (Zeile 30742–30744), dass Ulix nur in qemu bootet. Bochs und VirtualBox sind nie getestet worden. Auffällige Stellen, nach Wahrscheinlichkeit sortiert.

**Hinweis:** Die Stellen im Code (Zeilennummern, Ports, Schleifen) habe ich gelesen. Wie sich VirtualBox dort konkret verhält, ist dagegen **Vermutung aus Erfahrung mit der Hardware, nicht getestet**. Bitte jeden Punkt in einer echten VM verifizieren.

1. **Floppy-Treiber mit ISA-DMA** (Zeile 26504–26870). Der Treiber schreibt direkt auf den 8237-DMA-Controller (Ports 0x0A, 0x0C, 0x0B, 0x04/0x05, 0x81) und benutzt `outb_delay`. VirtualBox emuliert den FDC, aber Timing und DMA-Verhalten unterscheiden sich von qemu. Der Reset/Recalibrate und die `for (i < 10000/30000)`-Warteschleifen (Zeile 26526, 26562) sind zeitabhängig und können bei VirtualBox ablaufen, bevor der Controller antwortet.
2. **ATA-Erkennung in `ata_init`** (Zeile 26420). Die Schleife prüft nur `status != 0`. Auf einem Bus ohne Gerät liefert VirtualBox je nach Chipsatz `0xFF` (floating bus) statt `0`, dann wird fälschlich ein Laufwerk "erkannt" und `IDENTIFY` auf nichts ausgeführt. Dazu kommt, dass die Platten als **primärer Master/Slave** (0x1F0) angenommen werden. Beim Anlegen der VM müssen beide Platten genau so am IDE-Controller hängen.
3. **Hartkodierte 64 MB RAM** (`MEM_SIZE`, Zeile 5998), plus feste Speicherabbildung (Zeile 910). Die VM darf nicht mehr als 64 MB haben, oder der Rest bleibt ungenutzt; mit weniger crasht der Kernel. Die Multiboot-Speicherinfo wird nicht ausgewertet.
4. **CMOS-Uhr** (Zeile 17023–17036). Lies Register 0x32 (Jahrhundert) ohne Prüfung. Das Register ist in VirtualBox nicht zwingend belegt (kann 0 liefern, dann stimmt das Jahr nicht), und das Update-in-Progress-Flag (Register 0x0A) wird nie geprüft, es kann Zwischenwerte geben. Außerdem gilt: Der Code nimmt BCD an und liest Register 0x0B nicht. VirtualBox hat zusätzlich `--rtcuseutc`, die Zeit kann also um die Zeitzone verschoben sein.
5. **Serieller Port** (Zeile 16901, 17303). Die Schleifen warten nur bis zu 128 Mal auf das Sende-Bit (`LSR & 0x20`); ist COM1 in VirtualBox nicht eingeschaltet, wird Debugausgabe stillschweigend verworfen. Dann sieht man beim Absturz nichts. Die VM-Einstellung `--uart1 0x3F8 4` ist deshalb Pflicht.
6. **Boot über GRUB Legacy von Floppy** (`MENU.LST`, `ulixboot.img`). Das funktioniert in VirtualBox nur, wenn das Floppy-Image als primäres Boot-Gerät hängt, und es ist ein ungeprüfter Weg.
7. **PIT-Konstante und Interrupt-Takt** (Zeile 16978–16998). `1193180 / hz` stimmt, aber der Code geht von einem exakt periodischen IRQ0 aus. VirtualBox verliert unter Last Ticks; das ist ein Zeit-, kein Absturzproblem.
8. **CPU-Annahmen**: `-march=i586` (Compilerflags) und `__sync_*`-Atomics (Zeile 19439) erzeugen `lock cmpxchg`. Das läuft in VirtualBox. Wenn dort VT-x/AMD-V aus ist, startet ein Gast mit Paging im Echtmodus-Übergang meist trotzdem, nur langsamer.
9. **Nicht getestete Compiler-Kette**: Das Makefile hat feste Pfade `/usr/bin/gcc-4.4` (Linux) und `/usr/cross/bin/i586-elf-*` (macOS). Beides gibt es auf modernen Systemen nicht; GCC 4.4 ist 14 Jahre alt, mit neueren Versionen brechen vermutlich Inline-ASM und `-fno-function-cse`.

## 4. Vorschlag für das Vorgehen

Ein vollständiger Port ist ein mehrwöchiges Projekt. Sinnvolle Etappen, jede für sich lauffähig:

1. **Toolchain**: Build von Ulix auf dem Mac reproduzierbar machen (`aarch64-elf-gcc`, `noweb`, Skript statt fester Pfade).
2. **Bare-Metal-Skelett** für `qemu-system-aarch64 -M virt`: Startcode, Stack, PL011-Ausgabe, `printf` aus Ulix zum Laufen bringen.
3. **Exceptions + GIC + Generic Timer**: ersetzt IDT/PIC/PIT.
4. **MMU/Paging** auf 4 Stufen: der aufwendigste Teil, weil Ulix' Adressraum-Logik fest auf 32-Bit/2-stufig ausgelegt ist.
5. **Prozesse und Syscalls** (`svc`, EL0/EL1): Scheduler und Kontextwechsel anpassen.
6. **Massenspeicher**: virtio-blk statt IDE/Floppy, Minix-Dateisystem wiederverwenden.
7. **Userland** neu kompilieren (AArch64-ELF) und den ELF-Loader anpassen.

Parallel dazu die Liste aus Abschnitt 3 abarbeiten, falls Ulix weiterhin auf x86/VirtualBox laufen soll.

## 5. Status Etappe 1 (Build reproduzierbar)

Erledigt und getestet auf macOS/arm64 (Homebrew: `noweb nasm mtools i686-elf-gcc qemu`):

- `bin-build/assembler-parser.py`: von Python 2 (`file()`) auf Python 3 portiert.
- `bin-build/Makefile`: Cross-Compiler per `CROSS=` überschreibbar, Standard `i686-elf-` aus dem PATH statt `/usr/cross/bin/i586-elf-`; Modul-Extraktion nur, wenn `bachelorarbeit.nw` existiert (die Datei fehlt im Repo, vorher wurde `module.c` dabei auf 0 Byte abgeschnitten).
- Ergebnis: `make build` erzeugt mit GCC 16.2 einen `ulix.bin` (ELF32 i386, 200 KB). Der Kernel bootet in `qemu-system-i386` (64 MB, Floppy + 2 Platten) bis "swapper launched in background", serielle Ausgabe geprüft.
- Nicht geprüft: Tastatur/VGA-Interaktion, Userland-Programme in den Shells, `make pdf`.
- `notangle` meldet weiterhin "undefined chunk name"-Warnungen und Exit-Code 2 (Buchfehler, schon im Original per `; true` ignoriert).
- Das Repo enthält erzeugte Dateien (`ulix.c`, `*.o`, `ulix.bin`, `*.img`); ein Build im Verzeichnis ändert sie. Ich habe in einer Kopie gebaut.

## 6. Status Etappe 2 (Bare-Metal-Skelett)

Neues Verzeichnis `arm64/` (bewusst noch außerhalb des Literate-Programming-Quelltexts `ulix-book.nw`; die Übernahme ins Buch kommt, wenn die Struktur steht):

- `start.S`: Boot auf Core 0 (andere Kerne parken), Abstieg EL2 → EL1, FP/SIMD an, Stack, `.bss` löschen, Sprung nach `kmain`.
- `ulix.ld`: Ladeadresse `0x40080000` (RAM-Start von qemu `virt` + Kernel-Offset).
- `kmain.c`: PL011-UART (`0x09000000`) als Konsole, kleines `printf` (`%s %c %d %u %x`, `l`, Breite/0-Padding).
- `Makefile`: `make` und `make run` (`qemu-system-aarch64 -M virt -cpu cortex-a72`).

Getestet: Start sowohl bei EL1 (Standard) als auch bei EL2 (`virtualization=on`); Ausgabe zeigt EL1, `sizeof(void*) = 8`. Noch nicht vorhanden: Exceptions, Timer, MMU (Etappen 3–4). Der Linker warnt vor einem RWX-Segment, das ist bis zur MMU-Etappe unkritisch.

## 7. Status Etappe 3 (Exceptions, GIC, Timer)

In `arm64/`:

- `vectors.S`: Exception-Vektortabelle (16 Einträge, `VBAR_EL1`), sichert x0–x30, `ELR`, `SPSR` in einem 272-Byte-Frame und ruft `exc_handler (frame, kind)`; Rückkehr per `eret`. Ersetzt die x86-IDT samt `iret`-Frames.
- `irq.c`: GICv2 (Distributor `0x08000000`, CPU-Interface `0x08010000` auf qemu `virt`) ersetzt den 8259-PIC; der Generic Timer (`CNTP_TVAL/CTL`, PPI 30, 62,5 MHz in qemu) ersetzt den PIT und tickt mit 100 Hz wie `HZ` im x86-Kernel. `svc #0` (EC 0x15) wird als Syscall-Einstieg erkannt und kehrt zurück; alle anderen synchronen Exceptions geben ESR/FAR/ELR aus und halten an.

Getestet in qemu (`virt`, Cortex-A72): 3 Timer-Sekunden mit je 100 Ticks (`wfi`-Schleife), `svc #0` kehrt zurück, `brk #0` landet im Handler mit EC=0x3c.

Nicht getestet/vorhanden: FP/SIMD-Register werden im Frame nicht gesichert (Kernel baut mit `-mgeneral-regs-only`); Exceptions aus EL0 und eigener Stack je Kontext kommen mit den Prozessen (Etappe 5); GICv3 (nötig für `-M virt,gic-version=3` und echte Hardware) fehlt; SMP wird nicht benutzt.

## 8. Status Etappe 4 (MMU und Paging)

Der Kernel läuft jetzt in der oberen Adresshälfte (`TTBR1_EL1`), virtuelle Adresse = `0xFFFF000000000000` + physische Adresse. Das entspricht der Rolle von `0xC0000000` (Kernel) und `0xD0000000` (lineares RAM-Fenster) auf x86.

- `ulix.ld`: VMA `0xFFFF000040080000`, LMA `0x40080000`; Einstieg über `_start_phys` (qemu springt bei ausgeschalteter MMU an die physische Adresse).
- `start.S`: baut mit PC-relativem Code (`adr`) Boot-Tabellen (4 KB-Granule, 48 Bit, 1-GB-Blöcke: Geräte `0–1 GB` als Device-nGnRnE, RAM `1–2 GB` als Normal-WB) für `TTBR1` und eine Identity-Map für `TTBR0`, setzt `MAIR/TCR/TTBR` und `SCTLR` (M, C, I), springt auf die hohe Adresse, setzt Stack, löscht `.bss`. `IPS` wird aus `ID_AA64MMFR0_EL1` übernommen.
- `mmu.c`: Frame-Allocator (Bump-Pointer + Free-List, Frames kommen zeroed), `map_page`/`unmap_page` über 4 Tabellenebenen (legt fehlende Tabellen an, lehnt Doppel-Mapping und Block-Kollisionen ab, TLB-Invalidierung beim Unmap), `virt_to_phys` über die `AT`-Instruktion (Gegenstück zu `mmu_p`), `drop_identity_map` (`EPD0`).
- Treiber (`kmain.c`, `irq.c`) greifen auf PL011 und GIC jetzt über die lineare Abbildung zu (`KERNEL_VA + 0x09000000` usw.).

Test in qemu (Cortex-A72, 128 MB, EL1 und EL2-Start): Map/Unmap, AT-Übersetzung, Schreiben über Mapping sichtbar im linearen Fenster, Doppel-Map abgelehnt, Lesen einer ungemappten Seite → Translation Fault Level 3 (FAR korrekt), Schreiben auf RO-Seite → Permission Fault Level 3, Frame-Wiederverwendung. Alle 10 Prüfungen bestehen.

Offen und bewusst nicht gemacht:
- RAM-Größe ist noch fest (128 MB); soll später aus dem Device Tree kommen.
- Der ganze Kernel liegt in einem RWX-1-GB-Block; Trennung in `.text` (RX), `.rodata` (RO), `.data` (RW, NX) auf 4-KB-Seiten fehlt.
- `TTBR0` ist leer; benutzerseitige Adressräume (`address_spaces[]`, ASIDs, Kopie des Kernel-Teils entfällt, da `TTBR1` global ist) kommen in Etappe 5.
- Kein Copy-on-Write, kein Swap (x86-Ulix hat beides): diese Teile des Buches müssen auf die neue Seitentabellen-API umgeschrieben werden.

## 9. Status Etappe 5 (Prozesse, Syscalls, EL0)

Neu in `arm64/` (gemeinsame Definitionen jetzt in `ulix.h`):

- `mmu.c`: Adressräume pro Prozess (`TTBR0`): `new_user_l0`, `map_page_in` (beliebige Wurzeltabelle), `free_user_space` (gibt Tabellen und Datenframes rekursiv frei), `sync_icache` (D-Cache säubern, I-Cache invalidieren nach dem Kopieren von Code), `user_mm_init` (leere `TTBR0`, `EPD0` wieder aus). Seitenflags `PAGE_URX` (Code, EL0-lesbar/ausführbar) und `PAGE_URW` (Stack, nie ausführbar). Zähler `frames_in_use` für den Leck-Test.
- `sched.c`: Tasks mit eigenem Kernel-Stack (4 Seiten) und eigenem Adressraum; Round-Robin-Scheduler; `cpu_switch` (in `vectors.S`) sichert x19–x30, SP und `SP_EL0`. Neue Prozesse starten über einen vorbereiteten Frame auf ihrem Kernel-Stack und `eret` nach EL0. ASID = PID, daher kein TLB-Flush beim Wechsel. Der Idle-Task (Slot 0) ist der Boot-Thread und räumt Zombies ab, da ein Prozess seinen eigenen Kernel-Stack nicht freigeben kann.
- `irq.c`: Syscalls per `svc #0` (`x8` = Nummer, `x0–x2` Argumente): `write`, `getpid`, `yield`, `exit`. Benutzerzeiger werden nie direkt dereferenziert: Seite für Seite mit `AT` übersetzt und über das lineare Fenster gelesen. Fehler im EL0-Code (Data/Instruction Abort) töten nur den Prozess. Der Timer-Interrupt beendet nach 10 ms die Zeitscheibe (`schedule_preempt`), nachdem er per `EOIR` quittiert wurde.
- `user.S`: zwei EL0-Programme (positionsunabhängig): „Hello“ (5 Runden `write` mit Busy-Loop) und „bad“ (liest Kernel-Speicher).

Test in qemu (Cortex-A72, EL1- und EL2-Start): zwei Hello-Prozesse laufen verzahnt (Wechsel durch Timer-Preemption, 12–14 von 16–18 Kontextwechseln), der böse Prozess wird mit EC=0x24 / Permission Fault Level 1 beendet und der Kernel läuft weiter, alle drei werden abgeräumt, `frames_in_use` ist danach wieder auf dem Ausgangswert (kein Leck). Zusammen mit den Tests aus Etappe 4: alle Prüfungen bestehen.

Offen: der Kernel ist nicht präemptiv (Interrupts bleiben im Handler maskiert; das reicht, solange Syscalls kurz sind); kein `fork`/`exec`/`wait`, keine Signale, kein ELF-Loader; die User-Programme sind handgeschriebener Assembler im Kernel-Image (Etappe 7: AArch64-ELF laden). Der Code ist bewusst eine neue, kleine Implementierung der Ulix-Konzepte (Task, Adressraum, Syscall) und noch nicht die portierte Ulix-Prozessverwaltung aus `ulix-book.nw` (Thread-Tabelle, `fork`, Signale, Semaphore); diese aufzusetzen ist der Hauptteil von Etappe 5b.

## 10. Status Etappe 6 (Massenspeicher: virtio-blk + Minix)

Neu in `arm64/`:

- `virtio.c`: virtio-mmio-Blocktreiber (Polling) für die qemu-`virt`-Maschine, sucht das Gerät in den 32 MMIO-Slots ab `0x0a000000`, unterstützt Legacy-Transport (Version 1, qemu-Standard) und modernen Transport (Version 2). Eine Virtqueue mit 8 Einträgen, jede Anfrage ist eine Kette aus Header, Daten und Statusbyte. Schnittstelle: `blk_read/blk_write (sector, buf, n)`, `blk_capacity`. Das ersetzt IDE-PIO und den Floppy-DMA-Treiber des x86-Kernels (gleiche Aufgabe, Sektoren lesen/schreiben).
- `minixfs.c`: Minix-v2-Dateisystem (nur lesend) auf dem Blockgerät, 14- und 30-Zeichen-Namen, direkte, einfach und doppelt indirekte Zonen, Pfadauflösung (`minix_lookup`), `minix_read`, `minix_readdir`. Das ist dasselbe On-Disk-Format wie bei x86-Ulix, `bin-build/minixdata.img` wird unverändert verwendet.
- `Makefile`: `make run` startet qemu mit `-snapshot` und dem Image als virtio-Platte (`DISK=` überschreibbar). Mit `-snapshot` landen Schreibzugriffe nur im Zwischenspeicher, die Image-Datei im Repo bleibt unverändert (geprüft: `git status` bleibt sauber).

Getestet in qemu (Cortex-A72, EL1- und EL2-Start, Transport v1 und v2): Gerät gefunden, Kapazität 2880 Sektoren, Superblock-Magic 0x2478, Lesen hinter das Plattenende wird abgelehnt, Schreiben und Zurücklesen identisch, Mount, Verzeichniszahlen (`/` 16 Einträge, `/bin` 63), unbekannter Pfad, Dateigrößen und Prüfsummen von `/hello.c`, `/test.txt`, `/init` (nutzt die indirekte Zone) und `/bin/vi` (25 KByte, Unterverzeichnis). Die Referenzwerte stammen aus einem unabhängigen Python-Parser auf dem Host. Insgesamt 30 Prüfungen, alle bestehen, inklusive der Tests der Etappen 4 und 5.

Bewusst offen: Treiber arbeitet per Polling (kein Interrupt, kein Schlafenlegen); DMA ohne Cache-Wartung, was nur auf kohärenten Plattformen wie qemu stimmt; nur eine Anfrage gleichzeitig; Dateisystem ist schreibgeschützt und ohne Puffercache; es gibt noch keine `open/read/close`-Syscalls und kein VFS (die Mount-Tabelle, `/dev`, `/tmp` und Swap aus dem Buch fehlen).

## 11. Status Etappe 7 (ELF-Loader und AArch64-Userland)

Neu in `arm64/`:

- `user/`: Freestanding-Userland für AArch64 (`crt0.S`, `ulib.c/.h` mit Syscall-Wrappern und `printf`, Linkerskript `user.ld` mit je einem `PT_LOAD` pro Rechteklasse auf eigener 4-KB-Seite) und drei Programme: `hello` (prüft sich selbst: `.data`, `.bss` über drei Seiten, `.rodata`, Rekursion auf dem Stack, `argc/argv`; Exit-Status = Zahl fehlgeschlagener Checks), `cat` (`open/read/close`) und `crash` (schreibt auf die eigene Code-Seite). Gebaut ohne FP/SIMD, da der Kernel diese Register beim Kontextwechsel nicht sichert.
- `elf.c`: ELF64-Loader (`ET_EXEC`, `EM_AARCH64`). Prüft Header, Segmentgrenzen und Dateigröße, bildet die Segmente seitenweise ein (R+X → Code, R → read-only, R+W → Daten/BSS, BSS kommt aus genullten Frames), lehnt W+X-Segmente ab (W^X), prüft, dass der Einstiegspunkt in einem ausführbaren Segment liegt, und räumt bei Fehlern wieder auf.
- `sched.c`: `task_create_elf (path, argc, argv)` legt Adressraum, Stack (4 Seiten) und `argc/argv` auf dem Stack an (`x0 = argc`, `x1 = argv`), pro Prozess eine Dateideskriptor-Tabelle (8 Einträge), Exit-Codes pro PID.
- `irq.c`: neue Syscalls `open`, `read`, `close`; `write` auch für fd 2. Benutzerzeiger gehen durch `user_to_phys` (`AT S1E1R/W`): nur User-Bereich, beim Schreiben muss die Seite wirklich beschreibbar sein.
- `tools/minix_add.py`: Host-Werkzeug, das Dateien in das Wurzelverzeichnis eines Minix-v2-Images schreibt (auf macOS gibt es kein `mkfs.minix`/Mount). `make` erzeugt damit `disk.img` aus dem x86-Image plus Programme; das Original unter `bin-build/` bleibt unberührt. Dabei werden zwei absichtlich kaputte ELF-Varianten (falsche Architektur, RWX-Segment) für die Loader-Tests angelegt.

Test in qemu (EL1 und EL2): 45 Prüfungen, alle bestehen. Neu geprüft: Programme starten von der Platte und laufen mit korrektem `argv`; die 7 Selbsttests in `hello`; `cat` liest 316 Byte bzw. 1680 Byte (zwei Zonen) korrekt, fehlende Datei liefert Fehlercode 1; `crash` wird mit Permission Fault getötet und der Kernel läuft weiter; ELF mit falscher Architektur, W+X-Segment, flaches x86-Binary, Textdatei und fehlende Datei werden abgelehnt (jeweils mit der richtigen Begründung); danach ist kein Frame verloren.

Bewusst offen: kein `fork`/`exec`/`wait`/`spawn` aus dem User-Modus (Programme startet nur der Kernel); `write` ist auf 256 Byte pro Aufruf begrenzt; keine dynamischen Bibliotheken, kein `brk`/`mmap`; Fehler beim Kopieren von Benutzerzeigern in `sys_open` sind pro Byte statt pro Seite (langsam, aber korrekt).

Damit ist die in Abschnitt 4 beschriebene Etappenfolge 1–7 abgeschlossen. Was fehlt, ist der eigentliche Ulix-Kernel aus `ulix-book.nw` (siehe Abschnitt 1 und 9): Thread-Tabelle, `fork`/`exec`/`wait`, Signale, Semaphore, VFS, Terminals, Swap, Copy-on-Write und die Rückführung in das Literate-Programming-Dokument.

## 12. Bestandsaufnahme des eigentlichen Ulix-Kernels (`ulix.c`, 8 897 Zeilen)

Grundlage: `notangle -Rulix.c` aus dem Buch, dann Funktionen gezählt (269 Funktionen, 4 800 Zeilen; die übrigen rund 4 100 Zeilen sind Strukturen, Konstanten, Makros, Globals und Tabellen) und der Code testweise mit `aarch64-elf-gcc -fsyntax-only -Wall -Wextra` übersetzt. Die Einteilung unten ist **heuristisch** (Suche nach x86-typischen Namen), keine manuelle Prüfung jeder Funktion.

**Gute Nachricht:** Der Kernel-Code übersetzt für AArch64 **ohne einen einzigen Fehler** (die 31 Inline-Assembler-Blöcke prüft der Compiler im Modus `-fsyntax-only` nicht; sie müssen ohnehin von Hand ersetzt werden). Es bleiben 205 Warnungen, davon 153 echte 64-Bit-Probleme (97× Integer → Zeiger, 56× Zeiger → Integer) und 40× `pointer-sign`.

### Ursachen der 64-Bit-Probleme

- Fast alles geht auf **eine** Definition zurück: `typedef unsigned int memaddress;` (Zeile 584). Auf `unsigned long` umgestellt verschwindet der Großteil der Warnungen. Dazu `size_t` (derzeit `int`).
- 63 der 153 Treffer liegen in Makros und Globals außerhalb von Funktionen, der Rest verteilt sich auf 53 Stellen (zum Beispiel `syscall_open` 13×, `ksh_command_test` 9×).
- `ulong`/`unsigned long` werden an wenigen Stellen als 32-Bit-Wert benutzt (Signalmasken, `unixtime`, GDT/IDT-Hilfen). Auf AArch64 sind sie 64 Bit breit; das ist für die Maskenfelder egal, für On-Disk-Strukturen müsste es geprüft werden (die Minix-Strukturen aus Etappe 6 sind bereits mit festen Breiten umgesetzt).

### Was wohin gehört (Funktionszeilen)

| Bereich | Fkt. | Zeilen | Einschätzung |
|---|---|---|---|
| Minix-Dateisystem (`mx_*`) | 30 | 743 | fast unverändert übernehmbar; ist nur über `blk_read/blk_write` mit der Platte verbunden |
| Syscall-Rümpfe (`u_*`) | 35 | 660 | größtenteils portabel; `u_fork` und `u_kill` brauchen Adressraum-/Frame-Anpassung |
| Syscalls (`syscall_*`) | 36 | 533 | Dispatcher und Registerzugriff neu (`regs->eax` → `frame->x[0]`), Rümpfe meist übernehmbar |
| Kernel-Shell (`ksh_*`) | 10 | 351 | portabel, hängt an Konsole und Tastatur |
| Adressräume/Paging | 19 | 417 | **neu schreiben** auf `map_page_in`/`free_user_space` (Etappen 4/5): 2-stufiges x86-Paging, Swap (`page_out`) und Copy-on-Write müssen auf 4-stufige Tabellen |
| Threads/Scheduler | 11 | 262 | Logik übernehmbar, Kontextwechsel und Stack-Frames neu (Etappe 5 hat die Bausteine) |
| Konsole/VT/Tastatur | 26 | 411 | **ersetzen** durch UART (zehn virtuelle Terminals und VGA-Text entfallen oder werden auf eine serielle Konsole abgebildet) |
| Floppy/IDE/Disk | 22 | 398 | **entfällt** (virtio-blk aus Etappe 6 ersetzt es) |
| Interrupts/Timer/Uhr (GDT/IDT/PIC/TSS) | 9 | 164 | **entfällt** (Etappe 3), `unixtime` bleibt; Uhr kommt aus PL031 statt CMOS |
| Übrige (Pipes, Signale, Hilfsfunktionen) | 71 | 861 | meist portabel; Signalzustellung (Rücksprung-Trampolin, Frame manipulieren) ist x86-spezifisch |

Grob: etwa 55 % der Funktionszeilen enthalten nichts Architekturspezifisches und müssen nur mit dem neuen `memaddress` neu übersetzt werden. Rund 35 % berühren x86-Register, Segmente oder Paging und müssen angepasst werden. Etwa 10 % sind Hardware-Treiber, die durch die bereits geschriebenen ersetzt werden.

### Benutzerseite

- `lib-build/`: `ulixlib.c` (570 Zeilen), `init.c`, `orig-sh.c` (824 Zeilen, die Shell) und rund 60 kleine Programme in `lib-build/tools/`. Sie werden bisher mit `i586-elf-gcc` gebaut und rufen den Kernel über `int 0x80`. Für AArch64 müssen die Syscall-Stubs auf `svc #0` (Nummer in `x8`) umgestellt und alles mit dem neuen Linkerskript `user.ld` gebaut werden (das Format steht seit Etappe 7).
- Die Syscall-**Nummern** und Strukturen (zum Beispiel `getpsinfo`, `stat`) müssen zwischen Kernel und Bibliothek identisch bleiben; die kommen aus demselben Buch, das hilft.

### Realistische Reihenfolge für die Portierung

1. `memaddress` auf 64 Bit, Kernel-Code in `arm64/` einbinden, zunächst nur die portablen Bereiche (Minix inkl. Schreiben, `dev_*`, Pipes, Signalstrukturen). Ziel: Dateisystem-Tests aus dem Buch laufen auf der virtio-Platte.
2. Prozess-/Thread-Tabelle (TCB) und Scheduler aus dem Buch statt der vereinfachten aus Etappe 5; `fork` mit Adressraum-Kopie (`as_*`) auf den neuen Seitentabellen.
3. Syscall-Dispatcher auf `svc` mit der Syscall-Tabelle aus dem Buch; Signale; Semaphore/Mutex.
4. Konsole: UART statt VGA/Tastatur, `kernel_shell` darauf.
5. Userland neu bauen (`ulixlib`, `init`, `sh`, Werkzeuge) und `init` von der Platte starten.
6. Optionale Teile: Swap und Copy-on-Write auf 4-KB-Seiten, mehrere Terminals.
7. Ergebnis zurück in `ulix-book.nw` als Literate-Programm (neue Kapitel für ARM64) und Dokumentation.

**Einschätzung:** Es ist weniger als befürchtet, aber nicht wenig. Die Hardware-Schicht (Etappen 1–7) ist erledigt; der portable Kern ist überschaubar und übersetzt schon. Der Aufwand liegt bei den rund 1 600 Zeilen mit x86-Paging-, Register- und Prozesskontext-Bezug, dem Userland und dem Debugging des Zusammenspiels. Eine seriöse Stundenangabe kann ich nicht geben; im Umfang ist das ungefähr so viel wie die bisherigen sieben Etappen zusammen, und die schwierigsten Stellen (`fork`, Swap, Signale) kommen erst noch.

## 13. Schritt 1 der Kernel-Portierung: Buchcode läuft auf ARM64 (Dateisystem lesen und schreiben)

**Randbedingung: der x86-32-Bit-Build bleibt erhalten.** Alle Änderungen am Buch (`ulix-book.nw`) sind deshalb architekturneutral oder per `#if defined(__i386__)` getrennt. Nachweis nach jeder Buchänderung: `.text` der x86-Objektdatei ist **bit-identisch** zur Baseline (gleicher MD5 des `.text`-Abschnitts, gleiche Größen von text/data/bss), und der x86-Kernel bootet in `qemu-system-i386` mit identischer Ausgabe.

Änderungen am Buch:
- `memaddress` ist jetzt `__UINTPTR_TYPE__` (x86: `unsigned int`, AArch64: `unsigned long`), `size_t` ist `__PTRDIFF_TYPE__` (x86: `int`, AArch64: `long`). Das beseitigt den Großteil der 153 Zeiger/Integer-Warnungen aus Abschnitt 12; im übernommenen Code bleiben null Cast-Warnungen.
- `PHYSICAL(x)` (RAM-Fenster) hat auf AArch64 die Basis `0xFFFF000000000000`.
- Der Aufruf des eigenen Syscalls `readchar` in `u_read` steckt jetzt im Makro `READCHAR_SYSCALL`: x86 expandiert zum selben Assembler wie zuvor, andere Architekturen rufen `arch_readchar()`.

Neu in `arm64/` (der Kernel-Code des Buchs wird **nicht kopiert**, sondern bei jedem Build neu aus dem Buch erzeugt):
- `ulix-arm64.nw`: noweb-Wurzelchunks, die die Chunks des Buchs (Konstanten, Makros, Typen, Prototypen, globale Variablen, Funktionen) wiederverwenden: `notangle -Rulix-decl.h ../ulix-book.nw ulix-arm64.nw`.
- `tools/port.py` + `port.manifest`: wählen einzelne Funktionen und Globals aus; aktuell 77 Funktionen und 14 Globals: das komplette Minix-Dateisystem `mx_*`, die Hilfsfunktionen (`splitpath`, `relpath_to_abspath`, String-/Speicherfunktionen), die Blockschicht (`readblock`, `writeblock_raw`) und die darüberliegende Geräte-/VFS-Schicht (`u_open`, `u_read`, `u_stat`, `u_close`, `u_unlink`, `u_chdir`, `u_getcwd`, `dev_*`, `check_access`, `get_dev_and_path`).
- `glue.c`: plattformabhängiger Ersatz: `readblock_hd/writeblock_hd` über virtio-blk, `dev_size`, Stubs für Floppy und seriellen Datenträger, `arch_readchar` über die UART.
- **Bekannte Lücke:** Der Pufferspeicher (Buffer Cache) des Buchs ist noch nicht portiert (er braucht Thread-Tabelle und Mutexe). `writeblock` schreibt deshalb in `glue.c` sofort durch auf die Platte (das Buch speichert nur im Cache und lässt später schreiben). Sobald der Scheduler des Buchs läuft, entfallen `glue.c`-Stubs und `writeblock` wieder zugunsten der Buchversionen (nur `port.manifest` ändern).
- `bookfs_test.c`: Tests des Buchcodes auf der virtio-Platte: Lesen der x86-Dateien (Größe, Prüfsumme), Anlegen/Schreiben/Lesen/Überschreiben (40 000 Byte über die indirekte Zone), `lseek`, `ftruncate`, `unlink`, 300 000 Byte über die doppelt indirekte Zone, `mkdir` (auch verschachtelt), Hardlinks mit Link-Zähler, und die Block-Bilanz: nach dem Löschen aller Hilfsdateien stimmt die Zahl freier Blöcke exakt mit dem Bedarf der übrig gebliebenen Dateien überein.
- `tools/minix_check.py` und `make verify`: Der Test läuft auf einer Kopie des Images **ohne** `-snapshot`; danach prüft ein unabhängiges Python-Programm das veränderte Image (Verzeichnisbaum, Inode- und Zonen-Bitmaps, Link-Zähler, verwaiste Zonen/Inodes) und vergleicht den Inhalt von `/keep.bin` byteweise mit dem erwarteten Muster. Ergebnis: 0 Probleme, 0 verwaiste Zonen oder Inodes. Das Skript hat dabei auch das Hilfsprogramm `minix_add.py` und das Original-x86-Image geprüft (beide in Ordnung).
- `minix_add.py --grow=N` vergrößert das Test-Image (Dateisystem von 1440 auf 4096 Zonen = 4 MByte), damit Schreibtests mit Platz laufen; `disk.img` ist damit 4 MByte groß.

Ergebnis in qemu (Cortex-A72, EL1 und EL2, Legacy- und modernes virtio): **79 Prüfungen, alle bestehen**, einschließlich der Tests der Etappen 4 bis 7.

Zwei Eigenheiten des Buchs, die ich beim Testen gelernt habe (keine Portierungsfehler): `mx_getdent` liefert auch unbelegte 32-Byte-Slots (Inode 0), und `mx_mkdir` prüft nicht, ob das Verzeichnis schon existiert (das macht `u_mkdir` eine Ebene höher); ein zweiter `mx_mkdir` auf einen vorhandenen Pfad würde das Dateisystem beschädigen.

Nächster Schritt (Schritt 2 aus Abschnitt 12): Thread-Tabelle (TCB) und Scheduler des Buchs statt der vereinfachten aus Etappe 5; dann fallen Buffer-Cache-Stubs und Durchschreib-`writeblock` weg.
