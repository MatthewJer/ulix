#!/usr/bin/env python3
"""Stress test (chapter "Stress Tests" of lit/stress.nw): /musl/stress hammers the kernel from many processes while this program watches.

usage: stress_test.py KERNEL DISK.img [DISK2.img DISK3.img]

Boots the kernel (virt machine, a second and a third disk when given, network), logs in and runs "/musl/stress all SECONDS SEED MB host tcp udp".
The watchdog looks at the output: a failed check ("user FAIL"), a kernel exception, a process killed by a fault, "BKL: CPU", out of memory or
a failed eviction is a failure at once; when the output stops completely, or one mode stops printing its progress lines, the machine is
considered hung and is asked through qemu's monitor: the registers of every CPU (PC samples, resolved with addr2line) and the kernel shell (Ctrl-]).
The dump goes to stress-logs/.

Environment (all optional):
  STRESS_SECONDS  length of one run (default 100)         STRESS_CONFIGS  runs, "SMP:FAST:MEM" separated by blanks (default "4:0:128")
  STRESS_SEEDS    first seed, one more for each run (1)   STRESS_MODE     all (default) or one mode, e.g. fs:/tmp
  STRESS_ROUNDS   how often the list of configurations is repeated (1)
  STRESS_STALL    seconds without a line of a mode (150)  STRESS_SILENT   seconds without any output (90)
  QEMU_PORT       first TCP port (45230; port+1 is the monitor, +2 the host's echo servers)
FAST=1 uses the hvf accelerator: real parallel cores, other interleavings than the emulated Cortex-A72.
Exit status 0: no failure in any run.
"""
import os, re, socket, subprocess, sys, threading, time, socketserver

kernel, disk = sys.argv[1:3]
disk2, disk3 = (sys.argv[3:5] + [None, None])[:2]
SECONDS = int(os.environ.get('STRESS_SECONDS', 100))
CONFIGS = os.environ.get('STRESS_CONFIGS', '4:0:128').split()
SEED0 = int(os.environ.get('STRESS_SEEDS', 1))
ROUNDS = int(os.environ.get('STRESS_ROUNDS', 1))
STALL = int(os.environ.get('STRESS_STALL', 150))
SILENT = int(os.environ.get('STRESS_SILENT', 90))
PORT = int(os.environ.get('QEMU_PORT', 45230))
MODE = os.environ.get('STRESS_MODE', 'all')                     # one mode (fork, fs:/tmp, ...) to find out which one fails
PROMPT = rb'esser@ulix\[\d+\]:(/[\w/]*)\$ '
STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
BAD = re.compile(rb'user FAIL|KERNEL EXCEPTION|\[kernel\] process \d+ killed on CPU|BKL: CPU|cannot pick a page|failed: out of memory|did not end and was killed')
os.makedirs('stress-logs', exist_ok=True)


class Echo(socketserver.BaseRequestHandler):                  # the host's echo servers (TCP and UDP) for the socket mode
    def handle(self):
        if isinstance(self.request, tuple):                   # UDP: (data, socket)
            data, sock = self.request
            sock.sendto(data, self.client_address)
        else:
            try:
                while True:
                    d = self.request.recv(4096)
                    if not d: break
                    self.request.sendall(d)
            except OSError: pass


class TcpServer(socketserver.ThreadingMixIn, socketserver.TCPServer): allow_reuse_address = True; daemon_threads = True
class UdpServer(socketserver.ThreadingMixIn, socketserver.UDPServer): allow_reuse_address = True; daemon_threads = True


def monitor(port, commands, wait=0.4):
    out = b''
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=3)
        s.settimeout(wait)
        def drain():
            nonlocal out
            try:
                while True:
                    d = s.recv(65536)
                    if not d: break
                    out += d
            except OSError: pass
        drain()
        for c in commands:
            s.sendall(c.encode() + b'\n'); drain()
        s.close()
    except OSError as e: out += b'monitor: %s' % str(e).encode()
    return out.decode('latin1').replace('\r', '')


