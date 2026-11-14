#!/usr/bin/env python3
"""Test of scp with the SSH server of Ulix: usage: scp_test.py ulix-shell.bin DISK.img

qemu forwards TCP port 5523 of the host to port 2222 of the kernel (the scp port of sshd: password login, files only). The host's own scp (OpenSSH, old protocol: scp -O)
copies files to the machine and back; the contents are compared, a wrong password and a missing file are refused. Every check says what it proves.
"""
import hashlib, os, shutil, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
work = tempfile.mkdtemp(prefix="ulix-scp-")
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

PORT = 5523
log = os.path.join(work, "console.log")
qemu = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none", "-serial", "file:" + log,
    "-kernel", kernel, "-snapshot", "-drive", "file=%s,format=raw,if=none,id=hd0" % disk, "-device", "virtio-blk-device,drive=hd0",
    "-netdev", "user,id=n0,hostfwd=tcp:127.0.0.1:%d-10.0.2.15:2222" % PORT, "-device", "virtio-net-device,netdev=n0", "-device", "virtio-rng-device"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

askpass = os.path.join(work, "askpass.sh")
def set_password(pw):
    open(askpass, "w").write("#!/bin/sh\necho %s\n" % pw); os.chmod(askpass, 0o755)

def scp(args, pw="xyz"):
    set_password(pw)
    env = dict(os.environ, SSH_ASKPASS=askpass, SSH_ASKPASS_REQUIRE="force", DISPLAY="x")
    cmd = ["scp", "-O", "-P", str(PORT), "-o", "KexAlgorithms=curve25519-sha256", "-o", "Ciphers=aes128-ctr", "-o", "MACs=hmac-sha2-256",
           "-o", "HostKeyAlgorithms=ecdsa-sha2-nistp256", "-o", "UserKnownHostsFile=/dev/null", "-o", "StrictHostKeyChecking=no", "-o", "LogLevel=ERROR",
           "-o", "PubkeyAuthentication=no", "-o", "PreferredAuthentications=password", "-o", "NumberOfPasswordPrompts=1", "-o", "ConnectTimeout=20", *args]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=240)
    if r.returncode and os.environ.get('SCP_DEBUG'): print('    scp', args, '->', r.returncode, (r.stdout + r.stderr).strip()[:200])
    return r.returncode, r.stdout + r.stderr

try:
    up = False
    for _ in range(1200):
        if os.path.exists(log) and "sshd: ssh server on port" in open(log, errors="replace").read(): up = True; break
        time.sleep(0.1)
    # (the boot prints it on terminal 9; if the line is not in the serial log, wait for the login instead)
    for _ in range(600):
        if os.path.exists(log) and "ulix login: esser" in open(log, errors="replace").read(): break
        time.sleep(0.1)
    time.sleep(3)
    small = os.path.join(work, "small.txt"); open(small, "w").write("hello over scp\n" * 40)
    big = os.path.join(work, "big.bin"); data = os.urandom(150000); open(big, "wb").write(data)
    code, out = scp([small, "esser@127.0.0.1:scp-small.txt"])
    check(code == 0, "scp esser@host: a small file is copied to the home directory (%s)" % out.strip()[:100])
    code, out = scp([big, "esser@127.0.0.1:/tmp/scp-big.bin"])
    check(code == 0, "a file of 150000 bytes is copied to /tmp with an absolute name (%s)" % out.strip()[:100])
    back1 = os.path.join(work, "back1.txt"); back2 = os.path.join(work, "back2.bin")
    code, out = scp(["esser@127.0.0.1:scp-small.txt", back1])
    check(code == 0 and os.path.exists(back1) and open(back1).read() == open(small).read(), "the small file comes back unchanged (download, relative name)")
    code, out = scp(["esser@127.0.0.1:/tmp/scp-big.bin", back2])
    check(code == 0 and os.path.exists(back2) and hashlib.sha256(open(back2, "rb").read()).digest() == hashlib.sha256(data).digest(), "the big file comes back with the same SHA-256 (more than the window of the channel)")
    code, out = scp([small, "esser@127.0.0.1:/tmp"])
    code2, out2 = scp(["esser@127.0.0.1:/tmp/small.txt", os.path.join(work, "back3.txt")])
    check(code == 0 and code2 == 0 and open(os.path.join(work, "back3.txt")).read() == open(small).read(), "the target is a directory (/tmp): the file keeps its name")
    code, out = scp([small, "esser@127.0.0.1:scp-small.txt"])
    code2, out2 = scp(["esser@127.0.0.1:scp-small.txt", os.path.join(work, "back4.txt")])
    check(code == 0 and code2 == 0 and open(os.path.join(work, "back4.txt")).read() == open(small).read(), "copying to an existing file replaces it")
    code, out = scp([small, "esser@127.0.0.1:x.txt"], pw="wrong")
    check(code != 0, "a wrong password is refused (%s)" % out.strip()[:80])
    code, out = scp(["esser@127.0.0.1:no-such-file", os.path.join(work, "nothing")])
    check(code != 0, "a file that does not exist is an error, not an empty file")
finally:
    qemu.kill()
    shutil.rmtree(work, ignore_errors=True)
print("scp test: %s" % ("all passed" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
