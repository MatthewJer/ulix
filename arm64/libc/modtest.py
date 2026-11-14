# Imports a list of standard library modules and runs small programs with them (issue #133, chapter "Python" in lit/libc.nw).
# Run it as:  /musl/python -B /musl/modtest.py      (-B: do not try to write .pyc files)
# With words as arguments it runs only the checks that have one of them in the name (modtest.py sqlite ctypes).
# Every check prints "  ok:   ..." or "  FAIL: ..."; the last line says how many failed. A module that is known not to work on Ulix
# (no zlib, no OpenSSL, no ctypes, ...) is listed in KNOWN_MISSING below: its import must fail with ImportError, and it does.
import sys, time

results = {"ok": 0, "fail": 0}
slowest = []

ONLY = sys.argv[1:]                       # modtest.py NAME...: run only the checks whose name contains one of the words

def check(name, fn):
    if ONLY and not any(w in name for w in ONLY): return
    t = time.monotonic()
    try:
        r = fn()
        if r is False:
            raise AssertionError("returned False")
    except BaseException as e:
        results["fail"] += 1
        print("  FAIL: %s: %s: %s" % (name, type(e).__name__, e))
        import traceback; traceback.print_exc(limit=-3)
    else:
        results["ok"] += 1
        print("  ok:   %s" % name)
    slowest.append((time.monotonic() - t, name))

def imp(name):
    return lambda: __import__(name)

MODULES = """json re os sys collections itertools functools datetime math random hashlib struct textwrap argparse pathlib logging
unittest socket select threading subprocess string copy operator bisect heapq array binascii base64 io time enum dataclasses typing
contextlib abc types weakref glob fnmatch shutil tempfile csv configparser queue fractions statistics pprint shlex secrets email
urllib.parse html difflib calendar locale getopt optparse traceback inspect ast dis pickle copyreg gettext codecs unicodedata
zoneinfo xml.etree.ElementTree xml.parsers.expat pyexpat _decimal _asyncio asyncio selectors signal errno stat posixpath numbers reprlib sched atexit warnings platform sysconfig uuid ipaddress
http.client http.server socketserver urllib.request xml.dom.minidom json.tool cmd code codeop pdb timeit tokenize token keyword
graphlib linecache getpass pkgutil importlib.util importlib.metadata zipfile tarfile zlib gzip bz2 lzma readline crypt sqlite3 mmap ctypes curses curses.panel ssl _hashlib multiprocessing multiprocessing.pool multiprocessing.queues mimetypes smtplib ftplib wsgiref.simple_server
concurrent.futures decimal cProfile profile trace tracemalloc bdb faulthandler fileinput filecmp colorsys
cgi wave sndhdr netrc plistlib quopri uu mailbox imaplib poplib telnetlib nntplib""".split()

KNOWN_MISSING = "tkinter _tkinter _uuid".split()

for m in MODULES:
    check("import " + m, imp(m))

for m in KNOWN_MISSING:
    def missing(m=m):
        try:
            __import__(m)
        except ImportError:
            return True
        return False
    check("import " + m + " fails (known not to work)", missing)

def t_json():
    import json
    d = {"a": [1, 2.5, None, True], "b": {"c": "ü"}}
    return json.loads(json.dumps(d)) == d and json.dumps([1, 2]) == "[1, 2]"

def t_re():
    import re
    return (re.findall(r"\d+", "a1b22c333") == ["1", "22", "333"] and re.sub(r"(\w+) (\w+)", r"\2 \1", "hello world") == "world hello")

def t_collections():
    import collections
    c = collections.Counter("mississippi")
    d = collections.deque([1, 2, 3], maxlen=3); d.append(4)
    return c["s"] == 4 and list(d) == [2, 3, 4] and collections.OrderedDict(a=1)["a"] == 1

