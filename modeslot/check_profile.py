"""Check modeslot's profile against an MPC binary on disk (read only; run with python3 -I).

usage: python3 -I check_profile.py MPC-BINARY

Checks, for the profile whose build-id matches: the four entry instructions of getModeName, the
first instruction of juce::String(const char*), that getModeName's 0x2080 jump table sends the slot
ID to the same place as the other unnamed IDs (the default case), and that the binary has no BTI
property note. Prints one line per check; exit status 1 if any fails.
"""
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def profiles():
    src = (HERE / "src" / "modeslot.c").read_text()
    out = []
    num = lambda blk, k: int(re.search(r"\." + k + r" = (0x[0-9a-f]+)", blk).group(1), 16)

    def site(blk, k):
        m = re.search(r"\." + k + r" = \{(0x[0-9a-f]+), \{(.*?)\}\}", blk)
        return (int(m.group(1), 16), [int(x, 16) for x in re.findall(r"0x[0-9a-f]{8}", m.group(2))]) if m else None

    for blk in re.findall(r"\{\s*\.label = (.*?)\n    \},", src, re.S):
        hexes = re.search(r"\.build_id = \{(.*?)\}", blk, re.S).group(1)
        out.append({
            "label": re.match(r'"(.*?)"', blk).group(1),
            "build_id": bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", hexes)),
            "candidates": [int(x, 16) for x in re.findall(r"0x[0-9a-f]+", re.search(r"\.candidates = \{(.*?)\}", blk).group(1)) if int(x, 16)],
            "tables": {k: num(blk, k) for k in ("name_table", "name_table_base", "name_default", "page_table",
                                                "page_table_base", "page_default", "table_len")},
            "sites": {k: site(blk, k) for k in ("get_mode_name", "register_mode", "mode_changed", "image_from_memory", "mode_icon", "menu_order", "page_paint")},
            "string_ctor": num(blk, "string_ctor"),
            "string_ctor_first": num(blk, "string_ctor_first"),
            "alias_from": num(blk, "alias_from") if ".alias_from" in blk else None,
            "placeholder_ptr_var": num(blk, "placeholder_ptr_var") if ".placeholder_ptr_var" in blk else None,
            "placeholder_size": num(blk, "placeholder_size") if ".placeholder_size" in blk else None,
            "fns": {k: (int(m.group(1), 16), int(m.group(2), 16)) for k in ("repaint", "repaint_area", "memstream_ctor",
                    "memstream_dtor", "find_format", "set_bounds")
                    for m in [re.search(r"\." + k + r" = \{(0x[0-9a-f]+), (0x[0-9a-f]+)\}", blk)] if m},
            "mouse_slots": [int(x, 16) for x in re.findall(r"0x[0-9a-f]+", (re.search(r"\.mouse_slots = \{(.*?)\}", blk) or
                            re.match("()", "")).group(1))],
            "mouse_default": num(blk, "mouse_default") if ".mouse_default" in blk else None,
            "dummy_page_offset": num(blk, "dummy_page_offset") if ".dummy_page_offset" in blk else None,
            **{k: num(blk, k) if "." + k + " =" in blk else None for k in ("page_holder_field", "page_holder_get",
                                                                         "page_vtable", "page_dtor", "wrapper_vtable")},
        })
    return out


