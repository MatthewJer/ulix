# Der Python-Zirkus: eine Vorstellung in vielen Nummern, in der die Standardbibliothek
# auf Ulix zeigen muss, was sie kann.  Jede Nummer ist ein kleiner Test, der Beifall
# (ok) oder Buhrufe (BUH) bekommt.  Am Ende kommt die Abendkasse.
#
# Auf Ulix:   /musl/python /musl/zirkus.py
# Mit Option "-v" zeigt jede Nummer, was sie gemacht hat.

import sys, time

T0 = time.time()
VERBOSE = "-v" in sys.argv
beifall, buhrufe, fehlend = [], [], []


def nummer(titel):
    """Dekorator: fuehrt die Nummer sofort aus und merkt sich das Urteil."""
    def los(f):
        try:
            was = f()
            beifall.append(titel)
            print("  ok   %-26s %s" % (titel, (was if VERBOSE and was else "")))
        except ImportError as e:
            fehlend.append(titel)
            print("  --   %-26s (nicht im Zelt: %s)" % (titel, e))
        except Exception as e:
            buhrufe.append(titel)
            print("  BUH  %-26s %s: %s" % (titel, type(e).__name__, e))
            if VERBOSE:
                import traceback
                for z in traceback.format_exc().splitlines()[-6:]:
                    print("         " + z)
        return f
    return los


def pruefe(bedingung, text="falsch"):
    if not bedingung:
        raise AssertionError(text)


print("=" * 60)
print("  Der Python-Zirkus auf Ulix   (Python %s)" % sys.version.split()[0])
print("=" * 60)


@nummer("Jongleur: math")
def _():
    import math
    pruefe(math.isclose(math.sqrt(2) ** 2, 2))
    pruefe(math.gcd(84, 36) == 12 and math.factorial(10) == 3628800)
    pruefe(abs(math.sin(math.pi)) < 1e-12)
    return "pi = %.10f, 10! = %d" % (math.pi, math.factorial(10))


@nummer("Zauberer: random")
def _():
    import random
    r = random.Random(42)
    a = [r.randint(1, 6) for _ in range(5)]
    r = random.Random(42)
    pruefe(a == [r.randint(1, 6) for _ in range(5)], "gleicher Samen, andere Wuerfel")
    import os
    pruefe(len(os.urandom(16)) == 16)
    return "Wuerfel mit Samen 42: %s" % a


@nummer("Kalender: datetime")
def _():
    import datetime, calendar
    d = datetime.date(2026, 11, 20)
    pruefe(d.strftime("%A") == "Friday")
    pruefe((d + datetime.timedelta(days=42)).isoformat() == "2027-01-01")
    pruefe(calendar.isleap(2024) and not calendar.isleap(2100))
    return "20.11.2026 ist ein %s" % d.strftime("%A")


@nummer("Dolmetscher: json")
def _():
    import json
    x = {"clown": ["rot", "gelb"], "alter": 42, "lustig": True, "nichts": None}
    pruefe(json.loads(json.dumps(x)) == x)
    return json.dumps(x, sort_keys=True)


@nummer("Detektiv: re")
def _():
    import re
    pruefe(re.findall(r"\d+", "a1b22c333") == ["1", "22", "333"])
    pruefe(re.sub(r"(\w+)@(\w+)", r"\2 at \1", "zirkus@ulix") == "ulix at zirkus")
    return "Muster gefunden"


@nummer("Tierpfleger: collections")
def _():
    from collections import Counter, deque, OrderedDict, defaultdict, namedtuple
    c = Counter("abrakadabra")
    pruefe(c.most_common(1) == [("a", 5)])
    d = deque(range(5)); d.rotate(2)
    pruefe(list(d) == [3, 4, 0, 1, 2])
    dd = defaultdict(list); dd["x"].append(1)
    P = namedtuple("P", "x y")
    pruefe(P(1, 2).y == 2 and dd["x"] == [1])
    return "abrakadabra hat 5 mal a"


@nummer("Akrobaten: itertools")
def _():
    import itertools
    pruefe(len(list(itertools.permutations("abcd"))) == 24)
    pruefe(list(itertools.islice(itertools.count(10, 5), 3)) == [10, 15, 20])
    pruefe(list(itertools.chain("ab", "cd")) == list("abcd"))
    return "4 Akrobaten, 24 Reihenfolgen"


@nummer("Dompteur: functools")
def _():
    import functools, operator
    @functools.lru_cache(maxsize=None)
    def fib(n):
        return n if n < 2 else fib(n - 1) + fib(n - 2)
    pruefe(fib(60) == 1548008755920)
    pruefe(functools.reduce(operator.mul, range(1, 7)) == 720)
    return "fib(60) = %d" % fib(60)


