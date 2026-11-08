#!/usr/bin/env python3
"""Drives a complete Ulix session over the UART like a user at a terminal:
boot -> /init -> /bin/login (auto login) -> /bin/sh, then runs commands (the AArch64 builds of the
programs in lib-build/tools), logs out, logs in again with a password and switches terminals.

usage: shell_test.py KERNEL.elf DISK.img
"""
import os, re, shlex, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45125))
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
    try: sock = socket.create_connection(('127.0.0.1', PORT)); break
    except OSError: time.sleep(0.1)
if not sock: qemu.kill(); sys.exit('cannot connect to qemu')
sock.settimeout(0.2)
log, fails = b'', 0

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

def pump(seconds):
    global log
    end = time.time() + seconds
    while time.time() < end:
        try:
            data = sock.recv(4096)
            if data: feed(data)
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
    if os.environ.get('HAVE_DISK2'):                     # qemu was given a second disk (make pcheck, job "shell2")
        run('df', r'/dev/hdb\s+32768 K .*/tmp/\r?\n', 'df: /dev/hdb (32 MByte) is mounted on /tmp/')
        run('ls /dev', r'hda.*hdb.*kmem', 'ls /dev: the device files hda, hdb and kmem exist')
        run('cp /hello.c /tmp/h2.c', r'', 'cp into /tmp (second disk)', PROMPT)
        run('ls /tmp', r'h2\.c.*swap|swap.*h2\.c', '/tmp (on hdb) holds the new file and the swap file')
        run('diff /hello.c /tmp/h2.c', r'', 'the copy on the second disk is identical', PROMPT)
        run('rm /tmp/h2.c', r'', 'rm on the second disk', PROMPT)
    if os.environ.get('HAVE_DISK3'):                     # a third disk stands in for the floppy /dev/fd1, mounted on /mnt
        run('df', r'/dev/fd1\s+1440 K .*/mnt/\r?\n', 'df: /dev/fd1 (1440 KByte) is mounted on /mnt/')
        run('ls /mnt', r'a\.txt', 'ls /mnt shows a.txt')
        run('cp /hello.c /mnt/h.c', r'', 'cp onto the floppy', PROMPT)
        run('diff /hello.c /mnt/h.c', r'', 'the copy on the floppy is identical', PROMPT)
        send('testcp\\r'); expect(r'copying /mnt/a\.txt to /mnt/b\.txt', 'the book\'s testcp program copies /mnt/a.txt to /mnt/b.txt')
        pump(2); send('\\x03'); expect(PROMPT, 'Ctrl-C ends testcp (it copies forever)')
        run('diff /mnt/a.txt /mnt/b.txt', r'', 'the copy made by testcp is identical to a.txt', PROMPT)
        run('rm /mnt/h.c', r'', 'rm on the floppy', PROMPT)
    if os.environ.get('HAVE_NET'):                       # qemu was given a network card (make pcheck, job "shellnet")
        run('ping -c 2 10.0.2.2', r'2 packets transmitted, 2 received, 0% packet loss', 'ping -c 2 10.0.2.2: both requests are answered by the router of qemu')
        run('ping -c 1 10.0.2.99', r'0 packets transmitted, 0 received', 'ping of an address that nobody has: no answer to ARP, nothing is sent')
        run('ping', r'usage: ping', 'ping without an address shows how to use it')
        run('ifconfig', r'(?s)vioif0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> mtu 1500.*ether 52:54:00:12:34:56.*inet 10\.0\.2\.15 netmask 0xffffff00 broadcast 10\.0\.2\.255.*status: active', 'ifconfig shows the interface, the Ethernet address, the address and the link status (as the BSDs do)')
        run('ifconfig vioif0 inet 10.0.2.16', r'permission denied', 'ifconfig: only root may change the address')
        run('route add default 10.0.2.9', r'permission denied', 'route: only root may change the routes')
        run('dhclient', r'permission denied', 'dhclient: only root may configure the network')
        run('route show', r'(?s)Destination\s+Gateway\s+Flags\s+Netif\s+default\s+10\.0\.2\.2\s+UGS\s+vioif0\s+10\.0\.2\.0/24\s+link#1\s+U\s+vioif0', 'route show: the default route and the route to our own network (anybody may look)')
    run('fbomb', r'', 'fbomb starts 512 processes', PROMPT); pump(8)
    run('ps', r'(?s)PID\s+PPID.*login', 'the system is alive after the fork bomb (ps lists the login processes)')
    send('top\\r'); expect(r'(?s)top - up .*processes.*CPU0 \[.*\] \d+%.*Mem  \[.*MByte.*PID\s+PPID\s+UID S\s+CPU%\s+TIME\s+COMMAND.*/bin/sh', 'top shows the load of the CPU, the memory and the processes')
    send('q'); expect(PROMPT, 'q ends top, the shell is back')
    run('readelf /bin/cat', r'e_entry:\s+400000\r?\n#pheaders: 3\r?\n', 'readelf reads the ELF64 header (entry 0x400000, 3 program headers)')
    run('inode 2', r'i_nlinks: 1\r?\n.*i_size:\s+\d+', 'inode: the book\'s debug system call 777 prints an inode')
    send('su\\r'); expect(r'Enter root password: ', 'su asks for the root password')
    send('xyz\\r'); expect(r'root@ulix\[\d+\]:/root# ', 'su: the password is accepted, a root shell starts')
    send('chown 5 /hello.c\\r'); expect(r'root@ulix\[\d+\]:/root# ', 'chown as root is not refused')
    send('stat /hello.c\\r'); expect(r'uid/gid: 5\r?\n', 'stat shows the new owner')
    send('chown 1000 /hello.c\\r'); expect(r'root@ulix\[\d+\]:/root# ', 'owner set back')
    if os.environ.get('HAVE_NET'):                       # root configures the network like on a BSD
        R = r'root@ulix\[\d+\]:/root# '
        run('ifconfig vioif0 inet 10.0.2.16 netmask 255.255.255.0', r'', 'ifconfig sets the address 10.0.2.16', R)
        run('ifconfig', r'inet 10\.0\.2\.16 netmask 0xffffff00 broadcast 10\.0\.2\.255', 'ifconfig shows the new address', R)
        run('ifconfig vioif0 10.0.2.17/25', r'', 'ifconfig sets 10.0.2.17/25 (the netmask by its length)', R)
        run('ifconfig', r'inet 10\.0\.2\.17 netmask 0xffffff80 broadcast 10\.0\.2\.127', 'ifconfig shows the netmask 0xffffff80', R)
        run('ifconfig vioif9 10.0.2.15', r'interface vioif9 does not exist', 'ifconfig: an unknown interface is refused', R)
        run('route delete default', r'', 'route delete default', R)
        run('route show', r'(?s)Routing tables.*10\.0\.2\.0/25\s+link#1(?!.*default)', 'route show: the default route is gone, the route to 10.0.2.0/25 is there', R)
        run('route add default 10.0.2.2', r'', 'route add default 10.0.2.2', R)
        run('route show', r'default\s+10\.0\.2\.2\s+UGS', 'route show: the default route is back', R)
        run('dhclient vioif0', r'(?s)DHCPDISCOVER on vioif0.*DHCPACK from 10\.0\.2\.2\s+bound to 10\.0\.2\.15 netmask 255\.255\.255\.0\s+router 10\.0\.2\.2\s+name server 10\.0\.2\.3\s+lease \d+ seconds', 'dhclient: DHCP gives 10.0.2.15, router and name server back', R)
        run('ifconfig', r'inet 10\.0\.2\.15 netmask 0xffffff00', 'ifconfig shows the address from DHCP', R)
        run('ping -c 1 10.0.2.2', r'1 packets transmitted, 1 received', 'the network works with the address from DHCP', R)
    send('exit\\r'); expect(PROMPT, 'exit leaves the root shell (back to esser)')
    send('cd /tmp\\r'); expect(r'esser@ulix\[\d+\]:/tmp\$ ', 'cd /tmp changes the prompt')
    P = r'esser@ulix\[\d+\]:/tmp\$ '
    run('touch f1', r'', 'touch f1', P)
    run('cp /hello.c h2', r'', 'cp /hello.c h2', P)
    run('wc h2', r'16 45 316 h2', 'wc counts 16 lines, 45 words, 316 bytes', P)
    run('grep Hello h2', r'printf \("Hello World', 'grep finds the line', P)
    run('stat h2', r'size:\s+316', 'stat shows size 316', P)
    run('stat h2', r'mtime:\s+1[7-9]\d{8}', 'the new file has a current modification time', P)
    run('mkdir d', r'', 'mkdir d', P)
    run('ls', r'(?s)f1.*h2.*d/', 'ls shows f1, h2 and d/', P)
    run('wc h2 > out', r'', 'output redirection with >', P)
    run('cat out', r'316', 'the redirected output is in the file', P)
    run('rm h2', r'unlink returns 0', 'rm h2', P)
    run('rm f1', r'unlink returns 0', 'rm f1', P)
    run('rm out', r'unlink returns 0', 'rm out', P)
    run('rmdir d', r'rmdir returns 0', 'rmdir d', P)
    run('ls', r'\./\s+\d+ \.\./\s+\d+ swap\s*\r?\n', 'only . and .. and the swap file /tmp/swap are left', P)
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
    send('\\x142'); expect(r'root@ulix\[\d+\]:/root# ', 'Ctrl-T 2 switches to terminal 2: its screen is painted, with the prompt of the root shell')
    send('ps\\r'); expect(r'tty1.*/bin/sh', 'ps on terminal 2 works')
    send('\\x141'); expect(PROMPT, 'Ctrl-T 1 switches back: the screen of the first shell is painted again')
    send('\\r'); expect(PROMPT, 'the first shell still works')

    # log out and log in again with a password (typed, not echoed)
    send('exit\\r'); expect(r'ulix login: ', 'exit ends the shell, login asks again')
    send('esser\\r'); expect(r'Password: ', 'login asks for the password')
    send('xyz\\r');   expect(PROMPT, 'the password "xyz" is accepted, a new shell starts')
    run('pwd', r'/home/esser', 'pwd', PROMPT)
    # vi: the editor of lib-build/tools uses clrscr, set_xy, printf and ureadchar (screen functions of the book)
    # the stack of a process grows by itself: the book's recurse recurses until Ctrl-C
    send('recurse\\r'); expect(r'recursing: level = 2000,', 'recurse: 2000 levels deep (the stack grows page by page)', timeout=60)
    send('\\x03'); expect(r'Killed', 'Ctrl-C ends recurse')
    expect(PROMPT, 'the shell is back after recurse')
    send('vi /hello.c\\r'); expect(r'DONE READING', 'vi reads the file')
    expect(r'\x1b\[\d+;\d+H[^\x1b]*#include', 'vi paints the first line of the file at the top of the screen')
    send('x'); expect(r'DELETE_C', 'vi command mode: x (delete character) is shown in line 24')
    send(':'); send('q!\\r'); expect(r'quit!', 'vi: :q! ends the editor')
    expect(PROMPT, 'the shell is back after vi')
    pump(0.5)
    if b'EXCEPTION' in log or b'killed:' in log: print('  FAIL: kernel exception or killed process'); fails += 1
    else: print('  ok:   no kernel exception, no process killed by a fault')
    # a bad pointer in a system call: like on the x86 the kernel ends in the kernel shell
    send('/badptr64\\r'); expect(r'KERNEL EXCEPTION', 'a bad pointer in a system call is a kernel exception')
    expect(r'Ulix Kernel Shell', 'the kernel shell is started, as on the x86')
    send('uname\\r'); expect(r'Ulix', 'the kernel shell works (uname)')
    send('test\\r'); expect(r'root table of the AS', 'kernel shell: test shows the page table data')
    send('rp\\r'); expect(r'releasing page range', 'kernel shell: rp releases a page range')
    send('div0\\r'); expect(r'KERNEL EXCEPTION', 'kernel shell: div0 raises the exception like on the x86')
    expect(r'Ulix Kernel Shell', 'and ends in the kernel shell again')
finally:
    qemu.kill()
print('shell test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
