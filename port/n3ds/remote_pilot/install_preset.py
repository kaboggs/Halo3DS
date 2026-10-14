# New3DS integration additions by Kevin Boggs.
# Host tool: https://github.com/kaboggs/RemotePilot
# MIT terms: LICENSE-KEVIN-MIT.md at the game source root.
"""Install this source-only Halo3DS preset into a local Remote Pilot checkout."""

import argparse
from datetime import datetime, timezone
from pathlib import Path
import shutil
import sys


FILES = ("manifest.json", "adapter.py", "halo_protocol.py", "catalog.json")
SOURCE = Path(__file__).resolve().parent / "preset" / "halo3ds"


# Kevin Boggs: choose_tool_root - New3DS integration.
def choose_tool_root(value):
    if value:
        return Path(value).expanduser().resolve()
    try:
        from tkinter import Tk, filedialog
        dialog = Tk()
        dialog.withdraw()
        dialog.attributes("-topmost", True)
        chosen = filedialog.askdirectory(title="Select the Remote Pilot folder")
        dialog.destroy()
        if chosen:
            return Path(chosen).resolve()
    except (ImportError, RuntimeError):
        pass
    raise ValueError("Select the Remote Pilot folder with --tool-root")


# Kevin Boggs: install - New3DS integration.
def install(tool_root):
    root = Path(tool_root).resolve()
    if any(not (root / name).is_file() for name in ("server.py", "presets.py", "reference_adapter.py")):
        raise ValueError("This folder is not a compatible Remote Pilot source checkout")
    presets = root / "presets"
    target = presets / "halo3ds"
    if presets.is_symlink() or target.is_symlink() or not presets.resolve().is_relative_to(root):
        raise ValueError("Unsafe preset destination")
    if target.exists() and (not target.is_dir() or not target.resolve().is_relative_to(presets.resolve())):
        raise ValueError("Unsafe existing preset")
    if any((target / name).is_symlink() for name in FILES):
        raise ValueError("Unsafe preset file link")
    changes = [name for name in FILES if not (target / name).is_file() or
               (target / name).read_bytes() != (SOURCE / name).read_bytes()]
    if not changes:
        return "Halo3DS preset is already current."
    if target.exists():
        local = root / "local"
        if local.is_symlink() or not local.resolve().is_relative_to(root):
            raise ValueError("Unsafe local backup folder")
        backup = local / "preset-backups" / ("halo3ds-" + datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f"))
        backup.mkdir(parents=True, exist_ok=False)
        for name in FILES:
            current = target / name
            if current.is_file():
                shutil.copy2(current, backup / name)
    target.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        source = SOURCE / name
        if not source.is_file() or source.is_symlink():
            raise ValueError(f"Preset source missing: {name}")
        temporary = target / (name + ".tmp")
        shutil.copyfile(source, temporary)
        temporary.replace(target / name)
    return "Halo3DS preset installed. Restart Remote Pilot, select Halo3DS Dev, and choose Prepare pairing."


# Kevin Boggs: main - New3DS integration.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, help="Remote Pilot folder; omit to choose with a folder dialog")
    args = parser.parse_args()
    try:
        print(install(choose_tool_root(args.tool_root)))
    except (OSError, ValueError) as exc:
        print(f"Preset installation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
