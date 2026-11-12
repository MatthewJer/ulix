#!/usr/bin/env python3
"""Tests the daemon concept (chapter "Daemons and services"): the programs service and daemon, /etc/rc.d, the pid file and the start
of the services at boot. Like shell_test.py it drives a real login session over the UART.

First boot (the disk as it is, heartbeat is switched off): service -l, status, start, the daemon writes its log, a second start is
refused, the daemon runs on its own terminal, stop, restart, an unknown service. Second boot (rc.conf says heartbeat_enable="YES"):
the kernel starts the service before the login appears.

usage: daemon_test.py KERNEL DISK.img
"""
import os, re, shlex, shutil, socket, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
here = os.path.dirname(os.path.abspath(__file__))
PORT = int(os.environ.get('QEMU_PORT', 45160))
PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
fails = 0


class Session:
    """one boot of the machine, and the keyboard and the screen of the console"""
    def __init__(self, image):
        cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '128', '-display', 'none', '-monitor', 'none', '-kernel', kernel,
               '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % image, '-device', 'virtio-blk-device,drive=hd0',
               '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
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
        print('  FAIL: %s -- did not see %r; got: %r' % (what or pattern, pattern, self.log[-200:])); fails += 1
        return ''

    def send(self, text):
        for b in text.encode('latin1'):
            self.sock.send(bytes([b])); self.pump(0.06)

    def run(self, cmd, answer, what=None, timeout=25.0):
        """type a command, expect its answer and the next prompt; returns everything the command printed"""
        self.send(cmd + '\r')
        seen = self.expect(answer, what or '%s -> %s' % (cmd, answer), timeout)
        return seen + self.expect(PROMPT, 'prompt is back after "%s"' % cmd)

    def close(self): self.qemu.kill()


def check(cond, what):
    global fails
    print('  %s %s' % ('ok:  ' if cond else 'FAIL:', what))
    if not cond: fails += 1


# ---- first boot
s = Session(disk)
try:
    s.expect(r'console ready', 'kernel booted')
    s.expect(r'ulix login: esser', 'init started login, which logs in "esser" automatically')
    s.expect(PROMPT, 'shell prompt')
    s.run('service -l', r'heartbeat\s+\(off\)', 'service -l: heartbeat is a service, and it is switched off')
    s.run('service -e', r'\r\n', 'service -e: nothing is switched on')
    s.run('service heartbeat status', r'heartbeat is not running', 'status of a service that was never started')
    out = s.run('service heartbeat start', r'Starting heartbeat \(pid \d+\)', 'service start: the daemon is started and the pid is shown')
    pid = int(re.search(r'pid (\d+)', out).group(1))
    out = s.run('cat /var/run/heartbeat.pid', r'\d+', 'the pid file holds the process number')
    check(str(pid) in out, 'the number in the pid file is the one that service printed')
    s.pump(3.5)
    out = s.run('cat /var/log/heartbeat.log', r'heartbeat 1\r?\nheartbeat 2', 'the daemon works in the background: it wrote its first lines to its log')
    s.run('service heartbeat start', r'heartbeat is already running \(pid %d\)' % pid, 'a second start is refused (the pid file belongs to a living process)')
    s.run('service heartbeat status', r'heartbeat is running as pid %d' % pid, 'status: running')
    out = s.run('ps', r'heartbeat', 'ps shows the daemon')
    check(re.search(r'\n\s*%d\s' % pid, out) is not None, 'ps lists exactly the pid that service printed')
    s.run('service heartbeat stop', r'Stopped heartbeat', 'service stop: SIGTERM reaches the handler of the daemon, it ends')
    s.run('service heartbeat status', r'heartbeat is not running', 'status: not running')
    out = s.run('ls /var/run', r'\r\n', 'ls /var/run')
    check('heartbeat.pid' not in out, 'stop removed the pid file')
    out = s.run('ps', r'\r\n', 'ps after stop')
    check('heartbeat' not in out, 'the daemon is gone from the process list')
    s.run('service heartbeat restart', r'Starting heartbeat \(pid \d+\)', 'service restart: stops (if needed) and starts')
    s.run('service heartbeat stop', r'Stopped heartbeat', 'stop again')
    s.run('service nonsense start', r'no such service', 'an unknown service is reported')
    s.run('service', r'usage: service', 'service without arguments shows how to use it')
    s.pump(0.5)
    check(b'EXCEPTION' not in s.log and b'killed:' not in s.log, 'no kernel exception, no process killed by a fault')
finally:
    s.close()

# ---- second boot: rc.conf switches the service on, the kernel starts it before the login
tmp = tempfile.mkdtemp()
on = os.path.join(tmp, 'on.img')
shutil.copy(disk, on)
conf = os.path.join(tmp, 'rc.conf')
open(conf, 'w').write('# heartbeat on\nheartbeat_enable="YES"\n')
subprocess.run([sys.executable, os.path.join(here, 'minix_add.py'), on, '/etc/rc.conf=' + conf], check=True, stdout=subprocess.DEVNULL)
s = Session(on)
try:
    s.expect(r'Starting heartbeat \(pid \d+\)', 'at boot the kernel ran "service -a start": heartbeat was started before login', timeout=60)
    s.expect(r'ulix login: esser', 'the login appears afterwards')
    s.expect(PROMPT, 'shell prompt')
    out = s.run('service -e', r'heartbeat', 'service -e: heartbeat is switched on')
    check('(off)' not in out, 'it is not marked off')
    s.pump(2.5)
    s.run('cat /var/log/heartbeat.log', r'heartbeat 1', 'the daemon that the boot started has written its log')
    s.run('service heartbeat status', r'heartbeat is running as pid', 'status shows the daemon of root although esser may not signal it')
    s.run('service heartbeat stop', r'permission denied', 'only the owner (root) may stop it: esser is refused')
finally:
    s.close()
    shutil.rmtree(tmp, ignore_errors=True)

print('daemon test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
