#!/usr/bin/env bash
set -euo pipefail

# This rootless bootstrap targets the Debian 13 x64 cloud image. Signed apt
# metadata authenticates package checksums; no signature/TLS bypass is used.
source /etc/os-release
if [[ "$ID" != debian || "$VERSION_ID" != 13 || "$(dpkg --print-architecture)" != amd64 ]]; then
    printf '%s\n' 'This bootstrap requires Debian 13 amd64; use normal system packages elsewhere.' >&2
    exit 1
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
deps_dir=${SOLIDARA_DEPS_DIR:-$(dirname "$(dirname "$script_dir")")/.solidara-deps}
mkdir -p "$deps_dir/apt/lists/partial" "$deps_dir/apt/archives/partial" "$deps_dir/root" "$deps_dir/apt/empty"
deps_dir=$(cd "$deps_dir" && pwd)
cat > "$deps_dir/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
SOURCES
cat > "$deps_dir/apt/apt.conf" <<CONFIG
Dir::Etc::sourcelist "$deps_dir/apt/sources.list";
Dir::Etc::sourceparts "$deps_dir/apt/empty";
Dir::Etc::main "-";
Dir::Etc::parts "$deps_dir/apt/empty";
Dir::State::lists "$deps_dir/apt/lists";
Dir::State::status "/var/lib/dpkg/status";
Dir::Cache::archives "$deps_dir/apt/archives";
Debug::NoLocking "true";
APT::Get::List-Cleanup "false";
Acquire::Languages "none";
CONFIG
export APT_CONFIG="$deps_dir/apt/apt.conf"
apt-get update
apt-get --download-only --no-install-recommends --assume-yes install \
    cmake ninja-build qt6-base-dev qt6-svg-dev libtbb-dev libfontconfig-dev \
    libocct-data-exchange-dev libocct-visualization-dev libocct-modeling-algorithms-dev \
    xvfb xauth libgl1-mesa-dri libxkbcommon-dev libxkbcommon-x11-dev
for package in "$deps_dir"/apt/archives/*.deb; do
    dpkg-deb -x "$package" "$deps_dir/root"
done

# Debian dev packages assume system runtime libraries. Reuse installed files
# without copying or modifying them, and relocate OCCT's absolute CMake paths.
python3 - "$deps_dir/root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
for base in map(Path, ['/usr/lib/x86_64-linux-gnu', '/usr/lib/qt6',
                       '/usr/share/qt6', '/usr/share/opencascade']):
    if not base.exists():
        continue
    for source in base.rglob('*'):
        if not source.is_file():
            continue
        target = root / source.relative_to('/')
        if target.is_symlink() or target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(source)
for config in (root / 'usr/lib/x86_64-linux-gnu/cmake/opencascade').glob('*.cmake'):
    text = config.read_text()
    text = text.replace('"/usr/', f'"{root}/usr/').replace(';/usr/', f';{root}/usr/')
    config.write_text(text)
PY
SOLIDARA_DEPS_DIR="$deps_dir" source "$script_dir/env-linux.sh"
cmake --version
ninja --version
printf 'Dependencies prepared. Run: source %q\n' "$script_dir/env-linux.sh"
