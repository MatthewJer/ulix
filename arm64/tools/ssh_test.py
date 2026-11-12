#!/usr/bin/env python3
"""Test of the SSH server of Ulix: usage: ssh_test.py ulix-shell.bin DISK.img

Boots the shell kernel with a network card and a random number generator. qemu forwards TCP port 5522 of the host to port 22 of the
kernel (10.0.2.15). The host's own ssh client (OpenSSH) is the other side: it checks the host key, makes the key exchange
(curve25519-sha256), talks AES-128-CTR with HMAC-SHA-256, opens a session with a terminal and a shell, and a person (this
program) logs in on the terminal with the password, runs programs, hangs up and connects again. A second boot with
sshd_enable="NO" in /etc/rc.conf checks that the server can be switched off. Every check says what it proves.
"""
import os, re, shutil, socket, subprocess, sys, tempfile, threading, time

kernel, disk = sys.argv[1:3]
work = tempfile.mkdtemp(prefix="ulix-ssh-")
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

PORT = 5522
def boot(image):
    log = os.path.join(work, "console-%d.log" % int(time.time() * 1000))
    qemu = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none",
        "-serial", "file:" + log, "-kernel", kernel, "-snapshot", "-drive", "file=%s,format=raw,if=none,id=hd0" % image,
        "-device", "virtio-blk-device,drive=hd0",
        "-netdev", "user,id=n0,hostfwd=tcp:127.0.0.1:%d-10.0.2.15:22" % PORT, "-device", "virtio-net-device,netdev=n0",
        "-device", "virtio-rng-device"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return qemu, log
def console(log): return open(log, errors="replace").read() if os.path.exists(log) else ""
def wait_log(log, text, seconds):
    for _ in range(seconds * 10):
        if text in console(log): return True
        time.sleep(0.1)
    return False