class Elf:
    def __init__(self, path):
        self.f = open(path, "rb")
        h = self.f.read(64)
        if h[:4] != b"\x7fELF" or h[4] != 2 or struct.unpack_from("<H", h, 18)[0] != 183:
            raise SystemExit("not an aarch64 ELF64 file")
        phoff, = struct.unpack_from("<Q", h, 32)
        phentsize, phnum = struct.unpack_from("<HH", h, 54)
        self.ph = []
        for i in range(phnum):
            self.f.seek(phoff + i * phentsize)
            t, fl, off, va, _pa, fsz, msz, _al = struct.unpack("<IIQQQQQQ", self.f.read(56))
            self.ph.append((t, fl, off, va, fsz, msz))

    def read(self, va, n):
        for t, _fl, off, v, fsz, _msz in self.ph:
            if t == 1 and v <= va and va + n <= v + fsz:
                self.f.seek(off + va - v)
                return self.f.read(n)
        raise ValueError("address %#x not in a file-backed segment" % va)

    def exec_has(self, va, n):
        return any(t == 1 and fl & 1 and v <= va and va + n <= v + msz for t, fl, _o, v, _f, msz in self.ph)

    def relative_relocs(self):
        """{offset: addend} of the R_AARCH64_RELATIVE relocations (through PT_DYNAMIC)."""
        dyn = next((ph for ph in self.ph if ph[0] == 2), None)
        if not dyn:
            return {}
        self.f.seek(dyn[2])
        d = self.f.read(dyn[4])
        tags = {}
        for i in range(0, len(d), 16):
            t, v = struct.unpack_from("<qQ", d, i)
            if t == 0:
                break
            tags[t] = v
        rela, sz = tags.get(7), tags.get(8)
        if not rela or not sz:
            return {}
        data = self.read(rela, sz)
        out = {}
        for i in range(0, sz, 24):
            off, info, add = struct.unpack_from("<QQq", data, i)
            if info & 0xffffffff == 1027:
                out[off] = add
        return out

    def notes(self):
        for t, _fl, off, _v, fsz, _msz in self.ph:
            if t != 4:
                continue
            self.f.seek(off)
            d = self.f.read(fsz)
            p = 0
            while p + 12 <= len(d):
                nsz, dsz, typ = struct.unpack_from("<III", d, p)
                name = d[p + 12:p + 12 + nsz].rstrip(b"\0")
                q = p + 12 + ((nsz + 3) & ~3)
                yield name, typ, d[q:q + dsz]
                p = q + ((dsz + 3) & ~3)


