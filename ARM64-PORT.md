# Ulix → ARM64: Bestandsaufnahme

> **Hinweis: nicht das Werk der ursprünglichen Autoren.** Dieses Dokument gehört zu einem inoffiziellen Fork von Ulix, der einen
> Port auf AArch64 enthält. Port und Dokument wurden **nicht von den Autoren von Ulix** (Hans-Georg Eßer und Felix Freiling)
> geschrieben. Diese haben mit dem Fork nichts zu tun, haben ihn weder geprüft noch gebilligt und sind nicht dafür verantwortlich.
> Das Originalprojekt: <https://github.com/hgesser/ulix>. Lizenz: GNU GPL Version 3 wie beim Original (siehe `LICENSE`).


Stand: Branch `arm64-port`. Dieses Dokument ist ein **Arbeitsprotokoll**: Die Abschnitte 1 bis 4 sind die Analyse, die vor dem Port nötig war; ab Abschnitt 5 folgt das Protokoll der Etappen. Zahlen, Testzahlen und Aussagen wie "fehlt noch" gelten jeweils für den Stand der Etappe, in der sie stehen. Der Port läuft inzwischen vollständig in qemu; den heutigen Stand beschreiben `arm64/README.md` und `arm64/TODO.md`.
Alle Zeilennummern beziehen sich auf den **ursprünglichen** `ulix-book.nw` (32 557 Zeilen, Upstream-Commit `32535a1`; das Buch in diesem Fork ist inzwischen länger). Das Buch ist die einzige Quelle: `ulix.c`, `start.asm` und `ulix.ld` werden per `notangle` daraus erzeugt.

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

Zwei Eigenheiten des Buchs, die ich beim Testen gelernt habe (keine Portierungsfehler): `mx_getdent` liefert auch unbelegte 32-Byte-Slots (Inode 0), und `mx_mkdir` ([Zeile 23942](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23942)) prüft nicht, ob das Verzeichnis schon existiert (das macht `u_mkdir` eine Ebene höher); ein zweiter `mx_mkdir` auf einen vorhandenen Pfad würde das Dateisystem beschädigen.

Nächster Schritt (Schritt 2 aus Abschnitt 12): Thread-Tabelle (TCB) und Scheduler des Buchs statt der vereinfachten aus Etappe 5; dann fallen Buffer-Cache-Stubs und Durchschreib-`writeblock` weg.

## 14. Schritt 2 der Kernel-Portierung: Thread-Code des Buchs läuft auf ARM64

Aus dem Buch laufen jetzt auch **Scheduler, Ready- und Blocked-Queues, `block`/`deblock`, Mutexe (`mutex_lock/unlock`, `get_new_lock`) und Kernel-Semaphore** auf ARM64 (zusammen 98 Funktionen und 23 Globals in der Auswahl). Die Kernel-Threads schalten über den Kontextwechsel aus Etappe 5 um.

Änderungen am Buch (alle x86-neutral, geprüft):
- `<<scheduler implementation>>`: Der Kontextwechsel (ESP/EBP, CR3, TSS, `iret` für neue Threads) sowie die Blöcke „Signale prüfen“ und „alte Kernel-Stacks freigeben“ stehen jetzt in `#if defined(__i386__)`; sonst ruft der Scheduler `arch_context_switch()`.
- `<<resign>>`: x86 behält `int 0x80` (Syscall 66), andere Architekturen rufen `arch_resign()`.
- Prototypen von `arch_resign` und `arch_context_switch` unter `#if !defined(__i386__)`.
- **Fehlerkorrektur im Buch:** `wait_semaphore` und `signal_semaphore` arbeiteten auf einer **Kopie** des Semaphors (`kl_semaphore sem = kl_semaphore_table[sid]`; Fundstelle im Original: [Zeile 18163](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L18163-L18164) und [Zeile 18186](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L18186-L18187), ebenso in der inaktiven Variante [Zeile 19413–19426](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L19413-L19426)). Zähler und Warteschlange wurden nur in der Kopie auf dem Stack verändert, die Semaphore haben auf x86 daher nie funktioniert (der Kernel benutzt sie nirgends, nur die Mutexe, deshalb fiel es nicht auf). Jetzt wird mit einem Zeiger gearbeitet, im Text steht eine Anmerkung dazu.

Nachweis, dass x86 unverändert bleibt: Nach den Scheduler-/`resign`-Änderungen ist der `.text`-Abschnitt weiterhin bit-identisch. Nach der Semaphor-Korrektur unterscheiden sich von 353 Funktionen genau die beiden korrigierten (Vergleich der Disassembly mit normalisierten Adressen), und der x86-Kernel bootet in `qemu-system-i386` mit identischer Ausgabe.

Neu in `arm64/`:
- `arch_thread.c`: Plattformseite der Threads. Jeder Thread hat einen eigenen Kernel-Stack (4 Seiten); `arch_context_switch` ruft `cpu_switch` (Callee-saved-Register, SP, `SP_EL0`). `arch_threads_init` macht den Boot-Thread zum Idle-Thread 1 der Ready-Queue, `arch_thread_create` startet Kernel-Threads (Eltern-ID 1, damit der Scheduler Zombies selbst abräumt), `arch_threads_reap` gibt Stacks beendeter Threads frei. Wie auf x86 läuft Kernelcode mit maskierten Interrupts, dadurch sind die Queues des Buchs ohne weitere Sperren sicher; der Timer wechselt Threads nur, wenn er den Idle-Thread im `wfi` trifft (später auch im User-Modus).
- `thread_test.c`: 3 Threads erhöhen einen Zähler unter einem Mutex und geben die CPU **innerhalb** des kritischen Abschnitts ab (erwartet genau 450, nie mehr als ein Thread im Abschnitt, 1352 Kontextwechsel); Erzeuger/Verbraucher mit zwei Semaphoren über einen 4-Slot-Puffer (60 Elemente, Reihenfolge und Summe korrekt); ein Thread, der auf ein Semaphor wartet und von einem anderen geweckt wird; Zombie-Aufräumen und Frame-Bilanz.
- `tools/port.py` erzeugt zusätzlich `gen/ulix-api.h` mit `extern`-Deklarationen aller ausgewählten Globals, damit die Plattformdateien auf die Buch-Variablen zugreifen können.

