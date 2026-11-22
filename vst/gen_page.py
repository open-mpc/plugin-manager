#!/usr/bin/env python3
"""Compile one tab of the Manager's built skin into the page table the daemon draws the mode-slot page from.

The modeslot addin (mpc-vst-plugins tools/mpc_patch/modeslot) turns an unused MPC mode into a page an outside
process draws. The daemon draws the Manager there (src/page.c) the way MPC draws the skin: the same components,
images and live text, from this table instead of the 11 MB TUI.json.

  gen_page.py <skin dir> <params.json> [-o <skin dir>/openmpc_page.txt] [--tab 0]

Output, one item per line, space-separated, strings last (they may hold spaces):
  S <w> <h>                                     page size (the tab's initialSize)
  P <index> <kind> <key> [<opt>|<opt>|...]      a parameter the page uses; kind m (momentary), o (options),
                                                t (text from the engine), n (number)
  C <x> <y> <w> <h> <param|-1> <tap 0|1>        a component (absolute bounds); its items follow:
  W <param> <option>                              shown only while that parameter is at that option (all must hold)
  I <x> <y> <w> <h> <image>                       an image (bounds relative to the component)
  B <x> <y> <w> <h> <buttonId> <inGroup> <off> <on>   a button: off image, or on image while its group's option is
                                                  buttonId (inGroup > 1); "-" for none
  L <x> <y> <w> <h> <height> <aarrggbb> <just> <style>  the parameter's value text (just: left|right|centred)
  I lines before any C are free-standing images, absolute.
Standard library only."""
import argparse
import json
import os
import re
import sys


def comp_param(c):
    for m in c.get("handle remapping", {}).get("map", []):
        mm = re.match(r"Parameter (\d+)$", str(m.get("value", "")))
        if m.get("key") == "Data" and mm:
            return int(mm.group(1))
    return -1


def conditions(c):
    out = []
    for h in c["bounds"].get("additionalInvalidatingHandles", []):
        m = re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)$", h)
        if m:
            out.append((int(m.group(3)), int(m.group(1))))
    return out


def xywh(b):
    return [int(round(float(v))) for v in b["bounds"].split()]


def name(s):
    return s if s else "-"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("skin")
    ap.add_argument("params")
    ap.add_argument("-o")
    ap.add_argument("--tab", type=int, default=0)
    a = ap.parse_args()
    t = json.load(open(os.path.join(a.skin, "TUI.json")))["pageData"]
    defs = {d["key"]: d["value"] for d in t["componentDefinitions"]["localComponentDefinitions"]}
    tab = t["tabs"][a.tab]
    page = tab["componentDefinition"] if "componentDefinition" in tab else defs[tab["componentName"]]
    plist = json.load(open(a.params))["params"]

    lines, used = [], set()
    w, h = [int(round(float(v))) for v in tab["initialSize"].split()[2:4]]
    lines.append("S %d %d" % (w, h))
    for c in page["componentsData"]:
        cd = c["componentData"]
        x, y, cw, ch = xywh(c["bounds"])
        conds = conditions(c)
        for p, _ in conds:
            used.add(p)
        if cd["type"] == "Image":
            if conds:   # a conditional free-standing image: a component of its own
                lines.append("C %d %d %d %d -1 0" % (x, y, cw, ch))
                lines += ["W %d %d" % pc for pc in conds]
                lines.append("I 0 0 %d %d %s" % (cw, ch, cd["data"]["image"]))
            else:
                lines.append("I %d %d %d %d %s" % (x, y, cw, ch, cd["data"]["image"]))
            continue
        d = defs[cd["type"]]
        p = comp_param(c)
        tap = int(any(act.get("handler") == "Toggle Switch" for act in d.get("actions", [])) or
                  any(s["componentData"]["type"] == "Button" for s in d["componentsData"]))
        if p >= 0:
            used.add(p)
        items = []
        for s in d["componentsData"]:
            sd = s["componentData"]
            sx, sy, sw, sh = xywh(s["bounds"])
            dd = sd["data"]
            if sd["type"] == "Image" and dd.get("image"):
                items.append("I %d %d %d %d %s" % (sx, sy, sw, sh, dd["image"]))
            elif sd["type"] == "Button":
                items.append("B %d %d %d %d %d %d %s %s" % (sx, sy, sw, sh, dd.get("buttonId", 0), dd.get("numButtonsInGroup", 1),
                                                         name(dd.get("offImage")), name(dd.get("onImage"))))
            elif sd["type"] == "Label" and dd.get("type") == "Value":
                ts = dd["textStyle"]
                just = ts.get("justification", "")
                j = "left" if "left" in just else "right" if "right" in just else "centred"
                items.append("L %d %d %d %d %g %s %s %s" % (sx, sy, sw, sh, ts["font"]["height"], ts["colour"], j,
                                                           ts["font"].get("style", "SemiBold")))
            elif sd["type"] in ("Knob", "Meter"):
                print("note: %s in %s is not drawn" % (sd["type"], cd["type"]), file=sys.stderr)
        if not items and not tap:
            continue
        lines.append("C %d %d %d %d %d %d" % (x, y, cw, ch, p, tap))
        lines += ["W %d %d" % pc for pc in conds]
        lines += items
    plines = []
    for i in sorted(used):
        if i >= len(plist):
            sys.exit("parameter %d is past params.json (%d)" % (i, len(plist)))
        q = plist[i]
        if q.get("momentary"):
            kind, opts = "m", ""
        elif q.get("options"):
            kind, opts = "o", "|".join(str(o) for o in q["options"])
        elif q.get("display") == "string":
            kind, opts = "t", ""
        else:
            kind, opts = "n", ""
        if " " in q["key"] or "|" in q["key"]:
            sys.exit("parameter key %r has a space or |" % q["key"])
        plines.append(("P %d %s %s %s" % (i, kind, q["key"], opts)).rstrip())
    out = a.o or os.path.join(a.skin, "openmpc_page.txt")
    open(out, "w").write("\n".join(lines[:1] + plines + lines[1:]) + "\n")
    print("%s: %d components, %d parameters" % (out, sum(1 for l in lines if l.startswith("C ")), len(plines)))


if __name__ == "__main__":
    main()
