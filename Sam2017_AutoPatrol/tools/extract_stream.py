"""Write the original CPuppetProperties meta data stream of an executable (raw qwords).
usage: extract_stream.py ed|gm|ed26|gm26 <exe> <out.bin>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE
STREAM = {'ed': 0x1420e3150, 'gm': 0x1412f92a0, 'ed26': 0x142145bd0, 'gm26': 0x14128e8e0}
NQ = {'ed': 0x463e, 'gm': 0x463e, 'ed26': 0x469e, 'gm26': 0x469e}
which, path, out = sys.argv[1], sys.argv[2], sys.argv[3]
pe = PE(path)
open(out, 'wb').write(pe.read(STREAM[which], NQ[which] * 8))
