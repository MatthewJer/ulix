#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int fails = 0, checks = 0;
#define CHECK(c, msg) do { checks++; if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)
#define SECTION(name) printf ("== %s\n", name)
#define SELF "/musl/libctest"
#define TMP "/tmp/libctest.tmp"

static void test_string (void) {
  SECTION ("string");
  char buf[64];
  CHECK (strlen ("") == 0 && strlen ("abc") == 3 && strnlen ("abcdef", 4) == 4, "strlen, strnlen");
  strcpy (buf, "abc");  strcat (buf, "def");
  CHECK (!strcmp (buf, "abcdef"), "strcpy, strcat");
  strncpy (buf, "xy", 5);
  CHECK (!memcmp (buf, "xy\0\0\0", 5), "strncpy pads with zero bytes up to n");
  CHECK (strcmp ("a", "b") < 0 && strcmp ("b", "a") > 0 && strcmp ("a", "a") == 0 && strcmp ("a", "ab") < 0, "strcmp: order and equality");
  CHECK (strcmp ("\xff", "a") > 0, "strcmp compares as unsigned char (byte 255 is above 'a')");
  CHECK (strncmp ("abcX", "abcY", 3) == 0 && strncmp ("abcX", "abcY", 4) < 0, "strncmp stops after n");
  CHECK (strcasecmp ("HeLLo", "hello") == 0 && strncasecmp ("HELp", "help!", 4) == 0, "strcasecmp, strncasecmp");
  const char *s = "one two  three";
  CHECK (strchr (s, 't') == s + 4 && strrchr (s, 't') == s + 9 && strchr (s, 'z') == 0 && strchr (s, 0) == s + strlen (s), "strchr, strrchr (and the terminating zero is found)");
  CHECK (strstr (s, "two") == s + 4 && strstr (s, "") == s && strstr (s, "four") == 0, "strstr");
  CHECK (strspn (s, "eno") == 3 && strcspn (s, " ") == 3 && strpbrk (s, "wt") == s + 4, "strspn, strcspn, strpbrk");
  char tok[] = "a,b,,c";  char *save, *t, last[8] = "";
  int n = 0;
  for (t = strtok_r (tok, ",", &save); t; t = strtok_r (0, ",", &save)) { n++; strcpy (last, t); }
  CHECK (n == 3 && !strcmp (last, "c"), "strtok_r: three tokens (the empty one between two commas is skipped)");
  char *d = strdup ("copy");
  CHECK (d && !strcmp (d, "copy"), "strdup");
  free (d);
  memset (buf, 'x', 8);  memcpy (buf + 8, buf, 8);
  CHECK (!memcmp (buf, "xxxxxxxxxxxxxxxx", 16), "memset, memcpy");
  char ov[16] = "0123456789";
  memmove (ov + 2, ov, 8);
  CHECK (!memcmp (ov, "0101234567", 10), "memmove with overlap, destination above source");
  memmove (ov, ov + 2, 8);
  CHECK (!memcmp (ov, "0123456767", 10), "memmove with overlap, destination below source");
  CHECK (memchr ("abcdef", 'd', 6) != 0 && memchr ("abcdef", 'd', 3) == 0 && memcmp ("abc", "abd", 3) < 0 && memcmp ("abc", "abd", 2) == 0, "memchr (with limit), memcmp");
  // long strings take musl's word-at-a-time code; try every start alignment
  static char big[300], big2[300];
  int ok = 1;
  for (int off = 0; off < 9; off++) {
    memset (big, 'a', 299);  big[299] = 0;
    memcpy (big2, big, 300);
    if (strlen (big + off) != (size_t) (299 - off) || strcmp (big + off, big2 + off) || memcmp (big + off, big2 + off, 299 - off)) ok = 0;
    big2[200] = 'b';
    if (strcmp (big + off, big2 + off) >= 0 || strchr (big2 + off, 'b') != big2 + 200) ok = 0;
  }
  CHECK (ok, "strlen, strcmp, memcmp, strchr on a 299 byte string from nine different alignments");
}

