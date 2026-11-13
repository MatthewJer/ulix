#!/usr/bin/env python3
"""Test of the telnet server of Ulix: usage: telnet_test.py ulix-shell.bin DISK.img

Boots the shell kernel with a network card. qemu forwards TCP port 5523 of the host to port 23 of the kernel (10.0.2.15).
Then this program does what a person with a telnet client does: connects, logs in, runs programs (ps, top), tries a second
connection, hangs up and connects again. A second boot with "telnetd_enable="NO"" in /etc/rc.conf checks that the server can
be switched off. The server is the program /bin/telnetd, started by the boot as the service telnetd (/etc/rc.d/telnetd).
"""
import os, re, shutil, socket, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

def boot(disk_image):
    log = os.path.join(tempfile.mkdtemp(), "console.log")
    qemu = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none",
        "-serial", "file:" + log, "-kernel", kernel, "-snapshot", "-drive", "file=%s,format=raw,if=none,id=hd0" % disk_image,
        "-device", "virtio-blk-device,drive=hd0",
        "-netdev", "user,id=n0,hostfwd=tcp:127.0.0.1:5523-10.0.2.15:23", "-device", "virtio-net-device,netdev=n0"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return qemu, log

def console(log): return open(log, errors="replace").read() if os.path.exists(log) else ""
def wait_log(log, text, seconds):
    for _ in range(seconds * 10):
        if text in console(log): return True
        time.sleep(0.1)
    return False

class Telnet:
    """A telnet client of the simplest kind: it collects what arrives (the commands IAC ... are cut out and kept apart)."""
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", 5523), timeout=5)
        self.sock.settimeout(0.2)
        self.text = ""
        self.commands = []
        self.raw = b""
    def pump(self, seconds=0.0):
        end = time.time() + seconds
        while True:
            try:
                data = self.sock.recv(4096)
                if not data: return False
                self.raw += data
                self.take(data)
            except socket.timeout:
                if time.time() >= end: return True
            except OSError:
                return False
    def take(self, data):
        i = 0
        while i < len(data):
            if data[i] == 255 and i + 2 < len(data) + 0 and data[i + 1] in (251, 252, 253, 254):
                self.commands.append((data[i + 1], data[i + 2])); i += 3
            else:
                self.text += chr(data[i]); i += 1
    def expect(self, pattern, seconds=60):
        end = time.time() + seconds
        while time.time() < end:
            if re.search(pattern, self.text): return True
            if not self.pump(0.3) and not re.search(pattern, self.text): return False
        return False
    def send(self, s): self.sock.sendall(s.encode() if isinstance(s, str) else s)
    def close(self): self.sock.close()

qemu, log = boot(disk)
try:
    check(wait_log(log, "ulix login: esser", 120), "the system is up (the boot started the services, among them telnetd, before the login appeared)")
    time.sleep(2)

    t = Telnet()
    check(t.expect(r"ulix login: ", 30), "a connection to the program telnetd shows the login prompt of terminal 5")
    check((251, 1) in t.commands, "the server says IAC WILL ECHO (it echoes the keys)")
    check((251, 3) in t.commands, "the server says IAC WILL SUPPRESS-GO-AHEAD (every key is sent at once)")
    t.send("root\r\n")
    check(t.expect(r"Password: ", 30), "login asks for the password")
    t.send("xyz\r\n")
    check(t.expect(r"root@ulix\[\d+\]:/root# ", 30), "the password is accepted: a root shell runs on the telnet terminal")
    t.text = ""
    t.send("ps\r\n")
    check(t.expect(r"(?s)PID\s+PPID.*tty4.*/bin/sh.*root@ulix\[\d+\]:/root# ", 30), "ps shows the shell on tty4, the terminal of the session")

    t2 = Telnet()
    t2.pump(5)
    check("in use" in t2.text, "a second connection is told that the terminal is in use")
    t2.close()

    t.text = ""
    t.send("top\r\n")
    check(t.expect(r"top - up .*processes", 30), "top draws its first line (cursor addressing works through telnet)")
    check(t.expect(r"CPU0 \[.*\] \d+%", 30) and t.expect(r"Mem  \[", 5), "top shows the load of the CPU and the memory")
    t.send("q")
    check(t.expect(r"root@ulix\[\d+\]:/root# ", 30), "q ends top and the shell is back")

    t.close()                                                  # hang up
    time.sleep(3)
    t3 = Telnet()
    check(t3.expect(r"ulix login: ", 30), "after the hang up the programs of the session are gone: login asks again")
    t3.send("root\r\n"); t3.expect(r"Password: ", 30); t3.send("xyz\r\n")
    check(t3.expect(r"root@ulix\[\d+\]:/root# ", 30), "the next session can log in")
    t3.text = ""
    t3.send("ps\r\n")
    t3.expect(r"(?s)PID.*root@ulix\[\d+\]:/root# ", 30)
    shells = len(re.findall(r"tty4 \S+\s+/root\s+/bin/sh", t3.text))
    check(shells == 1, "only one shell runs on tty4 (the old one was ended: %d)" % shells)
    t3.close()
finally:
    qemu.kill(); qemu.wait()

# ---- second boot: the server switched off in /etc/rc.conf
off = os.path.join(tempfile.mkdtemp(), "off.img")
shutil.copy(disk, off)
conf = os.path.join(tempfile.mkdtemp(), "rc.conf")
open(conf, "w").write('# switched off\ntelnetd_enable="NO"\n')
subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "minix_add.py"), off, "/etc/rc.conf=" + conf], check=True, stdout=subprocess.DEVNULL)
qemu, log = boot(off)
try:
    check(wait_log(log, "netd: network thread started", 120), "second boot: the network thread started")
    check(wait_log(log, "ulix login: esser", 60), "second boot: the system is up")
    time.sleep(2)
    answered = False
    try:
        s = socket.create_connection(("127.0.0.1", 5523), timeout=5); s.settimeout(5)
        answered = bool(s.recv(100))
        s.close()
    except OSError:
        answered = False
    check(not answered, "nobody answers on port 23")
finally:
    qemu.kill(); qemu.wait()

print("telnet test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
