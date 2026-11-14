#!/usr/bin/env python3
"""Tests the CPython interpreter (chapter "Python", issue #61): a login session over the UART in which /musl/python is run.

usage: python_test.py KERNEL DISK.img

The interpreter is a normal program of the disk (a static executable linked with musl). Each check starts it with a few lines of
Python and compares what it prints. qemu has a random number generator (virtio-rng) because CPython wants random numbers for its
hash tables at start-up.
"""
import os, re, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45170))
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
    out = s.run('/musl/python -c print(6*7)', r'\r\n42\r\n|Fatal Python|Traceback|Killed', 'python -c print(6*7): the interpreter ends', timeout=120)
    check('\n42\r' in out, 'the interpreter starts and prints 42')
    if '\n42\r' not in out: print(out[-1500:])
    out = s.run('/musl/python /musl/hello.py', r'musl python ulix 14|Fatal Python|Traceback|Killed', 'python hello.py: the script ends', timeout=120)
    check('hello from Python 3.12 on linux' in out, 'the script printed its first line: version and sys.platform')
    check('answer 42 sqrt(2) = 1.414214' in out, 'integer and floating point arithmetic, math module')
    check('cwd /home/esser pid > 1: True' in out, 'os.getcwd and os.getpid (system calls getcwd and getpid)')
    check('musl python ulix 14' in out, 'set, sorted, join: the last line')
    if 'musl python ulix 14' not in out: print(out[-1500:])
    # the standard library on the disk (issue #133): modtest.py imports about 150 modules, runs small programs with them (json, re, hashlib,
    # socket, threading, subprocess, unittest, xml, decimal, ...) and checks that the modules that cannot work do fail to import
    out = s.run('/musl/python -B /musl/modtest.py', r'modtest: \d+ ok, \d+ failed|Fatal Python|Killed', 'modtest.py: the module test ends', timeout=300)
    m = re.search(r'modtest: (\d+) ok, (\d+) failed', out)
    check(m is not None and int(m.group(2)) == 0, 'modtest.py: no check failed')
    check(m is not None and int(m.group(1)) >= 180, 'modtest.py: at least 180 checks passed (%s)' % (m.group(1) if m else 'none'))
    for what in ['import json', 'import re', 'import socket', 'import select', 'import threading', 'import hashlib', 'import decimal', 'import xml.etree.ElementTree',
                 'import zlib', 'import sqlite3', 'import ctypes', 'import curses', 'import ssl', 'program: zlib, gzip, bz2, lzma, zipfile (deflate), tarfile (gz)', 'program: sqlite3: memory database and a file', 'program: ctypes: C functions, structures, pythonapi, a callback', 'program: curses: terminal description, tparm, panel', 'program: ssl: certificate store, TLS handshake and data (memory BIOs)', 'program: socket.socketpair (kernel: AF_UNIX stream pair)', 'program: tempfile: mkdtemp, a file in it, rmtree', 'program: hashlib: md5, sha1, sha2, sha3, blake2', 'program: threading: four threads and a lock',
                 'program: socket and select', 'program: subprocess runs /musl/cat (fork, exec, pipes)']:
        check('ok:   ' + what in out, 'modtest.py: ' + what)
    for line in out.splitlines():
        if 'FAIL' in line or line.startswith('slowest'): print('        ' + line)
    s.pump(0.5)
    if b'EXCEPTION' in s.log or b'killed:' in s.log:
        print('  FAIL: kernel exception or process killed by a fault'); fails += 1
finally:
    s.close()

print('python test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
