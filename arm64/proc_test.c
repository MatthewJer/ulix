// Ulix ARM64 -- test of processes: system calls of the book (fork, exec, waitpid, exit, open, read,
// write, ...) with user programs that were compiled for AArch64 and are loaded from the disk.

#include "ulix-api.h"

extern int  arch_start_user (const char *path, int argc, char **argv);
extern int  arch_threads_reap (void);
extern void capture_start (void);
extern int  capture_stop (char **text);
extern long frames_in_use;
extern volatile int procs_killed;

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf ("  ok:   %s\n", msg); \
                              else { printf ("  FAIL: %s\n", msg); fails++; } } while (0)

static int count_in (const char *hay, const char *needle) {
  int n = 0, nl = strlen (needle);
  for (int i = 0; hay[i]; i++) {
    int j = 0;
    while (j < nl && hay[i + j] == needle[j]) j++;
    if (j == nl) n++;
  }
  return n;
}

static int live_threads (void) {
  int n = 0;
  for (thread_id t = 2; t < MAX_THREADS; t++) if (thread_table[t].used) n++;
  return n;
}

// start a program and wait until every process is gone; returns its pid (-1: could not be started)
static int run (const char *path, int argc, char **argv) {
  int pid = arch_start_user (path, argc, argv);
  if (pid < 0) return pid;
  while (live_threads () > 0) __asm__ volatile ("wfi");
  return pid;
}