def t_itertools_functools():
    import itertools, functools
    @functools.lru_cache(maxsize=None)
    def fib(n): return n if n < 2 else fib(n - 1) + fib(n - 2)
    return (len(list(itertools.permutations(range(4)))) == 24 and fib(40) == 102334155 and
            functools.reduce(lambda a, b: a * b, range(1, 11)) == 3628800)

def t_datetime():
    import datetime
    a = datetime.date(2024, 2, 28) + datetime.timedelta(days=2)
    return a.isoformat() == "2024-03-01" and datetime.datetime(2000, 1, 1, 12).strftime("%Y-%m-%d %H:%M") == "2000-01-01 12:00"

def t_hashlib():
    import hashlib
    return (hashlib.md5(b"abc").hexdigest() == "900150983cd24fb0d6963f7d28e17f72" and
            hashlib.sha1(b"abc").hexdigest() == "a9993e364706816aba3e25717850c26c9cd0d89d" and
            hashlib.sha256(b"abc").hexdigest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" and
            hashlib.sha512(b"abc").hexdigest().startswith("ddaf35a193617aba") and
            hashlib.sha3_256(b"abc").hexdigest().startswith("3a985da74fe225b2") and
            hashlib.blake2b(b"abc").hexdigest().startswith("ba80a53f981c4d0d"))

def t_struct():
    import struct, binascii, array
    return (struct.unpack("<IH", struct.pack("<IH", 7, 9)) == (7, 9) and binascii.hexlify(b"\x01\xff") == b"01ff" and
            array.array("i", [1, 2, 3]).tolist() == [1, 2, 3])

def t_text():
    import textwrap, string, shlex, pprint
    return (textwrap.fill("a b c d e f", 5) == "a b c\nd e f" and string.Template("$x!").substitute(x="hi") == "hi!" and
            shlex.split("a 'b c'") == ["a", "b c"] and pprint.pformat({"a": 1}) == "{'a': 1}")

def t_argparse():
    import argparse
    p = argparse.ArgumentParser(prog="x")
    p.add_argument("--n", type=int, default=1); p.add_argument("files", nargs="*")
    a = p.parse_args(["--n", "5", "f1", "f2"])
    return a.n == 5 and a.files == ["f1", "f2"]

def t_pathlib_os():
    import pathlib, os
    p = pathlib.Path("/etc/hosts")
    names = os.listdir("/musl")
    return p.exists() and "localhost" in p.read_text() and "python" in names and pathlib.Path("/musl/python").stat().st_size > 1000000 and p.parent.name == "etc"

def t_files():
    import os, tempfile
    fn = "/tmp/modtest.txt"
    with open(fn, "w") as f: f.write("hello\n" * 1000)
    with open(fn) as f: n = len(f.read())
    st = os.stat(fn).st_size
    os.rename(fn, fn + ".2"); os.remove(fn + ".2")
    return n == 6000 and st == 6000 and not os.path.exists(fn) and tempfile.gettempdir() == "/tmp"

def t_logging():
    import logging, io
    s = io.StringIO()
    h = logging.StreamHandler(s); h.setFormatter(logging.Formatter("%(levelname)s:%(message)s"))
    lg = logging.getLogger("t"); lg.addHandler(h); lg.setLevel(logging.INFO); lg.info("hi"); lg.debug("no")
    return s.getvalue() == "INFO:hi\n"

def t_unittest():
    import unittest, io
    class T(unittest.TestCase):
        def test_a(self): self.assertEqual(1 + 1, 2)
        def test_b(self): self.assertIn(1, [1])
    s = io.StringIO()
    r = unittest.TextTestRunner(stream=s).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
    return r.wasSuccessful() and r.testsRun == 2

def t_threading():
    import threading
    n = [0]; lock = threading.Lock()
    def work():
        for _ in range(200):
            with lock: n[0] += 1
    ts = [threading.Thread(target=work) for _ in range(4)]
    for t in ts: t.start()
    for t in ts: t.join()
    return n[0] == 800

