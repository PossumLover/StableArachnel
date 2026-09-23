#!/usr/bin/env bash
# Launch a game from its replay script, watch it, collect its logs, and stop it.
#
#   scripts/dev/launch-probe.sh <gameId> [--seconds N] [--headless] [--keep]
#
# Needs launch-<gameId>.replay.sh, which Arachnel writes on every real launch of
# that game (launch it once from the app). The replay re-runs the exact program,
# arguments and environment Arachnel spawned - without Arachnel rewriting
# steam_appid.txt, re-healing the fix layout, or disabling Online Fix after a
# quick exit, all of which change the thing being tested between runs.
#
#   --seconds N   how long to let it run before stopping it (default 60)
#   --headless    run inside `gamescope --backend headless` so nothing appears on
#                 screen; the game still gets the GPU
#   --keep        do not stop the game at the end
#   --winedebug CHANNELS
#                 run a copy of the replay with WINEDEBUG=CHANNELS instead of
#                 Arachnel's -all. `+msgbox` puts the text of Windows message
#                 boxes in the output - the only way to read a dialog headless.
#
# Every process of the run is tagged with ARACHNEL_PROBE=<token> in its
# environment and stopped by that tag, never by name - Arachnel itself and any
# other game are never touched.
set -uo pipefail

DATA="${ARACHNEL_DATA:-$HOME/.local/share/Arachnel/Arachnel}"
id="" seconds=60 headless=0 keep=0 winedebug=""
while [ $# -gt 0 ]; do
    case "$1" in
        --seconds) seconds="$2"; shift 2 ;;
        --headless) headless=1; shift ;;
        --keep) keep=1; shift ;;
        --winedebug) winedebug="$2"; shift 2 ;;
        -h|--help) sed -n '2,/^set -uo/{/^#/s/^# \{0,1\}//p}' "$0"; exit 0 ;;
        *) id="$1"; shift ;;
    esac
done
[ -n "$id" ] || { echo "usage: $0 <gameId> [--seconds N] [--headless] [--keep] [--winedebug CHANNELS]" >&2; exit 2; }

replay="$DATA/launch-$id.replay.sh"
[ -f "$replay" ] || { echo "no $replay - launch $id once from Arachnel first" >&2; exit 1; }

# Pull what we need out of the replay itself.
compat=$(grep -oE "STEAM_COMPAT_DATA_PATH=[^']*" "$replay" | head -1 | cut -d= -f2-)
workdir=$(sed -n "s/^cd '\(.*\)' || exit 1$/\1/p" "$replay" | head -1)

# pids whose environment carries a given NAME=value. One grep over every environ
# (entries are NUL-separated, hence -z); unreadable or vanished pids are skipped.
pids_with_env() {
    grep -lzxF -- "$1" /proc/[0-9]*/environ 2>/dev/null | sed -n 's|^/proc/\([0-9]*\)/environ$|\1|p'
}

if [ -n "$compat" ] && [ -n "$(pids_with_env "STEAM_COMPAT_DATA_PATH=$compat")" ]; then
    echo "$id is already running (a process has STEAM_COMPAT_DATA_PATH=$compat). Close it first." >&2
    exit 1
fi

token="probe-$$-$(date +%s)"
out="$DATA/probe-$id.log"
marker=$(mktemp)
trap 'rm -f "$marker"' EXIT

if [ -n "$winedebug" ]; then
    run_script=$(mktemp)
    sed -E "s/'WINEDEBUG=[^']*'/'WINEDEBUG=$winedebug'/" "$replay" > "$run_script"
    grep -q "WINEDEBUG=$winedebug" "$run_script" \
        || sed -i "s|^exec env|exec env 'WINEDEBUG=$winedebug'|" "$run_script"
    trap 'rm -f "$marker" "$run_script"' EXIT
    replay="$run_script"
fi

cmd=(sh "$replay")
if [ "$headless" = 1 ]; then
    command -v gamescope >/dev/null || { echo "gamescope not installed" >&2; exit 1; }
    cmd=(gamescope --backend headless -W 1920 -H 1080 -- sh "$replay")
fi

echo "== probe $id  token=$token  seconds=$seconds  headless=$headless"
start=$(date +%s)
ARACHNEL_PROBE="$token" setsid "${cmd[@]}" > "$out" 2>&1 < /dev/null &

# Wait, stopping early if every tagged process is gone.
alive=1
sleep 3
while :; do
    elapsed=$(( $(date +%s) - start ))
    if [ -z "$(pids_with_env "ARACHNEL_PROBE=$token")" ]; then
        alive=0; break
    fi
    [ "$elapsed" -ge "$seconds" ] && break
    sleep 2
done
elapsed=$(( $(date +%s) - start ))

if [ "$alive" = 0 ]; then
    echo "== exited on its own after ${elapsed}s"
else
    echo "== still running at ${elapsed}s"
fi

echo
echo "== process output ($out, last 40 lines)"
tail -n 40 "$out"

echo
echo "== logs written during the run"
roots=()
[ -n "$compat" ] && roots+=("$compat/pfx/drive_c/users/steamuser")
[ -n "$workdir" ] && roots+=("$workdir")
found=0
[ ${#roots[@]} -eq 0 ] && roots=("/nonexistent")
while IFS= read -r -d '' f; do
    found=1
    size=$(stat -c %s "$f")
    echo
    echo "--- $f  (${size} bytes, last 30 lines)"
    tail -n 30 "$f"
done < <(find "${roots[@]}" -maxdepth 8 -type f \( -iname '*.log' -o -iname '*.txt' \) \
             -newer "$marker" -size -60M -print0 2>/dev/null)
[ "$found" = 0 ] && echo "(none)"

if [ "$alive" = 1 ] && [ "$keep" = 0 ]; then
    echo
    echo "== stopping tagged processes"
    mapfile -t victims < <(pids_with_env "ARACHNEL_PROBE=$token")
    [ ${#victims[@]} -gt 0 ] && kill -TERM "${victims[@]}" 2>/dev/null
    sleep 4
    mapfile -t victims < <(pids_with_env "ARACHNEL_PROBE=$token")
    [ ${#victims[@]} -gt 0 ] && kill -KILL "${victims[@]}" 2>/dev/null
    sleep 1
    left=$(pids_with_env "ARACHNEL_PROBE=$token" | wc -l)
    echo "stopped (${left} left)"
fi