Ergebnis in qemu (EL1 und EL2): **91 Prüfungen, alle bestehen**; `make verify` (Dateisystem-Schreibtests auf dem Image, Host-Kontrolle) weiterhin ohne Probleme.

Bekannte Lücken: Der Buffer Cache des Buchs ist noch nicht eingebunden (die Schreib-Durchreichung in `glue.c` bleibt); das Umschalten zwischen **Adressräumen** (TTBR0/ASID in `arch_context_switch`) fehlt, weil alle Test-Threads im Kernel-Adressraum laufen; Signale und der Aufräum-Code für Kernel-Stacks aus dem Scheduler sind auf ARM64 noch nicht vorhanden. Die einfache Prozessverwaltung aus Etappe 5 läuft daneben weiter (der Timer wählt, welcher Scheduler gerade aktiv ist), bis `fork`/`exec` aus dem Buch (Schritt 3) sie ablösen.

## 15. Schritt 3 der Kernel-Portierung: Syscalls, fork, exec, wait auf ARM64

Der Syscall-Dispatcher des Buchs (`syscall_handler`, `install_syscall_handler`) und die Systemaufrufe `exit`, `fork`, `read`, `write`, `open`, `close`, `waitpid`, `execve`, `unlink`, `chdir`, `lseek`, `getpid`, `getppid`, `gettid`, `resign`, `stat`, `getcwd`, `get_errno` laufen jetzt auf ARM64. Die erste, vereinfachte Prozessverwaltung aus Etappe 5 (`sched.c`, handgeschriebene Assembler-Programme, eigene Syscall-Schicht) ist **entfernt**; ihre Tests laufen in neuer Form über den Code des Buchs.

**Syscall-ABI** (spiegelt die x86-Konvention des Buchs): `svc #0`, Syscall-Nummer in `x0` und Ergebnis ebenfalls in `x0`, Argumente in `x1`–`x5`. Die Nummern sind die `__NR_*` des Buchs. Der ARM64-`context_t` im Buch trägt die x86-Registernamen als Alias (`eax`=`x0`, `ebx`=`x1`, …, `eip`=`ELR`, `useresp`=`SP_EL0`), dadurch laufen `r->ebx`, `eax_return(...)` und `r->eax = …` im Buch unverändert. Dafür sichert der Exception-Eintritt jetzt zusätzlich `SP_EL0` im Frame (288 Byte).

Neu in `arm64/arch_thread.c` (plattformabhängiger Ersatz für das, was das Buch für x86 mit 2-stufigem Paging und Kernel-Stacks im Benutzer-Adressraum macht, aber mit denselben Namen und Datenstrukturen):
- Adressräume: `address_spaces[]` des Buchs, dazu die TTBR0-Wurzel je Adressraum; `destroy_address_space` (verlässt den Adressraum zuerst), Wechsel im Kontextwechsel-Hook (TLB-Flush bei jedem Wechsel; ASIDs kämen später).
- `u_fork`: kopiert alle Seiten des Elternprozesses (wie das Buch, ohne Copy-on-Write), kopiert das TCB, öffnet Dateien neu (`u_reopen`), legt für das Kind einen Kernel-Stack mit Kopie des Exception-Frames an (`fork` liefert dort 0), trägt es in die Ready-Queue ein.
- `u_execv`: kopiert die Argumente in den Kernel, lädt das ELF64-Programm in neue Tabellen (der Loader aus Etappe 7), baut `[argc][argv]` auf dem neuen Stack, schaltet um und gibt den alten Adressraum frei; bei Fehlern bleibt der alte erhalten.
- `arch_start_user`: startet den ersten Prozess aus einer Datei (der Kernel spielt den Elternteil; Elternprozess 1 = Idle, damit der Scheduler ihn ohne `waitpid` entsorgt).
- Fehler im Benutzercode (zum Beispiel Schreiben auf die Code-Seite) beenden den Prozess über das `syscall_exit` des Buchs mit Status -1.

Änderungen am Buch (alle x86-neutral): `context_t` für ARM64 (neben dem x86-Typ in `#if`).

User-Seite (`arm64/user/`): `ulib.h` benutzt die ABI des Buchs und hat jetzt `fork`, `waitpid`, `execv`, `getppid`, `lseek`, `yield`; `crt0.S` liest `argc/argv` vom Stack; neues Testprogramm `forktest`.

Tests in qemu (EL1 und EL2, modernes virtio): **89 Prüfungen im Kernel plus 25 innerhalb der Benutzerprogramme, alle bestehen.** Neu: `fork` mit getrennten Adressräumen (Schreiben im Kind ändert das Elternteil nicht), Exit-Status über `waitpid`, `fork`+`exec` von `/hello64` mit Argumenten, drei Kinder laufen abwechselnd, ein abstürzendes Kind wird beendet und der Elternprozess läuft weiter (Status -1), `execv` einer fehlenden Datei liefert -1, Enkelprozess, kein Frame verloren. `make verify` (Host-Prüfung des Images) weiterhin 0 Probleme.

