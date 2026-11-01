#!/usr/bin/env python3
"""Convert a Broadcom SROM image (rev 11/12/13) into brcmfmac NVRAM text.

Follows _initvars_srom_pci() from Broadcom's bcmsrom.c: the pci_sromvars and
perpath_pci_sromvars tables of bcmsrom_tbl.h (offsets from bcmsrom_fmt.h) are
applied to the image for the given SROM revision. The Broadcom headers are not
shipped here; pass their paths.

usage: srom2nvram.py <srom.bin> <bcmsrom_fmt.h> <bcmsrom_tbl.h> [--rev N]
"""
import argparse, re, struct, sys

SRFL_MORE, SRFL_NOFFS, SRFL_PRHEX, SRFL_PRSIGN = 1, 2, 4, 8
SRFL_CCODE, SRFL_ETHADDR, SRFL_LEDDC, SRFL_NOVAR, SRFL_ARRAY = 0x10, 0x20, 0x40, 0x80, 0x100
PATHS = {11: ("SROM11_PATH0", "SROM11_PATH1", "MAX_PATH_SROM_11"),
         12: ("SROM12_PATH0", "SROM12_PATH1", "MAX_PATH_SROM_12"),
         13: ("SROM13_PATH0", "SROM13_PATH1", "MAX_PATH_SROM_13")}


def load_defines(*files):
    raw = {}
    for f in files:
        for m in re.finditer(r'^#define\s+(\w+)\s+([^\n/]+?)\s*(?:/[*/].*)?$', open(f).read(), re.M):
            raw[m.group(1)] = m.group(2).strip()
    vals = {}

    def ev(name, depth=0):
        if name in vals:
            return vals[name]
        if depth > 50 or name not in raw:
            raise KeyError(name)
        expr = re.sub(r'\b([A-Za-z_]\w*)\b', lambda m: str(ev(m.group(1), depth + 1)), raw[name])
        expr = re.sub(r'\b(0x[0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', expr)
        vals[name] = int(eval(expr, {"__builtins__": {}}))
        return vals[name]

    return raw, ev


def load_table(tbl, name, ev):
    src = open(tbl).read()
    body = src[src.index(f"sromvar_t {name}[] = {{"):]
    body = body[:body.index("\n};")]
    flagmap = {k: v for k, v in globals().items() if k.startswith("SRFL_")}
    out = []
    for m in re.finditer(r'\{\s*(NULL|"[^"]*")\s*,\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^}]+)\}', body):
        if m.group(1) == "NULL":
            break
        nm = m.group(1).strip('"')
        try:
            revmask = int(m.group(2), 0)
            flags = 0
            for f in m.group(3).split("|"):
                f = f.strip()
                flags |= flagmap[f] if f in flagmap else int(f, 0)
            off = m.group(4).strip()
            off = sum(ev(t.strip()) if not re.match(r'^\d', t.strip()) else int(t, 0) for t in off.split("+"))
            mask = int(m.group(5).strip(), 0)
        except (KeyError, ValueError, SyntaxError):
            out.append(None)  # unresolvable (e.g. CABLECPE devid) - keeps chains aligned
            continue
        out.append((nm, revmask, flags, off, mask))
    return out


def shift_width(mask):
    s = (mask & -mask).bit_length() - 1
    return s, mask.bit_length() - s


def fmt_val(flags, val, width):
    if flags & SRFL_PRHEX:
        return f"0x{val:x}"
    if flags & SRFL_PRSIGN and val & (1 << (width - 1)):
        return str(val - (1 << width))
    return str(val)


def walk(table, srom, rev, base=0, suffix=""):
    """Yield (name, value-string) like _initvars_srom_pci()."""
    bit = 1 << rev
    i, arr_name, arr_vals = 0, None, []
    while i < len(table):
        e = table[i]
        i += 1
        if e is None:
            continue
        nm, revmask, flags, off, mask = e
        if not revmask & bit:
            continue
        if flags & SRFL_NOVAR:
            while flags & SRFL_MORE and i < len(table):
                flags = table[i][2] if table[i] else 0
                i += 1
            continue
        if flags & SRFL_ETHADDR:
            w = srom[base + off:base + off + 3]
            yield nm + suffix, ":".join(f"{b:02x}" for x in w for b in (x >> 8, x & 0xff))
            continue
        s, width = shift_width(mask)
        val = (srom[base + off] & mask) >> s
        first_flags = flags
        while flags & SRFL_MORE:
            e2 = table[i]
            i += 1
            s2, w2 = shift_width(e2[4])
            val += ((srom[base + e2[3]] & e2[4]) >> s2) << width
            width += w2
            flags = e2[2]
        flags = first_flags | (flags & SRFL_ARRAY)
        if arr_name is None and first_flags & SRFL_NOFFS and val == (1 << width) - 1:
            continue
        if first_flags & SRFL_CCODE:
            yield nm + suffix, "" if val == 0 else chr(val >> 8) + chr(val & 0xff)
            continue
        txt = fmt_val(first_flags, val, width)
        if arr_name is not None or first_flags & SRFL_ARRAY:
            if arr_name is None:
                arr_name = nm
            arr_vals.append(txt)
            if not first_flags & SRFL_ARRAY:  # last element of the array
                yield arr_name + suffix, ",".join(arr_vals)
                arr_name, arr_vals = None, []
            continue
        yield nm + suffix, txt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("srom")
    ap.add_argument("fmt_h")
    ap.add_argument("tbl_h")
    ap.add_argument("--rev", type=int)
    a = ap.parse_args()

    data = open(a.srom, "rb").read()
    srom = list(struct.unpack(f"<{len(data) // 2}H", data))
    raw, ev = load_defines(a.fmt_h, a.tbl_h)
    rev = a.rev
    if rev is None:
        rev = srom[ev("SROM12_CRCREV")] & 0xff
    print(f"# generated by srom2nvram.py from {a.srom.split('/')[-1]}, SROM rev {rev}")
    print(f"sromrev={rev}")
    for nm, v in walk(load_table(a.tbl_h, "pci_sromvars", ev), srom, rev):
        print(f"{nm}={v}")
    p0, p1, npath = (ev(x) for x in PATHS[rev])
    for p in range(npath):
        for nm, v in walk(load_table(a.tbl_h, "perpath_pci_sromvars", ev), srom, rev,
                          base=p0 + p * (p1 - p0), suffix=str(p)):
            print(f"{nm}={v}")


if __name__ == "__main__":
    main()
