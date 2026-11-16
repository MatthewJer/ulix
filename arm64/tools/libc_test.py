#!/usr/bin/env python3
"""Conformance tests of the C library (chapter "Conformance Tests for the C Library", issue #41): a login session in which the programs of /musl run.

usage: libc_test.py KERNEL DISK.img

/musl/libctest (strings, ctype, errno, stdlib, math, time, malloc, setjmp, printf and scanf, stdio on files, fork/exec/wait, signals), /musl/filetest (file
system calls), /musl/localetest (locale, wide characters, regular expressions) and /musl/threadtest (pthreads). Each program prints "user ok:" or
"user FAIL:" per check; this script runs them one after the other and demands that none failed and that all checks were really run.
"""
import os, re, socket, subprocess, sys, threading, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45175))
PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
fails = 0


class Session:
    def __init__(self, image):
        cpu = ['-accel', 'hvf', '-cpu', 'host'] if os.environ.get('QEMU_FAST') == '1' else ['-cpu', 'cortex-a72']      # (QEMU_FAST=1: for the development loop only)
        cmd = ['qemu-system-aarch64', '-M', 'virt'] + cpu + [ '-m', '128', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
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
        print('  FAIL: %s -- did not see %r; got: %r' % (what or pattern, pattern, self.log[-1500:])); fails += 1
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
    s.expect(r'ulix login: esser', 'init started login, which logs in "esser" automatically')
    s.expect(PROMPT, 'shell prompt')
    s.pump(3)
    # (program, number of checks it must have run; None: the program says it on its last line)
    for prog, count in (('/musl/libctest', None), ('/musl/filetest', 30), ('/musl/localetest', 15), ('/musl/threadtest', 17)):
        s.send(prog + '\r')
        out = s.expect(PROMPT, '%s ran and the prompt is back' % prog, int(os.environ.get('LIBC_TIMEOUT', 300)))
        print(out[-8000:])
        oks, bad, kn = out.count('user ok:'), out.count('user FAIL'), out.count('user KNOWN:')
        if count is None:
            m = re.search(r'libctest: (\d+) checks, (\d+) failed, (\d+) known', out)
            check(m is not None, '%s printed its summary line' % prog)
            count = int(m.group(1)) if m else -1
        check(bad == 0, '%s: no check failed (%d "user FAIL" lines)' % (prog, bad))
        check(oks + bad + kn == count, '%s: %d checks ran (expected %d; %d of them are known faults of the kernel, see ARM64-PORT.md)' % (prog, oks + bad + kn, count, kn))
        check('not implemented' not in out, '%s needed no Linux system call that the kernel does not have' % prog)
    s.pump(0.5)
    if b'EXCEPTION' in s.log:
        print('  FAIL: kernel exception'); fails += 1
finally:
    s.close()

print('libc test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