x86-Nachweis: Von 353 Funktionen im x86-Objekt unterscheiden sich weiterhin nur die zwei bewusst korrigierten Semaphor-Funktionen; der x86-Kernel bootet in `qemu-system-i386` mit identischer Ausgabe.

Offen / bekannte Lücken:
- Kein Copy-on-Write und kein `brk`/`sbrk` (Heap) für Benutzerprogramme; `write` meldet wegen eines Fehlers im Buch (`u_write` fällt nach der Ausgabe auf STDOUT/STDERR in `default: return -1`, Fundstelle [Zeile 20713–20731](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L20713-L20731)) immer -1 zurück, die Ausgabe selbst funktioniert.
- Das Buch dereferenziert Benutzerzeiger direkt (wie auf x86). Auf ARM64 funktioniert das, weil TTBR0 aktiv ist, aber ein ungültiger Zeiger in einem Systemaufruf würde den Kernel anhalten.
- Signale, Pipes, pthreads, Terminals und die Kernel-Shell fehlen noch; der Buffer Cache ist weiter nicht eingebunden.

## 16. Schritt 4 der Kernel-Portierung: Signale (und: Pipes gibt es nicht)

**Pipes:** Die Suche im gesamten Repository ergab, dass x86-Ulix **keine Pipes benutzt**: es gibt kein `pipe()` im Kernel (das Buch schreibt selbst „pipes have not been implemented in this version“), keinen Werkzeug-Quelltext mit Pipes und im Shell-Quelltext (`lib-build/tools/sh.c`) kein `|`. Die Shell kann Umleitungen (`<`, `>`, `>>`, `2>`), und zwar über `close(1)` + `open(datei)` (der neue Deskriptor belegt den freien Platz), also ohne `dup2`; von `dup2` existiert nur die Nummer 63, ein Handler ist nicht installiert. Das läuft mit dem bereits portierten Dateisystem- und VFS-Code. Pipes werden deshalb nicht nachgebaut (neue Funktion ohne Nutzer).

**Signale** aus dem Buch (`u_signal`, `u_kill`, `syscall_signal`, `syscall_kill`: Standardaktion, `SIG_IGN`, `SIGSTOP`/`SIGCONT`/`SIGKILL`, Pending-Bits) laufen unverändert. Neu auf ARM64 ist nur die Zustellung an den Benutzercode (im Buch steht sie im Scheduler und manipuliert den x86-Stack):
- `arch_signal_check`: wird am Ende jeder Exception aus dem User-Modus aufgerufen (Systemaufruf und Timer-Interrupt). Es sichert den **gesamten** unterbrochenen Registerrahmen auf dem User-Stack, ruft den Handler wie eine C-Funktion (`x0` = Signalnummer) und setzt das Link-Register auf eine Trampolin-Seite.
- Trampolin-Seite (`0x7FFF0000`, in jedem Adressraum): `movz x0,#600; svc #0`. Der neue Systemaufruf `sigreturn` (Nummer 600, nur ARM64) stellt den gesicherten Rahmen wieder her; Status-Register und Rücksprungadresse werden dabei geprüft (der Prozess bleibt in EL0).
- Beim ersten Eintritt eines neuen Prozesses in den User-Modus wird ebenfalls geprüft: Ein Signal, das vor dem ersten Lauf eintraf, wird dort zugestellt. Den Fehler (Signal blieb für immer ausstehend, weil ein schnelles Kind nie eine Exception hatte) habe ich über einen Test gefunden.
- Ein von einem anderen Prozess mit `SIGKILL` beendeter Prozess läuft nie durch `syscall_exit`; sein Adressraum wird jetzt beim Aufräumen der Kernel-Stacks freigegeben (kein Speicherverlust).

Änderung am Buch: `eax_return` castet auf `memaddress` statt `unsigned int` (x86 identisch, auf ARM64 bleiben Zeiger und negative Werte erhalten; hatte zwei echte 64-Bit-Warnungen in `syscall_signal` gezeigt).

Test (`user/sigtest.c`, alle 8 Prüfungen laufen im Benutzerprogramm): Ein Handler unterbricht eine Rechnung mit vielen Registern 8 Millionen Schleifendurchläufe lang, das Ergebnis ist trotzdem exakt, und der Handler lief mehrfach (vor dem ersten Befehl und mitten in der Rechnung); `SIG_IGN` stört nicht; ohne Handler tötet `SIGTERM` den Prozess (Meldung des Buchs); `SIGSTOP` hält einen Prozess an (kein Fortschritt in einer Logdatei), `SIGCONT` setzt fort, `SIGKILL` beendet sofort. Ergebnis in qemu (EL1 und EL2, modernes virtio): **93 Prüfungen im Kernel, alle bestehen**; `make verify` ohne Probleme. x86: Von 353 Funktionen unterscheiden sich weiterhin nur die zwei korrigierten Semaphor-Funktionen, der x86-Kernel bootet mit identischer Ausgabe.

Bekannte Eigenheiten des Buchs, die ich nicht geändert habe (`u_kill`: [Zeile 27781](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L27781)): `kill` auf einen blockierten Elternprozess lässt dessen später endende Kinder als Zombies zurück (`u_kill` setzt Kinder nicht um); `SIGKILL` setzt `used = false` sofort, ein folgendes `waitpid` meldet -1.

## 17. Schritt 5 der Kernel-Portierung: Konsole über die UART und Kernel-Shell

Die Konsole läuft jetzt über die PL011-UART. Alles hinter dem Eingabepuffer ist Code des Buchs: `syscall_readchar` (blockiert den Prozess in `keyboard_queue`), `kgetch`, `kreadline`, `kernel_shell`, `ksh_run_command` und die Shell-Befehle `uname`, `ps`, `queues`, `locks`, `lsof`, `inode`, `time`, `listas`, `longhelp`, `help`, `enable`/`disable`.

