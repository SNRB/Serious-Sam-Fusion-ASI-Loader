import sys, struct, pickle
sys.path.insert(0, __import__('os').path.dirname(__file__))
import xref, meta
which = sys.argv[1]
x = xref.get(which)
pe = x.pe
REG = int(sys.argv[2], 16)
calls = x.calls_to(REG) + x.jmps_to(REG)
streams = []
for c in calls:
    b = pe.read(c - 7, 7)
    if b[:3] == b'\x48\x8d\x0d':
        d = struct.unpack('<i', b[3:])[0]
        streams.append((c, c + d))
print('calls', len(calls), 'streams', len(streams), file=sys.stderr)
allents = {}
bad = 0
for c, s in streams:
    try:
        allents[s] = meta.parse_stream(pe, s)
    except Exception as ex:
        bad += 1
        print('ERR', hex(c), hex(s), ex, file=sys.stderr)
print('bad', bad, file=sys.stderr)
HERE = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
pickle.dump(allents, open(__import__('os').path.join(HERE, 'meta_%s.pkl' % which), 'wb'))
with open(__import__('os').path.join(HERE, 'meta_%s.txt' % which), 'w') as f:
    for s, ents in sorted(allents.items()):
        print('##### STREAM %x' % s, file=f)
        meta.dump(ents, f)
