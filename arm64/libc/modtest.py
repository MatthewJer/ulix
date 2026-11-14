# Imports a list of standard library modules and runs small programs with them (issue #133, chapter "Python" in lit/libc.nw).
# Run it as:  /musl/python -B /musl/modtest.py      (-B: do not try to write .pyc files)
# Every check prints "  ok:   ..." or "  FAIL: ..."; the last line says how many failed. A module that is known not to work on Ulix
# (no zlib, no OpenSSL, no ctypes, ...) is listed in KNOWN_MISSING below: its import must fail with ImportError, and it does.
import sys, time

results = {"ok": 0, "fail": 0}
slowest = []

def check(name, fn):
    t = time.monotonic()
    try:
        r = fn()
        if r is False:
            raise AssertionError("returned False")
    except BaseException as e:
        results["fail"] += 1
        print("  FAIL: %s: %s: %s" % (name, type(e).__name__, e))
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
graphlib linecache getpass pkgutil importlib.util importlib.metadata zipfile tarfile mimetypes smtplib ftplib wsgiref.simple_server
concurrent.futures decimal cProfile profile trace tracemalloc bdb faulthandler fileinput filecmp colorsys
cgi wave sndhdr netrc plistlib quopri uu mailbox imaplib poplib telnetlib nntplib""".split()

KNOWN_MISSING = "zlib bz2 lzma ssl _ssl _hashlib ctypes _ctypes sqlite3 _sqlite3 curses _curses readline tkinter _tkinter mmap _multiprocessing multiprocessing.pool _uuid _crypt".split()

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
    import asyncio, errno
    async def f(): await asyncio.sleep(0.01); return 7
    try:
        return asyncio.run(f()) == 7
    except OSError as e:                                   # known: the event loop wants socket.socketpair (AF_UNIX), which Ulix does not have
        if e.errno != errno.ENOSYS: raise
        print("        (asyncio.run needs socketpair: ENOSYS, as documented)")
        return True

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
                 ("decimal (C version) and xml (expat)", t_decimal), ("xml.etree and minidom parse", t_xml), ("zoneinfo imports (no time zone database)", t_zoneinfo), ("pickle and copy", t_pickle_copy), ("asyncio.run (works, or fails with ENOSYS for socketpair)", t_asyncio)]:
    check("program: " + name, fn)

check("program: subprocess runs /musl/cat (fork, exec, pipes)", t_subprocess)

slowest.sort(reverse=True)
print("slowest: " + ", ".join("%s %.1f s" % (n, t) for t, n in slowest[:5]))
print("modtest: %d ok, %d failed" % (results["ok"], results["fail"]))
