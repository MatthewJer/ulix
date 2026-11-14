#!/usr/bin/env python3
"""Tests a web server written in Python (libc/hello_http.py, sockets listen and accept): a login session over the UART starts it, the host asks it with curl.

usage: httpd_test.py KERNEL DISK.img

The interpreter is a normal program of the disk (a static executable linked with musl). Each check starts it with a few lines of
Python and compares what it prints. qemu has a random number generator (virtio-rng) because CPython wants random numbers for its
hash tables at start-up.
"""
import os, re, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45172))
PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
fails = 0


class Session:
    def __init__(self, image):
        cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '256', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
               '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % image, '-device', 'virtio-blk-device,drive=hd0',
               '-device', 'virtio-rng-device', '-netdev', 'user,id=n0,hostfwd=tcp:127.0.0.1:5531-10.0.2.15:8080', '-device', 'virtio-net-device,netdev=n0', '-smp', '2', '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
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


def check(cond, what):
    global fails
    print('  %s %s' % ('ok:  ' if cond else 'FAIL:', what))
    if not cond: fails += 1


s = Session(disk)
try:
    s.expect(r'console ready', 'kernel booted')
    s.expect(r'ulix login: esser', 'auto login')
    s.expect(PROMPT, 'shell prompt')
    s.send('/musl/python /musl/hello_http.py\r')
    s.expect(r'serving hello world on port 8080', 'the server is up', timeout=240)
    for n in range(3):
        r = subprocess.run(['curl', '-s', '-m', '60', '-i', 'http://127.0.0.1:5531/'], capture_output=True, text=True)
        check('200 OK' in r.stdout and '<h1>hello world</h1>' in r.stdout and 'text/html' in r.stdout, 'request %d: status 200, content type text/html, the page says hello world' % (n + 1))
    r = subprocess.run(['curl', '-s', '-m', '60', '-o', '/dev/null', '-w', '%{http_code}', 'http://127.0.0.1:5531/anything/else'], capture_output=True, text=True)
    check(r.stdout == '200', 'any path gets the page')
    s.send('\x03')                                         # Ctrl-C ends the server
    s.expect(PROMPT, 'Ctrl-C ends the server and the shell is back', timeout=60)
    s.pump(0.5)
    if b'EXCEPTION' in s.log or b'killed:' in s.log:
        print('  FAIL: kernel exception or process killed by a fault'); fails += 1
finally:
    s.close()

print('httpd test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
