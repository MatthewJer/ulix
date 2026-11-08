#!/usr/bin/env python3
"""Test of "shutdown", "poweroff" and "reboot": usage: poweroff_test.py ulix-shell.bin DISK.img

Boots the system, types the command at the shell prompt and checks that qemu ends by itself (the system call poweroff: buffer cache to the
disks, then the PSCI function SYSTEM_OFF); with reboot the machine must start again (PSCI SYSTEM_RESET: the boot messages and the prompt
appear a second time). Done with shutdown, poweroff and reboot."""
import os, re, socket, subprocess, sys, time

kernel, disk = sys.argv[1:3]
fails = 0
def check(cond, msg):
    global fails
    print("  %s %s" % ("ok:  " if cond else "FAIL:", msg))
    if not cond: fails += 1

for number, command in enumerate(("shutdown", "poweroff", "reboot")):
    port = 45150 + number
    qemu = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-m", "128", "-display", "none", "-monitor", "none", "-kernel", kernel, "-snapshot",
        "-drive", "file=%s,format=raw,if=none,id=hd0" % disk, "-device", "virtio-blk-device,drive=hd0", "-serial", "tcp:127.0.0.1:%d,server=on,wait=on" % port],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sock = None
    for _ in range(100):
        try: sock = socket.create_connection(("127.0.0.1", port)); break
        except OSError: time.sleep(0.1)
    sock.settimeout(0.2)
    seen = b""
    def pump(t):
        global seen
        end = time.time() + t
        while time.time() < end:
            try:
                d = sock.recv(4096)
                if d: seen += d
            except (socket.timeout, ConnectionError): pass
    end = time.time() + 60
    while time.time() < end and not re.search(rb"esser@ulix\[\d+\]:/home/esser\$ ", seen): pump(0.2)
    check(bool(re.search(rb"esser@ulix\[\d+\]:/home/esser\$ ", seen)), "%s: the shell is up" % command)
    for ch in command + "\r":
        sock.send(ch.encode()); pump(0.05)
    if command == "reboot":
        end = time.time() + 60
        while time.time() < end and not (seen.count(b"Ulix-arm64 (work in progress)") >= 2 and len(re.findall(rb"esser@ulix\[\d+\]:/home/esser\$ ", seen)) >= 2): pump(0.2)
        check(seen.count(b"Ulix-arm64 (work in progress)") >= 2, "reboot: the kernel started again (its first message came twice)")
        check(len(re.findall(rb"esser@ulix\[\d+\]:/home/esser\$ ", seen)) >= 2, "reboot: the shell prompt is there again after the restart")
        check(b"Rebooting NOW!" in seen and b"Rebooting ..." in seen, "reboot: the messages of the program and of the kernel were shown")
    else:
        end = time.time() + 20
        while time.time() < end and qemu.poll() is None: pump(0.2)
        check(qemu.poll() is not None, "%s: qemu has ended by itself" % command)
        check(b"Shutdown NOW!" in seen and b"System halted." in seen, "%s: the messages of the program and of the kernel were shown" % command)
    if qemu.poll() is None: qemu.kill()
    qemu.wait()
print("poweroff test: %s" % ("all passed" if fails == 0 else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
