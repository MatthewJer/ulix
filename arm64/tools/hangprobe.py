#!/usr/bin/env python3
"""Starts a command in a login session, waits, and then asks the kernel shell (Ctrl-]) what the threads are doing.

usage: hangprobe.py KERNEL DISK.img SECONDS COMMAND [KSH-COMMAND...]      (default kernel shell commands: ps queues locks)
Prints the terminal output, then the answers of the kernel shell. For finding out where a program that hangs is stuck.
"""
import os, re, socket, subprocess, sys, time

kernel, disk, wait, command = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
ksh = sys.argv[5:] or ['ps', 'queues', 'locks']
PORT = int(os.environ.get('QEMU_PORT', 45195))
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '256', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
       '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk, '-device', 'virtio-blk-device,drive=hd0',
       '-device', 'virtio-rng-device', '-smp', os.environ.get('SMP', '2'), '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT, '-gdb', 'tcp::%d' % (PORT + 1)]
q = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = None
for _ in range(100):
    try: sock = socket.create_connection(('127.0.0.1', PORT)); break
    except OSError: time.sleep(0.1)
sock.settimeout(0.2)
log = b''
def pump(sec):
    global log
    end = time.time() + sec
    while time.time() < end:
        try:
            d = sock.recv(4096)
            if d: log += STATUS.sub(b'', d)
        except socket.timeout: pass
def send(text):
    for b in text.encode('latin1'):
        sock.send(bytes([b])); pump(0.03)
try:
    t = time.time()
    while b'$ ' not in log and time.time() - t < 90: pump(0.2)
    mark = len(log)
    send(command + '\r')
    pump(wait)
    print(log[mark:].decode('latin1').replace('\r', ''))
    if os.environ.get('LLDB'):                  # LLDB=ulix-shell.elf: ask the CPUs through qemu's gdb stub (where is each CPU, what do the threads say)
        elf = os.environ['LLDB']
        script = ['target create ' + elf, 'gdb-remote 127.0.0.1:%d' % (PORT + 1), 'thread list'] + [
            'thread select %d' % i for i in range(1, 3)] + ['register read pc sp x30 cpsr'] + os.environ.get('LLDBCMD', '').split(';')
        args = ['lldb', '-b'] + sum([['-o', c] for c in script if c], [])
        print(subprocess.run(args, capture_output=True, text=True, timeout=120).stdout)
    else:
        mark = len(log)
        send('\x1d'); pump(1.5)
        for k in ksh:
            send(k + '\r'); pump(1.5)
        print('=== kernel shell ===')
        print(log[mark:].decode('latin1').replace('\r', ''))
finally:
    q.kill()
