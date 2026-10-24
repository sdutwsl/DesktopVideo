"""Offline Qt interaction checks. No browser profile or network access is used."""
import os
from pathlib import Path
import sys
import time

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gui"))
from PySide6.QtCore import QCoreApplication, QEvent, QUrl
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineCore import QWebEnginePage
from main import MainWindow


class FakeBackend:
    def __init__(self): self.calls = []
    def request(self, command, *args):
        self.calls.append((command, args))
        return {"ok": True, "volume": 50, "paused": False,
                "monitors": [{"id": "screen-a", "width": 1920, "height": 1080}]}


@pytest.fixture(scope="module")
def application():
    return QApplication.instance() or QApplication([])


def drain(app, predicate, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if predicate(): return
        time.sleep(0.01)
    pytest.fail("Qt operation timed out")


def test_window_play_to_monitor_and_navigation_policy(application, tmp_path):
    backend = FakeBackend()
    window = MainWindow(backend, tmp_path, smoke=True)
    try:
        window.extractor.setText("C:/tools/yt-dlp.exe")
        window.monitor.setCurrentIndex(1)
        window.source.setText("https://youtu.be/abcdefghijk?t=15")
        window.play_button.click()
        drain(application, lambda: window.jobs.pending == 0)
        assert backend.calls[-1] == ("set", ("https://www.youtube.com/watch?v=abcdefghijk", "Demo"))
        assert backend.calls[0][0] == "online" and backend.calls[0][1][1] == str(tmp_path / "youtube-cookies.txt")
        clicked = []
        drain(application, lambda: window.page.url().host() == "www.youtube.com")
        window.page.playRequested.connect(clicked.append)
        url = QUrl("videowallpaper://play?url=https%3A%2F%2Fwww.youtube.com%2Fwatch%3Fv%3D0123456789_")
        assert not window.page.acceptNavigationRequest(url, QWebEnginePage.NavigationType.NavigationTypeOther, True)
        assert clicked == ["https://www.youtube.com/watch?v=0123456789_"]
        bad = QUrl("videowallpaper://play?url=file%3A%2F%2Fsecret")
        window.page.acceptNavigationRequest(bad, QWebEnginePage.NavigationType.NavigationTypeOther, True)
        assert len(clicked) == 1
        window.auto.setChecked(False)
        window.page.acceptNavigationRequest(url, QWebEnginePage.NavigationType.NavigationTypeOther, True)
        assert len(clicked) == 1
        assert not window.page.acceptNavigationRequest(QUrl("file:///secret"), QWebEnginePage.NavigationType.NavigationTypeOther, True)
        assert len(window.page.scripts().find("wallpaper-video-click")) == 1
        drain(application, lambda: window.jobs.pending == 0)
    finally:
        window.close()
        window.web.setPage(QWebEnginePage(window.web))
        window.page.deleteLater()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        window.deleteLater()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