static void test_ctype (void) {
  SECTION ("ctype");
  int bad = 0;
  for (int c = 0; c < 256; c++) {
    int upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
    int space = c == ' ' || (c >= 9 && c <= 13), print = c >= 32 && c < 127;
    if ((isupper (c) != 0) != upper || (islower (c) != 0) != lower || (isdigit (c) != 0) != digit || (isalpha (c) != 0) != (upper || lower)) bad++;
    if ((isalnum (c) != 0) != (upper || lower || digit) || (isspace (c) != 0) != space || (isprint (c) != 0) != print || (isgraph (c) != 0) != (print && c != ' ')) bad++;
    if ((iscntrl (c) != 0) != (c < 32 || c == 127) || (ispunct (c) != 0) != (print && c != ' ' && !(upper || lower || digit))) bad++;
    if ((isxdigit (c) != 0) != (digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) bad++;
    if (toupper (c) != (lower ? c - 32 : c) || tolower (c) != (upper ? c + 32 : c)) bad++;
  }
  CHECK (bad == 0, "the is* functions, toupper and tolower agree with the C locale for all values 0..255");
  CHECK (isalpha (EOF) == 0 && toupper (EOF) == EOF, "the value EOF (-1) is allowed as argument");
}

// a call that must fail with one error number: says what it got if that is not so
static int fails_with (long rc, int want) {
  int got = errno;
  if (rc == -1 && got == want) return 1;
  printf ("    (the call returned %ld, errno %d (%s), wanted -1 and %d (%s))\n", rc, got, strerror (got), want, strerror (want));
  return 0;
}

static void test_errno (void) {
  SECTION ("errno");
  char c;
  errno = 0;
  CHECK (fails_with (open ("/no/such/file", O_RDONLY), ENOENT), "open of a missing file: -1 and ENOENT");
  errno = 0;
  CHECK (fails_with (read (-1, &c, 1), EBADF), "read from descriptor -1: EBADF");
  errno = 0;
  CHECK (fails_with (mkdir ("/etc", 0755), EEXIST), "mkdir of an existing directory: EEXIST");
  errno = 0;
  CHECK (fails_with (rmdir ("/no/such/dir"), ENOENT), "rmdir of a missing directory: ENOENT");
  errno = 0;
  CHECK (fails_with (chdir ("/etc/rc.conf"), ENOTDIR), "chdir to a file: ENOTDIR");
  CHECK (!strcmp (strerror (ENOENT), "No such file or directory") && !strcmp (strerror (EEXIST), "File exists") && !strcmp (strerror (EBADF), "Bad file descriptor"),
         "strerror gives the standard texts");
  errno = 0;
  strtol ("99999999999999999999", 0, 10);
  CHECK (errno == ERANGE, "a library function sets errno without a system call (strtol: ERANGE)");
  errno = 5;
  getpid ();
  CHECK (errno == 5, "a system call that succeeds leaves errno alone");
}
static int cmp_int (const void *a, const void *b) { int x = *(const int *) a, y = *(const int *) b; return (x > y) - (x < y); }

static void test_stdlib (void) {
  SECTION ("stdlib");
  char *end;
  CHECK (atoi ("  -42xyz") == -42 && atol ("123456789012") == 123456789012L && atoll ("-9") == -9, "atoi, atol, atoll");
  CHECK (strtol ("0x1f", &end, 16) == 31 && *end == 0 && strtol ("0x1f", 0, 0) == 31 && strtol ("017", 0, 0) == 15 && strtol ("zz", 0, 36) == 1295, "strtol: base 16, base 0 (hex and octal prefix), base 36");
  CHECK (strtol ("12abc", &end, 10) == 12 && !strcmp (end, "abc") && strtol ("abc", &end, 10) == 0 && !strcmp (end, "abc"), "strtol: end pointer after the digits, and unchanged if there are none");
  errno = 0;
  CHECK (strtol ("9223372036854775808", 0, 10) == LONG_MAX && errno == ERANGE, "strtol overflow: LONG_MAX and ERANGE");
  errno = 0;
  CHECK (strtoul ("18446744073709551616", 0, 10) == ULONG_MAX && errno == ERANGE && strtoul ("-1", 0, 10) == ULONG_MAX, "strtoul overflow: ULONG_MAX and ERANGE; \"-1\" wraps around");
  CHECK (strtoll ("-9223372036854775808", 0, 10) == LLONG_MIN, "strtoll (LLONG_MIN)");
  CHECK (strtod ("3.25", 0) == 3.25 && strtod ("1e3", 0) == 1000.0 && strtod ("-0.5e-1", 0) == -0.05 && strtod ("0x1p4", 0) == 16.0, "strtod: decimal, exponent, hexadecimal float");
  CHECK (isinf (strtod ("inf", 0)) && isnan (strtod ("nan", 0)) && strtod ("1.5abc", &end) == 1.5 && !strcmp (end, "abc"), "strtod: inf, nan, end pointer");
  CHECK (abs (-5) == 5 && labs (-5L) == 5 && llabs (-5LL) == 5, "abs, labs, llabs");
  div_t q = div (-7, 2);  ldiv_t lq = ldiv (7, -2);
  CHECK (q.quot == -3 && q.rem == -1 && lq.quot == -3 && lq.rem == 1, "div, ldiv truncate toward zero");
  CHECK (setenv ("LIBCTEST", "one", 1) == 0 && !strcmp (getenv ("LIBCTEST"), "one"), "setenv, getenv");
  CHECK (setenv ("LIBCTEST", "two", 0) == 0 && !strcmp (getenv ("LIBCTEST"), "one"), "setenv without overwrite keeps the old value");
  CHECK (setenv ("LIBCTEST", "two", 1) == 0 && !strcmp (getenv ("LIBCTEST"), "two") && unsetenv ("LIBCTEST") == 0 && getenv ("LIBCTEST") == 0, "setenv with overwrite, unsetenv");
  srand (7);  int a1 = rand (), a2 = rand ();  srand (7);
  CHECK (rand () == a1 && rand () == a2 && a1 >= 0 && a1 <= RAND_MAX, "srand makes the sequence of rand repeatable, values are in 0..RAND_MAX");
  int v[100], bad = 0;
  for (int i = 0; i < 100; i++) v[i] = (i * 37) % 101;               // 100 different values in a scrambled order
  qsort (v, 100, sizeof v[0], cmp_int);
  for (int i = 1; i < 100; i++) if (v[i - 1] >= v[i]) bad++;
  CHECK (bad == 0, "qsort sorts 100 integers");
  int key = v[63], *hit = bsearch (&key, v, 100, sizeof v[0], cmp_int), miss = 1000;
  CHECK (hit == v + 63 && bsearch (&miss, v, 100, sizeof v[0], cmp_int) == 0, "bsearch finds an element and reports a missing one");
  int one[1] = { 5 }, zero = 0;
  qsort (one, 1, sizeof one[0], cmp_int);  qsort (&zero, 0, sizeof zero, cmp_int);
  CHECK (one[0] == 5, "qsort with one element and with none");
  struct rec { int key, order; } r[40];
  for (int i = 0; i < 40; i++) { r[i].key = i % 4; r[i].order = i; }
  qsort (r, 40, sizeof r[0], cmp_int);                                  // compares only the first int: the key
  bad = 0;
  for (int i = 1; i < 40; i++) if (r[i - 1].key > r[i].key) bad++;
  CHECK (bad == 0, "qsort on 8 byte records sorts by the key (the order of equal keys is not defined)");
  char tmpl[] = "/tmp/libctestXXXXXX";
  int fd = mkstemp (tmpl);
  CHECK (fd >= 0 && strcmp (tmpl, "/tmp/libctestXXXXXX") && access (tmpl, F_OK) == 0, "mkstemp makes a new file and fills in the name");
  close (fd);  unlink (tmpl);
}

static int exit_order[4], exit_count;
static void at_a (void) { exit_order[exit_count++] = 1; }
static void at_b (void) { exit_order[exit_count++] = 2; }

static void test_atexit (void) {
  SECTION ("atexit");
  // the handlers run when the process exits: a child registers two, the parent sees the output order through a pipe
  int p[2];
  pipe (p);
  pid_t pid = fork ();
  if (pid == 0) {
    close (p[0]);  dup2 (p[1], 1);
    atexit (at_a);  atexit (at_b);
    printf ("buffered text, no newline at the end ");
    exit (0);                                                   // exit flushes stdio and then runs the handlers in reverse order
  }
  close (p[1]);
  char out[100];  int n = read (p[0], out, sizeof out - 1);
  out[n > 0 ? n : 0] = 0;
  close (p[0]);
  int st = 0;  waitpid (pid, &st, 0);
  CHECK (n > 0 && !strcmp (out, "buffered text, no newline at the end ") && WIFEXITED (st) && WEXITSTATUS (st) == 0, "exit () flushes the stdio buffer of the process");
  at_a (); at_b ();                                              // (only to use the handlers here: order of registration versus order of the calls below)
  CHECK (exit_order[0] == 1 && exit_order[1] == 2, "atexit handlers are ordinary functions");
}
static int near (double a, double b) { return fabs (a - b) <= 1e-12 * (1 + fabs (b)); }

static void test_math (void) {
  SECTION ("math");
  volatile double two = 2.0, zero = 0.0, big = 1e308;                 // (volatile: the compiler must not compute the answers itself)
  CHECK (sqrt (two) * sqrt (two) > 1.9999999999999 && sqrt (16.0) == 4.0 && isnan (sqrt (-two)), "sqrt: exact for 16, nan for a negative number");
  CHECK (near (sin (1.0), 0.8414709848078965) && near (cos (1.0), 0.5403023058681398) && near (tan (1.0), 1.5574077246549023), "sin, cos, tan of 1");
  CHECK (near (exp (1.0), 2.718281828459045) && near (log (10.0), 2.302585092994046) && near (log10 (1000.0), 3.0) && near (log2 (8.0), 3.0), "exp, log, log10, log2");
  CHECK (near (pow (2.0, 10.0), 1024.0) && near (pow (two, 0.5), 1.4142135623730951) && pow (5.0, 0.0) == 1.0, "pow");
  CHECK (near (atan2 (1.0, 1.0), 0.7853981633974483) && near (atan (1.0), 0.7853981633974483) && near (asin (1.0), 1.5707963267948966) && near (acos (0.0), 1.5707963267948966), "atan2, atan, asin, acos");
  CHECK (near (sinh (1.0), 1.1752011936438014) && near (cosh (1.0), 1.5430806348152437) && near (tanh (1.0), 0.7615941559557649), "sinh, cosh, tanh");
  CHECK (floor (-1.5) == -2.0 && ceil (-1.5) == -1.0 && trunc (-1.5) == -1.0 && round (-2.5) == -3.0 && rint (2.5) == 2.0 && floor (1.5) == 1.0, "floor, ceil, trunc, round (half away from zero), rint (half to even)");
  CHECK (fmod (7.5, 2.0) == 1.5 && fmod (-7.5, 2.0) == -1.5 && fabs (-3.0) == 3.0 && copysign (3.0, -0.0) == -3.0 && fmax (1.0, 2.0) == 2.0 && fmin (1.0, 2.0) == 1.0, "fmod, fabs, copysign, fmax, fmin");
  int e;
  double m = frexp (48.0, &e);
  CHECK (m == 0.75 && e == 6 && ldexp (0.75, 6) == 48.0 && cbrt (27.0) == 3.0 && hypot (3.0, 4.0) == 5.0, "frexp, ldexp, cbrt, hypot");
  double ip, fp = modf (-3.25, &ip);
  CHECK (ip == -3.0 && fp == -0.25, "modf splits into integer and fraction part");
  CHECK (isinf (big * 10) && isinf (-big * 10) && isnan (zero / zero) && 1.0 / zero == INFINITY && !isnan (1.0) && isfinite (1.0) && !isfinite (INFINITY), "infinity and nan: overflow gives inf, 0/0 gives nan");
  CHECK (signbit (-zero) && !signbit (zero) && fpclassify (1e-310) == FP_SUBNORMAL && fpclassify (1.0) == FP_NORMAL && fpclassify (zero) == FP_ZERO, "negative zero, subnormal numbers");
  CHECK (DBL_EPSILON == 2.220446049250313e-16 && (1.0 + DBL_EPSILON / 2) == 1.0 && FLT_MAX > 3.4e38f && LDBL_MANT_DIG == 113, "double is IEEE binary64, long double is binary128 (AArch64)");
  volatile float f = 1.1f;
  CHECK ((double) f != 1.1 && fabsf (f - 1.1f) < 1e-7f && sqrtf (9.0f) == 3.0f && floorf (-0.5f) == -1.0f, "float: rounded to 24 bits, sqrtf, floorf");
  volatile long double ld = 1.0L;
  ld = ld / 3.0L;
  CHECK (ld * 3.0L == 1.0L && (double) ld == 1.0 / 3.0 && sqrtl (4.0L) == 2.0L, "long double arithmetic (software, 113 bits)");
  volatile long long big_ll = 9007199254740993LL;
  CHECK ((long long) (double) big_ll == 9007199254740992LL && (long) 1e18 == 1000000000000000000L && (int) -2.9 == -2 && (unsigned) 3e9 == 3000000000u, "conversions between integers and double");
}
static void test_time (void) {
  SECTION ("time");
  time_t t0 = time (0);
  CHECK (t0 > 1700000000, "time () is after November 2023 (the kernel has a clock)");
  struct timespec a, b, rt;
  clock_gettime (CLOCK_MONOTONIC, &a);
  struct timespec nap = { 0, 120 * 1000000 };
  CHECK (nanosleep (&nap, 0) == 0, "nanosleep (120 ms) returns 0");
  clock_gettime (CLOCK_MONOTONIC, &b);
  long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
  CHECK (ms >= 115 && ms < 2000, "CLOCK_MONOTONIC advanced by about the time that nanosleep slept (120 ms)");
  clock_gettime (CLOCK_REALTIME, &rt);
  CHECK (llabs ((long long) rt.tv_sec - t0) <= 2 && rt.tv_nsec >= 0 && rt.tv_nsec < 1000000000, "CLOCK_REALTIME agrees with time (), nanoseconds are in range");
  struct timeval tv;
  CHECK (gettimeofday (&tv, 0) == 0 && llabs ((long long) tv.tv_sec - t0) <= 2, "gettimeofday agrees with time ()");
  clock_t c = clock ();
  if (c == (clock_t) -1) printf ("    (clock () failed: errno %d)\n", errno);
  CHECK (c != (clock_t) -1, "clock () works (CPU time is available)");
  time_t z = 0;
  struct tm *g = gmtime (&z);
  CHECK (g->tm_year == 70 && g->tm_mon == 0 && g->tm_mday == 1 && g->tm_hour == 0 && g->tm_wday == 4 && g->tm_yday == 0, "gmtime (0) is Thursday 1 January 1970");
  struct tm tm = { 0 };
  tm.tm_year = 124; tm.tm_mon = 1; tm.tm_mday = 29; tm.tm_hour = 12; tm.tm_min = 34; tm.tm_sec = 56;
  time_t leap = timegm (&tm);
  CHECK (leap == 1709210096 && tm.tm_wday == 4 && tm.tm_yday == 59, "timegm: 29 February 2024 12:34:56 is 1709210096 (a leap day), Thursday, day 59 of the year");
  time_t back = leap + 86400 * 366;
  struct tm *g2 = gmtime (&back);
  CHECK (g2->tm_year == 125 && g2->tm_mon == 2 && g2->tm_mday == 1, "gmtime, 366 days later: 1 March 2025 (2025 is not a leap year)");
  char buf[80];
  size_t n = strftime (buf, sizeof buf, "%Y-%m-%d %H:%M:%S %a %b %j", &tm);
  CHECK (n > 0 && !strcmp (buf, "2024-02-29 12:34:56 Thu Feb 060"), "strftime: date, time, weekday and month names, day of year");
  n = strftime (buf, sizeof buf, "%A %B %e|%y|%p|%I|%%", &tm);
  CHECK (n > 0 && !strcmp (buf, "Thursday February 29|24|PM|12|%"), "strftime: long names, %e, %y, %p, %I, %%");
  n = strftime (buf, 5, "%Y-%m-%d", &tm);
  CHECK (n == 0, "strftime returns 0 if the result does not fit");
  setenv ("TZ", "UTC", 1);  tzset ();
  struct tm lt = { 0 };
  lt.tm_year = 100; lt.tm_mday = 1; lt.tm_hour = 0;
  CHECK (mktime (&lt) == 946684800 && lt.tm_wday == 6, "mktime in time zone UTC: 1 January 2000 is 946684800 (Saturday)");
  lt.tm_mon = 0; lt.tm_mday = 32;
  mktime (&lt);
  CHECK (lt.tm_mon == 1 && lt.tm_mday == 1, "mktime normalizes 32 January to 1 February");
  struct tm pt = { 0 };
  char *rest = strptime ("2023-12-24 18:30", "%Y-%m-%d %H:%M", &pt);
  CHECK (rest && *rest == 0 && pt.tm_year == 123 && pt.tm_mon == 11 && pt.tm_mday == 24 && pt.tm_hour == 18 && pt.tm_min == 30, "strptime reads what strftime writes");
  CHECK (difftime (100, 40) == 60.0, "difftime");
}
static int pattern_ok (unsigned char *p, size_t n, int seed) {
  for (size_t i = 0; i < n; i++) if (p[i] != (unsigned char) (seed + i * 7)) return 0;
  return 1;
}
static void fill (unsigned char *p, size_t n, int seed) { for (size_t i = 0; i < n; i++) p[i] = seed + i * 7; }

static void test_malloc (void) {
  SECTION ("malloc");
  void *p = malloc (1);
  CHECK (p != 0 && ((uintptr_t) p & 15) == 0, "malloc (1): not NULL, 16 byte aligned");
  free (p);  free (0);
  CHECK (1, "free (NULL) does nothing");
  unsigned char *z = calloc (1000, 4);
  int zero = 1;
  for (int i = 0; i < 4000; i++) if (z[i]) zero = 0;
  CHECK (z && zero, "calloc (1000, 4): 4000 bytes, all zero");
  free (z);
  CHECK (calloc (SIZE_MAX / 2, 4) == 0 && errno == ENOMEM, "calloc with a size that overflows size_t fails with ENOMEM");
  unsigned char *r = malloc (10);
  fill (r, 10, 5);
  r = realloc (r, 100000);
  CHECK (r && pattern_ok (r, 10, 5), "realloc to a larger block keeps the old contents");
  r = realloc (r, 5);
  CHECK (r && pattern_ok (r, 5, 5), "realloc to a smaller block keeps the first bytes");
  free (r);
  CHECK (realloc (0, 16) != 0, "realloc (NULL, n) is malloc");
  void *m = 0;
  CHECK (posix_memalign (&m, 4096, 100) == 0 && ((uintptr_t) m & 4095) == 0, "posix_memalign (4096)");
  free (m);
  CHECK (((uintptr_t) aligned_alloc (64, 256) & 63) == 0, "aligned_alloc (64, 256)");
  // many blocks of mixed sizes, all filled with their own pattern, checked afterwards (overlap or reuse bugs show up here)
  enum { N = 400 };
  static unsigned char *blk[N];  static size_t len[N];
  unsigned rnd = 12345;
  int ok = 1;
  for (int i = 0; i < N; i++) {
    rnd = rnd * 1103515245 + 12345;
    len[i] = 1 + (rnd >> 16) % (i % 10 == 0 ? 20000 : 300);
    blk[i] = malloc (len[i]);
    if (!blk[i]) { ok = 0; break; }
    fill (blk[i], len[i], i);
  }
  for (int i = 0; ok && i < N; i++) if (!pattern_ok (blk[i], len[i], i)) ok = 0;
  CHECK (ok, "400 blocks of 1 to 20000 bytes, each with its own pattern: none was overwritten");
  for (int i = 0; i < N; i += 2) free (blk[i]);                       // free every second one, allocate again: the holes are reused
  for (int i = 0; i < N; i += 2) { blk[i] = malloc (len[i]); if (!blk[i]) ok = 0; else fill (blk[i], len[i], i); }
  for (int i = 0; ok && i < N; i++) if (!pattern_ok (blk[i], len[i], i)) ok = 0;
  CHECK (ok, "free every second block and allocate again: the contents of all blocks are still right");
  for (int i = 0; i < N; i++) free (blk[i]);
  // large blocks (mmap): the pages are zero, and the loop does not use up the memory if free gives it back
  size_t mb = 1 << 20;
  unsigned char *L = malloc (mb);
  CHECK (L != 0 && L[0] == 0 && L[mb / 2] == 0 && L[mb - 1] == 0, "malloc (1 MByte) comes from fresh pages: zero at start, middle, end");
  fill (L, mb, 3);
  CHECK (pattern_ok (L, mb, 3), "write and read back 1 MByte");
  free (L);
  int loops = 0;
  for (; loops < 200; loops++) { unsigned char *x = malloc (mb); if (!x) break; x[0] = 1; x[mb - 1] = 1; free (x); }
  CHECK (loops == 200, "200 times malloc and free of 1 MByte (free returns the pages to the kernel with munmap)");
  errno = 0;
  CHECK (malloc ((size_t) 1 << 62) == 0 && errno == ENOMEM, "malloc of an absurd size fails with ENOMEM");
}
static jmp_buf env;
static void jump_back (int depth, int value) { if (depth == 0) longjmp (env, value); jump_back (depth - 1, value); }

static void test_setjmp (void) {
  SECTION ("setjmp");
  volatile int count = 0;
  int r = setjmp (env);
  if (r == 0) { count++; jump_back (10, 5); count = 100; }
  CHECK (r == 5 && count == 1, "longjmp (env, 5) returns from setjmp with 5, through ten frames of calls");
  r = setjmp (env);
  if (r == 0) longjmp (env, 0);
  CHECK (r == 1, "longjmp (env, 0) makes setjmp return 1, not 0");
  volatile double d = 2.5;
  r = setjmp (env);
  if (r == 0) { d = 7.25; longjmp (env, 2); }
  CHECK (d == 7.25 && r == 2, "a volatile double keeps its new value over longjmp");
  sigjmp_buf senv;
  sigset_t block, old, now;
  sigemptyset (&block);  sigaddset (&block, SIGUSR1);
  sigprocmask (SIG_BLOCK, &block, &old);
  if (sigsetjmp (senv, 1) == 0) {
    sigprocmask (SIG_UNBLOCK, &block, 0);
    siglongjmp (senv, 1);
  }
  sigprocmask (SIG_SETMASK, 0, &now);
  CHECK (sigismember (&now, SIGUSR1) == 1, "sigsetjmp (env, 1) and siglongjmp restore the signal mask (SIGUSR1 is blocked again)");
  sigprocmask (SIG_SETMASK, &old, 0);
}
static int fmt_is (const char *want, const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (buf, sizeof buf, fmt, ap);
  va_end (ap);
  if (strcmp (buf, want)) printf ("    (\"%s\" gave \"%s\", wanted \"%s\")\n", fmt, buf, want);
  return !strcmp (buf, want);
}

static void test_printf (void) {
  SECTION ("printf");
  CHECK (fmt_is ("42|-42|+42| 42", "%d|%d|%+d|% d", 42, -42, 42, 42), "%d with sign flags");
  CHECK (fmt_is ("   42|42   |00042|  042", "%5d|%-5d|%05d|%5.3d", 42, 42, 42, 42), "width, left justify, zero padding, precision of an integer");
  CHECK (fmt_is ("ff|FF|0xff|0XFF|377|0377", "%x|%X|%#x|%#X|%o|%#o", 255, 255, 255, 255, 255, 255), "%x, %X, %o and the # flag");
  CHECK (fmt_is ("-2147483648|2147483647|4294967295", "%d|%d|%u", INT_MIN, INT_MAX, UINT_MAX), "INT_MIN, INT_MAX, UINT_MAX");
  CHECK (fmt_is ("-9223372036854775808|18446744073709551615", "%lld|%llu", LLONG_MIN, ULLONG_MAX), "%lld of LLONG_MIN, %llu of ULLONG_MAX");
  CHECK (fmt_is ("-1|255|-1|65535|-1", "%hhd|%hhu|%hd|%hu|%zd", -1, 255, -1, 65535, (ssize_t) -1), "length modifiers hh, h, z");
  CHECK (fmt_is ("12345678901234|1234", "%jd|%td", (intmax_t) 12345678901234LL, (ptrdiff_t) 1234), "length modifiers j, t");
  CHECK (fmt_is ("3.141593|3.14|    3.14|3.14    |+3.14", "%f|%.2f|%8.2f|%-8.2f|%+.2f", 3.14159265, 3.14159265, 3.14159265, 3.14159265, 3.14159265), "%f: precision, width, flags");
  CHECK (fmt_is ("1.500000e+00|1.5E+00|1e+10", "%e|%.1E|%.0e", 1.5, 1.5, 1e10), "%e and %E");
  CHECK (fmt_is ("0.0001|1e-05|100000|1e+06|1.5", "%g|%g|%g|%g|%g", 0.0001, 0.00001, 100000.0, 1000000.0, 1.5), "%g switches to exponent form below 1e-4 and at 1e+6");
  CHECK (fmt_is ("2.0|1.50", "%#.1f|%.2f", 2.0, 1.5), "%f, # flag keeps the point");
  CHECK (fmt_is ("0.1|0.2|0.3", "%.1f|%.1f|%.1f", 0.1, 0.15 + 0.05, 0.3), "%.1f of 0.1, 0.2, 0.3 (printed from exact binary values, rounded correctly)");
  CHECK (fmt_is ("2|2|4", "%.0f|%.0f|%.0f", 2.5, 1.5, 3.5), "%.0f rounds half to even (the value is exact)");
  CHECK (fmt_is ("inf|-inf|nan|INF", "%f|%f|%f|%F", INFINITY, -INFINITY, NAN, INFINITY), "inf and nan");
  CHECK (fmt_is ("1.5000000000000000", "%.16Lf", 1.5L), "%Lf: long double");
  CHECK (fmt_is ("0x1.8p+0", "%a", 1.5), "%a: hexadecimal float");
  CHECK (fmt_is ("hello|  hello|hello  |hel|   hel", "%s|%7s|%-7s|%.3s|%6.3s", "hello", "hello", "hello", "hello", "hello"), "%s: width, precision");
  CHECK (fmt_is ("ab|", "%.*s|%.*s", 2, "abc", 0, "abc"), "%.*s takes the precision from the arguments");
  CHECK (fmt_is ("   7|7   ", "%*d|%-*d", 4, 7, 4, 7), "%*d takes the width from the arguments (a negative width left-justifies)");
  CHECK (fmt_is ("(null)", "%s", (char *) 0), "%s of a null pointer prints (null) (musl)");
  CHECK (fmt_is ("A|  A|A  |%", "%c|%3c|%-3c|%%", 'A', 'A', 'A'), "%c, %%");
  CHECK (fmt_is ("0x10|(nil)", "%p|%p", (void *) 16, (void *) 0), "%p: hexadecimal, (nil) for a null pointer (musl)");
  int n1 = 0, n2 = 0;
  char buf[200];
  sprintf (buf, "abc%nde%n", &n1, &n2);
  CHECK (n1 == 3 && n2 == 5, "%n stores the number of characters written so far");
  char small[8];
  int len = snprintf (small, sizeof small, "%s", "0123456789");
  CHECK (len == 10 && !strcmp (small, "0123456"), "snprintf returns the length of the full text (10) and cuts at the buffer size, with a terminating zero");
  CHECK (snprintf (0, 0, "%d%s", 12345, "xyz") == 8, "snprintf (NULL, 0, ...) only counts");
  CHECK (fmt_is ("1 2 3 4 5 6 7 8 9 10", "%d %d %d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10), "ten arguments (more than fit in registers: the rest goes on the stack)");
  CHECK (fmt_is ("1.5 2 3.5 4 5.5 6 7.5 8 9.5 10", "%.1f %.0f %.1f %.0f %.1f %.0f %.1f %.0f %.1f %.0f", 1.5, 2.0, 3.5, 4.0, 5.5, 6.0, 7.5, 8.0, 9.5, 10.0), "ten floating point arguments (eight in registers, two on the stack)");
  CHECK (fmt_is ("1 a 2.5 b 3 c 4.5 d 5 e 6.5 f", "%d %c %.1f %c %d %c %.1f %c %d %c %.1f %c", 1, 'a', 2.5, 'b', 3, 'c', 4.5, 'd', 5, 'e', 6.5, 'f'), "mixed integer and floating point arguments");
  CHECK (fmt_is ("12345678901234567890", "%.20s", "12345678901234567890"), "a 20 character string");
  char *as = 0;
  CHECK (asprintf (&as, "%d-%s", 7, "x") == 3 && as && !strcmp (as, "7-x"), "asprintf allocates the result");
  free (as);
  int a, b;  char word[20];  double x;  unsigned h;
  CHECK (sscanf ("12 -7 hello 2.5 ff", "%d %d %19s %lf %x", &a, &b, word, &x, &h) == 5 && a == 12 && b == -7 && !strcmp (word, "hello") && x == 2.5 && h == 255, "sscanf: %d %d %s %lf %x");
  CHECK (sscanf ("key=value", "%[a-z]=%[a-z]", word, buf) == 2 && !strcmp (word, "key") && !strcmp (buf, "value"), "sscanf: character classes %[a-z]");
  CHECK (sscanf ("abc", "%d", &a) == 0 && sscanf ("", "%d", &a) == EOF, "sscanf returns 0 if the text does not match and EOF at the end of the input");
  int pos;
  CHECK (sscanf ("12 34", "%d%n", &a, &pos) == 1 && pos == 2 && sscanf ("0x1A", "%i", &a) == 1 && a == 26, "sscanf: %n and %i (base from the prefix)");
}
static void test_stdio (void) {
  SECTION ("stdio");
  char line[100];
  unlink (TMP);
  FILE *f = fopen (TMP, "w");
  CHECK (f != 0, "fopen (\"w\") creates the file");
  CHECK (fprintf (f, "line one\nline two\n") == 18 && fputs ("third", f) >= 0 && fputc ('!', f) == '!' && putc ('\n', f) == '\n', "fprintf, fputs, fputc, putc");
  struct stat st;
  CHECK (stat (TMP, &st) == 0 && st.st_size == 0, "the text is still in the buffer: the file on the disk is empty");
  CHECK (fflush (f) == 0 && stat (TMP, &st) == 0 && st.st_size == 25, "fflush writes it: the file has 25 bytes");
  CHECK (fclose (f) == 0, "fclose");
  f = fopen (TMP, "r");
  CHECK (fgets (line, sizeof line, f) && !strcmp (line, "line one\n") && ftell (f) == 9, "fgets reads one line including the newline, ftell says 9");
  CHECK (fgets (line, 5, f) && !strcmp (line, "line") && fgets (line, sizeof line, f) && !strcmp (line, " two\n"), "fgets (buf, 5) reads at most 4 characters");
  CHECK (fgetc (f) == 't' && ungetc ('T', f) == 'T' && getc (f) == 'T' && getc (f) == 'h', "fgetc, ungetc, getc");
  CHECK (fseek (f, -2, SEEK_END) == 0 && fgetc (f) == '!' && fgetc (f) == '\n' && fgetc (f) == EOF && feof (f) && !ferror (f), "fseek from the end; reading past the end gives EOF and feof is set");
  clearerr (f);
  CHECK (!feof (f), "clearerr clears the end-of-file flag");
  rewind (f);
  char all[100];
  size_t n = fread (all, 1, sizeof all, f);
  CHECK (n == 25 && feof (f) && !memcmp (all, "line one\n", 9), "fread of 100 bytes returns the 25 that are there");
  fseek (f, 5, SEEK_SET);
  CHECK (fread (all, 5, 1, f) == 1 && !memcmp (all, "one\nl", 5) && ftell (f) == 10, "fseek to 5, fread of one 5 byte item");
  fclose (f);
  f = fopen (TMP, "a");
  fputs ("appended\n", f);
  fclose (f);
  f = fopen (TMP, "r+");
  fseek (f, 0, SEEK_END);
  CHECK (ftell (f) == 34, "fopen (\"a\") appended 9 bytes: the file has 34");
  fseek (f, 0, SEEK_SET);
  fputs ("LINE", f);
  fflush (f);
  fseek (f, 0, SEEK_SET);
  CHECK (fgets (line, sizeof line, f) && !strcmp (line, "LINE one\n"), "fopen (\"r+\"): write over the start, read it back after fseek");
  fclose (f);
  CHECK (rename (TMP, TMP ".new") == 0 && access (TMP, F_OK) == -1 && access (TMP ".new", F_OK) == 0, "rename moves the file");
  CHECK (remove (TMP ".new") == 0 && access (TMP ".new", F_OK) == -1, "remove deletes it");
  errno = 0;
  CHECK (fopen ("/no/such/dir/file", "r") == 0 && errno == ENOENT && fopen ("/etc", "w") == 0, "fopen of a missing file: NULL and ENOENT; fopen of a directory for writing fails");
  f = fopen (TMP, "wb+");
  unsigned char bin[256];
  for (int i = 0; i < 256; i++) bin[i] = i;
  fwrite (bin, 1, 256, f);  fwrite (bin, 1, 256, f);
  rewind (f);
  unsigned char back[512];
  CHECK (fread (back, 1, 512, f) == 512 && !memcmp (back, bin, 256) && !memcmp (back + 256, bin, 256), "binary file with all 256 byte values, twice: written and read back unchanged (no line end conversion)");
  fclose (f);
  f = fopen (TMP, "w");
  setvbuf (f, 0, _IONBF, 0);
  fputs ("direct", f);
  CHECK (stat (TMP, &st) == 0 && st.st_size == 6, "setvbuf (_IONBF): the text is in the file at once, without fflush");
  fclose (f);
  f = fopen (TMP, "w");
  char big[10000];
  memset (big, 'x', sizeof big);
  CHECK (fwrite (big, 1, sizeof big, f) == sizeof big && fclose (f) == 0 && stat (TMP, &st) == 0 && st.st_size == 10000, "fwrite of 10000 bytes (bigger than the buffer), the file has 10000 bytes");
  f = fopen (TMP, "r");
  int c, lines = 0;
  size_t chars = 0;
  while ((c = getc (f)) != EOF) { chars++; if (c == 'x') lines++; }
  CHECK (chars == 10000 && lines == 10000, "getc reads the 10000 bytes one by one (a read system call per buffer, not per byte)");
  fclose (f);
  int fd = open (TMP, O_RDONLY);
  f = fdopen (fd, "r");
  CHECK (f && fgetc (f) == 'x', "fdopen turns a descriptor into a stream");
  fclose (f);
  f = tmpfile ();
  CHECK (f && fputs ("temp", f) >= 0 && (rewind (f), fgets (line, sizeof line, f)) && !strcmp (line, "temp"), "tmpfile: a temporary file that can be written and read");
  fclose (f);
  f = fopen (TMP, "w");
  fputs ("a\nbb\n\nlast", f);
  fclose (f);
  f = fopen (TMP, "r");
  char *lp = 0;
  size_t cap = 0;
  ssize_t l1 = getline (&lp, &cap, f), l2 = getline (&lp, &cap, f), l3 = getline (&lp, &cap, f), l4 = getline (&lp, &cap, f), l5 = getline (&lp, &cap, f);
  CHECK (l1 == 2 && l2 == 3 && l3 == 1 && l4 == 4 && l5 == -1, "getline: lengths 2, 3, 1 (empty line), 4 (last line without newline), then -1");
  free (lp);
  fclose (f);
  unlink (TMP);
  // stdio and the descriptors under it: a parent's unflushed buffer is not written twice by the child
  f = fopen (TMP, "w");
  fputs ("once ", f);
  pid_t pid = fork ();
  if (pid == 0) _exit (0);                                    // _exit does not flush
  waitpid (pid, 0, 0);
  fclose (f);
  f = fopen (TMP, "r");
  fgets (line, sizeof line, f);
  CHECK (!strcmp (line, "once "), "fork, then _exit in the child: the buffered text is written once (by the parent)");
  fclose (f);
  unlink (TMP);
  char *sbuf = 0;  size_t slen = 0;
  f = open_memstream (&sbuf, &slen);
  fprintf (f, "in %s", "memory");
  fclose (f);
  CHECK (sbuf && slen == 9 && !strcmp (sbuf, "in memory"), "open_memstream writes into memory (no system call)");
  free (sbuf);
}
static void test_process (void) {
  SECTION ("process");
  int st = 0, value = 10;
  pid_t me = getpid ();
  CHECK (me > 1 && getppid () > 0, "getpid, getppid");
  pid_t pid = fork ();
  if (pid == 0) { value = 99; _exit (3); }
  CHECK (pid > 0 && pid != me, "fork returns the child's pid in the parent");
  CHECK (waitpid (pid, &st, 0) == pid && WIFEXITED (st) && WEXITSTATUS (st) == 3 && !WIFSIGNALED (st), "waitpid returns the pid, the exit status is 3");
  CHECK (value == 10, "the child changed its copy of a variable: the parent still has the old value (separate address spaces)");
  errno = 0;
  CHECK (waitpid (-1, &st, 0) == -1 && errno == ECHILD, "wait with no child left: -1 and ECHILD");
  // three children, wait () collects all of them, each exactly once
  pid_t kid[3];
  for (int i = 0; i < 3; i++) { kid[i] = fork (); if (kid[i] == 0) _exit (10 + i); }
  int seen = 0, sum = 0;
  for (int i = 0; i < 3; i++) { pid_t w = wait (&st); for (int k = 0; k < 3; k++) if (w == kid[k] && WEXITSTATUS (st) == 10 + k) { seen |= 1 + k * k; sum++; } }
  CHECK (sum == 3 && seen == (1 | 2 | 5), "three children: wait returns each pid once, with that child's status");
  // WNOHANG: the child is still sleeping
  pid = fork ();
  if (pid == 0) { struct timespec t = { 0, 300 * 1000000 }; nanosleep (&t, 0); _exit (0); }
  CHECK (waitpid (pid, &st, WNOHANG) == 0, "waitpid (WNOHANG) of a child that still runs returns 0");
  CHECK (waitpid (pid, &st, 0) == pid && WIFEXITED (st), "then waitpid waits for it");
  // a lot of data: the child writes to the pipe and the parent reads until end of file
  int p[2];
  pipe (p);
  pid = fork ();
  if (pid == 0) {
    close (p[0]);
    char chunk[1000];
    memset (chunk, 'p', sizeof chunk);
    for (int i = 0; i < 100; i++) if (write (p[1], chunk, sizeof chunk) != sizeof chunk) _exit (1);
    _exit (0);
  }
  close (p[1]);
  char buf[4096];
  long total = 0, n;
  while ((n = read (p[0], buf, sizeof buf)) > 0) total += n;
  close (p[0]);
  waitpid (pid, &st, 0);
  CHECK (total == 100000 && WEXITSTATUS (st) == 0, "pipe: 100000 bytes (more than the pipe holds) arrive, read returns 0 when the child has exited");
  // execve: the child starts this program again with the argument "child"; its output goes into a pipe
  pipe (p);
  pid = fork ();
  if (pid == 0) {
    close (p[0]);
    dup2 (p[1], 1);  close (p[1]);
    char *argv[] = { "libctest", "child", "arg two", 0 };
    char *envp[] = { "CHILDVAR=yes", 0 };
    execve (SELF, argv, envp);
    _exit (99);
  }
  close (p[1]);
  char out[200];
  n = read (p[0], out, sizeof out - 1);
  out[n > 0 ? n : 0] = 0;
  close (p[0]);
  waitpid (pid, &st, 0);
  CHECK (WIFEXITED (st) && WEXITSTATUS (st) == 7, "execve: the new program ran and its exit status (7) arrives");
  CHECK (!strcmp (out, "child: argc=3 argv1=child argv2=arg two CHILDVAR=yes\n"), "execve: the new program got argv and the new environment, its standard output went through dup2 into the pipe");
  pid = fork ();
  if (pid == 0) { char *argv[] = { "x", 0 }; execv ("/no/such/program", argv); _exit (errno == ENOENT ? 42 : 43); }
  waitpid (pid, &st, 0);
  CHECK (WIFEXITED (st) && WEXITSTATUS (st) == 42, "execv of a missing program: returns -1 with ENOENT, the old program continues");
  pid = fork ();
  if (pid == 0) { char *argv[] = { "x", 0 }; execv ("/etc/rc.conf", argv); _exit (errno == ENOEXEC || errno == EACCES ? 42 : 43); }
  waitpid (pid, &st, 0);
  CHECK (WIFEXITED (st) && WEXITSTATUS (st) == 42, "execv of a file that is not a program: ENOEXEC or EACCES");
  // descriptors are inherited by fork and exec
  int fd = open (TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  pid = fork ();
  if (pid == 0) { write (fd, "child ", 6); _exit (0); }
  waitpid (pid, &st, 0);
  write (fd, "parent", 6);
  close (fd);
  char both[32] = "";
  fd = open (TMP, O_RDONLY);
  n = read (fd, both, sizeof both - 1);
  close (fd);
  unlink (TMP);
  CHECK (n == 12 && !memcmp (both, "child parent", 12), "an open file is shared by parent and child: they use one file offset (the child's text comes first, the parent's after it)");
  // the stack and the heap are copied: a lot of data in both
  static char heap[200000];
  memset (heap, 'h', sizeof heap);
  pid = fork ();
  if (pid == 0) { int ok = heap[0] == 'h' && heap[sizeof heap - 1] == 'h'; heap[1000] = 'c'; _exit (ok ? 0 : 1); }
  waitpid (pid, &st, 0);
  CHECK (WEXITSTATUS (st) == 0 && heap[1000] == 'h', "a fork with 200 KByte of data: the child sees the data, its change stays in the child");
  // many forks one after the other: the kernel must give back what a process used
  int okcount = 0;
  for (int i = 0; i < 40; i++) { pid = fork (); if (pid == 0) _exit (i); if (pid > 0 && waitpid (pid, &st, 0) == pid && WEXITSTATUS (st) == i) okcount++; }
  CHECK (okcount == 40, "40 fork and wait in a row: every child ended with its own status");
  CHECK (getcwd (buf, sizeof buf) != 0 && buf[0] == '/', "getcwd");
  CHECK (chdir ("/etc") == 0 && getcwd (buf, sizeof buf) && !strcmp (buf, "/etc") && access ("rc.conf", F_OK) == 0, "chdir, then a relative name is found there");
  chdir ("/");
}

static int child_main (int argc, char **argv) {
  const char *v = getenv ("CHILDVAR");
  printf ("child: argc=%d argv1=%s argv2=%s CHILDVAR=%s\n", argc, argc > 1 ? argv[1] : "-", argc > 2 ? argv[2] : "-", v ? v : "-");
  return 7;
}
static volatile sig_atomic_t got, got_info, got_code;
static void on_signal (int sig) { got = sig; }
static void on_info (int sig, siginfo_t *si, void *ctx) { got_info = si->si_signo; got_code = si->si_code; got = sig; }

static void test_signals (void) {
  SECTION ("signals");
  struct sigaction sa;
  memset (&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;
  sigemptyset (&sa.sa_mask);
  CHECK (sigaction (SIGUSR1, &sa, 0) == 0, "sigaction (SIGUSR1)");
  got = 0;
  CHECK (raise (SIGUSR1) == 0 && got == SIGUSR1, "raise (SIGUSR1): the handler ran before raise returned, with the signal number");
  got = 0;
  CHECK (kill (getpid (), SIGUSR1) == 0 && got == SIGUSR1, "kill (getpid (), SIGUSR1) to itself: the handler ran");
  got = 0;
  for (int i = 0; i < 3; i++) raise (SIGUSR1);
  CHECK (got == SIGUSR1, "the handler can run many times (the frame on the stack is taken away each time)");
  // blocked: stays pending, is delivered at unblock
  sigset_t set, pend, old;
  sigemptyset (&set);  sigaddset (&set, SIGUSR1);
  got = 0;
  sigprocmask (SIG_BLOCK, &set, &old);
  raise (SIGUSR1);
  sigpending (&pend);
  CHECK (got == 0 && sigismember (&pend, SIGUSR1) == 1, "blocked signal: no handler, and sigpending reports it");
  sigprocmask (SIG_SETMASK, &old, 0);
  CHECK (got == SIGUSR1, "sigprocmask (unblock) delivers the pending signal at once");
  // two signals at once are only remembered once (standard signals do not queue)
  sigprocmask (SIG_BLOCK, &set, &old);
  raise (SIGUSR1);  raise (SIGUSR1);
  got = 0;
  sigprocmask (SIG_SETMASK, &old, 0);
  CHECK (got == SIGUSR1, "two raise calls while blocked: delivered after the unblock");
  // SA_SIGINFO
  struct sigaction si;
  memset (&si, 0, sizeof si);
  si.sa_sigaction = on_info;
  si.sa_flags = SA_SIGINFO;
  sigemptyset (&si.sa_mask);
  sigaction (SIGUSR2, &si, 0);
  got = got_info = 0;
  kill (getpid (), SIGUSR2);
  CHECK (got == SIGUSR2 && got_info == SIGUSR2, "SA_SIGINFO: the handler gets a siginfo_t with si_signo");
  CHECK (got_code == SI_USER, "SA_SIGINFO: si_code is SI_USER for kill");
  // ignore
  signal (SIGUSR1, SIG_IGN);
  got = 0;
  raise (SIGUSR1);
  CHECK (got == 0, "SIG_IGN: the signal is thrown away");
  CHECK (signal (SIGUSR1, SIG_DFL) == SIG_IGN, "signal () returns the old disposition");
  errno = 0;
  CHECK (sigaction (SIGKILL, &sa, 0) == -1 && errno == EINVAL, "a handler for SIGKILL is refused: EINVAL");
  errno = 0;
  CHECK (kill (getpid (), 0) == 0 && kill (999999, 0) == -1 && errno == ESRCH, "kill (pid, 0) tests if a process exists: ESRCH for a pid that does not");
  // alarm interrupts sleep
  sa.sa_handler = on_signal;
  sigaction (SIGALRM, &sa, 0);
  got = 0;
  alarm (1);
  time_t t0 = time (0);
  unsigned left = sleep (10);
  CHECK (got == SIGALRM && left > 5 && time (0) - t0 < 5, "alarm (1): SIGALRM ends sleep (10) early, sleep returns the seconds that are left");
  got = 0;
  alarm (1);
  CHECK (alarm (0) <= 1 && (sleep (2), got == 0), "alarm (0) cancels a pending alarm");
  // EINTR: a signal ends a blocking read; SA_RESTART restarts it
  int p[2];
  pipe (p);
  got = 0;
  struct itimerval it = { { 0, 0 }, { 0, 200000 } };
  setitimer (ITIMER_REAL, &it, 0);
  char c;
  errno = 0;
  long r = read (p[0], &c, 1);
  CHECK (r == -1 && errno == EINTR && got == SIGALRM, "a signal ends a blocking read: -1 and EINTR (setitimer, 200 ms)");
  sa.sa_flags = SA_RESTART;
  sigaction (SIGALRM, &sa, 0);
  got = 0;
  pid_t pid = fork ();
  if (pid == 0) { struct timespec t = { 0, 400 * 1000000 }; nanosleep (&t, 0); write (p[1], "x", 1); _exit (0); }
  setitimer (ITIMER_REAL, &it, 0);
  r = read (p[0], &c, 1);
  CHECK (r == 1 && c == 'x' && got == SIGALRM, "with SA_RESTART the read goes on after the handler, and gets the data of the child");
  waitpid (pid, 0, 0);
  sa.sa_flags = 0;
  sigaction (SIGALRM, &sa, 0);
  // sigsuspend and a signal from a child (SIGCHLD)
  sa.sa_handler = on_signal;
  sigaction (SIGCHLD, &sa, 0);
  sigemptyset (&set);  sigaddset (&set, SIGCHLD);
  sigprocmask (SIG_BLOCK, &set, &old);
  got = 0;
  pid = fork ();
  if (pid == 0) _exit (0);
  sigset_t wait_mask = old;
  sigdelset (&wait_mask, SIGCHLD);
  while (got != SIGCHLD) sigsuspend (&wait_mask);
  CHECK (got == SIGCHLD, "sigsuspend waits until SIGCHLD arrives (a child has ended)");
  waitpid (pid, 0, 0);
  sigprocmask (SIG_SETMASK, &old, 0);
  signal (SIGCHLD, SIG_DFL);
  // the default action
  pid = fork ();
  if (pid == 0) { signal (SIGTERM, SIG_DFL); for (;;) pause (); }
  struct timespec nap = { 0, 100 * 1000000 };
  nanosleep (&nap, 0);
  kill (pid, SIGTERM);
  int st = 0;
  CHECK (waitpid (pid, &st, 0) == pid && WIFSIGNALED (st) && WTERMSIG (st) == SIGTERM, "SIGTERM with the default action kills the child, waitpid reports WIFSIGNALED and WTERMSIG");
  pid = fork ();
  if (pid == 0) { for (;;) pause (); }
  nanosleep (&nap, 0);
  kill (pid, SIGKILL);
  CHECK (waitpid (pid, &st, 0) == pid && WIFSIGNALED (st) && WTERMSIG (st) == SIGKILL, "SIGKILL ends a child that sleeps in pause ()");
  pid = fork ();
  if (pid == 0) { sa.sa_handler = on_signal; sigaction (SIGUSR1, &sa, 0); got = 0; while (!got) pause (); _exit (got == SIGUSR1 ? 21 : 22); }
  nanosleep (&nap, 0);
  kill (pid, SIGUSR1);
  CHECK (waitpid (pid, &st, 0) == pid && WIFEXITED (st) && WEXITSTATUS (st) == 21, "a signal to a child with a handler: pause returns after the handler, the child exits normally");
  pid = fork ();
  if (pid == 0) { signal (SIGSEGV, SIG_DFL); volatile int *bad = (int *) 0; *bad = 1; _exit (0); }
  CHECK (waitpid (pid, &st, 0) == pid && WIFSIGNALED (st) && WTERMSIG (st) == SIGSEGV, "a write through a null pointer ends the process with SIGSEGV (the fault becomes a signal)");
  pid = fork ();
  if (pid == 0) { signal (SIGABRT, SIG_DFL); abort (); }
  CHECK (waitpid (pid, &st, 0) == pid && WIFSIGNALED (st) && WTERMSIG (st) == SIGABRT, "abort () ends the process with SIGABRT");
}
int main (int argc, char **argv) {
  if (argc > 1 && !strcmp (argv[1], "child")) return child_main (argc, argv);
  setvbuf (stdout, 0, _IOLBF, 0);                               // one line at a time: a crash in a later section does not lose the earlier lines
  test_string ();
  test_ctype ();
  test_errno ();
  test_stdlib ();
  test_atexit ();
  test_math ();
  test_time ();
  test_malloc ();
  test_setjmp ();
  test_printf ();
  test_stdio ();
  test_process ();
  test_signals ();
  printf ("libctest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
