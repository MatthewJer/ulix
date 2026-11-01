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
