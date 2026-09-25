#!/bin/sh
# Launch the fork's build with the same environment Rose's AppImage wrapper uses.
ROOT="$(cd "$(dirname "$0")" && pwd)"

# Qt6's default OpenGL/RHI path crashes in libnvidia-glcore under XWayland.
export QSG_RHI_BACKEND=vulkan
# Single-threaded scene graph: the launch path still pumps a nested
# processEvents() while installing runtime deps, which deadlocks against the
# threaded render loop. Remove once that step moves off the GUI thread.
export QSG_RENDER_LOOP=basic
export QT_QML_MATERIAL_IMPORT_PATH="${ROOT}/build/qml_modules"

exec "${ROOT}/build/SproutLauncher" "$@"
