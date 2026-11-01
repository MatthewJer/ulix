#include "ulib.h"

int strlen (const char *s) { int n = 0; while (s[n]) n++; return n; }

int strcmp (const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (u8)*a - (u8)*b;
}

struct out { char buf[256]; int n; };
static void put (struct out *o, char c) { if (o->n < (int)sizeof o->buf) o->buf[o->n++] = c; }

static void putnum (struct out *o, u64 v, int base, int width, char pad) {
  char t[24]; int n = 0;
  do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
  while (width > n) { put (o, pad); width--; }
  while (n) put (o, t[--n]);
}

void printf (const char *fmt, ...) {
  struct out o; o.n = 0;
  __builtin_va_list ap;
  __builtin_va_start (ap, fmt);
  for (; *fmt; fmt++) {
    if (*fmt != '%') { put (&o, *fmt); continue; }
    fmt++;
    char pad = ' '; int width = 0;
    if (*fmt == '0') { pad = '0'; fmt++; }
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    switch (*fmt) {
      case 's': { const char *s = __builtin_va_arg (ap, const char *); while (*s) put (&o, *s++); break; }
      case 'c': put (&o, (char)__builtin_va_arg (ap, int)); break;
      case 'd': { int v = __builtin_va_arg (ap, int); if (v < 0) { put (&o, '-'); v = -v; }
                  putnum (&o, v, 10, width, pad); break; }
      case 'u': putnum (&o, __builtin_va_arg (ap, u32), 10, width, pad); break;
      case 'x': putnum (&o, __builtin_va_arg (ap, u32), 16, width, pad); break;
      case '%': put (&o, '%'); break;
      default:  put (&o, '%'); put (&o, *fmt); break;
    }
  }
  __builtin_va_end (ap);
  write (1, o.buf, o.n);
}
