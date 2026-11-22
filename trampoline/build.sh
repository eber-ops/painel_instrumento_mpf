#!/usr/bin/env bash
set -euo pipefail
root="${TRAMPOLINE_ROOT:-/root/trampoline-test/source}"
here="$(cd -- "$(dirname -- "$0")" && pwd)"
dest="$root/examples/posix/panel"
python3 - "$here" "$dest" <<'PY'
import pathlib, shutil, sys
source, destination = map(pathlib.Path, sys.argv[1:])
destination.mkdir(parents=True, exist_ok=True)
for name in ("panel.c", "panel.oil"):
    shutil.copyfile(source / name, destination / name)
PY
cd "$dest"
"$root/goil/makefile-unix/goil" --target=posix/linux --templates="$root/goil/templates" panel.oil
python3 make.py
printf 'Backend: %s/panel_exe\n' "$dest"
