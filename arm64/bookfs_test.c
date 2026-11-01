// Ulix ARM64 -- test of the Minix file system code that is compiled unchanged from ulix-book.nw
// (mx_open, mx_read, mx_write, ...) on top of the virtio disk.  The results are checked here
// and, for the files that stay on the disk, once more on the host by tools/minix_check.py.

#include "ulix-api.h"

#define DEV DEV_HDA

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf ("  ok:   %s\n", msg); \
                              else { printf ("  FAIL: %s\n", msg); fails++; } } while (0)

static byte buf[4096];

static unsigned pattern (unsigned i) { return (i * 31 + 7) & 0xff; }

// write n bytes of the pattern in chunks of 'chunk' bytes
static long write_pattern (int mfd, unsigned n, unsigned chunk) {
  unsigned done = 0;
  while (done < n) {
    unsigned c = n - done < chunk ? n - done : chunk;
    for (unsigned i = 0; i < c; i++) buf[i] = pattern (done + i);
    if (mx_write (mfd, buf, c) != (int)c) return -1;
    done += c;
  }
  return done;
}

// compare file contents with the pattern, return the number of bytes that were read
static long verify_pattern (int mfd, unsigned n) {
  unsigned done = 0;
  while (done < n) {
    unsigned c = n - done < 1000 ? n - done : 1000;
    if (mx_read (mfd, buf, c) != (int)c) return -1;
    for (unsigned i = 0; i < c; i++) if (buf[i] != pattern (done + i)) return -2 - done - i;
    done += c;
  }
  return done;
}

static int free_blocks (void) {
  struct diskfree_query q;
  q.device = DEV;
  mx_diskfree (&q);
  return q.free;
}

// mx_getdent returns every 32-byte slot of the directory, including unused ones (inode 0)
static int count_entries (const char *dir) {
  struct dir_entry e;
  int n = 0, used = 0;
  while (mx_getdent (DEV, dir, n, &e) == 0) { n++; if (e.inode) used++; }
  return used;
}