def register_calls(e, target):
    """ModeIDs passed in w1 (mov w1, #imm within 5 instructions) by every bl to target."""
    ids = []
    for t, fl, off, va, fsz, _msz in e.ph:
        if t != 1 or not fl & 1:
            continue
        e.f.seek(off)
        w = struct.unpack("<%dI" % (fsz // 4), e.f.read(fsz - fsz % 4))
        for i, x in enumerate(w):
            if x & 0xfc000000 != 0x94000000:
                continue
            d = x & 0x3ffffff
            d = d - (1 << 26) if d & (1 << 25) else d
            if va + 4 * i + 4 * d != target:
                continue
            v = None
            for j in range(1, 6):
                if w[i - j] & 0xffe0001f == 0x52800001:
                    v = (w[i - j] >> 5) & 0xffff
                    break
            ids.append(v)
    return ids


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    e = Elf(sys.argv[1])
    notes = list(e.notes())
    bid = next((d for n, t, d in notes if n == b"GNU" and t == 3), None)
    ver = next((d.rstrip(b"\0").decode(errors="replace") for n, _t, d in notes if n == b"MPC"), "?")
    print("binary: version note %s, build-id %s" % (ver, bid.hex() if bid else "none"))
    match = [p for p in profiles() if p["build_id"] == bid]
    if not match:
        print("FAIL no profile for this build-id (the shim would refuse it)")
        return 1
    p = match[0]
    bad = 0

    def check(ok, what):
        nonlocal bad
        print(("ok   " if ok else "FAIL ") + what)
        bad += not ok

    for name, st in p["sites"].items():
        if not st:
            continue
        addr, want = st
        words = list(struct.unpack("<4I", e.read(addr, 16)))
        check(words == want, "%s %#x entry = %s" % (name, addr, " ".join("%08x" % w for w in words)))
        check(e.exec_has(addr, 16), "%s is in an executable segment" % name)
        bad_pc = [w for w in want if w & 0x9f000000 != 0x90000000 and (
            w & 0x1f000000 == 0x10000000 or w & 0x7c000000 == 0x14000000 or w & 0x7e000000 in (0x34000000, 0x36000000)
            or w & 0xff000010 == 0x54000000 or w & 0x3b000000 == 0x18000000)]
        check(not bad_pc, "%s entry has no PC-relative instruction other than adrp" % name)
    p["get_mode_name"] = p["sites"]["get_mode_name"][0]
    t = p["tables"]
    for kind in ("name", "page"):
        ent = struct.unpack("<%dh" % t["table_len"], e.read(t[kind + "_table"], 2 * t["table_len"]))
        tgt = lambda i: t[kind + "_table_base"] + 4 * ent[i - 0x2080]
        free = [c for c in p["candidates"] if tgt(c) == t[kind + "_default"]]
        check(free == p["candidates"], "%s table %#x: candidates %s go to the default case %#x (free: %s)" % (
            kind, t[kind + "_table"], " ".join("%#x" % c for c in p["candidates"]), t[kind + "_default"],
            " ".join("%#x" % c for c in free)))
        check(tgt(0x2091) != t[kind + "_default"], "%s table: 0x2091 (Browser) is not the default case" % kind)
    p["slot_id"] = p["candidates"][0]
    first, = struct.unpack("<I", e.read(p["string_ctor"], 4))
    check(first == p["string_ctor_first"], "String ctor %#x first instruction %08x" % (p["string_ctor"], first))

    # Find the 0x2080 switch in getModeName: mov w2,#-0x2080 ... adrp/add table ... adr base.
    code = e.read(p["get_mode_name"], 0x100)
    ins = struct.unpack("<%dI" % (len(code) // 4), code)
    tab = base = lim = None
    for i, w in enumerate(ins):
        if w & 0x9f000000 == 0x90000000 and i + 1 < len(ins) and ins[i + 1] & 0xffc00000 == 0x91000000:
            pc = p["get_mode_name"] + 4 * i
            immlo, immhi = (w >> 29) & 3, (w >> 5) & 0x7ffff
            imm = ((immhi << 2) | immlo) << 12
            if imm & (1 << 32):
                imm -= 1 << 33
            t = (pc & ~0xfff) + imm + ((ins[i + 1] >> 10) & 0xfff)
            # the cmp before it gives the limit; the adr after the ldrh gives the base
            if i >= 2 and ins[i - 2] & 0xff80001f == 0x7100001f:
                lim = (ins[i - 2] >> 10) & 0xfff
                a = ins[i + 3]
                if a & 0x9f000000 == 0x10000000:
                    off = (((a >> 5) & 0x7ffff) << 2) | ((a >> 29) & 3)
                    if off & (1 << 20):
                        off -= 1 << 21
                    if (p["slot_id"] - 0x2080) <= lim and lim < 0x40:
                        tab, base = t, p["get_mode_name"] + 4 * (i + 3) + off
    check(tab is not None, "found the 0x2080 jump table" + (" at %#x (base %#x, limit %#x)" % (tab, base, lim) if tab else ""))
    if tab:
        ent = struct.unpack("<%dh" % (lim + 1), e.read(tab, 2 * (lim + 1)))
        target = lambda id_: base + 4 * ent[id_ - 0x2080]
        default = target(0x2092)
        check(target(p["slot_id"]) == default, "slot %#x goes to the default case %#x (like 0x2092)" % (p["slot_id"], default))
        check(target(0x2091) != default, "0x2091 (Browser) does not")
    if p["sites"]["register_mode"]:
        ids = register_calls(e, p["sites"]["register_mode"][0])
        check(ids.count(p["alias_from"]) == 1, "%#x is registered once (%d bl callers, %d with a known id)" % (
            p["alias_from"], len(ids), sum(i is not None for i in ids)))
        check(p["slot_id"] not in ids, "%#x is not registered by MPC itself" % p["slot_id"])
    for name, (addr, first) in p["fns"].items():
        w, = struct.unpack("<I", e.read(addr, 4))
        check(w == first and e.exec_has(addr, 4), "%s %#x first instruction %08x" % (name, addr, w))
    if p["dummy_page_offset"] is not None:
        # the page table's default case picks the placeholder page: [bti j;] mov xN, #off; add x21, x19, xN
        ins = struct.unpack("<4I", e.read(p["tables"]["page_default"], 16))
        off = None
        for i in range(3):
            m, a = ins[i], ins[i + 1]
            if m & 0xffe00000 == 0xd2800000 and a & 0xffe0ffff == 0x8b000275 and (a >> 16) & 31 == m & 31:
                off = (m >> 5) & 0xffff
        check(off == p["dummy_page_offset"], "page default %#x picks the placeholder page at this + %s (profile %#x)" % (
            p["tables"]["page_default"], "%#x" % off if off is not None else "?", p["dummy_page_offset"]))
    if p["page_holder_get"] is not None:
        # the holder's page getter: [bti c;] ldr x0, [x0, #field]
        ins = struct.unpack("<2I", e.read(p["page_holder_get"], 8))
        want = 0xf9400000 | ((p["page_holder_field"] // 8) << 10)
        check(want in ins, "page holder getter %#x loads [x0, #%#x] (%08x %08x)" % (
            p["page_holder_get"], p["page_holder_field"], ins[0], ins[1]))
    if p["page_dtor"] is not None:
        # the placeholder page's destructor resets page_vtable at [x19] and the Component vtable (the mouse slots'
        # vtable) at [x19, #page_component_offset]: adrp/add pairs giving both addresses near its start
        ins = struct.unpack("<48I", e.read(p["page_dtor"], 4 * 48))
        made = set()
        for i, x in enumerate(ins):
            if x & 0x9f000000 == 0x90000000:
                imm = ((((x >> 5) & 0x7ffff) << 2) | ((x >> 29) & 3)) << 12
                page = ((p["page_dtor"] + 4 * i) & ~0xfff) + imm
                for y in ins[i + 1:i + 9]:
                    if y & 0xffc00000 == 0x91000000 and (y >> 5) & 31 == x & 31:
                        made.add(page + ((y >> 10) & 0xfff))
                        break
        comp_vt = p["mouse_slots"][0] - 0x28 if p["mouse_slots"] else None
        check(p["page_vtable"] in made and comp_vt in made, "page destructor %#x sets vtables %#x and %s" % (
            p["page_dtor"], p["page_vtable"], "%#x" % comp_vt if comp_vt else "?"))
    if p["wrapper_vtable"] is not None:
        # a vtable: its first slots are relocated pointers into executable code
        rel = e.relative_relocs()
        slots = [rel.get(p["wrapper_vtable"] + 8 * k) for k in range(4)]
        check(all(v is not None and e.exec_has(v, 4) for v in slots), "wrapper vtable %#x: slots %s" % (
            p["wrapper_vtable"], " ".join("%#x" % v if v else "?" for v in slots)))
    if p["mouse_slots"]:
        rel = e.relative_relocs()
        vals = [rel.get(a) for a in p["mouse_slots"]]
        check(all(v == p["mouse_default"] for v in vals), "mouse slots %s hold %s (JUCE's empty callback %#x)" % (
            " ".join("%#x" % a for a in p["mouse_slots"]), " ".join("%#x" % v if v is not None else "?" for v in vals),
            p["mouse_default"]))
    if p["placeholder_ptr_var"]:
        ptr, = struct.unpack("<Q", e.read(p["placeholder_ptr_var"], 8))
        png = e.read(ptr, p["placeholder_size"])
        w, h = struct.unpack(">II", png[16:24])
        check(png[:8] == b"\x89PNG\r\n\x1a\n" and png[-8:-4] == b"IEND",
              "placeholder: *%#x = %#x, a whole PNG of %d bytes, %dx%d" % (p["placeholder_ptr_var"], ptr, len(png), w, h))
    check(not any(n == b"GNU" and t == 5 for n, t, _d in notes), "no GNU property note (no BTI-guarded pages)")
    print("%s: %s" % (p["label"], "profile matches" if not bad else "%d checks failed" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
