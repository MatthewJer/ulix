/* ffi_closures.c: closures for libffi on a system that does not allow memory to be writable and executable at the same time (W^X).
 *
 * libffi's own closures.c gets one executable, writable area and cuts closures out of it. Ulix refuses such memory. The way around it is a
 * transition: every closure gets a page of its own, which is made while it is read+write (ffi_closure_alloc); libffi fills it in
 * (ffi_prep_closure_loc writes the little trampoline and the pointers to the function, the description of the call and the user data); then the
 * page is switched to read+execute and is never written again (__wrap_ffi_prep_closure_loc below; the kernel's mprotect cleans the
 * instruction cache for a page that becomes executable). ffi_closure_free gives the page back. A page per closure is a lot of memory for
 * a few dozen bytes, but a program has only a handful of callbacks. The link uses -Wl,--wrap=ffi_prep_closure_loc.
 */
#include <stddef.h>
#include <sys/mman.h>
#include <ffi.h>

#define PAGE 4096

void *ffi_closure_alloc (size_t size, void **code) {
  if (size > PAGE) return 0;
  void *p = mmap (0, PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) return 0;
  *code = p;                                       /* writable address and executable address are the same page, one after the other */
  return p;
}

void ffi_closure_free (void *closure) { munmap (closure, PAGE); }

extern ffi_status __real_ffi_prep_closure_loc (ffi_closure *, ffi_cif *, void (*) (ffi_cif *, void *, void **, void *), void *, void *);

ffi_status __wrap_ffi_prep_closure_loc (ffi_closure *closure, ffi_cif *cif, void (*fun) (ffi_cif *, void *, void **, void *), void *user_data, void *codeloc) {
  ffi_status r = __real_ffi_prep_closure_loc (closure, cif, fun, user_data, codeloc);
  if (r == FFI_OK && mprotect ((void *) ((size_t) codeloc & ~(size_t) (PAGE - 1)), PAGE, PROT_READ | PROT_EXEC) != 0) return FFI_BAD_ABI;
  return r;
}

/* (libffi's aarch64 code asks for the executable address that goes with a closure's address: here they are the same page) */
void *ffi_data_to_code_pointer (void *data) { return data; }
