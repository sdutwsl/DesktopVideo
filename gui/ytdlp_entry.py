"""Console entry point packaged alongside the GUI for mpv's ytdl hook."""
import os
from pathlib import Path
import sys

if getattr(sys, "frozen", False):
    os.environ["PATH"] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get("PATH", "")

import yt_dlp

if __name__ == "__main__":
    yt_dlp.main()
