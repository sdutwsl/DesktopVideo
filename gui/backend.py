"""Per-user IPC clients. No HTTP listener, shell commands or remote control port."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
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


def _windows_request(payload: bytes) -> bytes:
    import ctypes
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    name = ctypes.create_unicode_buffer(256)
    size = wintypes.DWORD(256)
    advapi.GetUserNameW.argtypes = [wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    if not advapi.GetUserNameW(name, ctypes.byref(size)):
        raise ctypes.WinError(ctypes.get_last_error())
    session = wintypes.DWORD()
    if not kernel.ProcessIdToSessionId(os.getpid(), ctypes.byref(session)):
        raise ctypes.WinError(ctypes.get_last_error())
    pipe = rf"\\.\pipe\Muralop.{session.value}.{name.value}"
    kernel.CallNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.LPVOID, wintypes.DWORD,
                                    wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    kernel.CallNamedPipeW.restype = wintypes.BOOL
    output = ctypes.create_string_buffer(65536)
    count = wintypes.DWORD()
    if not kernel.CallNamedPipeW(pipe, payload, len(payload), output, len(output), ctypes.byref(count), 2000):
        raise ctypes.WinError(ctypes.get_last_error())
    return output.raw[:count.value]


def _linux_request(payload: str) -> str:
    from PySide6.QtDBus import QDBusConnection, QDBusMessage

    message = QDBusMessage.createMethodCall("org.muralop.Daemon", "/org/muralop/Daemon",
                                          "org.muralop.Daemon", "Execute")
    message.setArguments([payload])
    reply = QDBusConnection.sessionBus().call(message, timeout=5000)
    if reply.type() == QDBusMessage.MessageType.ErrorMessage:
        raise ConnectionError(reply.errorMessage())
    return reply.arguments()[0]


class Backend:
    def __init__(self, executable: str):
        self.executable = executable

    def request(self, command: str, *args: str) -> dict:
        fields = (command, *args)
        if any(any(c in value for c in "\r\n\0") for value in fields):
            raise ValueError("参数不能包含换行或空字符")
        payload = "\n".join(fields)
        if len(payload.encode()) > 32768:
            raise ValueError("请求过长")
        data = _windows_request(payload.encode("utf-8")) if sys.platform == "win32" else _linux_request(payload)
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
        for _ in range(40):
            time.sleep(0.25)
            try:
                return self.request("status")
            except (OSError, ConnectionError):
                continue
        raise RuntimeError("壁纸后端未能启动。请检查 libmpv 和桌面环境，或在终端单独运行后端查看错误。")
