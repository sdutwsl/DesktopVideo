"""Cookie conversion and YouTube URL validation, independent of the GUI toolkit."""

from __future__ import annotations

import http.cookiejar
import os
from pathlib import Path
import re
import tempfile
import time
from urllib.parse import parse_qs, urlsplit


def youtube_video_url(value: str) -> str | None:
    """Accept only HTTPS YouTube video pages, never arbitrary navigation schemes."""
    try:
        url = urlsplit(value)
        if url.scheme != "https" or url.username or url.password or url.port not in (None, 443):
            return None
        host = (url.hostname or "").lower()
        video_id = None
        if host == "youtu.be":
            video_id = url.path.strip("/")
        elif host in ("youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com"):
            if url.path == "/watch":
                video_id = parse_qs(url.query).get("v", [None])[0]
            elif url.path.startswith(("/shorts/", "/live/")):
                video_id = url.path.split("/")[2]
        if video_id and re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
            return f"https://www.youtube.com/watch?v={video_id}"
    except ValueError:
        pass
    return None


def session_domain(domain: str) -> bool:
    domain = domain.lstrip(".").lower()
    return domain in ("youtube.com", "google.com", "accounts.google.com") or domain.endswith(".youtube.com")


def youtube_domain(domain: str) -> bool:
    domain = domain.lstrip(".").lower()
    return domain == "youtube.com" or domain.endswith(".youtube.com")


def cookie_record(cookie: http.cookiejar.Cookie) -> dict:
    return {
        "name": cookie.name, "value": cookie.value, "domain": cookie.domain,
        "path": cookie.path or "/", "secure": cookie.secure,
        "expires": cookie.expires or 0,
        "http_only": cookie.has_nonstandard_attr("HttpOnly") or cookie.has_nonstandard_attr("HTTPOnly"),
    }


def read_cookie_file(path: str) -> list[dict]:
    jar = http.cookiejar.MozillaCookieJar(path)
    jar.load(ignore_discard=True, ignore_expires=True)
    return [cookie_record(c) for c in jar if session_domain(c.domain) and (not c.expires or c.expires > time.time())]


class QuietLogger:
    def debug(self, *_args, **_kwargs): pass
    def info(self, *_args, **_kwargs): pass
    def warning(self, *_args, **_kwargs): pass
    def error(self, *_args, **_kwargs): pass


def read_browser_cookies(browser: str, profile: str = "") -> list[dict]:
    from yt_dlp import YoutubeDL

    if browser not in ("chrome", "edge", "firefox", "chromium", "brave"):
        raise ValueError("Unsupported browser")
    # yt-dlp performs the OS-specific extraction. Never log or persist other sites.
    with YoutubeDL({"cookiesfrombrowser": (browser, profile or None, None, None), "logger": QuietLogger()}) as ydl:
        records = [cookie_record(c) for c in ydl.cookiejar if session_domain(c.domain) and not c.is_expired()]
    if not any(youtube_domain(c["domain"]) for c in records):
        raise RuntimeError("没有读到 YouTube 会话。请确认浏览器已登录；Chrome/Edge 的加密可能阻止导入，可改用 cookies.txt。")
    return records


def write_youtube_cookies(path: Path, records: list[dict]) -> None:
    """Atomically write only YouTube cookies; Google login cookies stay in WebEngine."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".cookies-", dir=path.parent)
    try:
        os.chmod(temporary, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write("# Netscape HTTP Cookie File\n")
            for c in records:
                if not youtube_domain(c["domain"]):
                    continue
                fields = [c["domain"], "TRUE" if c["domain"].startswith(".") else "FALSE",
                          c.get("path") or "/", "TRUE" if c.get("secure") else "FALSE",
                          str(int(c.get("expires") or 0)), c["name"], c["value"]]
                if any(any(ch in field for ch in "\r\n\t\0") for field in fields):
                    continue
                if c.get("http_only"):
                    fields[0] = "#HttpOnly_" + fields[0]
                stream.write("\t".join(fields) + "\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
