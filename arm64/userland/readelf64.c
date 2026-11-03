#include "../ulixlib.h"
// AArch64 version of the book's readelf (lib-build/tools/readelf.c): the programs of the ARM64 port are ELF64 files,
// whose headers have other field sizes and another order than the ELF32 headers the book's program reads.
// Same output as the book's program. (Addresses are printed as 32 bit numbers: user space is below 2 GByte.)

typedef struct {
  unsigned char   e_ident[16];
  unsigned short  e_type, e_machine;
  unsigned int    e_version;
  unsigned long   e_entry, e_phoff, e_shoff;
  unsigned int    e_flags;
  unsigned short  e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  unsigned int    p_type, p_flags;
  unsigned long   p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

int main (int argc, char* argv[]) {
    if (argc != 2) {
      printf ("elf: requires one argument\n");
      return;
    }

    Elf64_Ehdr elf_header;
    Elf64_Phdr program_header;

    int fd = open(argv[1], 0);
    printf ("fd = open() = %d\n", fd);
    if (fd == -1)
      printf ("elf: %s: no such file\n", argv[1]);
    else {
      read (fd, &elf_header, sizeof(elf_header));
      printf ("version:   %d\n", elf_header.e_version);
      printf ("e_entry:   %x\n", (unsigned int)elf_header.e_entry);
      printf ("#pheaders: %d\n", elf_header.e_phnum);
      printf ("e_phoff  : %d\n", (int)elf_header.e_phoff);

      int phoffset = (int)elf_header.e_phoff;

      printf ("program header:\n");
      int i;
      for (i=0; i < elf_header.e_phnum; i++) {
        lseek (fd, phoffset + i * elf_header.e_phentsize, SEEK_SET);
        read (fd, &program_header, sizeof(program_header));
        if (program_header.p_type == 1) { // type = PT_LOAD
          printf ("p_type   : %d\n", program_header.p_type);
          printf ("p_offset : %d\n", (int)program_header.p_offset);
          printf ("p_vaddr  : 0x%x\n", (unsigned int)program_header.p_vaddr);
          printf ("p_filesz : 0x%x\n", (unsigned int)program_header.p_filesz);
          printf ("p_memsz  : 0x%x\n", (unsigned int)program_header.p_memsz);
          printf ("p_flags  : %d\n", program_header.p_flags);
        }
      }

      close (fd);
      return;
    }
}
