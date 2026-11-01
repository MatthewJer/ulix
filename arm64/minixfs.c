// Ulix ARM64 -- read-only Minix v2 file system on top of the block driver
// The same on-disk format as the x86 Ulix images (bin-build/minixdata.img):
// 1 KByte zones, 64-byte v2 inodes, 7 direct + 1 indirect + 1 double indirect zone.

#include "ulix.h"

#define MINIX2_14   0x2468
#define MINIX2_30   0x2478
#define ZONE_SIZE   1024

struct sb {
  unsigned short ninodes, nzones16, imap_blocks, zmap_blocks, firstdatazone, log_zone_size;
  u32 max_size;
  unsigned short magic, state;
  u32 zones;
};

struct inode {
  unsigned short mode, nlinks, uid, gid;
  u32 size, atime, mtime, ctime;
  u32 zone[10];
};

static struct sb sb;
static u64 inode_table_block;
static int dirent_size, name_len;

static int read_zone (u32 zone, void *buf) {
  return blk_read ((u64)zone * (ZONE_SIZE / 512), buf, ZONE_SIZE / 512);
}

int minix_mount (void) {
  static u8 b[ZONE_SIZE] __attribute__ ((aligned (16)));
  if (read_zone (1, b)) return -1;
  sb = *(struct sb *)b;
  if (sb.magic == MINIX2_30)      { dirent_size = 32; name_len = 30; }
  else if (sb.magic == MINIX2_14) { dirent_size = 16; name_len = 14; }
  else { printf ("minix: unknown magic 0x%x\n", sb.magic); return -1; }
  inode_table_block = 2 + sb.imap_blocks + sb.zmap_blocks;
  printf ("minix: v2, %u inodes, %u zones, %d-character names\n", sb.ninodes, sb.zones, name_len);
  return 0;
}

static int get_inode (u32 n, struct inode *out) {
  static u8 b[ZONE_SIZE] __attribute__ ((aligned (16)));
  if (n == 0 || n > sb.ninodes) return -1;
  u32 idx = n - 1;                          // 16 inodes of 64 bytes per zone
  if (read_zone (inode_table_block + idx / 16, b)) return -1;
  u8 *p = b + (idx % 16) * 64;
  for (int i = 0; i < 64; i++) ((u8 *)out)[i] = p[i];
  return 0;
}

// zone number of the n-th zone of a file (0 = hole)
static u32 file_zone (struct inode *ino, u32 n) {
  static u32 ind[ZONE_SIZE / 4] __attribute__ ((aligned (16)));
  if (n < 7) return ino->zone[n];
  n -= 7;
  if (n < 256) {
    if (!ino->zone[7] || read_zone (ino->zone[7], ind)) return 0;
    return ind[n];
  }
  n -= 256;
  if (!ino->zone[8] || read_zone (ino->zone[8], ind)) return 0;
  u32 l1 = ind[n / 256];
  if (!l1 || read_zone (l1, ind)) return 0;
  return ind[n % 256];
}

// read up to len bytes at offset; returns number of bytes read
long minix_read (u32 ino_nr, u32 offset, void *buf, u32 len) {
  static u8 z[ZONE_SIZE] __attribute__ ((aligned (16)));
  struct inode ino;
  if (get_inode (ino_nr, &ino)) return -1;
  if (offset >= ino.size) return 0;
  if (len > ino.size - offset) len = ino.size - offset;
  u32 done = 0;
  while (done < len) {
    u32 pos = offset + done, zn = file_zone (&ino, pos / ZONE_SIZE);
    u32 in = pos % ZONE_SIZE, n = ZONE_SIZE - in;
    if (n > len - done) n = len - done;
    if (zn) { if (read_zone (zn, z)) return -1; }
    else    { for (int i = 0; i < ZONE_SIZE; i++) z[i] = 0; }
    for (u32 i = 0; i < n; i++) ((u8 *)buf)[done + i] = z[in + i];
    done += n;
  }
  return len;
}

u32 minix_size (u32 ino_nr) {
  struct inode ino;
  return get_inode (ino_nr, &ino) ? 0 : ino.size;
}

static int streq (const char *a, const char *b, int n) {
  for (int i = 0; i < n; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; }
  return 1;
}

// iterate over a directory: call fn for every used entry
int minix_readdir (u32 dir, void (*fn)(u32 ino, const char *name, void *arg), void *arg) {
  static u8 ent[64];
  struct inode d;
  if (get_inode (dir, &d) || (d.mode & 0170000) != 0040000) return -1;
  int count = 0;
  for (u32 off = 0; off + dirent_size <= d.size; off += dirent_size) {
    if (minix_read (dir, off, ent, dirent_size) != dirent_size) return -1;
    u32 nr = ent[0] | (ent[1] << 8);
    if (!nr) continue;
    char name[32]; int i;
    for (i = 0; i < name_len && ent[2 + i]; i++) name[i] = ent[2 + i];
    name[i] = 0;
    if (fn) fn (nr, name, arg);
    count++;
  }
  return count;
}

struct lookup { const char *name; u32 found; };
static void lookup_cb (u32 ino, const char *name, void *arg) {
  struct lookup *l = arg;
  if (!l->found && streq (name, l->name, name_len + 1)) l->found = ino;
}

// resolve an absolute path ("/bin/vi") to an inode number, 0 if not found
u32 minix_lookup (const char *path) {
  u32 cur = 1;                              // root inode
  char part[32];
  while (*path) {
    while (*path == '/') path++;
    if (!*path) break;
    int n = 0;
    while (*path && *path != '/' && n < 31) part[n++] = *path++;
    part[n] = 0;
    struct lookup l = { part, 0 };
    if (minix_readdir (cur, lookup_cb, &l) < 0 || !l.found) return 0;
    cur = l.found;
  }
  return cur;
}

int minix_is_file (u32 ino_nr) {
  struct inode ino;
  return get_inode (ino_nr, &ino) == 0 && (ino.mode & 0170000) == 0100000;
}
