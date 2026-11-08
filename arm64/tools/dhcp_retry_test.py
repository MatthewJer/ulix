#!/usr/bin/env python3
"""Test: a cable that is plugged in after the boot. Usage: dhcp_retry_test.py ulix-console.bin DISK.img

The console kernel (qemu is started stopped, the cable pulled out, then it goes on) starts with "ip=dhcp" and a network whose cable is pulled out (qemu: set_link nic0 off), so the DHCP request at the
boot is not answered. Then the cable is plugged in (set_link on). The network thread must notice that and get an address from
DHCP by itself ("net: DHCP: address 10.0.2.15" a second time), without any command.
"""
import os, socket, subprocess, sys, tempfile, time

kernel, disk = sys.argv[1:3]
tmp = tempfile.mkdtemp()
log, mon = os.path.join(tmp, "console.log"), os.path.join(tmp, "monitor.sock")
qemu = subprocess.Popen(["qemu-system-aarch64", "-S", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none",
    "-monitor", "unix:%s,server,nowait" % mon, "-serial", "file:" + log, "-kernel", kernel, "-snapshot",
    "-drive", "file=%s,format=raw,if=none,id=hd0" % disk, "-device", "virtio-blk-device,drive=hd0",
    "-netdev", "user,id=n0", "-device", "virtio-net-device,netdev=n0,id=nic0", "-append", "ip=dhcp"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1
def console(): return open(log, errors="replace").read() if os.path.exists(log) else ""
def wait_for(text, seconds):
    for _ in range(seconds * 10):
        if text in console(): return True
        time.sleep(0.1)
    return False
def monitor(command):
    s = socket.socket(socket.AF_UNIX); s.connect(mon); s.settimeout(2)
    try: s.recv(4096)
    except socket.timeout: pass
    s.sendall((command + "\n").encode()); time.sleep(0.5); s.close()

try:
    for _ in range(50):                                        # the monitor socket exists at once; pull the cable before the kernel asks
        if os.path.exists(mon): break
        time.sleep(0.1)
    monitor("set_link nic0 off")                               # (qemu was started stopped: -S)
    monitor("cont")
    check(wait_for("net: no answer from a DHCP server", 90), "without a cable the DHCP request at the boot is not answered")
    check("net: DHCP: address" not in console(), "no address yet")
    monitor("set_link nic0 on")                                  # the cable is plugged in
    check(wait_for("net: DHCP: address 10.0.2.15", 60), "the network thread asks again by itself and gets the address")
finally:
    qemu.kill(); qemu.wait()
print("dhcp retry test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
