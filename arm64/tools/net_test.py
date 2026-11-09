#!/usr/bin/env python3
"""Test of the network of Ulix: usage: net_test.py ulix-net.bin

Starts qemu with a virtio network card on qemu's user-mode network. In that network qemu is the router (10.0.2.2) and the kernel has
the address 10.0.2.15. qemu forwards ports of the host to the kernel, so that ordinary sockets of this program talk to the stack
of Ulix: UDP 127.0.0.1:5556 and TCP 127.0.0.1:5555 arrive at port 7 of the kernel (the echo service). The kernel, in turn,
pings the router, sends a datagram to the host (10.0.2.2:5557 is this program, port 5557) and connects to it (TCP port 5558).
"""
import os, socket, struct, subprocess, sys, tempfile, threading, time

kernel = sys.argv[1]
tmp = tempfile.mkdtemp()
log = os.path.join(tmp, "console.log")

host_udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)       # where the kernel's datagram arrives
host_udp.bind(("127.0.0.1", 5557)); host_udp.settimeout(10)
host_udp6 = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)    # the same over IPv6: qemu's router fec0::2 passes it to [::1]
host_udp6.bind(("::1", 5559)); host_udp6.settimeout(10)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)       # where the kernel connects to (port 5558)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", 5558)); server.listen(1)

# A name server for the DNS checks of the kernel (the kernel asks 10.0.2.2:5559, which is this socket). It answers like a real one:
# "alias.test" gets a CNAME and then the A record, both with names as compression pointers (0xC00C = "the name at byte 12");
# "retry.test" is not answered the first time (the resolver must ask again); "nx.test" does not exist (rcode 3).
dns_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
dns_sock.bind(("127.0.0.1", 5559))
def dns_server():
    seen = {}
    while True:
        try:
            q, peer = dns_sock.recvfrom(512)
        except OSError:
            return
        end = 12
        while q[end]: end += 1 + q[end]
        name = q[12:end + 1]                                   # the question name as labels
        seen[name] = seen.get(name, 0) + 1
        question = q[12:end + 5]                               # name, type, class
        head = q[0:2]                                          # the id of the question comes back
        if name == b"\x05alias\x04test\x00":
            real = b"\x04real\xc0\x12"                          # "real" + pointer to "test" (byte 18, inside the question)
            reply = head + b"\x81\x80\x00\x01\x00\x02\x00\x00\x00\x00" + question
            reply += b"\xc0\x0c\x00\x05\x00\x01\x00\x00\x00\x3c" + struct.pack(">H", len(real)) + real          # alias.test CNAME real.test
            reply += real + b"\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04\xc0\x00\x02\x07"            # real.test A 192.0.2.7
        elif name == b"\x05retry\x04test\x00":
            if seen[name] < 2: continue                        # the first question is not answered
            reply = head + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00" + question + b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04\xc0\x00\x02\x08"
        else:
            reply = head + b"\x81\x83\x00\x01\x00\x00\x00\x00\x00\x00" + question      # rcode 3: no such name
        dns_sock.sendto(reply, peer)
threading.Thread(target=dns_server, daemon=True).start()

qemu = subprocess.Popen(["qemu-system-aarch64", "-M", os.environ.get("QEMU_M", "virt"), "-cpu", "cortex-a72", "-m", "128", "-display", "none",
    "-monitor", "none", "-serial", "file:" + log, "-kernel", kernel,
    "-netdev", "user,id=n0,hostfwd=udp:127.0.0.1:5556-10.0.2.15:7,hostfwd=tcp:127.0.0.1:5555-10.0.2.15:7",
    "-device", "virtio-net-device,netdev=n0"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

def console():
    return open(log, errors="replace").read() if os.path.exists(log) else ""

def wait_for(text, seconds):
    for _ in range(seconds * 10):
        if text in console(): return True
        time.sleep(0.1)
    return False

try:
    check(wait_for("net: ready for the host", 90), "the kernel is up and its network works (it pinged the router)")
    try:
        data, _ = host_udp.recvfrom(100)
    except socket.timeout:
        data = b""
    check(data == b"hello from Ulix", "the datagram of the kernel arrived on the host")

    try:
        data6, peer6 = host_udp6.recvfrom(100)
        host_udp6.sendto(b"pong6", peer6)                           # the answer goes back through qemu's router to the kernel
    except socket.timeout:
        data6 = b""
    check(data6 == b"hello6 from Ulix", "the IPv6 datagram of the kernel arrived on the host (neighbor discovery, SLAAC address as the source)")

    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); client.settimeout(5)
    for i in range(3):
        message = ("datagram %d " % i).encode() + bytes(range(65 + i, 90)) * (i + 1)
        client.sendto(message, ("127.0.0.1", 5556))
        try:
            reply, _ = client.recvfrom(2000)
        except socket.timeout:
            reply = b""
        check(reply == message, "UDP echo %d: %d bytes sent to the kernel and received back" % (i + 1, len(message)))

    # TCP: two connections to the echo service of the kernel; small and large messages (the large one does not fit into one segment
    # nor into the buffers of the kernel: it has to be sent in pieces, with the window of the receiver respected)
    for number, size in enumerate((100, 20000), 1):
        message = bytes((i * 7 + number) % 251 for i in range(size))
        conn = socket.create_connection(("127.0.0.1", 5555), timeout=10)
        conn.sendall(message)
        reply = b""
        try:
            while len(reply) < size:
                chunk = conn.recv(65536)
                if not chunk: break
                reply += chunk
        except socket.timeout:
            pass
        check(reply == message, "TCP echo %d: %d bytes sent to the kernel and received back unchanged" % (number, size))
        conn.close()                                               # the kernel closes its side when it sees ours

    # the kernel connects to the host (port 5558): it reads the line, answers "pong" and closes
    try:
        server.settimeout(30)
        peer, _ = server.accept()
        peer.settimeout(10)
        line = peer.recv(100)
        check(line == b"ping from Ulix", "the kernel connected to the host and sent its line (although its first segments were lost)")
        peer.sendall(b"pong")
        peer.shutdown(socket.SHUT_WR)
        rest = peer.recv(100)                                      # b"" when the kernel has closed its side, too
        check(rest == b"", "the kernel closed its side of the connection")
        peer.close()
    except (socket.timeout, OSError) as e:
        check(False, "the kernel connected to the host (%s)" % e)
    wait_for("RESULT", 60)
finally:
    qemu.kill(); qemu.wait()

text = console()
check("RESULT: all checks passed" in text, "the kernel's own checks all passed")
if "FAIL" in text or fails:
    print("--- end of the console log ---\n" + "\n".join(text.splitlines()[-25:]))
print("net test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
