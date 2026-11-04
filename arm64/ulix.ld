/* Ulix ARM64: kernel runs in the upper half (TTBR1), linear map of physical memory:
   virtual address = 0xFFFF000000000000 + physical address  (cf. 0xC0000000 / 0xD0000000 on x86) */
KERNEL_VA = 0xFFFF000000000000;
/* Physical load address: qemu "virt" puts RAM at 0x40000000 and loads the image 512 KByte above its start. The Raspberry Pi
   firmware loads kernel8.img at 0x80000 (RAM starts at 0); the Makefile passes --defsym=KERNEL_PA=0x80000 for that image. */
PROVIDE(KERNEL_PA = 0x40080000);

ENTRY(_start_phys)
_start_phys = _start - KERNEL_VA;      /* qemu enters at the physical address, MMU off */

SECTIONS
{
  . = KERNEL_VA + KERNEL_PA;
  .text   : AT(ADDR(.text) - KERNEL_VA)   { *(.text.boot) *(.text*) }
  .rodata : AT(ADDR(.rodata) - KERNEL_VA) { *(.rodata*) *(.user_prog) }
  .data   : AT(ADDR(.data) - KERNEL_VA)   { *(.data*) }
  . = ALIGN(4096);
  .pgtbl  : AT(ADDR(.pgtbl) - KERNEL_VA)  { *(.pgtbl) }
  . = ALIGN(8);
  __bss_start = .;
  .bss    : AT(ADDR(.bss) - KERNEL_VA)    { *(.bss*) *(COMMON) }
  . = ALIGN(4096);
  __bss_end = .;
  __kernel_end = .;
  __image_size = __kernel_end - (KERNEL_VA + KERNEL_PA);
}