SSH_OPTIONS = ["-p", str(PORT), "-o", "BatchMode=yes", "-o", "KexAlgorithms=curve25519-sha256", "-o", "Ciphers=aes128-ctr", "-o", "MACs=hmac-sha2-256",
               "-o", "HostKeyAlgorithms=ecdsa-sha2-nistp256", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=20"]

class Ssh:
    """the ssh client of the host with a terminal (-tt), driven through pipes like a person at a keyboard"""
    def __init__(self, known_hosts, extra=()):
        self.p = subprocess.Popen(["ssh", "-tt", *SSH_OPTIONS, "-o", "UserKnownHostsFile=" + known_hosts, "-o", "StrictHostKeyChecking=yes", *extra, "root@127.0.0.1"],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.text = ""
        self.lock = threading.Lock()
        threading.Thread(target=self.reader, daemon=True).start()
    def reader(self):
        while True:
            d = os.read(self.p.stdout.fileno(), 4096)
            if not d: return
            with self.lock: self.text += d.decode("latin1")
    def expect(self, pattern, seconds=60):
        end = time.time() + seconds
        while time.time() < end:
            with self.lock:
                m = re.search(pattern, self.text, re.S)
                if m: self.text = self.text[m.end():]; return True
            time.sleep(0.1)
        return False
    def send(self, s):
        self.p.stdin.write(s.encode()); self.p.stdin.flush()
    def close(self):
        self.p.kill(); self.p.wait()

qemu, log = boot(disk)
try:
    check(wait_log(log, "netd: ssh server on port 22", 120), "the kernel started the SSH server (host key found, random numbers present, /etc/rc.conf says YES)")
    m = re.search(r"netd: ssh host key ecdsa-sha2-nistp256 (SHA256:\S+)", console(log))
    check(m is not None, "the kernel printed the fingerprint of its host key")
    fingerprint = m.group(1) if m else ""
    check(wait_log(log, "ulix login: esser", 60), "the system is up (the console logged in)")
    time.sleep(2)

    # --- the host key: what the server sends in the key exchange is the key whose fingerprint the kernel printed
    scan = subprocess.run(["ssh-keyscan", "-p", str(PORT), "-t", "ecdsa", "127.0.0.1"], capture_output=True, text=True, timeout=60).stdout
    known = os.path.join(work, "known_hosts"); open(known, "w").write(scan)
    check("ecdsa-sha2-nistp256" in scan, "ssh-keyscan: the server answers the banner and the key exchange and offers an ECDSA P-256 host key")
    fp = subprocess.run(["ssh-keygen", "-lf", known], capture_output=True, text=True).stdout
    check(fingerprint != "" and fingerprint in fp, "the key that the server sent has the fingerprint that the kernel printed (%s)" % fingerprint)

    # --- a client that does not know the key refuses the server (the host key is what proves who the server is)
    other = os.path.join(work, "other_key")
    subprocess.run(["ssh-keygen", "-q", "-t", "ecdsa", "-N", "", "-f", other], check=True)
    wrong = os.path.join(work, "wrong_hosts")
    open(wrong, "w").write("[127.0.0.1]:%d %s" % (PORT, open(other + ".pub").read()))
    r = subprocess.run(["ssh", *SSH_OPTIONS, "-o", "UserKnownHostsFile=" + wrong, "-o", "StrictHostKeyChecking=yes", "root@127.0.0.1", "true"], capture_output=True, text=True, timeout=60)
    check(r.returncode != 0 and "Host key verification failed" in r.stderr + r.stdout, "a client with another key for this host refuses to go on: the signature of the exchange hash is checked against the known key")

    # --- garbage after the banner: the server closes that connection, and stays usable
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    s.sendall(b"SSH-2.0-garbage\r\n" + b"\xff\xff\xff\xff" + os.urandom(300))
    s.settimeout(15)
    got = b""
    try:
        while True:
            d = s.recv(4096)
            if not d: break
            got += d
        closed = True
    except socket.timeout:
        closed = False
    s.close()
    check(got.startswith(b"SSH-2.0-Ulix_1") and closed, "a packet with an impossible length instead of a key exchange: the server sent its banner and closed the connection")

    # --- half a packet and then silence: the server must not wait for ever (it drops a client that is not through the key exchange in 30 seconds)
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    s.sendall(b"SSH-2.0-slow\r\n" + b"\x00\x00\x10\x00" + os.urandom(100))
    s.settimeout(50)
    got = b""
    try:
        while True:
            d = s.recv(4096)
            if not d: break
            got += d
        closed = True
    except socket.timeout:
        closed = False
    s.close()
    check(closed, "half a packet and then silence: the server closes the connection after its time limit instead of waiting for ever")

    # --- the session
    c = Ssh(known)
    check(c.expect(r"ulix login: ", 90), "key exchange, new keys and the user authentication method none succeed; the shell request shows the login prompt of terminal 5")
    c.send("root\n")
    check(c.expect(r"Password: ", 30), "login asks for the user name's password: what is typed goes through the encrypted channel into the terminal")
    c.send("xyz\n")
    check(c.expect(r"root@ulix\[\d+\]:/root# ", 30), "the password is accepted by login: a root shell runs in the SSH session")
    c.send("ps\n")
    check(c.expect(r"(?s)PID\s+PPID.*tty4.*/bin/sh.*root@ulix\[\d+\]:/root# ", 30), "ps shows the shell on tty4, the terminal of the session")
    c.send("cat /etc/rc.conf\n")
    check(c.expect(r"(?s)sshd_enable=\"YES\".*root@ulix\[\d+\]:/root# ", 30), "cat /etc/rc.conf: output of a program comes back (several packets, both directions)")

    c2 = Ssh(known)
    refused = not c2.expect(r"ulix login: ", 10)
    time.sleep(1); c2.close()
    check(refused, "a second client is refused while the first one has the terminal (it does not get a login prompt)")

    c.close()                                                  # hang up
    time.sleep(3)
    c3 = Ssh(known)
    check(c3.expect(r"ulix login: ", 60), "after the hang up the programs of the session are gone: login asks again")
    c3.send("root\n"); c3.expect(r"Password: ", 30); c3.send("xyz\n")
    check(c3.expect(r"root@ulix\[\d+\]:/root# ", 30), "the next session can log in")
    c3.close()
    check(b"EXCEPTION" not in open(log, "rb").read(), "no kernel exception")
finally:
    qemu.kill(); qemu.wait()

# ---- second boot: the server switched off in /etc/rc.conf
off = os.path.join(work, "off.img")
shutil.copy(disk, off)
conf = os.path.join(work, "rc.conf")
open(conf, "w").write('# switched off\nsshd_enable="NO"\ntelnetd_enable="NO"\n')
subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "minix_add.py"), off, "/etc/rc.conf=" + conf], check=True, stdout=subprocess.DEVNULL)
qemu, log = boot(off)
try:
    check(wait_log(log, "netd: network thread started", 120), "second boot: the network thread started")
    check(wait_log(log, "ulix login: esser", 60), "second boot: the system is up")
    time.sleep(2)
    check("ssh server" not in console(log), 'with sshd_enable="NO" the SSH server is not started')
    answered = False
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=5); s.settimeout(5)
        answered = bool(s.recv(100))
        s.close()
    except OSError:
        answered = False
    check(not answered, "nobody answers on port 22")
finally:
    qemu.kill(); qemu.wait()
shutil.rmtree(work, ignore_errors=True)
print("ssh test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
