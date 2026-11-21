#!/usr/bin/env python3
"""The text editor nano in a login session (chapter "nano", lit/nano.nw): usage: nano_test.py KERNEL.elf DISK.img

Boots the shell kernel, logs in over the UART like a person at a terminal and runs /bin/nano. A small terminal emulator (cursor addressing,
colours, the second screen of xterm) shows what the user would see; the keys are sent as an xterm sends them (Ctrl-keys, the arrow keys
as ESC O A.., Home/End, Delete and Page Up/Down as ESC [ ..~). Checked: a Python file and a C file are typed, edited in the middle of a line and
saved, and the keywords, strings and comments have colours; a file that does not exist; a file of 300 lines with Page Down, search, cut and paste;
Ctrl-C does not end nano. A second boot with a network card repeats "open, type, save, quit" through the telnet server.
QEMU_PORT, QEMU_FAST=1, QEMU_M and QEMU_EXTRA select the machine as in shell_test.py (hvf needs QEMU_M=virt,gic-version=3 and -smp 2).
"""
import os, re, shlex, shutil, socket, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
PORT = int(os.environ.get('QEMU_PORT', 45225))
TELNET_PORT = int(os.environ.get('QEMU_TELNET_PORT', 45226))
fails = 0
def check(cond, msg):
    global fails
    print('  %s %s' % ('ok:  ' if cond else 'FAIL:', msg))
    if not cond: fails += 1