Neu in `arm64/`:
- `arch_console.c`: Der UART-Empfangsinterrupt (GIC-Nummer 33) ersetzt den Tastaturinterrupt des x86-Kernels. Er wandelt Zeichen um (Enter → `\n`, DEL → `\b`), schreibt sie in den Eingabepuffer des Terminals (`terminals[cur_vt]`) und weckt wartende Prozesse, wie `keyboard_handler` im Buch. **Strg-C** beendet den Vordergrundprozess (dieselbe Suche wie im Buch, `u_kill(..., SIGKILL)`), **Strg-]** hält den Scheduler an und startet die Kernel-Shell (x86: Shift-Esc). Die Initialisierung der Puffer kommt direkt aus dem Chunk `<<setup keyboard>>` des Buchs (`ulix-arm64.nw` hat dafür eine eigene Wurzel `ulix-init.c`).
- `glue.c`: Ersatz für Befehle, die x86-Hardware betreffen (`mem`, `test`, `div0`, `hexdump` auf einer Kernel-Adresse, `gf`/`gp` über den ARM64-Frame-Allocator), `vt_clrscr` als ANSI-Sequenz; `arch_readchar` ruft jetzt `syscall_readchar` auf (blockierend), statt die UART abzufragen.
- `kmain.c`: Ein einheitlicher Formatierer für `printf` und `sprintf` (zusätzlich `-` zum Linksbündigen, Breite für Strings, `%p`); zweites Kernel-Image `ulix-console.elf` (`-DCONSOLE_MODE`): startet `/contest64` und wartet auf Eingabe.
- `irq.c`: Interrupts werden jetzt **vor** der Bearbeitung quittiert, damit die Kernel-Shell innerhalb des UART-Interrupts weitere Zeichen empfangen kann.
- `tools/console_test.py` + `make console-test`: spielt Terminal. Es startet qemu mit der UART an einem TCP-Port, tippt Zeichen einzeln und prüft die Antworten: Zeile eingeben und per Echo zurückbekommen, Backspace, Strg-C (`Killed`), dann Strg-], Kernel-Shell mit allen Befehlen oben, ein unbekannter Befehl, `exit` und kein Kernel-Fehler. 21 Prüfungen bestehen.

Änderungen am Buch (alle für x86 unverändert im Verhalten):
- `<<enable interrupts>>` und `<<disable interrupts>>` sind prozessorabhängig (`sti`/`cli` auf x86, `msr daifclr/daifset` auf ARM64); `hexdump` und zwei Aufrufe nehmen `memaddress` statt `uint` (Adressen bleiben auf 64 Bit vollständig).
- **Drei Fehler im Buch korrigiert** (gefunden durch Tests, jeweils mit Nachweis):
  1. `u_open` und `u_chdir` rufen `u_stat` auf und ignorieren das Ergebnis; existiert die Datei noch nicht (zum Beispiel bei `O_CREAT`), blieb `struct stat` uninitialisiert, und der Stackinhalt entschied, ob die Datei wie ein Symlink behandelt wird. Ergebnis war zum Beispiel, dass `open("/progress.txt", O_CREAT)` das Wurzelverzeichnis öffnete. Jetzt `struct stat st = { 0 }`. Fundstellen im Original: `u_open` [Zeile 20519–20521](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L20519-L20521), `u_chdir` [Zeile 21444–21445](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L21444-L21445). (Das Symptom tauchte erst auf, nachdem sich durch eine zusätzliche Programmdatei der Stackinhalt geändert hatte: Fehler, der auf x86 genauso lauern kann.)
  2. `mx_open` hielt einen **geschlossenen** Eintrag der internen Inode-Tabelle für „schon offen“, wenn dessen Inode-Nummer übereinstimmte (`mx_close` löscht `ino`/`device` nicht). Bekam eine neue Datei die Nummer einer gelöschten, erbte sie deren alte Größe und Zonen. Jetzt zählt nur ein Eintrag mit `refcount > 0`. Fundstelle im Original: [Zeile 22860–22861](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L22860-L22861). Test in `bookfs_test.c`: ohne die Korrektur schlägt „... starts with size 0“ fehl, mit ihr besteht er.
  3. (aus Schritt 2) Semaphore arbeiteten auf einer Kopie.

Ergebnis in qemu (EL1 und EL2, Legacy- und modernes virtio): **95 Prüfungen im Kernel, alle bestehen**; `make verify` 0 Probleme; `make console-test` 21 Prüfungen. x86: Von 353 Funktionen im Objekt unterscheiden sich genau die bewusst geänderten (`hexdump`, `mx_open`, `u_open`, `u_chdir` und die zwei Semaphor-Funktionen); der x86-Kernel bootet mit identischer Ausgabe.

Offen: kein Erkennen von Escape-Sequenzen (Pfeiltasten), keine mehreren virtuellen Terminals (nur Terminal 0; der Rest der VT-Logik des Buchs entfällt), die Kernel-Shell zeigt keine Statuszeile; der Buffer Cache fehlt weiterhin.

## 18. Schritt 6 der Kernel-Portierung: Das Userland des Buchs läuft (init, login, Shell, 60 Programme)

Das System bootet jetzt wie das x86-Original: `/init` → `/bin/login` (automatische Anmeldung auf Terminal 1 und 2, Login mit Passwort auf den übrigen) → `/bin/sh`, und die Programme aus `lib-build/tools` laufen als AArch64-Binärdateien.

