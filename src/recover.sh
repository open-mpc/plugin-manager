#!/bin/sh
# Finish or undo an apply that was cut off (MPC switched off, power cut): run by the Plugin Manager at start and by apply.sh
# before it begins. Regenerate src/recover_sh.h with vst/gen_recover.py after editing.
#   sh recover.sh JOURNAL RESULT
# JOURNAL is written by apply.sh, one tab-separated line per event:
#   pid <pid of apply.sh>          first line
#   begin <id> <folder> <act>      a step starts (<id> "theme": <folder> is the Manager's own folder, its skin is being swapped)
#   end <id>                       that step finished, whatever its outcome (apply.sh already rolled it back if it failed)
# For every step begun and not ended, only renames and deletes of the Manager's and the installers' own leftovers are done,
# so this is safe while MPC runs. A folder that is in place is kept as it is (old or new version); a missing one is put back
# from the newest complete copy there is. One line per step goes to RESULT ("id|fail|interrupted: ..."), shown at next start.
# Nothing is done while the apply.sh that wrote the journal still runs (APPLY_PID: the apply.sh running this, never that one).
J=$1; RES=$2
[ -f "$J" ] || exit 0
TAB=$(printf '\t')
pid=$(sed -n "s/^pid$TAB\\([0-9][0-9]*\\)\$/\\1/p" "$J" | head -n 1)
if [ -n "$pid" ] && [ "$pid" != "${APPLY_PID:-}" ] && [ -r "/proc/$pid/cmdline" ] && tr '\0' ' ' < "/proc/$pid/cmdline" | grep -q 'apply\.sh'; then
    echo "recover: apply.sh ($pid) is still running, nothing done"; exit 0
fi
ver() { sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$1/mpc-plugin.json" 2>/dev/null | head -n 1; }
open=$(awk -F"$TAB" '$1 == "begin" { b[$2] = $0; o[++n] = $2 } $1 == "end" { delete b[$2] } END { for (i = 1; i <= n; i++) if (o[i] in b) { print b[o[i]]; delete b[o[i]] } }' "$J")
printf '%s\n' "$open" | while IFS="$TAB" read -r tag id dir act; do
    [ "$tag" = begin ] && [ -n "$id" ] || continue
    if [ "$id" = theme ]; then   # the skin swap: "Plugin Skins" -> .skin.old, .skin.new -> "Plugin Skins"
        T=$dir; what="the skin was left as it was"
        if [ ! -d "$T/Plugin Skins" ] && [ -d "$T/.skin.old" ]; then mv "$T/.skin.old" "$T/Plugin Skins" && what="the skin was put back"; fi
        [ -d "$T/Plugin Skins" ] && rm -rf "$T/.skin.new" "$T/.skin.old"
        echo "recover: theme: $what"
        continue
    fi
    [ -n "$dir" ] || { echo "$id|fail|interrupted: nothing was changed" >> "$RES"; continue; }
    p=${dir%/*}; b=${dir##*/}
    snap="$p/.pm-snap-$id"; old="$p/.$b.old"; bad="$p/.pm-failed-$id"
    what=""
    if [ ! -d "$dir" ]; then   # the folder is gone: the newest complete copy goes back (a safety copy, the installer's old folder, the failed one)
        if [ -d "$snap" ] && [ -f "$snap.done" ]; then mv "$snap" "$dir" && rm -f "$snap.done" && what="put back from the safety copy"
        elif [ -d "$old" ]; then mv "$old" "$dir" && what="put back as it was"
        elif [ -d "$bad" ]; then mv "$bad" "$dir" && what="put back (the files of the cut-off update)"
        fi
    fi
    if [ -d "$dir" ]; then
        rm -rf "$p/.$b.new" "$old" "$bad"
        [ -e "$snap" ] && rm -rf "$snap" "$snap.done"
        v=$(ver "$dir")
        [ -n "$what" ] || what="its folder is in place"
        echo "$id|fail|interrupted (was the MPC switched off?): $what${v:+, now $v}" >> "$RES"
    elif [ "$act" = remove ]; then
        rm -rf "$p/.$b.new"
        echo "$id|ok|remove" >> "$RES"
    else
        rm -rf "$p/.$b.new"
        if [ -e "$snap" ]; then echo "$id|fail|interrupted: its folder is missing; an incomplete copy is at $snap" >> "$RES"
        else echo "$id|fail|interrupted: its folder is missing, install it again" >> "$RES"; fi
    fi
    echo "recover: $id: ${what:-folder missing}"
done
rm -f "$J"
exit 0
