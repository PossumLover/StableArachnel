#!/usr/bin/env bash
# Screenshot a page of Sprout headless, from a sandboxed data copy.
#
#   scripts/dev/screenshot.sh <out.png> [page] [delay-ms] [view]
#
#   page      0 Library  1 Discover  2 Catalog  3 Friends  4 Favorites  5 Downloads
#   delay-ms  how long to let covers and animations settle (default 12000)
#   view      opened on top of the page: settings, settings:<section>
#             (appearance, storage, updates, launch, plugins, sources, friends, about)
#             or details:<gameId>
#
# Uses SANDBOX (default: $TMPDIR/jg-shot) as XDG_DATA_HOME / XDG_CONFIG_HOME, seeded
# once from your real data and config - so the real library is never written to -
# and runs as a separate instance (ARACHNEL_INSTANCE) beside the one you use.
# Retries up to 3 times: the app still has an intermittent startup crash
# (QQmlConnections::connectSignalsToMethods, ~3 s in) that is not caused by this.
set -uo pipefail
out="${1:?usage: $0 <out.png> [page] [delay-ms]}"
page="${2:-0}"
delay="${3:-12000}"
view="${4:-}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
sandbox="${SANDBOX:-${TMPDIR:-/tmp}/jg-shot}"
real_data="$HOME/.local/share/Arachnel/Arachnel"

if [ ! -d "$sandbox/data/Arachnel/Arachnel" ]; then
    mkdir -p "$sandbox/data/Arachnel/Arachnel" "$sandbox/config/Arachnel"
    for f in library.json settings.json jobs.json metadata-cache.json cover-cache catalog-cache \
             plugins plugin-catalog-cache; do
        [ -e "$real_data/$f" ] && cp -a "$real_data/$f" "$sandbox/data/Arachnel/Arachnel/"
    done
    [ -f "$HOME/.config/Arachnel/Arachnel.conf" ] && cp "$HOME/.config/Arachnel/Arachnel.conf" "$sandbox/config/Arachnel/"
fi

# Nothing from a previous run should cover the page: a pending crash dialog
# (the startup crash can leave one) or the "update available" sheet.
python3 - "$sandbox/data/Arachnel/Arachnel/settings.json" <<'PY'
import json, sys
p = sys.argv[1]
try:
    d = json.load(open(p))
except Exception:
    d = {}
d["autoCheckAppUpdates"] = False
json.dump(d, open(p, "w"), indent=2)
PY

app="$root/build/SproutLauncher"
[ -x "$app" ] || app="$root/build/arachnel_app"
for try in 1 2 3; do
    rm -f "$out" "$sandbox/data/Arachnel/Arachnel/crash-pending.json"
    # A real (invisible) display, not QT_QPA_PLATFORM=offscreen: the pages are
    # asynchronous Loaders, and Qt only advances incubation from a render loop that
    # is presenting frames. Offscreen windows never do, so every page stays blank.
    timeout $(( delay / 1000 + 60 )) gamescope --backend headless -W 1450 -H 900 -- \
        env -u WAYLAND_DISPLAY ARACHNEL_INSTANCE=shot \
        XDG_DATA_HOME="$sandbox/data" XDG_CONFIG_HOME="$sandbox/config" \
        QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=vulkan \
        QT_QML_MATERIAL_IMPORT_PATH="$root/build/qml_modules" \
        "$app" --screenshot "$out" --page "$page" --delay "$delay" ${view:+--open "$view"} > "$sandbox/last-run.log" 2>&1
    [ -f "$out" ] && { echo "$out"; exit 0; }
    echo "try $try failed ($(grep -m1 '^Summary' "$sandbox/data/Arachnel/Arachnel/crash-report-latest.txt" 2>/dev/null || echo 'no crash report'))" >&2
done
exit 1
