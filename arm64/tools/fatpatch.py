#!/usr/bin/env python3
"""Fills in the FS_FAT cases of the book's file system layer in gen/ulix-port.c (chapter "Mounting", lit/fat.nw).

The book's functions u_open, u_read, ... have a switch on the file system type with a case FS_FAT that says "not implemented" (the book
only explains how FAT could be added). This script replaces that line in each function by a call of the function of fatvfs.c.
usage: fatpatch.py gen/ulix-port.c
"""
import re, sys

CALLS = {                      # function of the book -> the text that replaces its "case FS_FAT: return -1;" line
    'book_u_open':      'case FS_FAT:    fd = fatfs_open (device, localpath, oflag);  if (fd == -1) return -1;  else return (fs << 8) + fd;',
    'book_u_read':      'case FS_FAT:   return fatfs_read (localfd, buf, nbyte);',
    'book_u_write':     'case FS_FAT:   return fatfs_write (localfd, buf, nbyte);',
    'book_u_close':     'case FS_FAT:   return fatfs_close (localfd);',
    'u_lseek':          'case FS_FAT:   return fatfs_lseek (localfd, offset, whence);',
    'book_u_unlink':    'case FS_FAT:   return fatfs_unlink (device, localpath);',
    'book_u_ftruncate': 'case FS_FAT:   return fatfs_ftruncate (localfd, length);',
    'book_u_stat':      'case FS_FAT:   return fatfs_stat (device, localpath, buf);',
    'book_u_mkdir':     'case FS_FAT:   return fatfs_mkdir (device, localpath);',
    'book_u_rmdir':     'case FS_FAT:   return fatfs_rmdir (device, localpath);',
    'book_u_getdent':   'case FS_FAT:   return fatfs_getdent (device, localpath, index, buf);',
    'book_u_reopen':    'case FS_FAT:   { int k = fatfs_reopen (localfd);  return k < 0 ? -1 : (fs << 8) + k; }',
    'book_u_chown':     'case FS_FAT:   return 0;                  // a FAT volume has no owners: nothing to change',
    'book_u_chmod':     'case FS_FAT:   return 0;                  // ... and no permissions',
}

path = sys.argv[1]
lines = open(path).read().split('\n')
func = None
done = set()
out = []
for line in lines:
    m = re.match(r'^(?:int|boolean|void|long|unsigned|static|struct)[^;]*?\b(\w+) \(.*\) *\{ *$', line)
    if m: func = m.group(1)
    if re.match(r'^\s*case FS_FAT:', line) and func in CALLS:
        out.append('    ' + CALLS[func])
        done.add(func)
        continue
    out.append(line)
missing = set(CALLS) - done
if missing: sys.exit('fatpatch: no FS_FAT case found in ' + ', '.join(sorted(missing)))
declarations = '''
// the functions of fatvfs.c (chapter "Mounting")
extern int fatfs_open (int device, const char *path, int oflag), fatfs_read (int fd, void *buf, int n), fatfs_write (int fd, void *buf, int n), fatfs_close (int fd);
extern int fatfs_lseek (int fd, int off, int whence), fatfs_unlink (int device, const char *path), fatfs_ftruncate (int fd, int length), fatfs_reopen (int fd);
extern int fatfs_stat (int device, const char *path, struct stat *st), fatfs_mkdir (int device, const char *path), fatfs_rmdir (int device, const char *path);
extern int fatfs_getdent (int device, const char *path, int index, struct dir_entry *buf);
'''
text = '\n'.join(out)
marker = '// function: get_dev_and_path'
assert marker in text
text = text.replace(marker, declarations + '\n' + marker, 1)
open(path, 'w').write(text)
print('fatpatch: %d functions' % len(done))
