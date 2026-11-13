#!/usr/bin/env python3
"""Tests the sockets of programs (chapter "Sockets for Programs", issue #37): a login session in which /musl/sockettest talks to servers of the host.

usage: socket_test.py KERNEL DISK.img

The host (10.0.2.2 for the guest, qemu's user network) runs a TCP server that answers every line in capital letters and closes when the client has
shut down its side, and a UDP server that answers every datagram in capital letters. The program checks sendto, recvfrom, connect, poll, select, non-blocking sockets, errors
and getaddrinfo; this script prints what the program printed.
"""
import os, re, socket, subprocess, sys, threading, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45172))
PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
fails = 0


class Session:
    def __init__(self, image):
        cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '128', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
               '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % image, '-device', 'virtio-blk-device,drive=hd0',
               '-device', 'virtio-rng-device', '-smp', '2', '-netdev', 'user,id=n0', '-device', 'virtio-net-device,netdev=n0', '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
        self.qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.sock = None
        for _ in range(100):
            try: self.sock = socket.create_connection(('127.0.0.1', PORT)); break
            except OSError: time.sleep(0.1)
        if not self.sock: self.qemu.kill(); sys.exit('cannot connect to qemu')
        self.sock.settimeout(0.2)
        self.log, self.carry = b'', b''

    def feed(self, data):                      # (the status line of the console is not part of the text that is checked)
        data = STATUS.sub(b'', self.carry + data); self.carry = b''
        i = data.rfind(b'\x1b7')
        if i >= 0 and len(data) - i < 110 and b'\x1b8' not in data[i:]: self.carry = data[i:]; data = data[:i]
        self.log += data

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
                if data: self.feed(data)
            except socket.timeout: pass

    def expect(self, pattern, what=None, timeout=25.0):
        global fails
        start = time.time()
        while time.time() - start < timeout:
            self.pump(0.1)
            m = re.search(pattern.encode(), self.log, re.S)
            if m:
                seen = self.log[:m.end()]; self.log = self.log[m.end():]
                print('  ok:   %s' % (what or pattern)); return seen.decode('latin1')
        print('  FAIL: %s -- did not see %r; got: %r' % (what or pattern, pattern, self.log[-300:])); fails += 1
        return ''

    def send(self, text):
        for b in text.encode('latin1'):
            self.sock.send(bytes([b])); self.pump(0.03)

    def run(self, cmd, answer, what=None, timeout=60.0):
        self.send(cmd + '\r')
        seen = self.expect(answer, what or '%s -> %s' % (cmd, answer), timeout)
        return seen + self.expect(PROMPT, 'prompt is back after "%s"' % cmd, timeout)

    def close(self): self.qemu.kill()


def tcp_server(server):
    while True:
        try: conn, _ = server.accept()
        except OSError: return
        data = b''
        conn.settimeout(10)
        try:
            while True:
                chunk = conn.recv(100)
                if not chunk: break
                conn.sendall(chunk.upper())
        except OSError: pass
        conn.close()

def udp_server(sock):
    while True:
        try: data, addr = sock.recvfrom(2000)
        except OSError: return
        sock.sendto(data.upper(), addr)

def check(cond, what):
    global fails
    print('  %s %s' % ('ok:  ' if cond else 'FAIL:', what))
    if not cond: fails += 1

tcp = socket.socket(); tcp.bind(('127.0.0.1', 0)); tcp.listen(5)
udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.bind(('127.0.0.1', 0))
threading.Thread(target=tcp_server, args=(tcp,), daemon=True).start()
threading.Thread(target=udp_server, args=(udp,), daemon=True).start()

s = Session(disk)
try:
    s.expect(r'console ready', 'kernel booted')
    s.expect(r'ulix login: esser', 'init started login, which logs in "esser" automatically')
    s.expect(PROMPT, 'shell prompt')
    s.pump(3)
    out = s.run('/musl/sockettest 10.0.2.2 %d %d' % (tcp.getsockname()[1], udp.getsockname()[1]), r'(?s)user (ok|FAIL):.*getaddrinfo \("gateway.ulix"\)|Killed', 'sockettest runs', timeout=120)
    print(out[-3000:])
    check(out.count('user ok:') == 24 and 'user FAIL' not in out, 'all 24 socket checks of the program passed')
    check('not implemented' not in out, 'no Linux system call that the kernel does not have')
    s.pump(0.5)
    if b'EXCEPTION' in s.log or b'killed:' in s.log:
        print('  FAIL: kernel exception or process killed by a fault'); fails += 1
finally:
    s.close()

print('socket test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