def t_socket_select():
    import socket, select
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setblocking(False)
    r, w, x = select.select([], [s], [], 0.1)
    s.close()
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.close()
    return socket.gethostbyname("localhost") == "127.0.0.1" and socket.htons(1) == 256 and w == [s]

def t_random():
    import random
    random.seed(42)
    a = random.random(); random.seed(42)
    return a == random.random() and 0 <= random.randint(1, 6) <= 6 and sorted(random.sample(range(10), 10)) == list(range(10))

def t_math_stat():
    import math, statistics, fractions
    return (math.isclose(math.sin(math.pi / 2), 1.0) and statistics.mean([1, 2, 3, 4]) == 2.5 and
            fractions.Fraction(1, 3) + fractions.Fraction(1, 6) == fractions.Fraction(1, 2))

def t_csv_config():
    import csv, io, configparser
    rows = list(csv.reader(io.StringIO("a,b\n1,\"x,y\"\n")))
    c = configparser.ConfigParser(); c.read_string("[s]\nk = v\n")
    return rows == [["a", "b"], ["1", "x,y"]] and c["s"]["k"] == "v"

def t_email_urllib():
    import email, urllib.parse
    m = email.message_from_string("Subject: hi\n\nbody\n")
    u = urllib.parse.urlparse("http://example.com:80/a?b=1")
    return m["Subject"] == "hi" and u.netloc == "example.com:80" and urllib.parse.parse_qs(u.query) == {"b": ["1"]}

def t_decimal():
    import decimal, _decimal
    return decimal.Decimal("0.1") + decimal.Decimal("0.2") == decimal.Decimal("0.3") and decimal.Decimal(2).sqrt() > decimal.Decimal("1.41421")

def t_xml():
    import xml.etree.ElementTree as ET, xml.dom.minidom
    root = ET.fromstring("<a><b x='1'>text</b><b x='2'/></a>")
    d = xml.dom.minidom.parseString("<r><i>1</i></r>")
    return [b.get("x") for b in root.iter("b")] == ["1", "2"] and root.find("b").text == "text" and d.getElementsByTagName("i")[0].firstChild.data == "1"

def t_zoneinfo():
    import zoneinfo, datetime
    return datetime.datetime(2024, 1, 1, tzinfo=datetime.timezone.utc).utcoffset() == datetime.timedelta(0) and zoneinfo.TZPATH == ()

def t_pickle_copy():
    import pickle, copy
    d = {"a": [1, (2, 3)], "b": {4, 5}}
    return pickle.loads(pickle.dumps(d)) == d and copy.deepcopy(d) == d

def t_asyncio():
    import asyncio
    async def f(): await asyncio.sleep(0.01); return 7
    async def g(): return sum(await asyncio.gather(f(), f(), f()))
    return asyncio.run(f()) == 7 and asyncio.run(g()) == 21

def t_subprocess():
    import subprocess
    r = subprocess.run(["/musl/cat", "/etc/hosts"], capture_output=True)
    return r.returncode == 0 and b"localhost" in r.stdout

for name, fn in [("json", t_json), ("re", t_re), ("collections", t_collections), ("itertools and functools", t_itertools_functools),
                 ("datetime", t_datetime), ("hashlib: md5, sha1, sha2, sha3, blake2", t_hashlib), ("struct, binascii, array", t_struct),
                 ("textwrap, string, shlex, pprint", t_text), ("argparse", t_argparse), ("pathlib and os.listdir", t_pathlib_os),
                 ("files: tempfile, open, write, read, remove", t_files), ("logging", t_logging), ("unittest runs two tests", t_unittest),
                 ("threading: four threads and a lock", t_threading), ("socket and select", t_socket_select), ("random", t_random),
                 ("math, statistics, fractions", t_math_stat), ("csv and configparser", t_csv_config), ("email and urllib.parse", t_email_urllib),
                 ("decimal (C version) and xml (expat)", t_decimal), ("xml.etree and minidom parse", t_xml), ("zoneinfo imports (no time zone database)", t_zoneinfo), ("pickle and copy", t_pickle_copy), ("asyncio.run (the event loop uses socketpair)", t_asyncio)]:
    check("program: " + name, fn)

