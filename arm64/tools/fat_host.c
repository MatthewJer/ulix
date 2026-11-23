// host test for fat.c: fat_host image [offset-sectors] command args...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fat.h"
static FILE *img; static long base;
int fat_dev_read (int disk, u64 lba, void *buf, u32 n) { fseek (img, base + lba * 512, SEEK_SET); return fread (buf, 512, n, img) == n ? 0 : -1; }
int fat_dev_write (int disk, u64 lba, const void *buf, u32 n) { fseek (img, base + lba * 512, SEEK_SET); return fwrite (buf, 512, n, img) == n ? 0 : -1; }
u32 fat_dev_time (void) { return 1700000000; }
void fat_dev_sync (int d) { fflush (img); }
int main (int argc, char **argv) {
  img = fopen (argv[1], "r+b"); int part = atoi (argv[2]);
  int v = fat_mount (0, part);
  if (v < 0) { printf ("mount failed %d\n", v); return 1; }
  const char *cmd = argv[3];
  if (!strcmp (cmd, "info")) { u64 t, f; u32 cb; fat_vol_space (v, &t, &f, &cb); printf ("label '%s' %s clusters %lu free %lu cluster %u\n", fat_vol_label (v), fat_vol_exfat (v) ? "exfat" : "fat32", t, f, cb); }
  else if (!strcmp (cmd, "ls")) { char name[256]; u32 ino; for (int i = 0; fat_getdent (v, argv[4], i, name, sizeof name, &ino) == 0; i++) { struct fat_stat st; char p[600]; snprintf (p, sizeof p, "%s/%s", strcmp (argv[4], "/") ? argv[4] : "", name); if (i >= 2 && fat_stat (v, p, &st) == 0) printf ("%c %10u %s\n", st.is_dir ? 'd' : '-', st.size, name); else printf ("d          0 %s\n", name); } }
  else if (!strcmp (cmd, "cat")) { int fd = fat_open (v, argv[4], 0); if (fd < 0) { printf ("open failed\n"); return 1; } char b[3000]; int n; while ((n = fat_read (fd, b, sizeof b)) > 0) fwrite (b, 1, n, stdout); fat_close (fd); }
  else if (!strcmp (cmd, "put")) { int fd = fat_open (v, argv[4], 1); if (fd < 0) { printf ("open failed\n"); return 1; } FILE *in = fopen (argv[5], "rb"); char b[1000]; size_t n; long tot = 0; while ((n = fread (b, 1, sizeof b, in)) > 0) { if (fat_write (fd, b, n) != (int) n) { printf ("write failed at %ld\n", tot); return 1; } tot += n; } fat_close (fd); printf ("wrote %ld\n", tot); }
  else if (!strcmp (cmd, "append")) { int fd = fat_open (v, argv[4], 1); fat_lseek (fd, 0, 2); fat_write (fd, argv[5], strlen (argv[5])); fat_close (fd); }
  else if (!strcmp (cmd, "mkdir")) printf ("mkdir %d\n", fat_mkdir (v, argv[4]));
  else if (!strcmp (cmd, "rm")) printf ("rm %d\n", fat_unlink (v, argv[4]));
  else if (!strcmp (cmd, "rmdir")) printf ("rmdir %d\n", fat_rmdir (v, argv[4]));
  else if (!strcmp (cmd, "mv")) printf ("mv %d\n", fat_rename (v, argv[4], argv[5]));
  else if (!strcmp (cmd, "trunc")) { int fd = fat_open (v, argv[4], 0); printf ("trunc %d\n", fat_ftruncate (fd, atol (argv[5]))); fat_close (fd); }
  else if (!strcmp (cmd, "stat")) { struct fat_stat st; int r = fat_stat (v, argv[4], &st); printf ("stat %d size %u dir %d mtime %u\n", r, st.size, st.is_dir, st.mtime); }
  fat_umount (v); fclose (img);
  return 0;
}
