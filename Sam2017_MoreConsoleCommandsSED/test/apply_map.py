"""apply_map.py MAP IN.c OUT.c - write OUT.c = IN.c with every hex number listed in MAP replaced by its new value.

MAP has one entry per line, "OLD NEW" in hex, then an optional comment after '#'. A number is replaced when its value
is an OLD of the map, whatever its spelling (0x1419EE000ULL, 0x1419ee000 in a message text, 0x027FB000); the new
number keeps the spelling's case and suffix. This turns the harness, written for the editor of 7 June 2019, into the
harness for the editor of 19 November 2026 (addr_2026.txt)."""
import re, sys
m = {}
for line in open(sys.argv[1]):
    line = line.split('#', 1)[0].split()
    if not line: continue
    m[int(line[0], 16)] = int(line[1], 16)
src = open(sys.argv[2]).read()
used = set()
def in_string(line, pos):
    q = False; i = 0
    while i < pos:
        if line[i] == '\\' and q: i += 2; continue
        if line[i] == '"': q = not q
        i += 1
    return q
def rep_line(line):
    def rep(t):
        v = int(t.group(1), 16)
        if v not in m: return t.group(0)
        used.add(v)
        # message texts (in strings) are lower case like the plugin's log, the C code keeps its case
        lower = in_string(line, t.start()) or any(c in 'abcdef' for c in t.group(1))
        return '0x' + ('%x' if lower else '%X') % m[v]
    return re.sub(r'0x([0-9a-fA-F]+)(?![0-9a-fA-F])', rep, line)
out = ''.join(rep_line(l) for l in src.splitlines(True))
open(sys.argv[3], 'w').write(out)
print('%s: %d of the %d map entries used' % (sys.argv[3], len(used), len(m)), file=sys.stderr)