check("program: subprocess runs /musl/cat (fork, exec, pipes)", t_subprocess)

# the modules that need a library or a piece of the kernel (the whole standard library, one small test for each)
def t_socketpair():
    import socket, threading
    a, b = socket.socketpair()
    a.sendall(b"ping")
    r = b.recv(10)
    w = threading.Thread(target=lambda: b.sendall(b"pong" * 2000))   # more than the buffer of one end: the writer waits for the reader
    w.start()
    got = b""
    a.settimeout(20)
    while len(got) < 8000: got += a.recv(4096)
    w.join()
    a.close()
    return r == b"ping" and got == b"pong" * 2000 and b.recv(10) == b""

def t_tempdir():
    import tempfile, os, shutil
    d = tempfile.mkdtemp()
    p = os.path.join(d, "f.txt")
    with open(p, "w") as f: f.write("hi")
    with tempfile.NamedTemporaryFile(dir=d) as t: t.write(b"x")
    with tempfile.TemporaryDirectory() as e: open(os.path.join(e, "g"), "w").close()
    ok = open(p).read() == "hi"
    shutil.rmtree(d)
    return ok and not os.path.exists(d)

def t_zlib():
    import zlib, gzip, bz2, lzma, io, zipfile, tarfile, os, tempfile, shutil
    data = b"Ulix is a teaching operating system. " * 500
    z = zlib.compress(data, 9)
    ok = len(z) < len(data) // 10 and zlib.decompress(z) == data
    ok = ok and zlib.crc32(b"abc") == 0x352441c2 and zlib.adler32(b"abc") == 0x024d0127
    c = zlib.compressobj(); d = zlib.decompressobj()                 # streaming
    out = d.decompress(c.compress(data) + c.flush())
    ok = ok and out == data
    ok = ok and gzip.decompress(gzip.compress(data)) == data
    ok = ok and bz2.decompress(bz2.compress(data)) == data and len(bz2.compress(data)) < len(data) // 10
    ok = ok and lzma.decompress(lzma.compress(data)) == data and len(lzma.compress(data)) < len(data) // 10
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as zf: zf.writestr("a.txt", data); zf.writestr("b/c.txt", "c")
    with zipfile.ZipFile(io.BytesIO(b.getvalue())) as zf: ok = ok and zf.read("a.txt") == data and zf.namelist() == ["a.txt", "b/c.txt"] and zf.testzip() is None
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w:gz") as tf:
        i = tarfile.TarInfo("x.txt"); i.size = len(data); tf.addfile(i, io.BytesIO(data))
    with tarfile.open(fileobj=io.BytesIO(b.getvalue()), mode="r:gz") as tf: ok = ok and tf.extractfile("x.txt").read() == data
    return ok

def t_sqlite3():
    import sqlite3, os, tempfile, shutil
    c = sqlite3.connect(":memory:")
    c.execute("create table t (a integer primary key, b text)")
    c.executemany("insert into t (b) values (?)", [("x",), ("y",), ("z",)])
    ok = c.execute("select b from t order by a desc").fetchall() == [("z",), ("y",), ("x",)]
    ok = ok and c.execute("select sqlite_version()").fetchone()[0].startswith("3.")
    d = tempfile.mkdtemp()                                           # a database in a file: journal, commit, read again
    p = os.path.join(d, "test.db")
    c = sqlite3.connect(p); c.execute("create table k (v)"); c.execute("insert into k values (42)"); c.commit(); c.close()
    c = sqlite3.connect(p); ok = ok and c.execute("select v from k").fetchone() == (42,); c.close()
    shutil.rmtree(d)
    return ok

