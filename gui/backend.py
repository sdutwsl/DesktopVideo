"""Per-user IPC clients. No HTTP listener, shell commands or remote control port."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time


def locate_backend(explicit: str | None = None) -> str:
    if explicit:
        return str(Path(explicit).resolve())
    name = "muralop.exe" if sys.platform == "win32" else "muralop"
    root = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent.parent
    candidates = [root / name, root / "build-win" / name, root / "build" / name]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(name)
    if found:
        return found
    raise FileNotFoundError("找不到壁纸后端；请通过 --backend 指定 muralop 的路径。")


def locate_ytdlp() -> str:
    roots = [Path(sys.executable).parent, Path(__file__).resolve().parent.parent / ".venv" / "Scripts"]
    for root in roots:
        for name in ("yt-dlp.exe", "yt-dlp"):
            if (root / name).is_file():
                return str(root / name)
    return shutil.which("yt-dlp") or ""


class Backend:
    def __init__(self, executable: str):
        self.executable = executable
        self.children = set()
        self.lock = threading.Lock()
        self.closed = False

    def close(self):
        with self.lock:
            self.closed = True
            for child in self.children:
                if child.poll() is None:
                    child.kill()

    def request(self, command: str, *args: str) -> dict:
        fields = (command, *args)
        if any(any(c in value for c in "\r\n\0") for value in fields):
            raise ValueError("参数不能包含换行或空字符")
        payload = "\n".join(fields)
        if len(payload.encode()) > 32768:
            raise ValueError("请求过长")
        # Native IPC lives in a disposable process. A stuck daemon or binding
        # cannot hold Qt/Python's event loop or prevent the window from closing.
        with self.lock:
            if self.closed:
                raise ConnectionError("界面已关闭")
            child = subprocess.Popen([self.executable, "rpc"], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
            self.children.add(child)
        try:
            data, _error = child.communicate(payload.encode("utf-8"), timeout=4)
            if child.returncode:
                raise ConnectionError("壁纸后端无法连接或没有响应，请重启播放后端。")
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate()
            raise RuntimeError("壁纸后端响应超时，界面仍可操作；请重启播放后端。") from None
        finally:
            with self.lock:
                self.children.discard(child)
        result = json.loads(data)
        if not result.get("ok"):
            raise RuntimeError(result.get("error", "壁纸后端请求失败"))
        return result

    def ensure_running(self) -> dict:
        try:
            return self.request("status")
        except (OSError, ConnectionError):
            pass
        subprocess.Popen([self.executable, "--background"], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            time.sleep(0.25)
            try:
                return self.request("status")
            except (OSError, ConnectionError):
                continue
        raise RuntimeError("壁纸后端未能启动。请检查 libmpv 和桌面环境，或在终端单独运行后端查看错误。")
