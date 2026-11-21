"""Write the original CPuppetProperties meta data stream of an executable (raw qwords)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE
STREAM = {'ed': 0x1420e3150, 'gm': 0x1412f92a0}
NQ = 0x463e
which, path, out = sys.argv[1], sys.argv[2], sys.argv[3]
pe = PE(path)
open(out, 'wb').write(pe.read(STREAM[which], NQ * 8))
