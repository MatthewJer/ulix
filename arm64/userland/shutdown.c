#include "../ulixlib.h"
#include "../arm64-extra.h"

// shutdown, poweroff, reboot -- stop the machine, or stop it and start it again (modelled on the programs of the BSDs, without most of
// their options). One program with three names: it looks at the name it was called with.
//
//     poweroff          switch the machine off
//     reboot            start the machine again
//     shutdown now      switch off          shutdown -r now     start again       (-h and -p mean "off", too)
//
// The data in the buffer cache is written to the disks first. Under qemu "off" ends qemu, which is the way out of "make run-shell"
// without any keys. Everybody may do it (this is a system for a single user at a time).

static int ends_with (char *s, char *tail) {
  int n = 0, m = 0;
  while (s[n]) n++;
  while (tail[m]) m++;
  if (m > n) return 0;
  for (int i = 0; i < m; i++) if (s[n - m + i] != tail[i]) return 0;
  return 1;
}

int main (int argc, char *argv[]) {
  int again = ends_with (argv[0], "reboot");
  for (int i = 1; i < argc; i++) if (argv[i][0] == '-' && argv[i][1] == 'r') again = 1;
  if (again) {
    printf ("Rebooting NOW!\n");
    reboot ();
    printf ("reboot: the machine cannot start itself again\n");
  } else {
    printf ("Shutdown NOW!\n");
    poweroff ();
    printf ("shutdown: the machine cannot be switched off\n");
  }
  return 1;
}