**Aufbau (`arm64/userland/`):** `ulixlib` und `init` werden bei jedem Build aus dem Buch erzeugt, die 59 Programme aus `lib-build/tools/*.c` unverändert mit `aarch64-elf-gcc` übersetzt (`recurse.c` enthält x86-Assembler und entfällt; bei `swapper` ersetzt ein kleiner Platzhalter den Quelltext, es gibt keinen Swap), zusammen mit `init` 60 Programme. Das Linkerskript und der Startcode sind die aus Etappe 7. `make` baut daraus das Image `disk.img`: die ARM64-Programme ersetzen die x86-Programme in `/bin` und `/init` (Blöcke der alten Dateien werden freigegeben, `tools/minix_add.py` kann jetzt Verzeichnisse und Ersetzen). Zweites Kernel-Image `ulix-shell.elf` startet `/init`.

**Änderungen am Buch** (x86-Bibliothek `.text` bit-identisch, Kernel nur an den Stellen, die Fehler korrigieren):
- Die vier Systemaufruf-Funktionen der Benutzerbibliothek (`syscall1`..`syscall4`) haben einen ARM64-Zweig (`svc #0`, Nummer in `x0`, Argumente `x1`..`x5`).
- `printf`/`sprintf` der Bibliothek lasen ihre Argumente direkt vom Stack (x86-Aufrufkonvention; Fundstelle im Original: [Zeile 29393–29399](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L29393-L29399)); über Makros (`PRINT_NEXT_INT` usw.) benutzt ARM64 jetzt `va_arg`, x86 behält dieselben Ausdrücke.
- **Weitere Fehler im Buch korrigiert** (jeweils beim Testen aufgefallen):
  1. `syscall_waitpid` schrieb durch den Status-Zeiger, auch wenn dieser 0 war (`login` ruft `waitpid (pid, 0, 0)`; Fundstelle im Original: [Zeile 11713](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L11713) und [11729](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L11729)). Auf x86 überschreibt das stillschweigend die ersten Bytes des Programms bei Adresse 0, auf ARM64 stürzte der Kernel ab.
  2. `mx_rmdir` wechselte in das **übergeordnete** Verzeichnis und löschte dort `.` und `..`, statt im zu löschenden Verzeichnis (Fundstelle im Original: [Zeile 24016–24022](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L24016-L24022)). Danach war das Elternverzeichnis leer. Neuer Test in `bookfs_test.c`.

**Anpassungen an der einen Konsole:**
- Zehn virtuelle Terminals des Buchs → eine UART: Ausgabe von Prozessen auf einem nicht sichtbaren Terminal wird je Terminal (4 KByte) gepuffert und beim Umschalten ausgegeben. **Strg-T und eine Ziffer** schaltet um (x86: Alt-1..0); nur Terminal 1 bekommt Eingabe-Interrupts zugeordnet, die Eingabepuffer sind je Terminal.
- Die Mount-Tabelle des Buchs mountet drei Platten (`hda`, `hdb`, `fd1`); hier gibt es eine, `/tmp` und `/mnt` sind normale Verzeichnisse (`/tmp` mit Rechten 1777, auf x86 ist es eine eigene Platte).
- Zusätzliche Systemaufrufe: `set_errno`, `getpsinfo`, `setpsname`, `isatty`, `mkdir`, `rmdir`, `readdir`, `truncate`, `ftruncate`, `link`, `symlink`, `readlink`, `diskfree`, `sync`, `setuid/setgid/seteuid/setegid`, `login`, `query_ids`, `chown`, `chmod`, `brk` (neue Heap-Verwaltung für 4-Level-Tabellen), `setterm`, `idle` (`wfi`), außerdem Platzhalter für den VGA-Bildschirm (`clrscr` per ANSI).
- argv-Strings stehen jetzt 512 Byte unter dem Stack-Ende (wie auf x86), weil `ls` Zeichen an sein Argument anhängt.

**Tests** (`make shell-test`, `make console-test`, `make verify`, Kerneltests): Der Shell-Test fährt eine vollständige Sitzung über die UART: Boot, Autologin, `ls`, `cat`, `cd`, `touch`, `cp`, `wc`, `grep`, `stat`, `mkdir`, `rm`, `rmdir`, Umleitung mit `>`, `ps`, `df`, `free`, `echo` mit Strg-C, Terminalwechsel auf Terminal 2 (Root-Shell, `ps`) und zurück, `exit`, erneutes Login mit Passwort. 61 Prüfungen im Shell-Test, 100 Kerneltests (EL1 und EL2), 21 im Konsolentest, `make verify` 0 Probleme. Die Erwartungswerte für Dateigrößen und Prüfsummen kommen beim Bauen vom unabhängigen Python-Parser (`gen/disk_expected.h`).

x86: Von 353 Funktionen im Kernel-Objekt unterscheiden sich nur `hexdump`, `mx_open`, `mx_rmdir`, `syscall_waitpid`, `u_chdir`, `u_open` und die zwei Semaphor-Funktionen (alles Korrekturen); der x86-Kernel bootet mit identischer Ausgabe, der `.text`-Abschnitt der x86-Bibliothek ist unverändert.

**Offen:** keine Pthreads (`thread`, `mutex`-Werkzeuge laufen nicht), kein Swap, keine VGA-Funktionen (`vi`, Cursor-Positionierung), `recurse` (x86-Assembler); das `ls`-Werkzeug zeigt keine Zeiten, da die Uhr nicht aus dem PL031 gelesen wird (Zeitstempel 0).


## 19. Schritt 7: Uhr, Copy-on-Write, ein Befehl für alle Tests, Dokumentation

- **Uhr** (`arch_clock.c`): `system_start_time` kommt aus der PL031-RTC des Boards, `system_ticks`/`system_time` werden wie im
  Timer-Handler des Buchs fortgeschrieben (100 Hz). Dateien bekommen jetzt echte Zeitstempel (`ls -l`, `stat`).
