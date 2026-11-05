#!/usr/bin/env python3
"""Test of the network thread of Ulix: usage: netd_test.py ulix-console.bin DISK.img

Boots the console kernel (scheduler, processes and a user program running) with a network card and the kernel command line "netecho".
There is no test code in this kernel: the network thread (netd) alone has to answer. This program talks to port 7 of the kernel through
qemu's user-mode network (UDP 127.0.0.1:5566 and TCP 127.0.0.1:5565 are forwarded to 10.0.2.15:7) and checks that everything that
it sends comes back unchanged, while the kernel does other things, too.
"""
import os, socket, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
log = os.path.join(tempfile.mkdtemp(), "console.log")
qemu = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none",
    "-serial", "file:" + log, "-kernel", kernel, "-snapshot", "-drive", "file=%s,format=raw,if=none,id=hd0" % disk, "-device", "virtio-blk-device,drive=hd0",
    "-netdev", "user,id=n0,hostfwd=udp:127.0.0.1:5566-10.0.2.15:7,hostfwd=tcp:127.0.0.1:5565-10.0.2.15:7", "-device", "virtio-net-device,netdev=n0",
    "-append", "netecho"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1
def console(): return open(log, errors="replace").read() if os.path.exists(log) else ""
def wait_for(text, seconds):
    for _ in range(seconds * 10):
        if text in console(): return True
        time.sleep(0.1)
    return False

try:
    check(wait_for("netd: network thread started", 90), "the kernel started the network thread")
    check("woken by the interrupt of the card" in console(), "the thread is woken by the interrupt of the card (not by polling)")
    check(wait_for("console ready", 30), "the kernel is up and runs its program")
    time.sleep(1)

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.settimeout(5)
    for i in range(3):
        message = ("datagram %d " % i).encode() + bytes(range(65 + i, 90)) * (i + 1)
        udp.sendto(message, ("127.0.0.1", 5566))
        try: reply, _ = udp.recvfrom(2000)
        except socket.timeout: reply = b""
        check(reply == message, "UDP echo %d: %d bytes sent to the kernel and received back" % (i + 1, len(message)))

    for number, size in enumerate((100, 20000, 3000), 1):
        message = bytes((i * 7 + number) % 251 for i in range(size))
        reply = b""
        try:
            conn = socket.create_connection(("127.0.0.1", 5565), timeout=10)
            conn.sendall(message)
            while len(reply) < size:
                chunk = conn.recv(65536)
                if not chunk: break
                reply += chunk
            conn.close()
        except (socket.timeout, OSError):
            pass
        check(reply == message, "TCP echo %d: %d bytes sent to the kernel and received back unchanged" % (number, size))
        time.sleep(0.5)
finally:
    qemu.kill(); qemu.wait()

text = console()
check("KERNEL EXCEPTION" not in text, "no kernel exception")
if fails: print("--- end of the console log ---\n" + "\n".join(text.splitlines()[-20:]))
print("netd test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
