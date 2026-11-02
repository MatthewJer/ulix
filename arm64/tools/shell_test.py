#!/usr/bin/env python3
"""Drives a complete Ulix session over the UART like a user at a terminal:
boot -> /init -> /bin/login (auto login) -> /bin/sh, then runs commands (the AArch64 builds of the
programs in lib-build/tools), logs out, logs in again with a password and switches terminals.

usage: shell_test.py KERNEL.elf DISK.img
"""
import re, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
PORT = 45125
qemu = subprocess.Popen(['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '128', '-display', 'none',
                         '-monitor', 'none', '-kernel', kernel, '-snapshot',
                         '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk, '-device', 'virtio-blk-device,drive=hd0',
                         '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = None
for _ in range(100):
    try: sock = socket.create_connection(('127.0.0.1', PORT)); break
    except OSError: time.sleep(0.1)
if not sock: qemu.kill(); sys.exit('cannot connect to qemu')
sock.settimeout(0.2)
log, fails = b'', 0

def pump(seconds):
    global log
    end = time.time() + seconds
    while time.time() < end:
        try:
            data = sock.recv(4096)
            if data: log += data
        except socket.timeout: pass

def expect(pattern, what=None, timeout=25.0):
    """wait for a regular expression in the output that arrived since the last successful expect()"""
    global log, fails
    start = time.time()
    while time.time() - start < timeout:
        pump(0.1)
        m = re.search(pattern.encode(), log)
        if m:
            seen = log[:m.end()]; log = log[m.end():]
            print('  ok:   %s' % (what or pattern)); return seen
    print('  FAIL: %s -- did not see %r; got: %r' % (what or pattern, pattern, log[-160:])); fails += 1
    return b''

def send(text):
    for b in text.encode('latin1').decode('unicode_escape').encode('latin1'):
        sock.send(bytes([b])); pump(0.06)

PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '

def run(cmd, answer, what=None, prompt=PROMPT):
    send(cmd + '\\r')
    seen = expect(answer, what or "%s -> %s" % (cmd, answer))
    expect(prompt, 'prompt is back after "%s"' % cmd)
    return seen

try:
    expect(r'console ready', 'kernel booted')
    expect(r'ulix login: esser', 'init started login, which logs in "esser" automatically')
    expect(PROMPT, 'shell prompt (/bin/sh on the AArch64 userland)')

    send('thread 1\\r'); expect(r'PROCESS EXIT, counter = 0', 'the book\'s thread program (3 threads, mutex) ends with counter = 0', timeout=150)
    expect(PROMPT, 'prompt is back after "thread 1"')
    run('ls /', r'bin/.*hello\.c', 'ls /')
    run('cat /hello.c', r'Hello World', 'cat /hello.c')
    send('cd /tmp\\r'); expect(r'esser@ulix\[\d+\]:/tmp\$ ', 'cd /tmp changes the prompt')
    P = r'esser@ulix\[\d+\]:/tmp\$ '
    run('touch f1', r'', 'touch f1', P)
    run('cp /hello.c h2', r'', 'cp /hello.c h2', P)
    run('wc h2', r'16 45 316 h2', 'wc counts 16 lines, 45 words, 316 bytes', P)
    run('grep Hello h2', r'printf \("Hello World', 'grep finds the line', P)
    run('stat h2', r'size:\s+316', 'stat shows size 316', P)
    run('stat h2', r'mtime:\s+1[7-9]\d{8}', 'the new file has a current modification time', P)
    run('mkdir d', r'', 'mkdir d', P)
    run('ls', r'f1.*h2.*d/', 'ls shows f1, h2 and d/', P)
    run('wc h2 > out', r'', 'output redirection with >', P)
    run('cat out', r'316', 'the redirected output is in the file', P)
    run('rm h2', r'unlink returns 0', 'rm h2', P)
    run('rm f1', r'unlink returns 0', 'rm f1', P)
    run('rm out', r'unlink returns 0', 'rm out', P)
    run('rmdir d', r'rmdir returns 0', 'rmdir d', P)
    run('ls', r'\./\s+\d+ \.\./\s*\r?\n', 'only . and .. are left', P)
    send('cd\\r'); expect(PROMPT, 'cd goes home')
    send('ps\\r'); seen = expect(PROMPT, 'ps ran')
    for what in [b'login', b'/bin/sh', b'swapper', b'tty9', b'idle']:
        if what in seen: print('  ok:   ps lists %s' % what.decode())
        else: print('  FAIL: ps does not list %s' % what.decode()); fails += 1
    run('df', r'/dev/hda\s+4096 K', 'df shows the disk')
    run('free', r'.', 'free runs')

    # a program that reads the terminal, ended with Ctrl-C
    send('echo\\r'); pump(0.5)
    send('typed to echo\\r'); expect(r'typed to echo\r?\n', 'echo repeats what is typed (the program prints it, the console does not)')
    send('\\x03'); expect(r'Killed', 'Ctrl-C ends the foreground program')
    expect(PROMPT, 'prompt is back after Ctrl-C')

    # a second terminal (Ctrl-T 2 = Alt-2): the root login runs there, and its output was kept
    send('\\x142'); expect(r'\[terminal 2\]', 'Ctrl-T 2 switches to terminal 2')
    expect(r'root@ulix\[\d+\]:/root# ', 'root has a shell there (prompt ends with #)')
    send('ps\\r'); expect(r'tty1.*/bin/sh', 'ps on terminal 2 works')
    send('\\x141'); expect(r'\[terminal 1\]', 'Ctrl-T 1 switches back')
    send('\\r'); expect(PROMPT, 'the first shell still works')

    # log out and log in again with a password (typed, not echoed)
    send('exit\\r'); expect(r'ulix login: ', 'exit ends the shell, login asks again')
    send('esser\\r'); expect(r'Password: ', 'login asks for the password')
    send('xyz\\r');   expect(PROMPT, 'the password "xyz" is accepted, a new shell starts')
    run('pwd', r'/home/esser', 'pwd', PROMPT)
    pump(0.5)
    if b'EXCEPTION' in log or b'killed:' in log: print('  FAIL: kernel exception or killed process'); fails += 1
    else: print('  ok:   no kernel exception, no process killed by a fault')
finally:
    qemu.kill()
print('shell test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