class Screen:
    """The screen of an xterm (80 x 24): the cells with their colour, the cursor, the scroll region, ESC 7/8, the second screen (1049)."""
    def __init__(s, w=80, h=24):
        s.w, s.h = w, h
        s.main = s.blank(); s.alt = None; s.grid = s.main
        s.x = s.y = 0; s.pend = False; s.top, s.bot = 0, h - 1; s.fg = None; s.bold = False; s.saved = (0, 0); s.buf = b''
    def blank(s): return [[(' ', None) for _ in range(s.w)] for _ in range(s.h)]
    def put(s, ch):
        if s.pend: s.x, s.pend = 0, False; s.index()
        s.grid[s.y][s.x] = (ch, s.color())
        if s.x == s.w - 1: s.pend = True
        else: s.x += 1
    def color(s): return None if s.fg is None else (s.fg + (8 if s.bold and s.fg < 8 else 0))      # (bold counts as the bright colour)
    def index(s):
        if s.y == s.bot: del s.grid[s.top]; s.grid.insert(s.bot, [(' ', None)] * s.w)
        elif s.y < s.h - 1: s.y += 1
    def rindex(s):
        if s.y == s.top: del s.grid[s.bot]; s.grid.insert(s.top, [(' ', None)] * s.w)
        elif s.y > 0: s.y -= 1
    def feed(s, data):
        data = s.buf + data; s.buf = b''; i = 0
        while i < len(data):
            b = data[i]; i += 1
            if b == 27:
                if i >= len(data): s.buf = data[i - 1:]; return
                c = data[i]; i += 1
                if c == ord('['):
                    j = i
                    while j < len(data) and not 0x40 <= data[j] <= 0x7e: j += 1
                    if j >= len(data): s.buf = data[i - 2:]; return
                    s.csi(data[i:j].decode('latin1'), chr(data[j])); i = j + 1
                elif c in b'()*+':
                    if i >= len(data): s.buf = data[i - 2:]; return
                    i += 1
                elif c == ord('7'): s.saved = (s.x, s.y)
                elif c == ord('8'): s.x, s.y = s.saved; s.pend = False
                elif c == ord('M'): s.rindex()
                elif c == ord('D'): s.index()
                elif c == ord('E'): s.x = 0; s.index()
                continue
            s.pend = s.pend and b >= 32
            if b == 13: s.x = 0
            elif b == 10: s.x = 0; s.index()
            elif b == 8: s.x = max(s.x - 1, 0)
            elif b == 9: s.x = min((s.x // 8 + 1) * 8, s.w - 1)
            elif b >= 32: s.put(chr(b) if b < 127 else '?')
    def csi(s, par, f):
        priv = par.startswith('?'); par = par.lstrip('?>')
        n = [int(p) if p.isdigit() else 0 for p in par.split(';')] if par else [0]
        a = n[0] or 1
        s.pend = False
        if priv:
            if f in 'hl' and 1049 in n:
                if f == 'h': s.main = s.grid; s.alt = s.blank(); s.grid = s.alt; s.x = s.y = 0
                elif s.alt is not None: s.grid = s.main; s.alt = None
            return
        if f == 'A': s.y = max(s.y - a, 0)
        elif f == 'B': s.y = min(s.y + a, s.h - 1)
        elif f == 'C': s.x = min(s.x + a, s.w - 1)
        elif f == 'D': s.x = max(s.x - a, 0)
        elif f == 'G': s.x = min(a - 1, s.w - 1)
        elif f == 'd': s.y = min(a - 1, s.h - 1)
        elif f in 'Hf': s.y = min((n[0] or 1) - 1, s.h - 1); s.x = min(((n[1] if len(n) > 1 else 0) or 1) - 1, s.w - 1)
        elif f == 'J':
            for r in range(s.h):
                if n[0] == 2 or (n[0] == 0 and r > s.y) or (n[0] == 1 and r < s.y): s.grid[r] = [(' ', None)] * s.w
            if n[0] in (0, 1): s.erase(n[0])
        elif f == 'K': s.erase(n[0])
        elif f == 'X':
            for c in range(s.x, min(s.x + a, s.w)): s.grid[s.y][c] = (' ', None)
        elif f == 'P': r = s.grid[s.y]; del r[s.x:s.x + a]; r.extend([(' ', None)] * (s.w - len(r)))
        elif f == '@': r = s.grid[s.y]; r[s.x:s.x] = [(' ', None)] * a; del r[s.w:]
        elif f == 'L':
            for _ in range(a): del s.grid[s.bot]; s.grid.insert(s.y, [(' ', None)] * s.w)
        elif f == 'M':
            for _ in range(a): del s.grid[s.y]; s.grid.insert(s.bot, [(' ', None)] * s.w)
        elif f == 'S':
            for _ in range(a): del s.grid[s.top]; s.grid.insert(s.bot, [(' ', None)] * s.w)
        elif f == 'T':
            for _ in range(a): del s.grid[s.bot]; s.grid.insert(s.top, [(' ', None)] * s.w)
        elif f == 'r': s.top = (n[0] or 1) - 1; s.bot = min((n[1] if len(n) > 1 and n[1] else s.h) - 1, s.h - 1); s.x = s.y = 0
        elif f == 'm':
            for v in n:
                if v == 0: s.fg, s.bold = None, False
                elif v == 1: s.bold = True
                elif v == 22: s.bold = False
                elif 30 <= v <= 37: s.fg = v - 30
                elif 90 <= v <= 97: s.fg = v - 90 + 8
                elif v == 39: s.fg = None
    def erase(s, mode):
        r = s.grid[s.y]
        for c in range(s.w):
            if (mode == 0 and c >= s.x) or (mode == 1 and c <= s.x) or mode == 2: r[c] = (' ', None)
    def row(s, i): return ''.join(c for c, _ in s.grid[i]).rstrip()
    def text(s): return '\n'.join(s.row(i) for i in range(s.h))
    def find(s, word):
        """(row, colours of the characters of the word) of the first line that holds the word"""
        for i in range(s.h):
            k = s.row(i).find(word)
            if k >= 0: return i, {c for _, c in s.grid[i][k:k + len(word)]}
        return None, set()

STATUS = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)

class Session:
    """A terminal on a socket (the UART of qemu or the telnet server): what arrives goes into the emulator and into `raw`."""
    def __init__(s, sock, telnet=False):
        s.sock, s.telnet, s.scr, s.raw, s.carry, s.iac = sock, telnet, Screen(), b'', b'', b''
        sock.settimeout(0.2)
    def pump(s, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try: data = s.sock.recv(4096)
            except socket.timeout: continue
            except OSError: return
            if not data: return
            s.take(data)
    def take(s, data):
        if s.telnet:                                                  # cut out the commands IAC WILL/WONT/DO/DONT x
            data = s.iac + data; s.iac = b''; out = bytearray(); i = 0
            while i < len(data):
                if data[i] == 255 and i + 2 < len(data) + 0 and data[i + 1] in (251, 252, 253, 254): i += 3
                elif data[i] == 255 and i + 2 >= len(data): s.iac = data[i:]; break
                else: out.append(data[i]); i += 1
            data = bytes(out)
        data = STATUS.sub(b'', s.carry + data); s.carry = b''
        k = data.rfind(b'\x1b7')
        if k >= 0 and len(data) - k < 110 and b'\x1b8' not in data[k:]: s.carry = data[k:]; data = data[:k]
        s.raw += data; s.scr.feed(data)
    def send(s, text, delay=0.05):
        """type the keys: one character after the other; the bytes of a key that starts with Escape go out together (a terminal sends them in one piece, and nano
        tells Escape-O-A from an Escape and a letter by the time between the bytes)"""
        s.raw = b''
        data = text.encode('latin1').decode('unicode_escape').encode('latin1'); i = 0
        while i < len(data):
            j = i + 1
            if data[i] == 27 and j < len(data):
                if data[j] == ord('O'): j += 2                                  # ESC O A
                elif data[j] == ord('['):                                       # ESC [ 3 ~
                    j += 1
                    while j < len(data) and not 0x40 <= data[j - 0] <= 0x7e: j += 1
                    j += 1
                else: j += 1
            s.sock.send(data[i:j]); s.pump(delay); i = j
    def wait(s, cond, what, seconds=30.0):
        end = time.time() + seconds
        while time.time() < end:
            s.pump(0.1)
            if cond(): check(True, what); return True
        check(False, what + ' -- screen:\n' + '\n'.join('      |' + s.scr.row(i) for i in range(s.scr.h)) + '\n      raw: %r' % s.raw[-200:])
        return False
    def on_screen(s, text, what, seconds=30.0): return s.wait(lambda: text in s.scr.text(), what, seconds)
    def prompt(s, what, seconds=30.0):
        return s.wait(lambda: re.search(PROMPT.encode(), s.raw) is not None, what, seconds)

PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '

def cat(sess, path, expect_lines, what):
    sess.send('cat %s\\r' % path)
    sess.prompt('prompt after cat %s' % path)
    got = sess.raw.decode('latin1').replace('\r', '').split('\n')[1:-1]
    check([l.rstrip() for l in got] == expect_lines, '%s: the file holds %r (got %r)' % (what, expect_lines, got))

UP, DOWN, RIGHT, LEFT, HOME, END = '\\x1bOA', '\\x1bOB', '\\x1bOC', '\\x1bOD', '\\x1bOH', '\\x1bOF'     # the arrow keys after "smkx" (application mode)
DEL, PGUP, PGDN, BS, CR = '\\x1b[3~', '\\x1b[5~', '\\x1b[6~', '\\x7f', '\\r'

def editor_tests(sess):
    # ---- a Python file: typed, edited in the middle of a line, saved
    sess.send('nano /tmp/t.py\\r')
    sess.on_screen('GNU nano', 'nano starts on the second screen and shows its title bar')
    check('^X Exit' in sess.scr.text() and 'New File' in sess.scr.text(), 'help line at the bottom, and "New File" for a name that does not exist')
    sess.send('def hello(name):\\r    s = "hi"  # greet\\r    return s')
    sess.on_screen('return s', 'the typed lines are on the screen')
    r, kw = sess.scr.find('def'); r2, st = sess.scr.find('"hi"'); r3, cm = sess.scr.find('# greet'); r4, plain = sess.scr.find('return')
    print('        colours: def=%s string=%s comment=%s return=%s' % (kw, st, cm, plain))
    check(None not in kw and None not in st and None not in cm and len(kw | st | cm) == 3, 'Python: keyword, string and comment have three different colours')
    sess.send(UP + UP + HOME + RIGHT * 4 + DEL * 5 + 'greet')                       # "def hello(name):" -> "def greet(name):"
    sess.on_screen('def greet(name):', 'Up, Home, Right, Delete and typing edit the middle of line 1')
    sess.send(DOWN + END + BS * 9)                                                  # '    s = "hi"  # greet' loses "  # greet"
    sess.on_screen('s = "hi"', 'Down, End and Backspace edit line 2')
    check('# greet' not in sess.scr.text(), 'the comment is gone after nine Backspaces')
    sess.send('\\x0f')                                                              # Ctrl-O
    sess.on_screen('Write to File: /tmp/t.py', 'Ctrl-O asks for the file name')
    sess.send(CR)
    sess.on_screen('Wrote 3 lines', 'Enter writes the file')
    sess.send('\\x18')                                                              # Ctrl-X
    sess.prompt('Ctrl-X ends nano: the shell prompt is back')
    check('GNU nano' not in sess.scr.text(), 'the first screen is back (no title bar of nano)')
    cat(sess, '/tmp/t.py', ['def greet(name):', '    s = "hi"', '    return s'], 'saved Python file')

    # ---- a C file
    sess.send('nano /tmp/t.c\\r')
    sess.on_screen('GNU nano', 'nano starts for a .c file')
    sess.send('#include <stdio.h>\\rint main(void) { /* c */\\r    return 0; // end\\r}')
    sess.on_screen('// end', 'the C lines are on the screen')
    r, inc = sess.scr.find('#include'); r, tp = sess.scr.find('int'); r, cm = sess.scr.find('// end'); r, ret = sess.scr.find('return')
    print('        colours: #include=%s int main=%s // end=%s return=%s' % (inc, tp, cm, ret))
    check(None not in inc and None not in cm and len(inc | cm) >= 2, 'C: #include and the comment have colours')
    check(None not in tp and None not in ret, 'C: types and keywords have colours (int, return)')
    sess.send('\\x0f'); sess.on_screen('Write to File: /tmp/t.c', 'Ctrl-O for the C file'); sess.send(CR)
    sess.on_screen('Wrote 4 lines', 'the C file is written')
    sess.send('\\x18'); sess.prompt('back in the shell')
    cat(sess, '/tmp/t.c', ['#include <stdio.h>', 'int main(void) { /* c */', '    return 0; // end', '}'], 'saved C file')

    # ---- a file that does not exist; leaving it without a change asks nothing
    sess.send('nano /tmp/nonexistent.txt\\r')
    sess.on_screen('New File', 'a name that does not exist: "New File"')
    sess.send('\\x18'); sess.prompt('Ctrl-X leaves an unchanged buffer without a question')

    # ---- 300 lines: Page Down and Up, search, cut and paste, Ctrl-C, leave without saving
    sess.send('nano /lines300.txt\\r')
    sess.on_screen('line 001', 'the first page of the 300 lines is shown')
    check(sess.scr.find('line 020')[0] is not None and sess.scr.find('line 021')[0] is None, 'the screen shows 21 lines of text (1 to 20)')
    sess.send(PGDN)
    sess.wait(lambda: sess.scr.find('line 001')[0] is None and sess.scr.find('line 030')[0] is not None, 'Page Down shows the next page')
    sess.send(PGDN + PGDN)
    sess.wait(lambda: sess.scr.find('line 060')[0] is not None, 'two more Page Down')
    sess.send(PGUP + PGUP + PGUP + PGUP)
    sess.on_screen('line 001', 'Page Up goes back to the top')
    sess.send('\\x17')                                                              # Ctrl-W
    sess.on_screen('Search:', 'Ctrl-W asks what to search')
    sess.send('line 250' + CR)
    sess.on_screen('line 250', 'the search finds line 250 and scrolls to it')
    sess.send('\\x03')                                                              # Ctrl-C: where am I
    sess.on_screen('line 250/30', 'Ctrl-C does not end nano: it shows the position')
    sess.send('\\x18')                                                              # unchanged: nano ends
    sess.prompt('Ctrl-X ends nano (the search did not change the text)')
    sess.send('nano /lines300.txt\\r')
    sess.on_screen('line 001', 'the file again')
    sess.send('\\x0b')                                                              # Ctrl-K
    sess.wait(lambda: sess.scr.row(1).startswith('line 002'), 'Ctrl-K cuts line 1: line 2 is the first one now')
    sess.send(DOWN * 2 + '\\x15')                                                   # Ctrl-U
    sess.wait(lambda: sess.scr.row(3).startswith('line 001'), 'Ctrl-U pastes it again, below the cursor')
    sess.send('\\x18')
    sess.on_screen('Save modified buffer?', 'Ctrl-X on a changed buffer asks whether to save')
    sess.send('n'); sess.prompt('N leaves without saving')
    cat(sess, '/tmp/t.py', ['def greet(name):', '    s = "hi"', '    return s'], 'the Python file is still there')

def boot_uart():
    cmd = ['qemu-system-aarch64', '-M', os.environ.get('QEMU_M', 'virt'), *(['-accel', 'hvf', '-cpu', 'host'] if os.environ.get('QEMU_FAST') == '1' else ['-cpu', 'cortex-a72']),
           '-m', os.environ.get('QEMU_MEM', '128'), '-display', 'none', '-monitor', 'none', '-kernel', kernel, '-snapshot', '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk2,
           '-device', 'virtio-blk-device,drive=hd0', '-serial', 'tcp:127.0.0.1:%d,server=on,wait=on' % PORT]
    q = subprocess.Popen(cmd + shlex.split(os.environ.get('QEMU_EXTRA', '')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        try: return q, socket.create_connection(('127.0.0.1', PORT))
        except OSError: time.sleep(0.1)
    q.kill(); sys.exit('cannot connect to qemu')

# the disk of the test: the disk of the system and a file of 300 lines
work = tempfile.mkdtemp()
disk2 = os.path.join(work, 'disk.img'); shutil.copy(disk, disk2)
lines = os.path.join(work, 'lines300.txt'); open(lines, 'w').write(''.join('line %03d\n' % i for i in range(1, 301)))
subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'minix_add.py'), disk2, '/lines300.txt=' + lines], check=True, stdout=subprocess.DEVNULL)

qemu, sock = boot_uart()
try:
    sess = Session(sock)
    sess.wait(lambda: re.search(PROMPT.encode(), sess.raw) is not None, 'kernel booted, login, shell prompt', 120)
    editor_tests(sess)
finally:
    qemu.kill(); qemu.wait()

# ---- the same through the telnet server: open, type, save, quit
qemu = subprocess.Popen(['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-m', '128', '-display', 'none', '-monitor', 'none', '-serial', 'null', '-kernel', kernel, '-snapshot',
    '-drive', 'file=%s,format=raw,if=none,id=hd0' % disk2, '-device', 'virtio-blk-device,drive=hd0',
    '-netdev', 'user,id=n0,hostfwd=tcp:127.0.0.1:%d-10.0.2.15:23' % TELNET_PORT, '-device', 'virtio-net-device,netdev=n0'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    t = None
    for _ in range(120):                                           # the system boots: connect when the server answers (it sends IAC WILL ECHO at once)
        try:
            c = socket.create_connection(('127.0.0.1', TELNET_PORT), timeout=5); t = Session(c, telnet=True); t.pump(2)
            if t.iac or t.raw or t.scr.text().strip(): break
            c.close(); t = None
        except OSError: t = None
        time.sleep(1)
    if t: t.wait(lambda: 'ulix login:' in t.scr.text(), 'telnet: the login prompt of terminal 5', 90)
    if t:
        t.send('root\\r'); t.on_screen('Password:', 'telnet: login asks for the password')
        t.send('xyz\\r'); t.sock.settimeout(0.2)
        t.wait(lambda: re.search(rb'root@ulix\[\d+\]:/root# ', t.raw) is not None, 'telnet: a root shell')
        t.send('nano /tmp/tn.py\\r')
        t.on_screen('GNU nano', 'telnet: nano starts')
        t.send('import os\\rdef f():\\r    return "x"  # c')
        t.on_screen('return "x"', 'telnet: the typed lines are on the screen')
        r, kw = t.scr.find('import'); r, st = t.scr.find('"x"'); r, cm = t.scr.find('# c')
        check(None not in kw and None not in st and None not in cm, 'telnet: keyword, string and comment have colours')
        t.send(UP + UP + END + BS + 'S')                          # "import os" -> "import oS"
        t.send('\\x0f'); t.on_screen('Write to File: /tmp/tn.py', 'telnet: Ctrl-O'); t.send(CR)
        t.on_screen('Wrote 3 lines', 'telnet: the file is written')
        t.send('\\x18')
        t.wait(lambda: re.search(rb'root@ulix\[\d+\]:/root# ', t.raw) is not None, 'telnet: Ctrl-X ends nano, the shell prompt is back')
        t.raw = b''; t.send('cat /tmp/tn.py\\r')
        t.wait(lambda: re.search(rb'root@ulix\[\d+\]:/root# ', t.raw) is not None, 'telnet: cat')
        got = t.raw.decode('latin1').replace('\r', '').split('\n')[1:-1]
        check([l.rstrip() for l in got] == ['import oS', 'def f():', '    return "x"  # c'], 'telnet: the saved file holds the edited text (got %r)' % got)
        t.sock.close()
finally:
    qemu.kill(); qemu.wait()

print('nano test: %s' % ('all passed' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