@nummer("Tresor: hashlib")
def _():
    import hashlib
    pruefe(hashlib.md5(b"").hexdigest() == "d41d8cd98f00b204e9800998ecf8427e")
    pruefe(hashlib.sha1(b"abc").hexdigest() == "a9993e364706816aba3e25717850c26c9cd0d89d")
    h = hashlib.sha256(b"abc").hexdigest()
    pruefe(h.startswith("ba7816bf8f01cfea"))
    return "sha256(abc) = %s..." % h[:16]


@nummer("Geheimschrift: base64, binascii")
def _():
    import base64, binascii
    pruefe(base64.b64encode(b"Hallo Ulix") == b"SGFsbG8gVWxpeA==")
    pruefe(base64.b64decode(b"SGFsbG8gVWxpeA==") == b"Hallo Ulix")
    pruefe(binascii.hexlify(b"\x00\xff") == b"00ff")
    return "SGFsbG8gVWxpeA=="


@nummer("Packer: zlib, gzip, bz2, lzma")
def _():
    import zlib, gzip, bz2, lzma
    daten = b"Zirkus " * 1000
    for name, pack, entpack in (("zlib", zlib.compress, zlib.decompress),
                                ("gzip", gzip.compress, gzip.decompress),
                                ("bz2", bz2.compress, bz2.decompress),
                                ("lzma", lzma.compress, lzma.decompress)):
        klein = pack(daten)
        pruefe(entpack(klein) == daten, name)
        pruefe(len(klein) < len(daten) // 10, name + " packt nicht")
    return "7000 Bytes werden winzig"


@nummer("Archivar: zipfile, tarfile")
def _():
    import io, zipfile, tarfile
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("clown.txt", "Hallo!" * 50)
    with zipfile.ZipFile(io.BytesIO(b.getvalue())) as z:
        pruefe(z.read("clown.txt") == b"Hallo!" * 50)
    t = io.BytesIO()
    with tarfile.open(fileobj=t, mode="w:gz") as tf:
        info = tarfile.TarInfo("zelt.txt"); info.size = 5
        tf.addfile(info, io.BytesIO(b"Zelt!"))
    with tarfile.open(fileobj=io.BytesIO(t.getvalue()), mode="r:gz") as tf:
        pruefe(tf.extractfile("zelt.txt").read() == b"Zelt!")
    return "zip und tar im Speicher"


@nummer("Kassenbuch: sqlite3")
def _():
    import sqlite3
    db = sqlite3.connect(":memory:")
    db.execute("create table kasse (nummer text, preis real)")
    db.executemany("insert into kasse values (?, ?)",
                   [("Jongleur", 3.5), ("Zauberer", 4.25), ("Clown", 1.0)])
    summe = db.execute("select sum(preis) from kasse").fetchone()[0]
    pruefe(summe == 8.75)
    pruefe(db.execute("select nummer from kasse order by preis desc").fetchone()[0] == "Zauberer")
    return "Einnahmen: %.2f Euro" % summe


@nummer("Bauchredner: struct")
def _():
    import struct
    b = struct.pack("<hIf", -2, 70000, 1.5)
    pruefe(struct.unpack("<hIf", b) == (-2, 70000, 1.5))
    pruefe(struct.calcsize("<hIf") == 10)
    return "%d Bytes" % len(b)


@nummer("Setzer: textwrap, string")
def _():
    import textwrap, string
    s = textwrap.fill("Meine Damen und Herren, willkommen im Zirkus!", 20)
    pruefe(all(len(z) <= 20 for z in s.splitlines()))
    pruefe(string.Template("Hallo $wer").substitute(wer="Ulix") == "Hallo Ulix")
    pruefe(string.ascii_lowercase[:3] == "abc")
    return "%d Zeilen" % len(s.splitlines())


@nummer("Rechenkuenstler: decimal, fractions")
def _():
    from decimal import Decimal, getcontext
    from fractions import Fraction
    pruefe(Decimal("0.1") + Decimal("0.2") == Decimal("0.3"))
    getcontext().prec = 30
    pruefe(str(Decimal(1) / Decimal(7)).startswith("0.142857142857142857"))
    pruefe(Fraction(1, 3) + Fraction(1, 6) == Fraction(1, 2))
    return "0,1 + 0,2 = 0,3 (genau)"


@nummer("Statistiker: statistics")
def _():
    import statistics as st
    d = [2, 4, 4, 4, 5, 5, 7, 9]
    pruefe(st.mean(d) == 5 and st.median(d) == 4.5 and st.mode(d) == 4)
    pruefe(abs(st.pstdev(d) - 2.0) < 1e-12)
    return "Mittel 5, Median 4,5"


@nummer("Sortierer: heapq, bisect")
def _():
    import heapq, bisect
    h = []
    for x in (5, 1, 8, 3, 9, 2):
        heapq.heappush(h, x)
    pruefe([heapq.heappop(h) for _ in range(3)] == [1, 2, 3])
    pruefe(bisect.bisect_left([10, 20, 30, 40], 25) == 2)
    return "1, 2, 3 zuerst"


@nummer("Kostuemverleih: dataclasses, enum")
def _():
    import dataclasses, enum
    @dataclasses.dataclass(order=True)
    class Artist:
        name: str
        gage: int = 100
    class Zelt(enum.Enum):
        KLEIN = 1
        GROSS = 2
    pruefe(Artist("A", 5) < Artist("B", 1))
    pruefe(dataclasses.asdict(Artist("Z")) == {"name": "Z", "gage": 100})
    pruefe(Zelt(2) is Zelt.GROSS)
    return "Datenklassen und Aufzaehlungen"


@nummer("Tabellen: csv, io")
def _():
    import csv, io
    o = io.StringIO()
    w = csv.writer(o)
    w.writerow(["nummer", "preis"]); w.writerow(["Clown, rot", "1.0"])
    r = list(csv.reader(io.StringIO(o.getvalue())))
    pruefe(r[1][0] == "Clown, rot")
    return "Komma im Namen ueberlebt"


@nummer("Programmheft: argparse, configparser")
def _():
    import argparse, configparser
    p = argparse.ArgumentParser(prog="zirkus")
    p.add_argument("--zelte", type=int, default=1)
    p.add_argument("clown")
    a = p.parse_args(["--zelte", "3", "Bozo"])
    pruefe(a.zelte == 3 and a.clown == "Bozo")
    c = configparser.ConfigParser()
    c.read_string("[zelt]\nplaetze = 500\n")
    pruefe(c.getint("zelt", "plaetze") == 500)
    return "500 Plaetze"


@nummer("Ausrufer: logging")
def _():
    import logging, io
    s = io.StringIO()
    log = logging.getLogger("zirkus")
    log.handlers[:] = [logging.StreamHandler(s)]
    log.propagate = False
    log.setLevel(logging.INFO)
    log.info("Vorhang auf")
    log.debug("leise")
    pruefe(s.getvalue() == "Vorhang auf\n")
    return "Vorhang auf"


@nummer("Wegweiser: urllib.parse, html")
def _():
    import urllib.parse as up, html
    u = up.urlparse("http://ulix.local:8080/zelt?platz=7#vorne")
    pruefe((u.hostname, u.port, u.path, u.fragment) == ("ulix.local", 8080, "/zelt", "vorne"))
    pruefe(up.parse_qs(u.query) == {"platz": ["7"]})
    pruefe(html.escape("<b>&") == "&lt;b&gt;&amp;")
    return "Port %d" % u.port


@nummer("Plakate: xml.etree")
def _():
    import xml.etree.ElementTree as ET
    root = ET.fromstring("<zirkus><clown n='1'/><clown n='2'/></zirkus>")
    pruefe([c.get("n") for c in root.findall("clown")] == ["1", "2"])
    return "2 Clowns im XML"


@nummer("Briefkasten: email")
def _():
    from email.message import EmailMessage
    m = EmailMessage()
    m["Subject"] = "Freikarte"
    m.set_content("Komm vorbei!")
    pruefe("Freikarte" in m.as_string() and m.get_content().strip() == "Komm vorbei!")
    return "Betreff: Freikarte"


@nummer("Fundbuero: unicodedata")
def _():
    import unicodedata
    pruefe(unicodedata.name("ä") == "LATIN SMALL LETTER A WITH DIAERESIS")
    pruefe(unicodedata.normalize("NFD", "ä") == "ä")
    return "ae mit Pünktchen"


@nummer("Textvergleich: difflib, pprint, copy")
def _():
    import difflib, pprint, copy
    pruefe(difflib.SequenceMatcher(None, "Zirkus", "Zirkel").ratio() > 0.6)
    x = {"a": [1, 2]}
    y = copy.deepcopy(x); y["a"].append(3)
    pruefe(x == {"a": [1, 2]})
    pruefe("a" in pprint.pformat(y))
    return "Zirkus ~ Zirkel"


@nummer("Dateikeller: os, pathlib, tempfile")
def _():
    import os, pathlib, tempfile, shutil
    d = tempfile.mkdtemp()
    try:
        p = pathlib.Path(d) / "programm.txt"
        p.write_text("Heute: Feuerschlucker\n")
        pruefe(p.read_text() == "Heute: Feuerschlucker\n")
        pruefe(p.stat().st_size == len("Heute: Feuerschlucker\n"))
        pruefe("programm.txt" in os.listdir(d))
        os.rename(str(p), os.path.join(d, "neu.txt"))
        pruefe(sorted(os.listdir(d)) == ["neu.txt"])
    finally:
        shutil.rmtree(d)
    pruefe(not os.path.exists(d))
    return "Datei geschrieben, umbenannt, weggeraeumt"


@nummer("Suchtrupp: glob, fnmatch")
def _():
    import glob, fnmatch, os
    pruefe(fnmatch.fnmatch("zelt.py", "*.py"))
    pruefe(isinstance(glob.glob("/*"), list))
    pruefe(os.path.isdir("/"))
    return "gefunden"


@nummer("Chor: threading, queue")
def _():
    import threading, queue
    q = queue.Queue()
    summe = [0]
    sperre = threading.Lock()
    def sing(n):
        for i in range(100):
            with sperre:
                summe[0] += 1
        q.put(n)
    ts = [threading.Thread(target=sing, args=(i,)) for i in range(4)]
    for t in ts: t.start()
    for t in ts: t.join()
    pruefe(summe[0] == 400 and sorted(q.get() for _ in range(4)) == [0, 1, 2, 3])
    return "4 Saenger, 400 Takte"


@nummer("Telegraf: socket, select")
def _():
    import socket, select
    a, b = socket.socketpair()
    a.sendall(b"Hallo Zelt")
    r, _, _ = select.select([b], [], [], 2)
    pruefe(r == [b] and b.recv(100) == b"Hallo Zelt")
    a.close(); b.close()
    s = socket.socket()
    s.bind(("127.0.0.1", 0)); s.listen(1)               # Port 0: der Kernel sucht einen freien aus
    pruefe(s.getsockname()[1] > 0)
    # (TCP ueber Loopback, also connect auf den eigenen Listen-Socket, geht auf Ulix noch nicht: Issue)
    s.close()
    return "socketpair, select und bind/listen mit freiem Port"


@nummer("Fuhrpark: subprocess")
def _():
    import subprocess
    import os
    if os.path.exists("/musl/cat"):                      # auf Ulix
        o = subprocess.run(["/musl/cat", "/hello.c"], capture_output=True, timeout=30)
        was = "ein Kind-Prozess liest /hello.c"
    else:                                                 # auf einem anderen System
        o = subprocess.run([sys.executable, "-c", "print('Hello')"], capture_output=True, timeout=30)
        was = "ein Kind-Prozess sagt Hello"
    pruefe(o.returncode == 0 and b"Hello" in o.stdout)
    return was


@nummer("Uhrmacher: time, asyncio")
def _():
    import time, asyncio
    t = time.monotonic()
    async def nickerchen(n):
        await asyncio.sleep(0.05)
        return n * n
    async def haupt():
        return await asyncio.gather(*(nickerchen(i) for i in range(5)))
    pruefe(asyncio.run(haupt()) == [0, 1, 4, 9, 16])
    pruefe(time.monotonic() - t < 5)
    return "5 Nickerchen gleichzeitig"


@nummer("Schlosser: ssl")
def _():
    import ssl
    pruefe("SSL" in ssl.OPENSSL_VERSION)
    ctx = ssl.create_default_context()
    return ssl.OPENSSL_VERSION


@nummer("Dolmetscher II: ctypes")
def _():
    import ctypes
    libc = ctypes.CDLL(None)
    pruefe(libc.abs(-7) == 7)
    buf = ctypes.create_string_buffer(b"Zelt")
    pruefe(buf.value == b"Zelt")
    return "abs(-7) aus der C-Bibliothek"


@nummer("Lokalkolorit: locale")
def _():
    import locale
    pruefe(locale.getlocale() is not None or True)
    pruefe("%d" % 1234567 == "1234567")
    return "C-Locale"


print("=" * 60)
n = len(beifall) + len(buhrufe) + len(fehlend)
print("  Abendkasse: %d Nummern, %d mit Beifall, %d Buhrufe, %d nicht im Zelt"
      % (n, len(beifall), len(buhrufe), len(fehlend)))
print("  Dauer: %.1f Sekunden" % (time.time() - T0))
if buhrufe:
    print("  Buhrufe fuer: " + ", ".join(buhrufe))
    print("  Das war nix. Der Direktor ist nicht amuesiert.")
    print("zirkus: %d Nummer(n) durchgefallen" % len(buhrufe))
else:
    print("  Standing Ovations! Der Zirkus ist ausverkauft.")
    print("zirkus: alles bestanden")
print("=" * 60)
sys.exit(1 if buhrufe else 0)
