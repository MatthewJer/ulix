#!/usr/bin/env python3
"""Test of random numbers, SNTP and the https client of Ulix (TLS 1.2): usage: https_test.py ulix-shell.bin DISK.img

No Internet is needed. The host plays everything the client talks to:
  * a test certificate authority (made with the openssl program) and a TLS 1.2 web server with a certificate for the name
    test.ulix.example, signed by that authority; the guest reaches it as 10.0.2.2 (the host as seen from qemu's user network);
  * a time server (SNTP, UDP port 12300) that says it is 6 May 2031.
The kernel is started with a network card and a virtio random number generator, and a user logs in on the console and runs
rand, sntp and https. A second boot with tls_enable="NO" in /etc/rc.conf checks the switch.
Every check says what it proves.
"""
import os, re, shlex, shutil, socket, ssl, struct, subprocess, sys, tempfile, threading, time, calendar
from http.server import BaseHTTPRequestHandler, HTTPServer

kernel, disk = sys.argv[1:3]
here = os.path.dirname(os.path.abspath(__file__))
work = tempfile.mkdtemp(prefix="ulix-https-")
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

# ---- the test certificate authority and the server certificate ----
def openssl(*args):
    subprocess.run(["openssl", *args], cwd=work, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
open(os.path.join(work, "cfg"), "w").write("""[req]
distinguished_name = dn
[dn]
[v3_ca]
basicConstraints = critical,CA:TRUE
keyUsage = keyCertSign,cRLSign
[v3_srv]
basicConstraints = CA:FALSE
subjectAltName = DNS:test.ulix.example
keyUsage = digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
""")
openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key", "-out", "ca.pem", "-subj", "/CN=Ulix Test CA", "-days", "30", "-config", "cfg", "-extensions", "v3_ca")
openssl("req", "-newkey", "rsa:2048", "-nodes", "-keyout", "srv.key", "-out", "srv.csr", "-subj", "/CN=test.ulix.example", "-config", "cfg")
openssl("x509", "-req", "-in", "srv.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial", "-out", "srv.pem", "-days", "30", "-extfile", "cfg", "-extensions", "v3_srv")

# ---- the web server (TLS 1.2 only) ----
BODY = b"hello from the Ulix test server\n"
seen = []                                               # (TLS version, cipher) of every handshake that was completed
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    def do_GET(self):
        seen.append((self.connection.version(), self.connection.cipher()[0]))
        if self.path == "/missing":
            self.send_response(404); self.end_headers(); self.wfile.write(b"no\n"); return
        self.send_response(200); self.send_header("Content-Length", str(len(BODY))); self.end_headers(); self.wfile.write(BODY)
    def log_message(self, *a): pass
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_2
ctx.load_cert_chain(os.path.join(work, "srv.pem"), os.path.join(work, "srv.key"))
class TLSServer(HTTPServer):
    def get_request(self):
        sock, addr = self.socket.accept()
        try: return ctx.wrap_socket(sock, server_side=True), addr
        except ssl.SSLError: sock.close(); raise OSError("handshake failed")      # (a client that refuses our certificate)
server = TLSServer(("127.0.0.1", 0), Handler)
HTTPS_PORT = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()

# ---- the time server: 6 May 2031, 07:08:09 UTC ----
FAKE = calendar.timegm((2031, 5, 6, 7, 8, 9))
udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp.bind(("127.0.0.1", 12300))
asked = []
def sntp_server():
    while True:
        data, peer = udp.recvfrom(512)
        if len(data) < 48: continue
        asked.append(data[0])
        answer = bytearray(48)
        answer[0] = 0x24; answer[1] = 2                  # version 4, mode 4 (server), stratum 2
        answer[24:32] = data[40:48]                      # the originate time: what the client sent as transmit time
        answer[40:44] = struct.pack(">I", FAKE + 2208988800)
        udp.sendto(bytes(answer), peer)
threading.Thread(target=sntp_server, daemon=True).start()

# ---- disks: the test authority is added to a copy; a second copy has tls_enable="NO" ----
disk_ca = os.path.join(work, "disk-ca.img"); shutil.copy(disk, disk_ca)
subprocess.run([sys.executable, os.path.join(here, "minix_add.py"), disk_ca, "/etc/ssl/test-ca.pem=" + os.path.join(work, "ca.pem")], check=True, stdout=subprocess.DEVNULL)
disk_off = os.path.join(work, "disk-off.img"); shutil.copy(disk_ca, disk_off)
open(os.path.join(work, "rc.off"), "w").write('telnetd_enable="NO"\ntls_enable="NO"\n')
subprocess.run([sys.executable, os.path.join(here, "minix_add.py"), disk_off, "/etc/rc.conf=" + os.path.join(work, "rc.off")], check=True, stdout=subprocess.DEVNULL)

class Session:
    """qemu with the shell kernel; the console is a TCP connection (like tools/shell_test.py)"""
    PROMPT = r'esser@ulix\[\d+\]:(/[\w/]*)\$ '
    def __init__(self, image, port, rng=True):
        cmd = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none", "-kernel", kernel, "-snapshot",
               "-drive", "file=%s,format=raw,if=none,id=hd0" % image, "-device", "virtio-blk-device,drive=hd0",
               "-netdev", "user,id=n0", "-device", "virtio-net-device,netdev=n0",
               "-serial", "tcp:127.0.0.1:%d,server=on,wait=on" % port]
        if rng: cmd += ["-device", "virtio-rng-device"]
        self.qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.sock = None
        for _ in range(100):
            try: self.sock = socket.create_connection(("127.0.0.1", port)); break
            except OSError: time.sleep(0.1)
        if not self.sock: self.qemu.kill(); sys.exit("cannot connect to qemu")
        self.sock.settimeout(0.2)
        self.log = b""
        self.status = re.compile(rb'\x1b7\x1b\[25;1H\x1b\[0;97;4[14]m.{80}\x1b\[0m\x1b8', re.S)
    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
                if data: self.log += self.status.sub(b"", data)
            except socket.timeout: pass
    def expect(self, pattern, what, timeout=25.0):
        start = time.time()
        while time.time() - start < timeout:
            self.pump(0.1)
            m = re.search(pattern.encode(), self.log, re.S)
            if m:
                seen_text = self.log[:m.end()]; self.log = self.log[m.end():]
                check(True, what); return seen_text.decode("latin1")
        check(False, "%s -- did not see %r; got: %r" % (what, pattern, self.log[-300:]))
        return ""
    def send(self, text):
        for b in text.encode():
            self.sock.send(bytes([b])); self.pump(0.04)
    def run(self, cmd, answer, what, timeout=25.0):
        self.send(cmd + "\r")
        t0 = time.time()
        out = self.expect(answer, what + " (%.1f s)" % 0, timeout)
        print("        [%.1f s]" % (time.time() - t0))
        self.expect(self.PROMPT, "the prompt is back after \"%s\"" % cmd, timeout)
        return out
    def login(self):
        self.expect(r"ulix login: esser", "the system started and logged in", 120)
        self.expect(self.PROMPT, "shell prompt")
    def close(self): self.qemu.kill()

