#!/usr/bin/env python3
"""Test of the server sockets for programs (chapter "Servers as programs"): usage: listen_test.py ulix-shell.bin DISK.img

The program /bin/echod listens on port 7007 (tcp_listen), waits for clients (tcp_accept: the process sleeps until one has connected) and
sends back what it receives. It is a service: this test boots the shell kernel with a network card on a copy of the disk whose
rc.conf says echod_enable="YES", so the boot starts it. qemu forwards TCP port 5507 of the host to port 7007 of the kernel
(10.0.2.15). The host then behaves like clients. A second boot with the default (echod off) must not answer.
"""
import os, shutil, socket, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

PORT = 5507
work = tempfile.mkdtemp(prefix="ulix-listen-")

def boot(image):
    log = os.path.join(work, "console-%d.log" % int(time.time() * 1000))
    qemu = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none",
        "-serial", "file:" + log, "-kernel", kernel, "-snapshot", "-drive", "file=%s,format=raw,if=none,id=hd0" % image,
        "-device", "virtio-blk-device,drive=hd0",
        "-netdev", "user,id=n0,hostfwd=tcp:127.0.0.1:%d-10.0.2.15:7007" % PORT, "-device", "virtio-net-device,netdev=n0"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return qemu, log
def console(log): return open(log, errors="replace").read() if os.path.exists(log) else ""
def wait_log(log, text, seconds):
    for _ in range(seconds * 10):
        if text in console(log): return True
        time.sleep(0.1)
    return False

def connect():
    s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
    s.settimeout(5)
    return s

def talk(s, data):
    """send data, collect as many bytes back as were sent; None if they do not come"""
    s.sendall(data)
    got = b""
    try:
        while len(got) < len(data):
            d = s.recv(65536)
            if not d: break
            got += d
    except socket.timeout:
        pass
    return got

def connect_when_up(seconds):
    """qemu accepts the connection on the host side at once; the server answers only when it is up: try until an echo comes"""
    end = time.time() + seconds
    while time.time() < end:
        try:
            s = connect()
            if talk(s, b"ping\n") == b"ping\n": return s
            s.close()
        except OSError:
            pass
        time.sleep(1)
    return None

conf = os.path.join(work, "rc.conf")
open(conf, "w").write('echod_enable="YES"\n')
on = os.path.join(work, "on.img")
shutil.copy(disk, on)
subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "minix_add.py"), on, "/etc/rc.conf=" + conf],
               check=True, stdout=subprocess.DEVNULL)

qemu, log = boot(on)
try:
    up = wait_log(log, "ulix login: esser", 120)
    check(up, "the system is up (the boot started the services before the login)")
    if not up: print("  console log, last lines:\n" + "\n".join("    " + l for l in console(log).splitlines()[-15:]))
    s = connect_when_up(60)
    check(s is not None, "echod is up: tcp_listen opened port 7007 and tcp_accept returned the first client (the text came back)")
    if s is None: print("  console log, last lines:\n" + "\n".join("    " + l for l in console(log).splitlines()[-15:]))
    if s:
        check(talk(s, b"hello, server\n") == b"hello, server\n", "what the client sends comes back: tcp_recv and tcp_send work on an accepted socket")
        s.close()

    time.sleep(1)
    s = connect()
    check(talk(s, b"second client\n") == b"second client\n", "after the first client has gone, accept serves the next one")
    s.close()

    # a client that comes while another one is served waits in the queue of TCP until the server accepts it
    a = connect()
    check(talk(a, b"first\n") == b"first\n", "client A is served")
    b = connect()
    b.sendall(b"queued\n")
    b.settimeout(2)
    try: early = b.recv(100)
    except socket.timeout: early = b""
    check(early == b"", "client B, who comes while A is connected, gets no answer yet (the server serves one client at a time)")
    a.close()
    b.settimeout(10)
    try: late = b.recv(100)
    except socket.timeout: late = b""
    check(late == b"queued\n", "when A hangs up the sleeping server is woken, accepts B, and B's waiting text is echoed")
    b.close()

    # a client that connects and leaves without a word must not hurt the server
    z = connect(); z.close()
    time.sleep(1)
    s = connect()
    check(talk(s, b"still there\n") == b"still there\n", "a client that left without sending anything did not stop the server")
    s.close()

    # more than the buffers hold (4 KByte each way): the server must go on reading while it sends
    s = connect()
    data = bytes((i * 7 + (i >> 8)) & 255 for i in range(20000))
    s.sendall(data[:2000])
    got = b""
    deadline = time.time() + 60
    try:
        sent = 2000
        while len(got) < len(data) and time.time() < deadline:
            if sent < len(data):
                s.settimeout(0.05)
                try: sent += s.send(data[sent:sent + 1000])
                except socket.timeout: pass
            s.settimeout(0.2)
            try:
                d = s.recv(65536)
                if not d: break
                got += d
            except socket.timeout:
                pass
    except OSError:
        pass
    check(got == data, "20000 bytes pass through the server, complete and in order (%d came back)" % len(got))
    s.close()
    check("EXCEPTION" not in console(log), "no kernel exception")
finally:
    qemu.kill(); qemu.wait()

# ---- second boot: echod is off by default, nobody listens on the port
qemu, log = boot(disk)
try:
    check(wait_log(log, "ulix login: esser", 120), "second boot: the system is up")
    time.sleep(2)
    answered = False
    try:
        s = connect()
        answered = talk(s, b"anybody?\n") != b""
        s.close()
    except OSError:
        answered = False
    check(not answered, "with echod switched off (the default) nobody answers on the port")
finally:
    qemu.kill(); qemu.wait()

shutil.rmtree(work, ignore_errors=True)
print("listen test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
