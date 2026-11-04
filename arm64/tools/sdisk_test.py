#!/usr/bin/env python3
"""Test of the serial disk: usage: sdisk_test.py KERNEL.elf SYSTEM_DISK.img SERIAL_DISK.img

Starts qemu with a second serial port that is a TCP socket, serves a copy of SERIAL_DISK.img on it (tools/serial_disk.py), lets the
kernel run its test suites (which use /dev/sdisk when they find the second UART) and afterwards checks the copy on the host."""
import os, shutil, subprocess, sys, tempfile, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_disk

kernel, sysdisk, sdisk = sys.argv[1:4]
port = int(os.environ.get("QEMU_PORT", 45131))
tmp = tempfile.mkdtemp()
img = os.path.join(tmp, "sdisk.img"); log = os.path.join(tmp, "console.log")
shutil.copy(sdisk, img)
orig = open(img, "rb").read()

qemu = subprocess.Popen(["qemu-system-aarch64", "-M", os.environ.get("QEMU_M", "virt"), "-cpu", "cortex-a72", "-m", "128", "-display", "none",
    "-monitor", "none", "-serial", "file:" + log, "-serial", "tcp:127.0.0.1:%d,server=on,wait=on" % port, "-kernel", kernel, "-snapshot",
    "-drive", "file=%s,format=raw,if=none,id=hd0" % sysdisk, "-device", "virtio-blk-device,drive=hd0"] + os.environ.get("QEMU_EXTRA", "").split(),
    stderr=subprocess.DEVNULL)
result = {}
def run():
    s = serial_disk.connect(port)
    result["stats"] = serial_disk.serve(s, img)
t = threading.Thread(target=run, daemon=True); t.start()
text = ""
for _ in range(int(os.environ.get('SDISK_WAIT', 240))):
    time.sleep(1)
    text = open(log, errors="replace").read() if os.path.exists(log) else ""
    if "\nRESULT" in text: break
qemu.kill(); qemu.wait()
t.join(5)

fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

sect = text.split("serial disk (second PL011")[1] if "serial disk (second PL011" in text else ""
check("serial disk (second PL011" in text, "the kernel found the second UART and ran the serial disk tests")
check(sect.count("  ok:") >= 7 and "FAIL" not in text, "all kernel checks passed (%d ok in the serial disk section)" % sect.count("  ok:"))
check("RESULT: all checks passed" in text, "RESULT line: all checks passed")
data = open(img, "rb").read()
check(len(data) == len(orig), "image size unchanged")
want1439 = bytes((i * 13 + 5) & 0xff for i in range(1024))
want1438 = bytes((i * 3 + 9) & 0xff for i in range(1024))
check(data[1439 * 1024:1440 * 1024] == want1439, "host: block 1439 contains what the kernel wrote with writeblock_serial")
check(data[1438 * 1024:1439 * 1024] == want1438, "host: block 1438 contains what the kernel wrote through the buffer cache")
check(orig[1:1024 * 1] == data[1:1024], "host: block 0 unchanged")
if "stats" in result:
    r, w, e = result["stats"]
    print("  info: served %d reads, %d writes, %d errors" % (r, w, e))
    check(e == 0 and r >= 12 and w >= 2, "no protocol errors, enough requests seen")
# the file system of the copy must be consistent and contain the new file (checked by the independent Python parser)
chk = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "minix_check.py"), img, "--files"], capture_output=True, text=True)
check("PROBLEM:" not in chk.stdout and "new.txt" in chk.stdout, "host: minix_check finds a consistent file system with /new.txt")
if fails: print("--- end of the console log ---\n" + "\n".join(text.splitlines()[-25:]))
shutil.rmtree(tmp, ignore_errors=True)
print("sdisk test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
