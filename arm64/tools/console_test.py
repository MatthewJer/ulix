#!/usr/bin/env python3
"""Talks to the Ulix console like a terminal would: starts qemu with the UART on a TCP port, types
characters and checks what comes back.

usage: console_test.py KERNEL.elf DISK.img
"""
import os, re, shlex, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45123))
# QEMU_M (machine), QEMU_MEM (MByte) and QEMU_EXTRA (further arguments, e.g. '-smp 4') select another configuration
if os.environ.get('QEMU_BOARD') == 'rpi4':               # the Raspberry Pi 4B in qemu: "disk" is the SD card image (make sd.img)
    cmd = ['qemu-system-aarch64', '-M', 'raspi4b', '-dtb', 'rpi4-qemu.dtb', '-display', 'none', '-monitor', 'none', '-kernel', kernel, '-snapshot',
           '-drive', 'file=%s,if=sd,format=raw' % disk, '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
else:
    cmd = ['qemu-system-aarch64', '-M', os.environ.get('QEMU_M', 'virt'), '-cpu', 'cortex-a72', '-m', os.environ.get('QEMU_MEM', '128'), '-display', 'none',
           '-monitor', 'none', '-kernel', kernel, '-snapshot',
           '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk, '-device', 'virtio-blk-device,drive=hd0',
           '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
qemu = subprocess.Popen(cmd + shlex.split(os.environ.get('QEMU_EXTRA', '')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = None
for _ in range(100):
    try:
        sock = socket.create_connection(('127.0.0.1', PORT)); break
    except OSError:
        time.sleep(0.1)
if not sock:
    qemu.kill(); sys.exit('cannot connect to qemu')
sock.settimeout(0.2)

log = b''
fails = 0

# The status line (line 25) is painted with its own escape sequence; it is for a real terminal and not part of the output
# that is checked here.  (A sequence may arrive in two pieces: the first piece is kept until the rest is there.)
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
carry = b''
def feed(data):
    global log, carry
    data = STATUS.sub(b'', carry + data); carry = b''
    i = data.rfind(b'\x1b7')
    if i >= 0 and len(data) - i < 110 and b'\x1b8' not in data[i:]: carry = data[i:]; data = data[:i]
    log += data

def pump(seconds=0.0):
    global log
    end = time.time() + seconds
    while True:
        try:
            data = sock.recv(4096)
            if data: feed(data)
            elif time.time() >= end: break
        except socket.timeout:
            if time.time() >= end: break

def expect(text, timeout=20.0):
    """wait until text appears in the output that arrived after the last expect()"""
    global log, fails
    start = time.time()
    while time.time() - start < timeout:
        pump(0.1)
        m = re.search(re.escape(text.encode()) if not text.startswith('re:') else text[3:].encode(), log)
        if m:
            log = log[m.end():]
            print('  ok:   saw %r' % text); return True
    print('  FAIL: did not see %r; got: %r' % (text, log[-200:])); fails += 1
    return False

def send(data, pause=0.15):
    for b in data:                       # one character at a time, like a person typing
        sock.send(bytes([b])); pump(pause)

try:
    expect('console ready')
    expect('contest: pid')
    expect('prompt1> ')
    send(b'hello\r')
    expect('you typed: [hello]')
    expect('prompt2> ')
    send(b'ab\x7fc\r')                   # a, b, DEL (backspace), c  ->  "ac"
    expect('you typed: [ac]')
    expect('now spinning')
    send(b'\x03')                        # Ctrl-C
    expect('Killed')

    send(b'\x1d')                        # Ctrl-]  -> kernel shell
    expect('Ulix Kernel Shell')
    for cmd, answer in [('uname', 'Ulix'), ('ps', 'idle'), ('queues', 'keyboard:'), ('time', 're:\\d+'),
                        ('hexdump', 're:[0-9a-f]{8}  [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2} '), ('gf', 'New frame ID'), ('mem2', 'frames in use'), ('mem', 'root table at'),
                        ('listas', 'kernel'), ('bogus', 'no such command'), ('help', 'Commands:'),
                        # the rest of the commands of the kernel shell (the answer may be only the next prompt)
                        ('test', 'root table of the AS'), ('locks', 'disk buffer'), ('longhelp', 'return to user mode'),
                        ('lsof', 'mfd='), ('inode', 'mx_read_inode'), ('gp', None), ('rp', 'releasing page range'),
                        ('disable', None), ('enable', None), ('clear', 're:\x1b\[2J'),
                        ('gp1k', None), ('gp10k', None)]:
        send(cmd.encode() + b'\r')
        if answer is None:                   # no output: the shell must still answer the next command
            time.sleep(0.5); send(b'uname\r'); expect('Ulix')
        else:
            expect(answer)
    send(b'exit\r')
    expect('returning from kernel shell')
    pump(1.0)
    if b'EXCEPTION' in log: print('  FAIL: kernel exception'); fails += 1
    else: print('  ok:   no kernel exception')
finally:
    qemu.kill()
print('console test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