- **Copy-on-Write für `fork`** (`mmu.c`, `arch_thread.c`, `irq.c`): Beschreibbare Seiten werden in Eltern- und Kindprozess
  schreibgeschützt und mit einem Software-Bit (Bit 55) markiert, jeder Frame hat einen Referenzzähler. Der erste Schreibzugriff
  löst einen Permission Fault aus; `cow_fault` kopiert die Seite (oder macht sie wieder beschreibbar, wenn der Frame nur noch einen
  Nutzer hat). Der Fall „der Kernel schreibt im Systemaufruf in einen Benutzerpuffer“ (`read`, `waitpid`) läuft über dieselbe Fehlerbehandlung
  im EL1, der Befehl wird wiederholt; `user_to_phys` löst COW ebenfalls auf. Code-Seiten (schreibgeschützt) werden dauerhaft geteilt.
  Test `user/cowtest.c` zählt freie Frames: `fork` kopiert keine Datenseiten, Lesen kopiert nichts, Schreiben kopiert genau die 64
  betroffenen Seiten, der Elternprozess behält seine Daten, nach dem Ende des Kindes sind alle Frames zurück (10 Prüfungen).
- **`make check`** führt alles aus: Kerneltests (EL1 mit Legacy-virtio, EL2 mit modernem virtio, je 104 Prüfungen), `make verify`
  (Schreibtests plus unabhängige Prüfung des Images), `make console-test` (21), `make shell-test` (63). Dauer etwa 2 Minuten. (Stand dieser Etappe; heute: 129 Kerneltests, 34 und 93 Prüfungen in Konsolen- und Shelltest, `make pcheck` mit 21 parallelen Tests in etwa 3 Minuten.)
  `make run-shell` startet das System zum Selbstbedienen.
- **Dokumentation:** `arm64/README.md` (Englisch, im Stil des Repositorys), Abschnitt im Haupt-`README.md`, dieser Bericht.

**Bewusst nicht gemacht (Stand dieser Etappe; Buffer Cache, Pthreads, Swap und das Kapitel im Buch sind inzwischen erledigt, siehe `arm64/TODO.md`):**
- **Buffer Cache des Buchs:** Er verzögert Schreibzugriffe (ein Flush-Thread schreibt später). Die Tests, die das Image von außen prüfen,
  setzen voraus, dass geschrieben ist; der Gewinn (weniger Plattenzugriffe) ist unter qemu gering. `writeblock` schreibt durch.
- **Pthreads** (`u_pthread_create` baut x86-Stacks): bräuchte einen ARM64-Entwurf für mehrere Threads je Adressraum
  (gemeinsame TTBR0-Tabelle, getrennte Kernel- und User-Stacks, Referenzzähler am Adressraum).
- **Swap** (`page_out`, Swapper-Prozess): setzt die x86-Frametabelle und ihre Seitenersetzung voraus; mit COW und 128 MByte RAM ohne
  praktischen Nutzen.
- **Rückführung als Kapitel ins Buch:** `ulix-arm64.nw` ist bereits eine kommentierte noweb-Datei, aber ein eigenes Kapitel in
  `ulix-book.nw` wäre ein größerer Eingriff in den TeX-Text, den ich ohne LaTeX-Installation nicht prüfen konnte
  (`make pdf` ist hier nicht möglich).
- **Echte Hardware:** GICv3, Device-Tree, Cache-Wartung für DMA, andere Boards. (Inzwischen geschrieben und in qemu getestet, auf echter Hardware nicht ausprobiert.)

**Zählung des Umfangs** (ursprünglich nach Schritt 7, aktualisiert am 2026-11-04, ohne den Commit der seriellen Platte): 31 Commits auf dem Branch `arm64-port` seit dem Upstream-Stand `32535a1`; im Buch 361 Zeilen hinzugefügt und 51 entfernt (der Anhang über den Port eingeschlossen, sonst x86-neutral oder Fehlerkorrekturen); im Verzeichnis `arm64/` rund 4 700 Zeilen C, Assembler und Header (ohne die aus dem Buch erzeugten Teile), dazu rund 1 000 Zeilen Python und Shell in `arm64/tools/` und 700 Zeilen Testprogramme in `arm64/user/`.

## 20. Raspberry Pi 4B, Teil 1: alles, was sich in qemu testen lässt (Branch `rpi4`, 2026-11-04)

Grundsätze und Plan stehen in `arm64/PRINCIPLES.md` (Lernsystem: einfach, lesbar, kleine Schritte; BSD-Code nur als Referenz für Fakten). Die Reihenfolge: erst läuft Ulix auf dem Board, dann Netzwerk, dann Telnet. Die Hardware ist bestellt; bis sie da ist, ist alles gemacht, was `qemu -M raspi4b` hergibt. Die Liste für den Tag, an dem das Board ankommt, steht in `arm64/TODO.md`.

