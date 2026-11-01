// Ulix ARM64 -- ELF64 (AArch64) loader: maps the PT_LOAD segments of a program from the
// Minix disk into a fresh user address space (replaces the x86 ELF32 loader).
// Permissions follow the segment flags: R+X -> code, R -> read-only data, R+W -> data/bss.
// A segment that is both writable and executable is refused (W^X).

#include "ulix.h"

struct ehdr {
  u8 ident[16];
  unsigned short type, machine;
  u32 version;
  u64 entry, phoff, shoff;
  u32 flags;
  unsigned short ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct phdr {
  u32 type, flags;
  u64 offset, vaddr, paddr, filesz, memsz, align;
};

#define PT_LOAD   1
#define PF_X      1
#define PF_W      2
#define EM_AARCH64 183
#define USER_LOAD_MAX (USER_STACK_TOP - 0x10000)       // keep 64 KByte of room for the stack

static int fail (const char *why) { printf ("exec: %s\n", why); return -1; }

// returns 0 and the entry point on success; on failure the caller frees the address space
int elf_load (u64 l0, u32 ino, u64 *entry, u64 *image_end) {
  static struct ehdr eh __attribute__ ((aligned (16)));
  static struct phdr ph __attribute__ ((aligned (16)));
  u32 fsize = minix_size (ino);

  if (minix_read (ino, 0, &eh, sizeof eh) != sizeof eh)  return fail ("file too short");
  if (eh.ident[0] != 0x7f || eh.ident[1] != 'E' || eh.ident[2] != 'L' || eh.ident[3] != 'F')
                                                         return fail ("not an ELF file");
  if (eh.ident[4] != 2 || eh.ident[5] != 1)              return fail ("not a 64-bit little-endian ELF");
  if (eh.machine != EM_AARCH64)                          return fail ("not an AArch64 executable");
  if (eh.type != 2)                                      return fail ("not a static executable (ET_EXEC)");
  if (eh.phentsize != sizeof ph || eh.phnum == 0 || eh.phnum > 8)
                                                         return fail ("bad program header table");

  int entry_ok = 0;
  u64 end = 0;
  for (int i = 0; i < eh.phnum; i++) {
    if (minix_read (ino, eh.phoff + i * sizeof ph, &ph, sizeof ph) != sizeof ph)
                                                         return fail ("cannot read program header");
    if (ph.type != PT_LOAD) continue;
    if (ph.memsz == 0) continue;
    if (ph.filesz > ph.memsz || ph.offset + ph.filesz > fsize)
                                                         return fail ("segment exceeds the file");
    if (ph.vaddr < USER_MIN || ph.vaddr + ph.memsz > USER_LOAD_MAX || ph.vaddr + ph.memsz < ph.vaddr)
                                                         return fail ("segment outside the user address range");
    if ((ph.flags & PF_W) && (ph.flags & PF_X))          return fail ("segment is writable and executable");

    u64 flags = (ph.flags & PF_X) ? PAGE_URX : (ph.flags & PF_W) ? PAGE_URW : PAGE_URO;
    u64 seg_end = ph.vaddr + ph.memsz, file_end = ph.vaddr + ph.filesz;
    if (seg_end > end) end = seg_end;
    for (u64 p = ph.vaddr & ~(PAGE_SIZE - 1); p < seg_end; p += PAGE_SIZE) {
      u64 pa = alloc_frame ();                           // comes zeroed: this is the .bss part
      u64 a = p > ph.vaddr ? p : ph.vaddr;               // file bytes that land in this page
      u64 b = p + PAGE_SIZE < file_end ? p + PAGE_SIZE : file_end;
      if (a < b && minix_read (ino, ph.offset + (a - ph.vaddr), (u8 *)KVA (pa) + (a - p), b - a) != (long)(b - a)) {
        free_frame (pa);
        return fail ("read error while loading a segment");
      }
      if (map_page_in (l0, p, pa, flags) != 0) { free_frame (pa); return fail ("segments overlap"); }
      if (ph.flags & PF_X) sync_icache (KVA (pa), PAGE_SIZE);
    }
    if ((ph.flags & PF_X) && eh.entry >= ph.vaddr && eh.entry < seg_end) entry_ok = 1;
  }
  if (!entry_ok) return fail ("entry point is not in an executable segment");
  *entry = eh.entry;
  *image_end = end;
  return 0;
}
