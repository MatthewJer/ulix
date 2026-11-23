#!/bin/sh
# stress.sh image : many files, a deep directory, rename, delete, truncate; checks content
img=$1
./fhost $img 0 mkdir /many >/dev/null
for i in $(seq 1 120); do echo "content $i" > f.txt; ./fhost $img 0 put "/many/File number $i with a longish name.txt" f.txt >/dev/null || echo "put $i failed"; done
n=$(./fhost $img 0 ls /many | grep -c "File number"); echo "listed $n of 120"
./fhost $img 0 cat "/many/File number 77 with a longish name.txt"
./fhost $img 0 mv "/many/File number 77 with a longish name.txt" "/many/renamed77.txt"
./fhost $img 0 cat /many/renamed77.txt
for i in $(seq 1 60); do ./fhost $img 0 rm "/many/File number $i with a longish name.txt" >/dev/null; done
n=$(./fhost $img 0 ls /many | grep -c "File number"); echo "left $n (expect 59 + renamed)"
./fhost $img 0 mkdir /a >/dev/null; ./fhost $img 0 mkdir /a/b >/dev/null; ./fhost $img 0 mkdir /a/b/c >/dev/null
echo deep > f.txt; ./fhost $img 0 put /a/b/c/deep.txt f.txt >/dev/null; ./fhost $img 0 cat /a/b/c/deep.txt
./fhost $img 0 mv /a/b /moved_b; ./fhost $img 0 cat /moved_b/c/deep.txt
./fhost $img 0 rmdir /moved_b/c; ./fhost $img 0 rm /moved_b/c/deep.txt; ./fhost $img 0 rmdir /moved_b/c; ./fhost $img 0 rmdir /moved_b; ./fhost $img 0 rmdir /a
python3 -c "import os; open('b2.bin','wb').write(os.urandom(1234567))"
./fhost $img 0 put /b2.bin b2.bin; ./fhost $img 0 cat /b2.bin | cmp - b2.bin && echo "1.2MB OK"
./fhost $img 0 trunc /b2.bin 70000; ./fhost $img 0 cat /b2.bin | cmp - b2.bin 2>&1 | head -1; ./fhost $img 0 stat /b2.bin
./fhost $img 0 rm /b2.bin; ./fhost $img 0 put /b3.bin b2.bin; ./fhost $img 0 cat /b3.bin | cmp - b2.bin && echo "reuse OK"
