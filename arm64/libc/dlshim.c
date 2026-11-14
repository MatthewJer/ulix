/* dlshim.c: dlopen, dlsym, dlclose and dlerror for a program that is linked statically (issue #40; chapter "Python" in lit/libc.nw).
 *
 * There is no dynamic linker on Ulix, so there are no shared libraries to load. What a program such as Python's ctypes wants from
 * dlopen is almost always "the C library" or "the program itself" (CDLL (None), CDLL ("libc.so.6"), pythonapi): both are the same
 * thing here, because the program already contains the whole C library. So dlopen answers every request for the program itself
 * or for a system library by name (libc, libm, libpthread, ...) with one handle, and dlsym looks the name up in a table of the
 * symbols of the program (dl_table, written by gen_dlsyms.py from the finished executable and sorted by name). A library that
 * is really somewhere on a disk cannot be loaded: dlopen fails with the message that Linux gives for a missing file.
 */
#include <dlfcn.h>
#include <string.h>

struct dl_entry { const char *name; void *addr; };
extern const struct dl_entry dl_table[];                 /* dlsyms.c, made after the first link */
extern const unsigned dl_count;

static const char *dl_error;
static int handle_object;                                /* the address of this is the handle of "the program" */

static int is_system_library (const char *name) {        /* libc.so.6, libm.so.6, libc.musl-aarch64.so.1, libpthread.so.0, ... */
  static const char *const known[] = { "libc.", "libm.", "libpthread.", "libdl.", "librt.", "libutil.", "libcrypt.", "libresolv.", "ld-musl-", 0 };
  const char *base = strrchr (name, '/');
  base = base ? base + 1 : name;
  for (int i = 0; known[i]; i++) if (strncmp (base, known[i], strlen (known[i])) == 0) return 1;
  return 0;
}

void *dlopen (const char *name, int mode) {
  (void) mode;
  dl_error = 0;
  if (!name || is_system_library (name)) return &handle_object;
  dl_error = "cannot open shared object file: No such file or directory (Ulix is linked statically)";
  return 0;
}

void *dlsym (void *handle, const char *name) {
  unsigned lo = 0, hi = dl_count;
  (void) handle;                                         /* there is only one place to look */
  while (lo < hi) {                                      /* binary search in the table */
    unsigned mid = (lo + hi) / 2;
    int c = strcmp (name, dl_table[mid].name);
    if (c == 0) { dl_error = 0;  return dl_table[mid].addr; }
    if (c < 0) hi = mid; else lo = mid + 1;
  }
  dl_error = "undefined symbol";
  return 0;
}

int dlclose (void *handle) { (void) handle;  return 0; }

char *dlerror (void) { const char *e = dl_error;  dl_error = 0;  return (char *) e; }
