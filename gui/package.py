"""Build a portable GUI and extractor. Run from the repository root."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend-dir", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / "dist" / "gui"
    work = root / "build-gui"
    work.mkdir(exist_ok=True)
    base = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--paths", str(root / "gui"),
            "--distpath", str(output), "--workpath", str(work), "--specpath", str(work)]
    subprocess.run(base + ["--windowed", "--name", "videowallpaper-ui", "--collect-all", "yt_dlp", str(root / "gui/main.py")], check=True)
    subprocess.run(base + ["--onefile", "--console", "--name", "yt-dlp", "--collect-all", "yt_dlp", str(root / "gui/ytdlp_entry.py")], check=True)
    suffix = ".exe" if sys.platform == "win32" else ""
    bundle = output / "videowallpaper-ui"
    if sys.platform == "win32":
        import PySide6
        # Recent Qt Windows wheels use the OS ICU API. PyInstaller can otherwise
        # pick an unrelated, ABI-incompatible ICU from a developer's PATH.
        qt_directory = Path(PySide6.__file__).parent
        wrong_icu = bundle / "_internal" / "icuuc.dll"
        if wrong_icu.is_file() and not (qt_directory / "icuuc.dll").is_file():
            wrong_icu.unlink()
    shutil.copy2(output / ("yt-dlp" + suffix), bundle)
    backend_dir = Path(args.backend_dir)
    shutil.copy2(backend_dir / ("videowallpaper" + suffix), bundle)
    if sys.platform == "win32":
        shutil.copy2(backend_dir / "libmpv-2.dll", bundle)
    # YouTube extraction needs a JS runtime. Bundle an upstream, checksum-pinned
    # Deno instead of depending on a separately installed developer runtime.
    assets = {
        "win32": ("deno-x86_64-pc-windows-msvc.zip", "cba84b26e0df5ae7f74fc66b86a95b866872a86292ebd84fa1e959927edd93bf"),
        "linux": ("deno-x86_64-unknown-linux-gnu.zip", "e890975bc2549807bbcc0b584ffafda72cc7c93349dd99c80483dd784f6c4609"),
    }
    asset, expected = assets[sys.platform]
    archive = work / asset
    with urllib.request.urlopen(f"https://github.com/denoland/deno/releases/download/v2.9.8/{asset}", timeout=180) as response:
        with archive.open("wb") as stream:
            shutil.copyfileobj(response, stream)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Deno download checksum mismatch")
    with zipfile.ZipFile(archive) as zipped:
        with zipped.open("deno" + suffix) as source, (bundle / ("deno" + suffix)).open("wb") as target:
            shutil.copyfileobj(source, target)
    (bundle / ("deno" + suffix)).chmod(0o755)
    shutil.copy2(root / "gui/README.md", bundle / "GUI-README.md")
    # A successful collection step does not prove Qt's DLLs/resources can load.
    smoke = work / "packaged-preview.png"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    subprocess.run([str(bundle / ("videowallpaper-ui" + suffix)), "--smoke-test", str(smoke)], env=env, check=True, timeout=60)
    if not smoke.is_file():
        raise RuntimeError("Packaged GUI did not produce its smoke-test image")
    print(bundle)


if __name__ == "__main__":
    main()