- **Ein Quellbaum, zwei Images.** `make kernel8.img` baut das Image für das Board aus denselben Objekten wie für qemu `virt`; nur der Startcode (`start-rpi4.o`: in welchem GByte sitzt die Konsolen-UART) und die Ladeadresse (`0x80000`, per `--defsym=KERNEL_PA` beim Linken) sind anders. `start.S` nimmt als RAM-Block das GByte, in dem der Code läuft (vorher: fest bei `0x40000000`); alles andere RAM und alle Geräte bildet `platform_map` aus dem Device Tree ab.
- **Device Tree mit `ranges`** (`dt.c`, `address ()`): Die Firmware beschreibt die Geräte mit Bus-Adressen (UART `0x7e201000`, GIC `0x40041000`); die `ranges` des Busses `soc` rechnen sie in die Adressen um, die die Kerne sehen (`0xfe201000`, `0xff841000`). `arm64/rpi4-qemu.dts` ist wie die echte `bcm2711.dtsi` aufgebaut (Wurzel mit 2 Adress- und 1 Größenzelle, `soc` mit drei `ranges`), weil qemu keine Firmware hat, die einen Baum liefert.
- **Uhr:** Der Pi hat keine RTC; ohne PL031 startet die Zeit am 1. Januar 2026 (`arch_clock.c`).
- **SD-Karte** (`sdcard.c`): ein Treiber für den SDHCI-Controller im einfachsten Betrieb (Polling, PIO, ein Sektor pro Befehl: Reset, Spannung, Takt aus den Capabilities, CMD0/8/41/2/3/7/16/17/24, SDSC und SDHC). Der MBR der Karte sagt, wo die Disks liegen: Partitionen vom Typ `0x81` sind `hda`, `hdb` und der `fd1`-Ersatz, Partition 1 (FAT) bleibt unberührt. Die Blockschicht (`virtio.c`) nimmt sie, wenn kein virtio-Gerät da ist; alles darüber (Buffer Cache, Minix-Code, Swap) blieb unverändert. `tools/make_sdcard.py` baut das Karten-Image (`make sd.img`).
- **Ein Fehler, den der Test gefunden hat:** Der erste Zugriff auf das Reset-Register (8 Bit, Offset `0x2f`) war ein 32-Bit-Lesezugriff und löste einen Alignment-Fault aus: Register müssen mit ihrer eigenen Breite gelesen werden (`wait_for` bekommt die Breite).
- **qemu legt die Karte an den Controller bei `0x7e300000`**, auf dem echten Board sitzt der Slot am EMMC2 (`0x7e340000`). Das steht im Baum, `dt.c` nimmt, was dort steht.
- **Tests** (`make pcheck`, jetzt 24 Jobs, etwa 4 Minuten): die Kerneltests (133 Prüfungen), der Konsolentest und die ganze Shell-Sitzung laufen auf der emulierten Platine. Die Prüfung „Plattenzugriffe laufen per Interrupt“ entfällt dort (`blk_irq` ist 0: die Karte wird gepollt, ein Prozess schläft nicht dafür).
- **Nicht getestet:** alles auf echter Hardware. Offen und in qemu nicht prüfbar: Interrupt, DMA, Mehrblock-Befehle, 4-Bit-Bus, 1,8-V-Umschaltung, Takt und Strom von EMMC2 über den Firmware-Mailbox, die Baudrate der PL011 (qemu ignoriert sie), die anderen drei CPUs (Spin-Table), die 4- und 8-GByte-Modelle (RAM und Peripherie im selben GByte). Das Netzwerk (virtio-net in qemu `virt`, dann GENET auf dem Board) und Telnet kommen danach.

## 21. Raspberry Pi 4B, Teil 2: SD-Karte schneller, Netzwerk bis TCP, Vorbereitung für das Board (2026-11-05)

Bis das Board da ist, ist alles gebaut, was sich ohne Hardware testen lässt.

