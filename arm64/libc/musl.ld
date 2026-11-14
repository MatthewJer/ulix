/* Ulix ARM64, programs linked with musl: as user/user.ld (static AArch64 ELF, one PT_LOAD per permission class, every segment on
   its own 4 KB page, so that the kernel can map it with W^X) plus the sections musl's start-up code walks. */
ENTRY(_start)
PHDRS {
  text   PT_LOAD FILEHDR PHDRS FLAGS(5);   /* R+X, with the ELF and program headers: musl's start-up code reads them (AT_PHDR) */
  rodata PT_LOAD FLAGS(4);   /* R   */
  data   PT_LOAD FLAGS(6);   /* R+W */
  tls    PT_TLS;             /* the thread-local variables (__thread, _Thread_local): musl's start-up code copies .tdata for each thread */
}
SECTIONS
{
  . = 0x400000 + SIZEOF_HEADERS;
  .text   : { *(.text.start) *(.text*) KEEP(*(.init)) KEEP(*(.fini)) } :text      /* (_init and _fini: crti.o starts them, crtn.o ends them; --gc-sections must not drop crtn's half) */
  . = ALIGN(4096);
  .rodata : { *(.rodata*) *(.gcc_except_table*) } :rodata
  . = ALIGN(4096);
  .data   : {
    *(.data*) *(.got*)
    . = ALIGN(8);
    PROVIDE_HIDDEN (__preinit_array_start = .); KEEP (*(.preinit_array)) PROVIDE_HIDDEN (__preinit_array_end = .);
    PROVIDE_HIDDEN (__init_array_start = .);    KEEP (*(SORT(.init_array.*))) KEEP (*(.init_array)) PROVIDE_HIDDEN (__init_array_end = .);
    PROVIDE_HIDDEN (__fini_array_start = .);    KEEP (*(SORT(.fini_array.*))) KEEP (*(.fini_array)) PROVIDE_HIDDEN (__fini_array_end = .);
  } :data
  .tdata  : { *(.tdata .tdata.* .gnu.linkonce.td.*) } :data :tls
  .tbss   : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) } :data :tls
  .bss    : { *(.bss*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.attributes) }
}