int proc_test (void) {
  fails = 0;
  long frames_before = frames_in_use;
  char *out;
  int pid;

  char *hello_argv[] = { "hello64", "foo", "bar", 0 };
  capture_start ();
  pid = run ("/hello64", 3, hello_argv);
  capture_stop (&out);
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/hello64 started from the disk, exit status 0");
  CHECK (count_in (out, "hello from an AArch64 ELF") == 1 && count_in (out, "argv[2]=bar") == 1,
         "its output reached the console, argv delivered");
  CHECK (count_in (out, "user ok:") == 7 && count_in (out, "user FAIL") == 0,
         "7 checks inside the program passed: .data, .bss, .rodata, stack, argv");

  char *cat_argv[] = { "cat64", "/hello.c", 0 };
  capture_start ();
  pid = run ("/cat64", 2, cat_argv);
  capture_stop (&out);
  CHECK (thread_table[pid].exitcode == 0 && count_in (out, "-- 316 bytes") == 1 &&
         count_in (out, "#include \"../ulixlib.h\"") == 1,
         "/cat64 /hello.c: open/read/close system calls deliver the 316-byte file");

  cat_argv[1] = "/test.txt";
  capture_start ();
  pid = run ("/cat64", 2, cat_argv);
  capture_stop (&out);
  CHECK (thread_table[pid].exitcode == 0 && count_in (out, "-- 1680 bytes") == 1, "/cat64 /test.txt: 1680 bytes");

  cat_argv[1] = "/nonexistent";
  capture_start ();
  pid = run ("/cat64", 2, cat_argv);
  capture_stop (&out);
  CHECK (thread_table[pid].exitcode == 1 && count_in (out, "cannot open") == 1, "open of a missing file fails cleanly");

  char *crash_argv[] = { "crash64", 0 };
  int killed = procs_killed;
  capture_start ();
  pid = run ("/crash64", 1, crash_argv);
  capture_stop (&out);
  CHECK (procs_killed == killed + 1 && thread_table[pid].exitcode == -1 && count_in (out, "not reached") == 0,
         "write to the read-only code page: process killed, exit status -1, kernel keeps running");

  capture_start ();
  CHECK (arch_start_user ("/badarch64", 1, crash_argv) == -1, "ELF with e_machine = x86 is refused");
  CHECK (arch_start_user ("/badwx64", 1, crash_argv) == -1, "ELF with a writable+executable segment is refused");
  CHECK (arch_start_user ("/ascii.txt", 1, crash_argv) == -1, "another text file (/ascii.txt) is refused");
  CHECK (arch_start_user ("/hello.c", 1, crash_argv) == -1, "text file is refused");
  CHECK (arch_start_user ("/nonexistent", 1, crash_argv) == -1, "missing file is refused");
  capture_stop (&out);
  CHECK (count_in (out, "not an AArch64 executable") == 1 && count_in (out, "writable and executable") == 1 &&
         count_in (out, "not an ELF file") == 2, "each refusal printed the right reason");

  // fork, exec, waitpid, separate address spaces, several processes, grandchildren
  char *fork_argv[] = { "forktest64", 0 };
  capture_start ();
  pid = run ("/forktest64", 1, fork_argv);
  capture_stop (&out);
  printf ("%s", "");                                  // keep the output of the program out of the check lines
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/forktest64 finished with exit status 0 (no failed checks)");
  CHECK (count_in (out, "user FAIL") == 0, "no check failed inside any of the processes");
  CHECK (count_in (out, "user ok:") == 16, "16 checks passed: 9 in forktest64 and 7 in the exec'ed /hello64");
  CHECK (count_in (out, "worker 2 round 2") == 1 && count_in (out, "child: pid") == 1, "children printed their lines");
  CHECK (live_threads () == 0, "all processes are gone");

  // copy-on-write fork
  char *cow_argv[] = { "cowtest64", 0 };
  capture_start ();
  pid = run ("/cowtest64", 1, cow_argv);
  capture_stop (&out);
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/cowtest64 finished with exit status 0 (no failed checks)");
  CHECK (count_in (out, "user FAIL") == 0 && count_in (out, "user ok:") == 10, "all 10 copy-on-write checks passed");
  CHECK (live_threads () == 0, "no process is left");

  // threads: pthread_create/exit, mutexes, exit() while another thread runs
  char *th_argv[] = { "thtest64", 0 };
  capture_start ();
  pid = run ("/thtest64", 1, th_argv);
  capture_stop (&out);
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/thtest64 finished with exit status 0 (no failed checks)");
  CHECK (count_in (out, "user FAIL") == 0 && count_in (out, "user ok:") == 14, "all 14 thread checks passed");
  CHECK (live_threads () == 0, "all threads are gone, also the one that was spinning");

  // swapping: page_out / page_in, access by the kernel, fork, page replacement
  char *swap_argv[] = { "swaptest64", 0 };
  capture_start ();
  pid = run ("/swaptest64", 1, swap_argv);
  capture_stop (&out);
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/swaptest64 finished with exit status 0 (no failed checks)");
  CHECK (count_in (out, "user FAIL") == 0 && count_in (out, "user ok:") == 14, "all 14 swap checks passed (one of them is the line that write () printed from a paged-out page)");
  int swap_used = 0;
  for (int i = 0; i < MAX_SWAP_FRAMES; i++) if (paging[i].used) swap_used++;
  CHECK (swap_used == 0, "the swap file entries of the finished processes are released");
  CHECK (live_threads () == 0, "no process is left");

  // screen functions: clrscr, set_xy, get_xy, read_screen, write_screen, wrapping, scrolling
  char *scr_argv[] = { "screentest64", 0 };
  capture_start ();
  pid = run ("/screentest64", 1, scr_argv);
  capture_stop (&out);
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/screentest64 finished with exit status 0 (no failed checks)");
  CHECK (count_in (out, "user FAIL") == 0 && count_in (out, "user ok:") == 9, "all 9 screen checks passed");
  CHECK ((vt[0].mem[(3 * 80 + 2) * 2] == 'D') && (vt[0].mem[(3 * 80 + 5) * 2] == 'E') && vt[0].x == 6 && vt[0].y == 3,
         "the terminal model of the kernel holds the final screen: DONE at column 2 of line 3, cursor behind it");
  CHECK (count_in (out, "\033[") > 0, "the visible terminal was repainted on the console with ANSI sequences");
  CHECK (live_threads () == 0, "no process is left");

  // signals: handler with exact register restore, SIG_IGN, default action, SIGSTOP/SIGCONT/SIGKILL
  char *sig_argv[] = { "sigtest64", 0 };
  capture_start ();
  pid = run ("/sigtest64", 1, sig_argv);
  capture_stop (&out);
  CHECK (pid > 0 && thread_table[pid].exitcode == 0, "/sigtest64 finished with exit status 0 (no failed checks)");
  CHECK (count_in (out, "user FAIL") == 0 && count_in (out, "user ok:") == 8, "all 8 signal checks passed");
  CHECK (count_in (out, "Replacing signal 15") == 1, "the book's kill() turned SIGTERM into SIGKILL (no handler)");
  CHECK (live_threads () == 0, "no process is left, also not the ones killed by signals");

  arch_threads_reap ();
  CHECK (frames_in_use == frames_before, "no frame leaked: address spaces and kernel stacks freed");
  return fails;
}
