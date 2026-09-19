"""Turn a PROF.BIN from `make prof-atari` into a per-function table.

usage: python tools/prof_report.py PROF.BIN symbols.txt
symbols.txt is `m68k-atari-mint-nm -n build/PROF.PRG > symbols.txt`
"""
import struct
import sys
from bisect import bisect_right


def load_symbols(path):
    symbols = []
    for line in open(path):
        parts = line.split()
        if len(parts) != 3 or parts[1] not in ("T", "t"):
            continue
        name = parts[2]
        if name.startswith(".") or "/" in name or name.endswith(".o"):
            continue
        symbols.append((int(parts[0], 16), name))
    symbols.sort()
    # assembly-local labels (no leading underscore) belong to the function before them
    merged = []
    for address, name in symbols:
        if not name.startswith("_") and merged:
            continue
        merged.append((address, name))
    return merged


def main():
    data = open(sys.argv[1], "rb").read()
    base, count = struct.unpack(">II", data[:8])
    samples = struct.unpack(">%dI" % count, data[8:8 + 4 * count])
    symbols = load_symbols(sys.argv[2])
    addresses = [a for a, _ in symbols]
    totals = {}
    for pc in samples:
        offset = pc - base
        index = bisect_right(addresses, offset) - 1
        name = symbols[index][1] if 0 <= index and offset >= 0 else "(outside: %x)" % (pc >> 16)
        totals[name] = totals.get(name, 0) + 1
    print("samples: %d (about %.0f ms of run time at 10.24 kHz)" % (count, count / 10.24))
    for name, hits in sorted(totals.items(), key=lambda item: -item[1])[:28]:
        print("%6.1f%%  %6d  %s" % (100.0 * hits / count, hits, name))


main()
