"""cpu_test.py: a CPU test for Ulix that logs the load of every core to a CSV file.

Run it in the directory where the log is to go, for example on a USB stick:

    cd /Volumes/STICKNAME
    /musl/python /Volumes/STICKNAME/cpu_test.py            # 40 seconds, writes cpu_load.csv into this directory
    /musl/python /Volumes/STICKNAME/cpu_test.py 60 0.25    # duration in seconds, interval between two lines in seconds

The test starts one busy process after the other (a sixth of the time apart: 1, 2, 3, 4 busy cores), lets all four run, stops them and
leaves the last sixth idle, so that the file shows the load going up and down. The parent samples /dev/cpustat (the kernel's counters
of busy and idle ticks of every CPU, the numbers that `top` shows) and writes one line per sample:

    timestamp,core0,core1,core2,core3        (the load of the core since the line before, in percent)
"""
import os
import sys
import time

duration = float(sys.argv[1]) if len(sys.argv) > 1 else 40.0
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
workers = 4


def read_counters(fd):
    """The busy and idle ticks of every CPU as {core: (busy, idle)}; every read of /dev/cpustat gives the numbers of that moment."""
    text = os.read(fd, 1024).decode()
    counters = {}
    for line in text.split("\n"):
        parts = line.split()
        if len(parts) == 3 and parts[0].startswith("cpu"):
            counters[int(parts[0][3:])] = (int(parts[1]), int(parts[2]))
    return counters


def stamp(t):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t)) + ".%03d" % int((t % 1) * 1000)


def burn(until):
    """A busy loop: integer arithmetic until the time is over."""
    x = 1
    while time.time() < until:
        for _ in range(2000):
            x = (x * 31 + 7) % 1000003
    os._exit(0)


try:
    fd = os.open("/dev/cpustat", os.O_RDONLY)
except OSError:
    print("cpu_test: there is no /dev/cpustat (this kernel is too old)")
    sys.exit(1)

before = read_counters(fd)
cores = sorted(before)
path = os.path.join(os.getcwd(), "cpu_load.csv")
log = open(path, "w")
log.write("timestamp," + ",".join("core%d" % c for c in cores) + "\n")
log.flush()

start = time.time()
end = start + duration
stop_workers = start + duration * 5 / 6          # the last sixth is idle
pids = []
print("cpu_test: %d seconds, a line every %.2f seconds into %s" % (duration, interval, path))

next_worker = 0
rows = 0
while True:
    now = time.time()
    if now >= end:
        break
    if next_worker < workers and now >= start + duration * (next_worker + 1) / 6:
        pid = os.fork()
        if pid == 0:
            burn(stop_workers)
        pids.append(pid)
        next_worker += 1
        print("cpu_test: %d busy process(es)" % next_worker)
    time.sleep(interval)
    after = read_counters(fd)
    load = []
    for c in cores:
        busy = after[c][0] - before[c][0]
        idle = after[c][1] - before[c][1]
        load.append("%.1f" % (100.0 * busy / (busy + idle) if busy + idle > 0 else 0.0))
    log.write(stamp(time.time()) + "," + ",".join(load) + "\n")
    log.flush()
    rows += 1
    before = after

for pid in pids:
    try:
        os.waitpid(pid, 0)
    except OSError:
        pass
log.close()
print("cpu_test: %d lines written to %s" % (rows, path))
print("DONE-CPU-TEST")
