import os
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gui"))
from session import read_cookie_file, session_domain, write_youtube_cookies, youtube_video_url
from backend import Backend


@pytest.mark.parametrize("url", [
    "https://www.youtube.com/watch?v=abcdefghijk&list=playlist&t=15",
    "https://youtu.be/abcdefghijk?t=5", "https://m.youtube.com/shorts/abcdefghijk",
    "https://www.youtube.com/live/abcdefghijk", "https://music.youtube.com/watch?v=abcdefghijk",
])
def test_video_urls(url):
    assert youtube_video_url(url) == "https://www.youtube.com/watch?v=abcdefghijk"


@pytest.mark.parametrize("url", [
    "https://youtube.com.evil.test/watch?v=abcdefghijk", "javascript:alert(1)",
    "file:///C:/secret", "http://youtube.com/watch?v=abcdefghijk", "https://youtube.com/",
    "https://youtube.com/playlist?list=abc", "https://youtube.com/watch?v=short",
    "https://user@youtube.com/watch?v=abcdefghijk", "https://youtube.com:123/watch?v=abcdefghijk",
    "https://youtu.be/abcdefghijk/other", "https://youtube.com:bad/watch?v=abcdefghijk",
])
def test_reject_non_video_or_untrusted_urls(url):
    assert youtube_video_url(url) is None


def test_cookie_export_scope_permissions_and_roundtrip(tmp_path):
    records = [
        dict(name="SID", value="youtube-session", domain=".youtube.com", path="/", secure=True, http_only=True, expires=4102444800),
        dict(name="SID", value="google-session", domain=".google.com", path="/", secure=True, expires=4102444800),
        dict(name="private", value="other-site", domain=".example.com", path="/"),
        dict(name="injected", value="a\n.youtube.com\tTRUE", domain=".youtube.com", path="/"),
    ]
    target = tmp_path / "cookies.txt"
    write_youtube_cookies(target, records)
    text = target.read_text()
    assert "#HttpOnly_.youtube.com" in text
    assert "google-session" not in text and "other-site" not in text and "injected" not in text
    loaded = read_cookie_file(str(target))
    assert len(loaded) == 1 and loaded[0]["value"] == "youtube-session" and loaded[0]["http_only"]
    if os.name != "nt":
        assert target.stat().st_mode & 0o777 == 0o600
    write_youtube_cookies(target, [])
    assert "youtube-session" not in target.read_text()


def test_import_scope_is_exact():
    assert session_domain(".youtube.com") and session_domain("accounts.google.com")
    assert not session_domain("youtube.com.evil.test") and not session_domain("evilgoogle.com")


def test_control_arguments_cannot_inject_second_command():
    backend = Backend("unused")
    with pytest.raises(ValueError):
        backend.request("set", "https://youtube.com/\nquit", "")
    with pytest.raises(ValueError):
        backend.request("set", "x" * 33000, "")
