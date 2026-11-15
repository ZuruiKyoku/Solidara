#!/usr/bin/env bash
# Source this file; it does not alter system installations.
solidara_script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
solidara_deps_dir=${SOLIDARA_DEPS_DIR:-$(dirname "$(dirname "$solidara_script_dir")")/.solidara-deps}
solidara_prefix="$solidara_deps_dir/root/usr"
if [[ ! -x "$solidara_prefix/bin/cmake" ]]; then
    printf '%s\n' 'Run scripts/bootstrap-linux.sh before sourcing scripts/env-linux.sh.' >&2
    return 1 2>/dev/null || exit 1
fi
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-$solidara_deps_dir/cache}"
mkdir -p "$XDG_CACHE_HOME"
export PATH="$solidara_prefix/bin:$PATH"
export CMAKE_PREFIX_PATH="$solidara_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export LD_LIBRARY_PATH="$solidara_prefix/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBRARY_PATH="$solidara_prefix/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
export QT_PLUGIN_PATH="$solidara_prefix/lib/x86_64-linux-gnu/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export PKG_CONFIG_PATH="$solidara_prefix/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
unset solidara_script_dir solidara_deps_dir solidara_prefix