- **SD-Karte** (`sdcard.c`): Der Treiber arbeitet jetzt mit Mehrblock-Befehlen (CMD18/25, der Controller sendet CMD12 selbst), dem 4-Bit-Bus (ACMD6) und Interrupts. Ein Transfer ist eine kleine Zustandsmaschine (`service ()`): „Puffer bereit“ → die CPU kopiert 128 Wörter durch das Datenregister, „Transfer fertig“ → Ende. Wer `service ()` aufruft, entscheidet die Betriebsart: der Interrupt-Handler, während ein Prozess schläft (`blk_irq_wait`), oder der Wartende selbst in einer Schleife (Idle-Thread, Boot, Tests). Gemessen auf der emulierten Platine: der Kerneltest läuft mit Interrupts in 287 s, mit Polling dauert er über 500 s, weil dann kein anderer Thread die Zeit nutzt. Die Zahlen sind von qemu, wo jeder Zugriff auf ein Geräteregister langsam ist.
- **Boot-Vorbereitung:** `uart_init ()` stellt die Baudrate der PL011 ein (115200 bei 48 MHz: IBRD 26, FBRD 3), falls die Firmware es nicht getan hat; `maxcpus=N` auf der Kommandozeile (`/chosen/bootargs`, auf dem Pi aus `cmdline.txt`) begrenzt die CPUs, weil die anderen drei Kerne dort in einer Spin-Table der Firmware warten und kein PSCI haben; `make sd-pi.img` baut die Karte mit FAT32-Bootpartition (mtools), `config.txt`, `cmdline.txt` und dem Kernel. Die Firmware-Dateien selbst fehlen noch (Download nach Rückfrage); getestet mit Platzhalterdateien.
- **Netzwerk** (alles im Quelltext ausführlich kommentiert, wie ein Kapitel zum Lesen): `virtionet.c` (die Netzwerkkarte von qemu `virt`: zwei Warteschlangen, Polling), `net.c` (Ethernet, ARP, IPv4 ohne Fragmente, ICMP-Echo, UDP), `tcp.c` (Verbindungsaufbau, Daten in beide Richtungen, Abbau; Neusenden mit Zeitgeber und wachsender Wartezeit, das Fenster des Empfängers wird beachtet; keine Überlastkontrolle, Segmente außer der Reihe werden verworfen).
- **Test** (`make net-test`, Job `net` in `make pcheck`): qemus User-Netzwerk mit Portweiterleitung. Der Kernel ist 10.0.2.15, Router und Host sind 10.0.2.2; gewöhnliche Python-Sockets des Hosts sprechen so mit dem Netzwerk von Ulix und prüfen es gegen eine echte TCP/IP-Implementierung: Ping des Routers (ARP, ICMP), UDP-Echo, TCP-Echo mit 100 und mit 20 000 Byte (mehr als die Puffer und ein Segment), und der Kernel als TCP-Client, bei dem das erste SYN und das erste Datensegment absichtlich verloren gehen (`net_drop_tx`): Beide werden nach dem Zeitablauf neu gesendet. Alles besteht beim ersten Lauf.
- **`ping` als Programm in der Ulix-Shell** (`userland/ping.c`): Das Programm baut die ICMP-Nachrichten selbst (Typ, Kennung, Folgenummer, Prüfsumme) und misst die Laufzeit; der Kernel bietet nur rohe ICMP-Sockets (`icmp_open/send/get` in `net.c`) über fünf neue Systemaufrufe des Ports (`net_syscalls.c`, Nummern 602 bis 606: `icmp_open`, `icmp_send`, `icmp_recv`, `icmp_close`, `clock_us`). `icmp_recv` wartet mit Zeitgrenze und gibt dabei die CPU ab (`arch_resign`); weil es noch keinen Thread gibt, der die Karte bedient, schaut das wartende Programm selbst nach. Beispiel in `make run-shell`: `ping -c 3 10.0.2.2` → drei Antworten, Laufzeit etwa 10 ms (der Scheduler gibt dem Programm die CPU im 10-ms-Takt zurück). Der Shell-Test hat eine Variante mit Netzwerkkarte (`shellnet`).
- **DHCP und Konfiguration nach BSD-Vorbild:** `dhcp.c` ist ein DHCP-Client (DISCOVER, OFFER, REQUEST, ACK; Adresse, Netzmaske, Router, Namensserver, Gültigkeitsdauer; ohne Verlängerung). `netconf.c` liest die Kernel-Kommandozeile (`ip=dhcp`, `ip=10.0.0.5/24`, `gw=`, `dns=`; auf dem Pi in `cmdline.txt`, jetzt `maxcpus=1 ip=dhcp`). Die Programme `ifconfig`, `route` und `dhclient` in `/bin` heißen und arbeiten wie die der BSDs (Schnittstelle `vioif0` wie bei NetBSD; Ausgabe mit `flags`, `ether`, `inet … netmask 0x… broadcast …`; `route show`/`add default`/`delete default`). Ändern darf nur root (`su`): die Systemaufrufe `net_set` und `net_dhcp` (607 bis 609) prüfen `euid == 0`; Ansehen darf jeder. Tests: die DHCP-Prüfungen in `net_test.c` (erst eine falsche Adresse setzen, DHCP muss 10.0.2.15, Router 10.0.2.2 und Namensserver 10.0.2.3 zurückbringen), die Root-Befehle im Shell-Test (`shellnet`) und der Job `netconf` (Kommandozeile mit fester Adresse und mit `ip=dhcp`).
- **Treiber für die Ethernet-Karte des Pi 4 (GENET), ungetestet:** `genet.c` ist neu für Ulix geschrieben (Polling, je ein Ring zum Senden und Empfangen, kein Adressfilter), folgt aber in Registern und Reihenfolge dem FreeBSD-Treiber `if_genet.c` (BSD-2-Clause; Lizenz und Copyrights stehen im Kopf der Datei). qemu `raspi4b` hat kein Modell des Controllers; der Treiber ist übersetzt, aber nie gelaufen. `netdev.c` wählt die Karte (`genet0` auf dem Pi, `vioif0` in qemu); `virtionet.c` und `genet.c` haben dieselbe Schnittstelle (`struct net_driver`), alles darüber bleibt unverändert (`net-test` besteht).
- **Netzwerk-Thread mit Interrupt** (`netd.c`): Ein Kernel-Thread schläft in einer Warteschlange (der des Disketten-Laufwerks, das es hier nicht gibt) und wird vom Interrupt der Netzwerkkarte geweckt (`start_irq` und `ack` in `struct net_driver`); für die Zeitgeber von TCP und für Karten ohne Interrupt weckt ihn zusätzlich der Timer alle 50 ms. Er bearbeitet jedes Frame sofort: ARP, Ping von außen, TCP. Mit `netecho` auf der Kommandozeile bedient er den Echo-Dienst (`echo.c`, Port 7). Test `make netd-test` (Job `netd`): Der Konsolen-Kernel hat keinen Testcode, nur den Thread; Python-Sockets des Hosts bekommen UDP- und TCP-Echo (bis 20 000 Byte) zurück, während der Kernel sein Programm ausführt, und der Test prüft, dass der Interrupt (nicht das Abfragen) den Thread weckt. Die Verdrahtung für GENET (Empfangs- und Sende-Interrupt freigeben, löschen) ist da, aber wie der ganze Treiber ungetestet.
- **`shutdown`, `poweroff`, `reboot`** (ein Programm mit drei Namen, `userland/shutdown.c`): Die Systemaufrufe 610 und 611 schreiben zuerst den Buffer Cache auf die Platten (`buffer_sync`) und bitten dann die Firmware: PSCI `SYSTEM_OFF` oder `SYSTEM_RESET`. Unter qemu beendet das qemu oder startet den Kernel neu (der Weg aus `make run-shell`, wenn Strg-A X im Terminal nicht geht). Auf dem Pi gibt es kein PSCI: `poweroff` hält die CPU an, `reboot` meldet einen Fehler; der Weg dort ist der Watchdog des Boards (siehe TODO). Test: Job `poweroff`.
- **Offen:** UDP- und TCP-Sockets für Benutzerprogramme, Verlängern der DHCP-Gültigkeit, der GENET-Treiber für den Pi (nur auf dem Board), der Telnet-Server. Siehe `arm64/TODO.md`.
