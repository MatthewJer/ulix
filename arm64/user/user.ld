/* Ulix ARM64 user programs: static AArch64 ELF, one PT_LOAD per permission class,
   every segment starts on its own 4 KB page so the kernel can map them with W^X. */
ENTRY(_start)
PHDRS {
  text   PT_LOAD FLAGS(5);   /* R+X */
  rodata PT_LOAD FLAGS(4);   /* R   */
  data   PT_LOAD FLAGS(6);   /* R+W */
}
SECTIONS
{
  . = 0x400000;
  .text   : { *(.text.start) *(.text*) } :text
  . = ALIGN(4096);
  .rodata : { *(.rodata*) } :rodata
  . = ALIGN(4096);
  .data   : { *(.data*) } :data
  .bss    : { *(.bss*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.attributes) }
}