def t_ctypes():
    import ctypes
    libc = ctypes.CDLL(None)                                         # the program itself: it contains the C library (libc/dlshim.c)
    libc.strlen.argtypes = [ctypes.c_char_p]; libc.strlen.restype = ctypes.c_size_t
    ok = libc.strlen(b"ulix") == 4 and libc.abs(-5) == 5
    buf = ctypes.create_string_buffer(16)
    libc.snprintf(buf, 16, b"%d-%s", 42, b"x")
    ok = ok and buf.value == b"42-x"
    class P(ctypes.Structure): _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_double)]
    p = P(3, 2.5)
    ok = ok and ctypes.sizeof(P) == 16 and p.x == 3 and p.y == 2.5
    ok = ok and ctypes.CDLL("libc.so.6").getpid() > 0
    ok = ok and ctypes.pythonapi.Py_GetVersion is not None           # the symbols of the interpreter are in the table, too
    CMP = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int))   # a callback: C (qsort) calls Python
    arr = (ctypes.c_int * 5)(5, 1, 4, 2, 3)
    libc.qsort(arr, 5, ctypes.sizeof(ctypes.c_int), CMP(lambda a, b: a[0] - b[0]))
    return ok and list(arr) == [1, 2, 3, 4, 5]

def t_readline_crypt():
    import readline, crypt
    readline.add_history("print(1)"); readline.add_history("print(2)")
    ok = readline.get_current_history_length() == 2 and readline.get_history_item(2) == "print(2)"
    readline.set_completer_delims(" "); readline.parse_and_bind("tab: complete")
    h = crypt.crypt("secret", "$6$saltsalt$")                        # SHA-512 crypt (musl): the same string every time, and different for another password
    return ok and h == crypt.crypt("secret", h) and h != crypt.crypt("other", h) and h.startswith("$6$saltsalt$")

def t_mmap():
    import mmap, os, tempfile, shutil, multiprocessing as mp
    m = mmap.mmap(-1, 8192); m[0:5] = b"hello"; m.seek(100); m.write(b"xyz")           # anonymous memory
    ok = m[:5] == b"hello" and m.find(b"xyz", 0) == 100
    m.close()
    d = tempfile.mkdtemp(); p = os.path.join(d, "f.bin")
    open(p, "wb").write(b"0123456789" * 500)                                          # a file: read, change, write back with flush
    with open(p, "r+b") as f:
        m = mmap.mmap(f.fileno(), 0)
        ok = ok and len(m) == 5000 and m[10:15] == b"01234"
        m[0:4] = b"ABCD"; m.flush(); m.close()
    ok = ok and open(p, "rb").read(14) == b"ABCD456789" + b"0123"
    with open(p, "rb") as f:                                                           # read only
        m = mmap.mmap(f.fileno(), 100, access=mmap.ACCESS_READ)
        ok = ok and m[:4] == b"ABCD"
        try: m[0] = 1; ok = False
        except TypeError: pass
        m.close()
    shutil.rmtree(d)
    ctx = mp.get_context("fork")                                                       # shared between processes: Value and Array live in a shared mapping
    v = ctx.Value("i", 7); a = ctx.Array("i", [1, 2, 3])
    def child(): v.value += 35; a[1] = 99
    pr = ctx.Process(target=child); pr.start(); pr.join(60)
    return ok and pr.exitcode == 0 and v.value == 42 and list(a) == [1, 99, 3]

def t_curses():
    import curses, curses.panel
    curses.setupterm("vt100", 1)                                     # the terminal descriptions are compiled in; there is no terminfo directory
    return (curses.tigetstr("clear").startswith(b"\x1b[H\x1b[J") and curses.tigetnum("cols") == 80 and      # (vt100 adds padding: $<50>)
            curses.tparm(curses.tigetstr("cup"), 3, 4).startswith(b"\x1b[4;5H"))