def run(smp, fast, mem, seed, index):
    port = PORT
    tag = 'run%d-smp%d-%s-m%d-seed%d' % (index, smp, 'hvf' if fast else 'tcg', mem, seed)
    tcp = TcpServer(('127.0.0.1', port + 2), Echo); udp = UdpServer(('127.0.0.1', port + 2), Echo)
    for srv in (tcp, udp): threading.Thread(target=srv.serve_forever, daemon=True).start()
    cpu = ['-accel', 'hvf', '-cpu', 'host'] if fast else ['-cpu', 'cortex-a72']
    cmd = ['qemu-system-aarch64', '-M', 'virt'] + cpu + ['-m', str(mem), '-display', 'none', '-monitor', 'tcp:127.0.0.1:%d,server=on,wait=off' % (port + 1),
           '-kernel', kernel, '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk, '-device', 'virtio-blk-device,drive=hd0']
    n = 1
    for d in (disk2, disk3):
        if d: cmd += ['-drive', 'file=%s,format=raw,if=none,id=hd%d' % (d, n), '-device', 'virtio-blk-device,drive=hd%d' % n]; n += 1
    cmd += ['-device', 'virtio-rng-device', '-smp', str(smp), '-netdev', 'user,id=n0', '-device', 'virtio-net-device,netdev=n0',
            '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % port]
    q = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sock = None
    for _ in range(100):
        try: sock = socket.create_connection(('127.0.0.1', port)); break
        except OSError: time.sleep(0.1)
    if not sock: q.kill(); return 'cannot connect to qemu'
    sock.settimeout(0.2)
    st = {'log': b'', 'carry': b'', 'last': time.time()}
    full = open('stress-logs/%s.log' % tag, 'wb')

    def pump(sec):
        end = time.time() + sec
        while time.time() < end:
            try: d = sock.recv(4096)
            except socket.timeout: continue
            except OSError: return
            if not d: time.sleep(0.05); continue
            d = STATUS.sub(b'', st['carry'] + d); st['carry'] = b''
            i = d.rfind(b'\x1b7')
            if i >= 0 and len(d) - i < 110 and b'\x1b8' not in d[i:]: st['carry'] = d[i:]; d = d[:i]
            st['log'] += d; st['last'] = time.time(); full.write(d); full.flush()

    def send(text):
        for b in text.encode('latin1'): sock.send(bytes([b])); pump(0.03)

    def hang(why):
        elf = kernel.replace('.bin', '.elf')
        dump = monitor(port + 1, ['info cpus'] + ['info registers -a'] * 3)
        pcs = re.findall(r'PC=([0-9a-f]+)|pc\s*=?\s*0x([0-9a-f]+)', dump, re.I)
        pcs = sorted(set(a or b for a, b in pcs))
        names = ''
        if pcs and os.path.exists(elf):
            r = subprocess.run(['aarch64-elf-addr2line', '-f', '-e', elf] + ['0x' + p for p in pcs], capture_output=True, text=True)
            names = ' '.join(r.stdout.split('\n')[0::2])
        send('\x1d'); pump(1.5)
        for k in ('ps', 'queues', 'locks'): send(k + '\r'); pump(2)
        open('stress-logs/%s.hang' % tag, 'w').write(why + '\n' + dump + '\n=== log tail ===\n' + st['log'][-6000:].decode('latin1'))
        print('  HANG (%s): PC samples %s -> %s; details in stress-logs/%s.hang' % (why, ' '.join(pcs[:8]), names, tag))

    result = 'ok'
    try:
        t0 = time.time()
        while b'ulix login' not in st['log'] and time.time() - t0 < 120: pump(0.2)
        while not re.search(PROMPT, st['log']) and time.time() - t0 < 180: pump(0.2)
        if not re.search(PROMPT, st['log']): hang('no shell prompt after boot'); return 'no prompt'
        st['log'] = b''
        mb = max(4, mem * 9 // 20)
        send('/musl/stress %s %d %d %d 10.0.2.2 %d %d\r' % (MODE, SECONDS, seed, mb, port + 2, port + 2))
        start = time.time()
        modes = {}
        scanned = 0
        while True:
            pump(1.0)
            now = time.time()
            if q.poll() is not None: result = 'qemu exited'; break
            text = st['log']
            for m in re.finditer(rb'stress: (\S+) (\d+|done)', text[scanned:]): modes[m.group(1).decode()] = now
            for m in BAD.finditer(text[scanned:]):
                line = text[scanned + m.start():].split(b'\n')[0][:200]
                print('  FAILURE: %s' % line.decode('latin1').strip()); result = 'failure'
            scanned = len(text)
            endmark = b'stress: all done' if MODE == 'all' else b'stress: %s done' % MODE.encode()
            if endmark in text:
                print('  ' + re.search(re.escape(endmark) + rb'[^\r\n]*', text).group().decode()); break
            if now - st['last'] > SILENT: hang('no output for %d s' % SILENT); result = 'hang'; break
            stalled = [k for k, v in modes.items() if now - v > STALL and b'stress: %s done' % k.encode() not in text]
            if stalled: hang('mode %s printed nothing for %d s' % (','.join(stalled), STALL)); result = 'hang'; break
            if now - start > SECONDS + 240: hang('run does not end'); result = 'hang'; break
        done = [m.group().decode() for m in re.finditer(rb'stress: \S+ done, \d+ rounds, \d+ failed', st['log'])]
        if result == 'ok' and (MODE == 'all' and (not b'stress: all done, 0 of' in st['log'] or len(done) < 11) or MODE != 'all' and not re.search(rb'done, \d+ rounds, 0 failed', st['log'])): result = 'modes failed or missing'
        print('  %s: %d modes finished: %s' % (tag, len(done), '; '.join(d.replace('stress: ', '').replace(' rounds', '') for d in done)[:600]))
    finally:
        q.kill(); full.close()
        for srv in (tcp, udp): srv.shutdown(); srv.server_close()
    return result


bad = 0
index = 0
for r in range(ROUNDS):
    for c in CONFIGS:
        smp, fast, mem = [int(x) for x in c.split(':')]
        seed = SEED0 + index
        print('stress run %d: %d CPU(s), %s, %d MByte, seed %d, %d s' % (index, smp, 'hvf' if fast else 'emulated Cortex-A72', mem, seed, SECONDS), flush=True)
        res = run(smp, fast, mem, seed, index)
        print('  -> %s' % res, flush=True)
        if res != 'ok': bad += 1
        index += 1
        PORT += 0
print('stress-test: %d of %d runs failed' % (bad, index))
sys.exit(1 if bad else 0)
