"""Bundle Electron, the player plugin, session importer, yt-dlp and Deno."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from package import download


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--player-dir", required=True, help="Directory containing muralop.node and generated package.json")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    player = Path(args.player_dir)
    if not (player / "muralop.node").is_file():
        parser.error("muralop.node is missing; build with -DMURALOP_BUILD_NODE_PLUGIN=ON first")
    metadata = json.loads((player / "package.json").read_text(encoding="utf-8"))
    runtime = json.loads(subprocess.check_output(["node", "-p", "JSON.stringify({electron:require('electron'),node:process.execPath})"], cwd=ROOT / "desktop", text=True))
    executable = Path(runtime["electron"])
    bundle = ROOT / "dist/gui/muralop-ui"
    if bundle.exists():
        shutil.rmtree(bundle)
    shutil.copytree(executable.parent, bundle)
    (bundle / executable.name).rename(bundle / ("muralop-ui" + suffix))
    if sys.platform == "linux":
        shutil.copy2(runtime["node"], bundle / "node")
    app_dir = bundle / "resources/app"
    app_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.cjs", "*.html", "*.css", "package.json"):
        for source in (ROOT / "desktop").glob(pattern):
            shutil.copy2(source, app_dir)
    (app_dir / "package.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(player / "muralop.node", app_dir)
    work = ROOT / "build-electron"
    work.mkdir(exist_ok=True)
    subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile",
        "--name", "muralop-session", "--distpath", str(work / "session"), "--workpath", str(work / "build"),
        "--specpath", str(work), str(ROOT / "gui/session.py")], check=True)
    shutil.copy2(work / "session" / ("muralop-session" + suffix), bundle)
    if sys.platform == "win32":
        shutil.copy2(player / "libmpv-2.dll", bundle)
    extractors = {
        "win32": ("yt-dlp.exe", "33121d192d706e62f684a06f80918e3f3a8ba52ac3a65213b6951c39c7d9f555"),
        "linux": ("yt-dlp_linux", "d8bd33fca9d5b23bd5b9952b3fca36db5c46331933fda9f598a3da14ce2c243f"),
    }
    asset, checksum = extractors[sys.platform]
    download(f"https://github.com/yt-dlp/yt-dlp/releases/download/2026.10.21/{asset}", bundle / ("yt-dlp" + suffix), checksum)
    (bundle / ("yt-dlp" + suffix)).chmod(0o755)
    deno = {
        "win32": ("deno-x86_64-pc-windows-msvc.zip", "cba84b26e0df5ae7f74fc66b86a95b866872a86292ebd84fa1e959927edd93bf"),
        "linux": ("deno-x86_64-unknown-linux-gnu.zip", "e890975bc2549807bbcc0b584ffafda72cc7c93349dd99c80483dd784f6c4609"),
    }
    asset, checksum = deno[sys.platform]
    archive = work / asset
    download(f"https://github.com/denoland/deno/releases/download/v2.9.8/{asset}", archive, checksum)
    with zipfile.ZipFile(archive) as zipped:
        with zipped.open("deno" + suffix) as source, (bundle / ("deno" + suffix)).open("wb") as target:
            shutil.copyfileobj(source, target)
    (bundle / ("deno" + suffix)).chmod(0o755)
    shutil.copy2(ROOT / "desktop/README.md", bundle / "GUI-README.md")
    print(bundle)


if __name__ == "__main__":
    main()