# a self-signed certificate for "localhost" and its key, made for this test only (valid for 100 years); the TLS test below uses no network
TEST_CERT = """-----BEGIN CERTIFICATE-----
MIICDTCCAbQCCQCFbx0IxXaw0TAKBggqhkjOPQQDAjAUMRIwEAYDVQQDDAlsb2Nh
bGhvc3QwIBcNMjYxMTE0MDk0MjA1WhgPMjEyNjEwMjEwOTQyMDVaMBQxEjAQBgNV
BAMMCWxvY2FsaG9zdDCCAUswggEDBgcqhkjOPQIBMIH3AgEBMCwGByqGSM49AQEC
IQD/////AAAAAQAAAAAAAAAAAAAAAP///////////////zBbBCD/////AAAAAQAA
AAAAAAAAAAAAAP///////////////AQgWsY12Ko6k+ez671VdpiGvGUdBrDMU7D2
O848PifSYEsDFQDEnTYIhucEk2pmeOETnSa3gZ9+kARBBGsX0fLhLEJH+Lzm5WOk
QPJ3A32BLeszoPShOUXYmMKWT+NC4v4af5uO5+tKfA+eFivOM1drMV7Oy7ZAaDe/
UfUCIQD/////AAAAAP//////////vOb6racXnoTzucrC/GMlUQIBAQNCAATpXJRj
2ikM7hDdteGJMsI34xtAVduKyHQVr4gdb/JJSChAN5ThGGW0HgsEMdZg17dslydv
hzOS5hacxNyehXymMAoGCCqGSM49BAMCA0cAMEQCIDKUazse9JA6siQMMUDFqInb
Pn8y/rWPRimOswOX0YFTAiArU5vAgZiCI4R2+d9Mc3+o5DO0AllSUllQ+M01Czup
9Q==
-----END CERTIFICATE-----
"""
TEST_KEY = """-----BEGIN PRIVATE KEY-----
MIIBeQIBADCCAQMGByqGSM49AgEwgfcCAQEwLAYHKoZIzj0BAQIhAP////8AAAAB
AAAAAAAAAAAAAAAA////////////////MFsEIP////8AAAABAAAAAAAAAAAAAAAA
///////////////8BCBaxjXYqjqT57PrvVV2mIa8ZR0GsMxTsPY7zjw+J9JgSwMV
AMSdNgiG5wSTamZ44ROdJreBn36QBEEEaxfR8uEsQkf4vOblY6RA8ncDfYEt6zOg
9KE5RdiYwpZP40Li/hp/m47n60p8D54WK84zV2sxXs7LtkBoN79R9QIhAP////8A
AAAA//////////+85vqtpxeehPO5ysL8YyVRAgEBBG0wawIBAQQg8hmGOgf3p3OQ
LJ5TcAFql2SUN2SPXMTAKfQLl1ybV+ahRANCAATpXJRj2ikM7hDdteGJMsI34xtA
VduKyHQVr4gdb/JJSChAN5ThGGW0HgsEMdZg17dslydvhzOS5hacxNyehXym
-----END PRIVATE KEY-----
"""

def t_ssl():
    import ssl, os, tempfile, hashlib, shutil
    ok = ssl.OPENSSL_VERSION.startswith("OpenSSL 3.") and hashlib.new("sha256", b"abc").hexdigest().startswith("ba7816bf")
    ctx = ssl.create_default_context()                              # the authorities of the system (/etc/ssl/cacert.pem), as https clients load them
    ctx.load_verify_locations("/etc/ssl/cacert.pem")
    ok = ok and ctx.cert_store_stats()["x509_ca"] > 50
    d = tempfile.mkdtemp()
    cf, kf = os.path.join(d, "c.pem"), os.path.join(d, "k.pem")
    open(cf, "w").write(TEST_CERT); open(kf, "w").write(TEST_KEY)
    srv = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); srv.load_cert_chain(cf, kf)
    cli = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT); cli.load_verify_locations(cf)       # trust exactly that certificate
    # the connection is two memory buffers: handshake, certificate check and one message each way, without threads and without a network
    ci, co, si, so = ssl.MemoryBIO(), ssl.MemoryBIO(), ssl.MemoryBIO(), ssl.MemoryBIO()
    c = cli.wrap_bio(ci, co, server_hostname="localhost"); s = srv.wrap_bio(si, so, server_side=True)
    def step(f, *a):
        try: return f(*a)
        except ssl.SSLWantReadError: return None
    for _ in range(10):
        step(c.do_handshake); step(s.do_handshake)
        si.write(co.read()); ci.write(so.read())
        if c.version() and s.version(): break
    step(c.do_handshake); step(s.do_handshake)
    c.write(b"ping"); si.write(co.read()); got = s.read(100)
    s.write(b"pong"); ci.write(so.read()); reply = c.read(100)
    shutil.rmtree(d)
    return ok and got == b"ping" and reply == b"pong" and c.version().startswith("TLSv1")

