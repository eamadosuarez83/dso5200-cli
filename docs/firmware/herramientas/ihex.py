import sys
def load(path):
    mem = {}
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'): continue
        b = bytes.fromhex(line[1:])
        n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        assert (sum(b) & 0xff) == 0, "checksum"
        if typ == 0:
            for i in range(n): mem[addr + i] = b[4 + i]
        elif typ == 1: break
    return mem
def ranges(mem):
    out, start, prev = [], None, None
    for a in sorted(mem):
        if start is None: start = prev = a
        elif a == prev + 1: prev = a
        else: out.append((start, prev)); start = prev = a
    if start is not None: out.append((start, prev))
    return out
def image(mem):
    top = max(mem) + 1
    return bytes(mem.get(i, 0xff) for i in range(top))