int bookfs_test (void) {
  struct stat st;
  fails = 0;
  int mfd;

  // ---- reading what the x86 version of Ulix put on the disk ----
  mfd = mx_open (DEV, "/hello.c", O_RDONLY);
  CHECK (mfd >= 0, "mx_open (/hello.c)");
  CHECK (mx_read (mfd, buf, 4096) == 316, "mx_read returns the 316 bytes of /hello.c");
  unsigned sum = 0; for (int i = 0; i < 316; i++) sum += buf[i];
  CHECK (sum == 22888, "checksum of /hello.c matches the host");
  CHECK (mx_read (mfd, buf, 100) == 0, "mx_read at end of file returns 0");
  mx_close (mfd);
  CHECK (mx_stat (DEV, "/hello.c", &st) == 0 && st.st_size == 316 && (st.st_mode & S_IFMT) == S_IFREG,
         "mx_stat: regular file, 316 bytes");
  CHECK (mx_stat (DEV, "/bin", &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR, "mx_stat: /bin is a directory");
  CHECK (mx_stat (DEV, "/nonexistent", &st) == -1, "mx_stat: missing file");
  CHECK (count_entries ("/") == 24, "mx_getdent: / has 24 used entries (16 from x86 Ulix + 8 AArch64 files)");
  CHECK (count_entries ("/bin") == 63, "mx_getdent: /bin has 63 entries");
  CHECK (mx_file_exists (DEV, "/bin/vi") && mx_file_is_directory (DEV, "/bin") &&
         !mx_file_is_directory (DEV, "/bin/vi"), "mx_file_exists / mx_file_is_directory");

  int free0 = free_blocks ();
  printf ("  info: %d free blocks before the write tests\n", free0);

  // ---- create and write a file that stays on the disk (checked on the host later) ----
  mfd = mx_open (DEV, "/keep.bin", O_CREAT | O_RDWR);
  CHECK (mfd >= 0, "mx_open (O_CREAT) creates /keep.bin");
  CHECK (write_pattern (mfd, 40000, 700) == 40000, "mx_write: 40000 bytes in 700-byte chunks (uses the indirect zone)");
  mx_close (mfd);
  CHECK (mx_stat (DEV, "/keep.bin", &st) == 0 && st.st_size == 40000 && st.st_nlink == 1, "size 40000, 1 link");
  mfd = mx_open (DEV, "/keep.bin", O_RDONLY);
  CHECK (verify_pattern (mfd, 40000) == 40000, "read back: every byte matches the pattern");
  CHECK (mx_lseek (mfd, 12345, SEEK_SET) == 12345 && mx_read (mfd, buf, 8) == 8 && buf[0] == pattern (12345),
         "mx_lseek + mx_read in the middle of the file");
  mx_close (mfd);

  // ---- overwrite in the middle, truncate, extend ----
  mfd = mx_open (DEV, "/keep.bin", O_RDWR);
  mx_lseek (mfd, 5000, SEEK_SET);
  for (int i = 0; i < 100; i++) buf[i] = 0xAA;
  CHECK (mx_write (mfd, buf, 100) == 100, "overwrite 100 bytes at offset 5000");
  mx_lseek (mfd, 4990, SEEK_SET);
  mx_read (mfd, buf, 120);
  CHECK (buf[0] == pattern (4990) && buf[10] == 0xAA && buf[109] == 0xAA && buf[110] == pattern (5100),
         "only those 100 bytes changed");
  mx_lseek (mfd, 5000, SEEK_SET);                       // restore the pattern for the host check
  for (int i = 0; i < 100; i++) buf[i] = pattern (5000 + i);
  mx_write (mfd, buf, 100);
  mx_close (mfd);

  // ---- a temporary file that is deleted again: all blocks must come back ----
  mfd = mx_open (DEV, "/tmp.bin", O_CREAT | O_RDWR);
  CHECK (write_pattern (mfd, 20000, 1024) == 20000, "second file, 20000 bytes");
  CHECK (mx_ftruncate (mfd, 3000) == 0, "mx_ftruncate to 3000 bytes");
  mx_close (mfd);
  CHECK (mx_stat (DEV, "/tmp.bin", &st) == 0 && st.st_size == 3000, "size is now 3000");
  mfd = mx_open (DEV, "/tmp.bin", O_RDONLY);
  CHECK (verify_pattern (mfd, 3000) == 3000, "the first 3000 bytes survived the truncation");
  mx_close (mfd);
  CHECK (mx_unlink (DEV, "/tmp.bin") == 0 && !mx_file_exists (DEV, "/tmp.bin"), "mx_unlink");

  // ---- big file through the double indirect zone (> 7 + 256 KByte) ----
  mfd = mx_open (DEV, "/big.bin", O_CREAT | O_RDWR);
  CHECK (write_pattern (mfd, 300000, 4096) == 300000, "300000 bytes (needs the double indirect zone)");
  mx_close (mfd);
  mfd = mx_open (DEV, "/big.bin", O_RDONLY);
  CHECK (verify_pattern (mfd, 300000) == 300000, "read back: 300000 bytes match");
  mx_close (mfd);
  CHECK (mx_unlink (DEV, "/big.bin") == 0, "delete the big file again");

  // ---- directories and links ----
  CHECK (mx_mkdir (DEV, "/d1", 0755) == 0 && mx_file_is_directory (DEV, "/d1"), "mx_mkdir /d1");
  CHECK (count_entries ("/d1") == 2, "new directory contains only . and ..");
  mfd = mx_open (DEV, "/d1/inner.txt", O_CREAT | O_RDWR);
  static const char text[] = "hello from the book's mx_write on AArch64\n";
  CHECK (mx_write (mfd, (void *)text, sizeof text - 1) == (int)sizeof text - 1, "write a text file inside /d1");
  mx_close (mfd);
  CHECK (mx_link (DEV, "/d1/inner.txt", "/d1/hard.txt") == 0, "mx_link: hard link");
  CHECK (mx_stat (DEV, "/d1/hard.txt", &st) == 0 && st.st_nlink == 2 && st.st_size == sizeof text - 1,
         "the link shares inode and size, link count 2");
  CHECK (mx_unlink (DEV, "/d1/hard.txt") == 0 && mx_stat (DEV, "/d1/inner.txt", &st) == 0 && st.st_nlink == 1,
         "unlink one name: the other stays, link count 1");
  CHECK (mx_mkdir (DEV, "/d1/sub", 0755) == 0 && mx_file_is_directory (DEV, "/d1/sub"), "nested directory /d1/sub");
  // (mx_mkdir itself does not check whether the directory exists; u_mkdir above it does)
  CHECK (mx_open (DEV, "/missing/x", O_RDONLY) == -1, "open in a missing directory fails");

  // ---- a new file that gets the inode number of a deleted one must start empty ----
  {
    int other = mx_open (DEV, "/hello.c", O_RDONLY);       // some file must be open, otherwise mx_open skips the table lookup
    mfd = mx_open (DEV, "/reuse1.bin", O_CREAT | O_RDWR);
    write_pattern (mfd, 5000, 1000);
    mx_stat (DEV, "/reuse1.bin", &st);
    int first_ino = st.st_ino;
    mx_close (mfd);
    mx_unlink (DEV, "/reuse1.bin");
    mfd = mx_open (DEV, "/reuse2.bin", O_CREAT | O_RDWR);
    mx_stat (DEV, "/reuse2.bin", &st);
    int second_ino = st.st_ino;
    int size_now = mx_lseek (mfd, 0, SEEK_END);
    mx_close (mfd);
    mx_unlink (DEV, "/reuse2.bin");
    mx_close (other);
    CHECK (second_ino == first_ino, "the second file reuses the inode number of the deleted one");
    CHECK (size_now == 0, "... and starts with size 0 (no stale cached inode of the old file)");
  }

  // ---- block accounting: only keep.bin, d1, inner.txt and d1/sub may remain ----
  int free1 = free_blocks ();
  int expected = (40000 + 1023) / 1024 + 1                 // keep.bin: data + indirect block
               + 1 + 1                                      // /d1 and /d1/sub: one directory block each
               + 1;                                         // inner.txt
  printf ("  info: %d free blocks after (expected %d fewer than before)\n", free1, expected);
  CHECK (free0 - free1 == expected, "free block count matches exactly what the remaining files need");

  return fails;
}
