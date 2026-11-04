#!/usr/bin/env python3
"""The other end of the serial disk (the book's "external controller process", chapter on device drivers).

Ulix talks to a disk that hangs on its second serial port: a command byte (2 = GET, 3 = PUT, 99 = terminate), the block
number as four bytes (lowest first), and for PUT the 1024 bytes of the block; GET is answered with the 1024 bytes.
This program serves such a disk from an image file.  qemu provides the serial port as a TCP socket:

    qemu-system-aarch64 ... -serial tcp:127.0.0.1:4444,server=on,wait=on     (second serial port; the first one is the console)
    tools/serial_disk.py disk3.img 4444

usage: serial_disk.py IMAGE [PORT]      (PORT default 4444)
"""
import socket, sys, time

BLOCK = 1024
CMD_STAT, CMD_GET, CMD_PUT, CMD_TERM = 1, 2, 3, 99


def recv_exact(sock, n):
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


def connect(port, timeout=20):
    end = time.time() + timeout
    while True:
        try:
            return socket.create_connection(("127.0.0.1", port))
        except OSError:
            if time.time() > end:
                raise
            time.sleep(0.1)


def serve(sock, image, log=None):
    """Answer requests until the command CMD_TERM or the end of the connection. Returns (reads, writes, errors)."""
    reads = writes = errors = 0
    nblocks = image_blocks(image)
    with open(image, "r+b") as f:
        while True:
            try:
                cmd = recv_exact(sock, 1)[0]
            except (EOFError, ConnectionError):
                break
            if cmd == CMD_TERM:
                break
            if cmd == CMD_STAT:
                continue
            if cmd not in (CMD_GET, CMD_PUT):
                errors += 1
                if log: log("unknown command %d" % cmd)
                continue
            try:
                blockno = int.from_bytes(recv_exact(sock, 4), "little")
            except (EOFError, ConnectionError):
                break
            if cmd == CMD_GET:
                f.seek(blockno * BLOCK)
                data = f.read(BLOCK) if blockno < nblocks else b""
                if len(data) < BLOCK:
                    errors += 1
                    if log: log("GET %d: beyond the end of the image" % blockno)
                    data = data.ljust(BLOCK, b"\0")
                sock.sendall(data)
                reads += 1
                if log: log("GET %d" % blockno)
            else:
                try:
                    data = recv_exact(sock, BLOCK)
                except (EOFError, ConnectionError):
                    break
                if blockno < nblocks:
                    f.seek(blockno * BLOCK); f.write(data); f.flush()
                    writes += 1
                else:
                    errors += 1
                if log: log("PUT %d" % blockno)
    return reads, writes, errors


def image_blocks(image):
    import os
    return os.path.getsize(image) // BLOCK


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    s = connect(int(sys.argv[2]) if len(sys.argv) > 2 else 4444)
    r, w, e = serve(s, sys.argv[1], log=lambda m: print(m, flush=True))
    print("served %d reads, %d writes, %d errors" % (r, w, e))
