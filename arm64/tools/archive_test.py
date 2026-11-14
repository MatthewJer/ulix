#!/usr/bin/env python3
"""Tests tar and unzip (bsdtar and bsdunzip of libarchive; chapter "Python" of lit/libc.nw): a login session over the UART.

usage: archive_test.py KERNEL DISK.img

The interpreter is a normal program of the disk (a static executable linked with musl). Each check starts it with a few lines of
Python and compares what it prints. qemu has a random number generator (virtio-rng) because CPython wants random numbers for its
hash tables at start-up.
"""
import os, re, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45171))
PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
fails = 0


class Session:
    def __init__(self, image):
        cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '256', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
               '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % image, '-device', 'virtio-blk-device,drive=hd0',
               '-device', 'virtio-rng-device', '-smp', '2', '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
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
        seen = self.expect(answer, what or '%s -> %s' % (cmd, answer), timeout) if answer else ''
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
    out = s.run('tar --version', r'bsdtar 3\.\d+\.\d+ - libarchive[^\r\n]*zlib', 'tar --version names libarchive and zlib')
    s.run('cd /tmp', None)
    s.run('mkdir src', None, 'mkdir src')
    s.run('cp /etc/hosts src/hosts.txt', r'\+\+\+\+', 'a file to pack (hosts)')
    s.run('cp /etc/services src/services.txt', r'\+\+\+\+', 'a second file to pack (services)')
    out = s.run('tar -cvf a.tar src', r'src/services\.txt', 'tar -cvf: creates a tar file and names the files')
    check('src/hosts.txt' in out and 'rror' not in out, 'tar -cvf: both files packed, no error')
    out = s.run('tar -tvf a.tar', r'services\.txt', 'tar -tvf: lists the archive')
    check('hosts.txt' in out and re.search(r'\b117\b', out) is not None, 'tar -tvf: the size of hosts.txt (117 bytes) is listed')
    out = s.run('tar -czf a.tgz src', None, 'tar -czf: gzip compression (zlib)')
    check('rror' not in out, 'tar -czf: no error')
    out = s.run('tar -cjf a.tbz src', None, 'tar -cjf: bzip2 compression')
    check('rror' not in out, 'tar -cjf: no error')
    out = s.run('tar -a -cf a.zip src', None, 'tar -a -cf a.zip: a zip file (the suffix decides the format)')
    check('rror' not in out, 'tar -a -cf a.zip: no error')
    out = s.run('unzip -l a.zip', r'services\.txt', 'unzip -l: lists the zip file')
    check('hosts.txt' in out and '162' in out, 'unzip -l: sizes and names (services.txt is 162 bytes)')
    for fmt, cmd, name in [('tar', 'tar -xf ../a.tar', 'x1'), ('tgz', 'tar -xzf ../a.tgz', 'x2'), ('tbz', 'tar -xjf ../a.tbz', 'x5'), ('zip by tar', 'tar -xf ../a.zip', 'x3')]:
        s.run('cd /tmp', None, 'cd /tmp')
        s.run('mkdir ' + name, None, 'mkdir ' + name)
        s.run('cd ' + name, None, 'cd ' + name)
        out = s.run(cmd, None, 'extract the %s file' % fmt)
        check('rror' not in out, '%s: no error' % fmt)
        out = s.run('wc src/hosts.txt', r'\d+ +\d+ +\d+ src/hosts\.txt', '%s: wc of the extracted file' % fmt)
        check(re.search(r' 117 src/hosts\.txt', out) is not None, '%s: hosts.txt has 117 bytes after extraction' % fmt)
        out = s.run('cat src/hosts.txt', r'gateway\.ulix', '%s: the contents are the same' % fmt)
    s.run('cd /tmp', None, 'cd /tmp')
    s.run('mkdir x4', None, 'mkdir x4')
    s.run('cd x4', None, 'cd x4')
    out = s.run('unzip ../a.zip', None, 'unzip: extracts the zip file')
    check('rror' not in out and 'Failed' not in out, 'unzip: no error')
    out = s.run('wc src/services.txt', r'src/services\.txt', 'unzip: wc of the extracted file')
    check(' 162 src/services.txt' in out, 'unzip: services.txt has 162 bytes')
    s.pump(0.5)
    if b'EXCEPTION' in s.log or b'killed:' in s.log:
        print('  FAIL: kernel exception or process killed by a fault'); fails += 1
finally:
    s.close()

print('archive test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