URL = "https://10.0.2.2:%d/" % HTTPS_PORT
try:
    s = Session(disk_ca, 45140)
    try:
        s.login()
        # --- random numbers (random.c: the virtio-rng device, getrandom)
        a = s.run("rand", r"\n([0-9a-f]{32})\r?\n", "rand: 16 bytes from the random number generator (32 hex digits)")
        b = s.run("rand", r"\n([0-9a-f]{32})\r?\n", "rand again")
        ra, rb = re.findall(r"\n([0-9a-f]{32})\r?\n", a), re.findall(r"\n([0-9a-f]{32})\r?\n", b)
        check(ra and rb and ra[-1] != rb[-1], "two calls give different numbers")
        # --- the time (sntp.c, clock_source)
        s.run("sntp", r"clock: 20\d\d-\d\d-\d\d \d\d:\d\d:\d\d UTC \(hardware clock\)", "sntp without arguments: the clock of qemu comes from a hardware clock")
        s.run("sntp 10.0.2.2:12300", r"says 2031-05-06 07:08:09 UTC", "sntp: the answer of the test time server is read (originate time checked, seconds converted to a date)", 60)
        check(asked and asked[-1] == 0x23, "the request was an SNTP version 4 client packet (byte 0 = 0x23)")
        s.run("sntp -s 10.0.2.2:12300", r"only root can set the clock", "sntp -s as a normal user: the clock is not changed")
        # --- https (userland/https.c with BearSSL)
        s.run("https", r"usage: https", "https without an address shows how to use it")
        s.run("https -n test.ulix.example " + URL, r"https: the certificate does not lead to a trusted certificate authority", "the real list of authorities (/etc/ssl/cacert.pem, right name) does not trust the test authority: the connection is refused", 120)
        s.run("https -c /etc/ssl/test-ca.pem " + URL, r"https: the certificate is not for this host name", "with the right authority but without -n: the name 10.0.2.2 is not in the certificate: refused", 120)
        out = s.run("https -v -c /etc/ssl/test-ca.pem -n test.ulix.example " + URL + "hello.txt",
                    r"(?s)1 certificate authorities.*TLS 1\.2, cipher suite 0x[0-9a-f]+, certificate for test\.ulix\.example checked.*hello from the Ulix test server", "https with the test authority and the right name: TLS 1.2 handshake, certificate chain, name and page", 180)
        check(len(seen) == 1 and seen[0][0] == "TLSv1.2", "the server saw one completed handshake, version TLSv1.2 (cipher %s)" % (seen[0][1] if seen else "?"))
        s.run("https -i -c /etc/ssl/test-ca.pem -n test.ulix.example " + URL + "missing", r"HTTP/1\.0 404.*https: the server answered 404", "https -i shows the header; an error status gives exit status 1 and a message", 180)
        s.run("https -c /etc/ssl/nonexistent.pem -n test.ulix.example " + URL, r"https: cannot read the certificate authorities from /etc/ssl/nonexistent.pem", "a missing authority file is reported")
        s.run("https -c /etc/ssl/test-ca.pem -n test.ulix.example https://10.0.2.2:1/", r"https: cannot connect to 10\.0\.2\.2 port 1", "a port where nobody listens: cannot connect (reset)", 60)
        pump_end = time.time() + 1; s.pump(1)
        check(b"EXCEPTION" not in s.log and b"killed:" not in s.log, "no kernel exception, no process killed by a fault")
    finally:
        s.close()

    # the switch: tls_enable="NO" in /etc/rc.conf (and no random number generator: the check for TLS comes first)
    s = Session(disk_off, 45141, rng=False)
    try:
        s.login()
        s.run("https " + URL, r"https: TLS is disabled \(tls_enable=\"NO\" in /etc/rc\.conf\)", "tls_enable=\"NO\": https says that TLS is disabled and stops, nothing is sent")
        s.run("rand", r"rand: this machine has no random number generator", "without a virtio-rng device: rand says so instead of making numbers up")
    finally:
        s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)
print("https test: %s" % ("all passed" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