def _square(n): return n * n

def t_multiprocessing():
    import multiprocessing as mp
    ctx = mp.get_context("fork")
    lock = ctx.Lock()
    ok = lock.acquire() and not lock.acquire(False)                  # the kernel's named semaphores (libc/semshim.c)
    lock.release()
    sem = ctx.Semaphore(2); sem.acquire(); sem.acquire(); ok = ok and not sem.acquire(timeout=0.2)
    q = ctx.Queue()
    ps = [ctx.Process(target=q.put, args=(i * i,)) for i in range(3)]   # three processes (fork), a queue (a pipe and a lock) back
    for p in ps: p.start()
    got = sorted(q.get(timeout=120) for _ in ps)
    for p in ps: p.join(60)
    ok = ok and got == [0, 1, 4] and all(p.exitcode == 0 for p in ps)
    return ok

def t_ssl_threads():
    import ssl, socket, threading, os, tempfile, shutil
    d = tempfile.mkdtemp()
    cf, kf = os.path.join(d, "c.pem"), os.path.join(d, "k.pem")
    open(cf, "w").write(TEST_CERT); open(kf, "w").write(TEST_KEY)
    srv = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); srv.load_cert_chain(cf, kf)
    cli = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT); cli.load_verify_locations(cf)
    a, b = socket.socketpair()
    got = []
    def server():
        with srv.wrap_socket(b, server_side=True) as s:
            got.append(s.recv(100)); s.sendall(b"pong")
    t = threading.Thread(target=server); t.start()
    with cli.wrap_socket(a, server_hostname="localhost") as c:        # a real connection between two threads (this hung once: see section 34)
        c.sendall(b"ping"); reply = c.recv(100)
    t.join()
    shutil.rmtree(d)
    return got == [b"ping"] and reply == b"pong"

def t_pool():
    import multiprocessing as mp
    with mp.get_context("fork").Pool(2) as pool:
        return pool.map(_square, [1, 2, 3, 4]) == [1, 4, 9, 16]

for name, fn in [("socket.socketpair (kernel: AF_UNIX stream pair)", t_socketpair), ("tempfile: mkdtemp, a file in it, rmtree", t_tempdir),
                 ("zlib, gzip, bz2, lzma, zipfile (deflate), tarfile (gz)", t_zlib),
                 ("sqlite3: memory database and a file", t_sqlite3), ("ctypes: C functions, structures, pythonapi, a callback", t_ctypes),
                 ("readline history and crypt", t_readline_crypt), ("curses: terminal description, tparm, panel", t_curses),
                 ("ssl: certificate store, TLS handshake and data (memory BIOs)", t_ssl),
                 ("multiprocessing: Lock, Semaphore, three processes with a Queue", t_multiprocessing),
                 ("ssl between two threads over a socket pair", t_ssl_threads), ("multiprocessing.Pool(2).map", t_pool),
                 ("mmap: anonymous, file (flush), read only, Value and Array shared with a child", t_mmap)]:
    check("program: " + name, fn)

slowest.sort(reverse=True)
print("slowest: " + ", ".join("%s %.1f s" % (n, t) for t, n in slowest[:5]))
print("modtest: %d ok, %d failed" % (results["ok"], results["fail"]))
