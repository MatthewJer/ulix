"""Does the name server of this host answer "localhost" with 127.0.0.1?

qemu's own name server (10.0.2.3) passes every question on to the name servers of the host (the lines "nameserver" in
/etc/resolv.conf). A home router answers "localhost"; the name server of a university or a company often does not (NXDOMAIN).
The tests that ask "localhost" through qemu (kernel DNS check, `host localhost`, musl's resolver test) then cannot pass, and
that is a property of the network the computer is in, not of Ulix. They call host_answers_localhost () and skip themselves
(with a message) when it is False. The environment variable HOSTDNS=ok or HOSTDNS=skip forces the answer.
"""
import os, socket, struct

def host_answers_localhost(timeout=2.0):
    forced = os.environ.get('HOSTDNS')
    if forced in ('ok', 'skip'): return forced == 'ok'
    servers = []
    try:
        for line in open('/etc/resolv.conf'):
            f = line.split()
            if len(f) >= 2 and f[0] == 'nameserver': servers.append(f[1])
    except OSError:
        return False
    query = struct.pack('>HHHHHH', 0x4c21, 0x0100, 1, 0, 0, 0) + b'\x09localhost\x00' + struct.pack('>HH', 1, 1)
    for server in servers[:2]:
        try:
            s = socket.socket(socket.AF_INET6 if ':' in server else socket.AF_INET, socket.SOCK_DGRAM)
            s.settimeout(timeout)
            s.sendto(query, (server, 53))
            reply = s.recv(512)
            s.close()
        except OSError:
            continue
        flags, answers = struct.unpack('>HH', reply[2:4] + reply[6:8])
        if (flags & 0xf) == 0 and answers > 0 and bytes([127, 0, 0, 1]) in reply[len(query):]:
            return True
    return False

if __name__ == '__main__':
    ok = host_answers_localhost()
    print('the name server of the host %s "localhost"' % ('answers' if ok else 'does not answer'))
    raise SystemExit(0 if ok else 1)
