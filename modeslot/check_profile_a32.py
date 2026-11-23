"""The 32-bit ARM half of check_profile.py: Gen1 MPC and Force (MPC OS 3.10.1). Read only.

Beyond the entry instructions and jump tables (as for Gen2) it uses the binary's RTTI: the TouchDummyPage vtable must
hold paint in slot 8 and have its Component sub-vtable (the mouse slots') at the offset-to-top the profile says; the
wrapper vtable must be TouchPageContainer's Component sub-vtable. The profile's Component layout constants are checked
against the code of setBounds, repaint and getFromMemory. See ../docs (plugin-manager DESIGN.md) for how the sites were found.
"""
import mmap
import struct
import sys


class Elf32:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        h = self.m
        phoff, = struct.unpack_from("<I", h, 28)
        phentsize, phnum = struct.unpack_from("<HH", h, 42)
        self.ph = []
        for i in range(phnum):
            t, off, va, _pa, fsz, msz, fl, _al = struct.unpack_from("<8I", h, phoff + i * phentsize)
            self.ph.append((t, fl, off, va, fsz, msz))

    def read(self, va, n):
        for t, _fl, off, v, fsz, _msz in self.ph:
            if t == 1 and v <= va and va + n <= v + fsz:
                return self.m[off + va - v:off + va - v + n]
        raise ValueError("address %#x not in a file-backed segment" % va)

    def u32(self, va):
        return struct.unpack("<I", self.read(va, 4))[0]

    def exec_has(self, va, n):
        return any(t == 1 and fl & 1 and v <= va and va + n <= v + msz for t, fl, _o, v, _f, msz in self.ph)

    def text(self):
        """(va, words) of the executable segment holding most code."""
        t, fl, off, va, fsz, msz = max((ph for ph in self.ph if ph[0] == 1 and ph[1] & 1), key=lambda ph: ph[4])
        return va, struct.unpack("<%dI" % (fsz // 4), self.m[off:off + fsz - fsz % 4])

    def relative(self):
        """{address: value}: R_ARM_RELATIVE (23) relocations, whose addend is the word in the file."""
        dyn = next((ph for ph in self.ph if ph[0] == 2), None)
        if not dyn:
            return {}
        d = self.m[dyn[2]:dyn[2] + dyn[4]]
        tags = {}
        for i in range(0, len(d), 8):
            t, v = struct.unpack_from("<iI", d, i)
            if t == 0:
                break
            tags[t] = v
        rel, sz = tags.get(17), tags.get(18)
        out = {}
        if rel and sz:
            data = self.read(rel, sz)
            for i in range(0, sz, 8):
                off, info = struct.unpack_from("<II", data, i)
                if info & 0xff == 23:
                    out[off] = self.u32(off)
        return out

    def notes(self):
        for t, _fl, off, _v, fsz, _msz in self.ph:
            if t != 4:
                continue
            d = self.m[off:off + fsz]
            p = 0
            while p + 12 <= len(d):
                nsz, dsz, typ = struct.unpack_from("<III", d, p)
                name = d[p + 12:p + 12 + nsz].rstrip(b"\0")
                q = p + 12 + ((nsz + 3) & ~3)
                yield name, typ, d[q:q + dsz]
                p = q + ((dsz + 3) & ~3)

    def find_all(self, needle):
        out, i = [], self.m.find(needle)
        while i >= 0:
            out.append(i)
            i = self.m.find(needle, i + 1)
        return out

    def off2va(self, off):
        for t, _fl, o, v, fsz, _msz in self.ph:
            if t == 1 and o <= off < o + fsz:
                return v + off - o
        return None


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def rot_imm(w):
    r = ((w >> 8) & 15) * 2
    v = w & 0xff
    return ((v >> r) | (v << (32 - r))) & 0xffffffff


def displaceable(w):
    """The same rule as arch_a32.c: a literal load or an instruction without pc (and no branch)."""
    if w & 0x0f7f0000 == 0x051f0000:
        return (w >> 12) & 15 != 15
    cls = (w >> 25) & 7
    if cls == 5:
        return False
    if w & 0x0fb00000 == 0x03000000:   # movw / movt
        return (w >> 12) & 15 != 15
    if cls <= 3:
        if (w >> 16) & 15 == 15 or (w >> 12) & 15 == 15:
            return False
        if cls in (0, 3) and w & 15 == 15:
            return False
    return True


def group_for(e, name, off_to_top):
    """The vtable (address of its first slot) of the class named `name` (length-prefixed RTTI name) whose group has the
    given offset-to-top, via its typeinfo."""
    found = []
    for o in e.find_all(name + b"\0"):
        na = e.off2va(o)
        if na is None:
            continue
        for p in e.find_all(struct.pack("<I", na)):
            pa = e.off2va(p)
            if pa is None:
                continue
            for q in e.find_all(struct.pack("<I", pa - 4)):
                qa = e.off2va(q)
                if qa is not None and struct.unpack("<i", e.read(qa - 4, 4))[0] == off_to_top:
                    found.append(qa + 4)
    return sorted(set(found))


def main(path, profiles):
    e = Elf32(path)
    notes = list(e.notes())
    bid = next((d for n, t, d in notes if n == b"GNU" and t == 3), None)
    ver = next((d.rstrip(b"\0").decode(errors="replace") for n, _t, d in notes if n == b"MPC"), "?")
    print("binary: version note %s, build-id %s" % (ver, bid.hex() if bid else "none"))
    match = [p for p in profiles if p["build_id"] == bid]
    if not match:
        print("FAIL no profile for this build-id (the shim would refuse it)")
        return 1
    p = match[0]
    bad = 0

    def check(ok, what):
        nonlocal bad
        print(("ok   " if ok else "FAIL ") + what)
        bad += not ok

    lay = p["layout"]
    for name, st in p["sites"].items():
        if not st:
            continue
        addr, want = st
        words = list(struct.unpack("<2I", e.read(addr, 8)))
        check(words == want[:2], "%s %#x entry = %s" % (name, addr, " ".join("%08x" % w for w in words)))
        check(e.exec_has(addr, 8) and addr % 4 == 0, "%s is in an executable segment, ARM state" % name)
        check(all(displaceable(w) for w in want[:2]), "%s entry can be displaced (no PC use but literal loads)" % name)
    for name, (addr, first) in p["fns"].items():
        w = e.u32(addr)
        check(w == first and e.exec_has(addr, 4), "%s %#x first instruction %08x" % (name, addr, w))
    w = e.u32(p["string_ctor"])
    check(w == p["string_ctor_first"], "String ctor %#x first instruction %08x" % (p["string_ctor"], w))

    # the switches: addls pc, pc, rN, lsl #2 right before `b default`, then one `b` per id from 0x2080
    t = p["tables"]
    def branch(at):
        w = e.u32(at)
        return at + 8 + sext(w & 0xffffff, 24) * 4 if w >> 24 == 0xea else None
    for kind in ("name", "page"):
        tab, dflt = t[kind + "_table"], t[kind + "_default"]
        sw = tab - 8
        check(e.u32(sw) & 0xfffffff0 == 0x908ff100 and e.u32(sw - 4) & 0xfff00000 == 0xe3500000 and
              e.u32(sw - 4) & 0xfff == t["table_len"] - 1,
              "%s switch at %#x: cmp #%#x; addls pc, pc, rN, lsl #2" % (kind, sw, t["table_len"] - 1))
        check(branch(tab - 4) == dflt, "%s table: the branch for ids past the table goes to the default case %#x" % (kind, dflt))
        free = [c for c in p["candidates"] if branch(tab + 4 * (c - 0x2080)) == dflt]
        check(free == p["candidates"], "%s table %#x: candidates %s go to the default case (free: %s)" % (
            kind, tab, " ".join("%#x" % c for c in p["candidates"]), " ".join("%#x" % c for c in free)))
        check(branch(tab + 4 * (0x2091 - 0x2080)) != dflt, "%s table: 0x2091 (Browser) is not the default case" % kind)
    slot = p["candidates"][0]

    # registerMode: every bl to it (directly or through a long-branch veneer) with the id in r1
    va, w = e.text()
    target = p["sites"]["register_mode"][0]
    veneers = set()
    for i in range(len(w) - 2):
        if w[i] == 0xe59fc000 and w[i + 1] == 0xe08ff00c and (va + 4 * i + 12 + sext(w[i + 2], 32)) & 0xffffffff == target:
            veneers.add(va + 4 * i)
    ids = []
    for i, x in enumerate(w):
        if x >> 24 != 0xeb:
            continue
        if (va + 4 * i + 8 + sext(x & 0xffffff, 24) * 4) & 0xffffffff not in veneers | {target}:
            continue
        v = None
        for j in range(1, 9):
            y = w[i - j]
            if y & 0xfff0f000 == 0xe3001000:                 # movw r1, #imm16
                v = ((y >> 4) & 0xf000) | (y & 0xfff)
                break
            if y & 0xfffff000 == 0xe3a01000:                 # mov r1, #imm
                v = rot_imm(y)
                break
        ids.append(v)
    check(ids.count(p["alias_from"]) == 1, "%#x is registered once (%d callers, %d with a known id)" % (
        p["alias_from"], len(ids), sum(i is not None for i in ids)))
    check(not any(c in ids for c in p["candidates"]), "no candidate slot is registered by MPC itself")

    # the page: RTTI
    rel = e.relative()
    prim = group_for(e, b"14TouchDummyPage", 0)
    check(prim == [p["page_vtable"]], "TouchDummyPage's vtable is %s (profile %#x)" % ([hex(x) for x in prim], p["page_vtable"]))
    check(e.u32(p["page_vtable"] + 4 * 8) == p["sites"]["page_paint"][0], "paint is slot 8 of that vtable")
    cvt = p["mouse_slots"][0] - 5 * 4
    comp = group_for(e, b"14TouchDummyPage", -lay["page_component_offset"])
    check(comp == [cvt], "its Component sub-vtable (offset-to-top -%d) is %s; the mouse slots are its entries 5..7" % (
        lay["page_component_offset"], [hex(x) for x in comp]))
    vals = [e.u32(a) for a in p["mouse_slots"]]
    check(all(v == p["mouse_default"] for v in vals) and all(a in rel for a in p["mouse_slots"]) and
          all(e.u32(cvt + 4 * k) == p["mouse_default"] for k in range(2, 9)),
          "mouse slots %s hold %s (JUCE's empty callback %#x; entries 2..8 all are)" % (
              " ".join("%#x" % a for a in p["mouse_slots"]), " ".join("%#x" % v for v in vals), p["mouse_default"]))
    check(e.u32(p["mouse_default"]) == 0xe12fff1e, "that callback is `bx lr`")
    wrap = group_for(e, b"18TouchPageContainer", -16)
    check(wrap == [p["wrapper_vtable"]] and e.u32(p["wrapper_vtable"] + 8) == p["mouse_default"] and
          e.exec_has(e.u32(p["wrapper_vtable"]), 4) and e.exec_has(e.u32(p["wrapper_vtable"] + 4), 4),
          "wrapper vtable %#x is TouchPageContainer's Component sub-vtable (%s)" % (p["wrapper_vtable"], [hex(x) for x in wrap]))

    # the placeholder picture
    ptr = e.u32(p["placeholder_ptr_var"])
    png = e.read(ptr, p["placeholder_size"])
    pw, ph = struct.unpack(">II", png[16:24])
    check(png[:8] == b"\x89PNG\r\n\x1a\n" and png[-8:-4] == b"IEND" and p["placeholder_ptr_var"] in rel,
          "placeholder: *%#x = %#x, a whole PNG of %d bytes, %dx%d" % (p["placeholder_ptr_var"], ptr, len(png), pw, ph))

    # the touch UI: where the placeholder page's holder is, and where it keeps the page
    getter = e.u32(p["page_holder_get"])
    reg = (getter >> 16) & 15
    check(getter & 0x0f700000 == 0x05100000 and getter & 0xfff == p["page_holder_field"] and (getter >> 12) & 15 == 0,
          "the holder's page load at %#x is ldr r0, [r%d, #%#x] (%08x)" % (p["page_holder_get"], reg, p["page_holder_field"], getter))
    base = {4: 0}
    for i in range(6):
        x = e.u32(t["page_default"] + 4 * i)
        if x & 0x0fe00000 == 0x02800000 and (x >> 16) & 15 in base:
            base[(x >> 12) & 15] = base[(x >> 16) & 15] + rot_imm(x)
    check(base.get(reg) == p["dummy_page_offset"], "the page switch's default case puts r%d at this + %s (profile %#x)" % (
        reg, "%#x" % base[reg] if reg in base else "?", p["dummy_page_offset"]))

    # the Component layout against the code that uses it
    def code(addr, n):
        return struct.unpack("<%dI" % n, e.read(addr, 4 * n))
    def ldr_off(words, off):
        return any(x & 0x0f100000 == 0x05100000 and x & 0xfff == off and (x >> 16) & 15 != 15 for x in words)
    sb = code(p["fns"]["set_bounds"][0], 150)
    check(all(ldr_off(sb, lay["comp_bounds"] + k) for k in (0, 4, 8, 12)),
          "setBounds reads the bounds at +%#x..+%#x" % (lay["comp_bounds"], lay["comp_bounds"] + 12))
    rp = code(p["fns"]["repaint"][0], 14)
    check(ldr_off(rp, lay["comp_parent"]), "repaint() reads the parent at +%#x" % lay["comp_parent"])
    gm = code(p["sites"]["image_from_memory"][0], 90)
    rc = any(x & 0x0fe00000 == 0x02800000 and rot_imm(x) == lay["img_refcount"]
             and any(y & 0x0ff00fff == 0x01900f9f for y in gm[i + 1:i + 5]) for i, x in enumerate(gm))   # add rX, rY, #off; ... ldrex
    check(rc, "getFromMemory adds %d to an image pointer, then ldrex: the reference count's offset" % lay["img_refcount"])
    check(lay["decode_vslot"] == 0x14, "decodeImage is vtable slot 5 (+0x14), as getFromMemory calls it")
    check(not any(n == b"GNU" and t == 5 for n, t, _d in notes), "no GNU property note")
    print("%s: %s" % (p["label"], "profile matches" if not bad else "%d checks failed" % bad))
    return 1 if bad else 0
