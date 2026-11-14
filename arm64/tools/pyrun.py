#!/usr/bin/env python3
"""Runs shell commands in a login session of Ulix under qemu and prints what they write (a helper for trying things out).

usage: pyrun.py KERNEL DISK.img [-t SECONDS] COMMAND...      (each COMMAND is one line for the shell; the disk is not changed)
"""
import os, re, socket, subprocess, sys, time

args = sys.argv[1:]
kernel, disk = args[:2]
args = args[2:]
limit = 300.0
if args[:1] == ['-t']: limit = float(args[1]); args = args[2:]
PORT = int(os.environ.get('QEMU_PORT', 45190))
PROMPT = rb'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)

cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '256', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
       '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk, '-device', 'virtio-blk-device,drive=hd0',
       '-device', 'virtio-rng-device', '-smp', '2', '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = None
for _ in range(100):
    try: sock = socket.create_connection(('127.0.0.1', PORT)); break
    except OSError: time.sleep(0.1)
if not sock: qemu.kill(); sys.exit('cannot connect to qemu')
sock.settimeout(0.2)
log = b''

def pump(seconds):
    global log
    end = time.time() + seconds
    while time.time() < end:
        try:
            d = sock.recv(4096)
            if d: log += STATUS.sub(b'', d)
        except socket.timeout: pass

def wait_prompt(timeout):                  # (prints what arrives, so that a long run can be followed)
    global log
    start, shown = time.time(), 0
    def show(upto):
        nonlocal shown
        sys.stdout.write(log[shown:upto].decode('latin1').replace('\r', '')); sys.stdout.flush(); shown = upto
    while time.time() - start < timeout:
        pump(0.1)
        m = re.search(PROMPT, log)
        if m:
            show(m.end()); log = log[m.end():]
            return
        show(len(log))
    show(len(log)); log = b''
    print('\n[pyrun: timeout]')

try:
    wait_prompt(60)
    for c in args:
        for b in (c + '\r').encode('latin1'):
            sock.send(bytes([b])); pump(0.03)
        wait_prompt(limit)
finally:
    qemu.kill()
