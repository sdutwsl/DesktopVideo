#!/usr/bin/env python3
"""Muralop control window and embedded YouTube browser."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import signal
import sys
from urllib.parse import parse_qs, urlsplit

from PySide6.QtCore import QCoreApplication, QDateTime, QEvent, QObject, QSettings, QStandardPaths, Qt, QTimer, QUrl, Signal
from PySide6.QtNetwork import QLocalServer, QLocalSocket, QNetworkCookie
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWebEngineCore import QWebEnginePage, QWebEngineProfile, QWebEngineScript
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialog, QFileDialog, QFormLayout,
    QHBoxLayout, QLabel, QLineEdit, QMainWindow, QPushButton, QSlider, QSplitter, QVBoxLayout, QWidget)

from backend import Backend, locate_backend, locate_ytdlp
from session import read_browser_cookies, read_cookie_file, session_domain, write_youtube_cookies, youtube_video_url


CLICK_SCRIPT = """
(() => {
  if (location.hostname !== 'www.youtube.com' && location.hostname !== 'm.youtube.com') return;
  window.__muralopToDesktop = AUTO;
  document.addEventListener('click', event => {
    if (!window.__muralopToDesktop || !event.isTrusted || event.button !== 0 ||
        event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    const link = event.target.closest?.('a[href]');
    if (!link) return;
    const url = new URL(link.href, location.href);
    if (url.hostname !== 'www.youtube.com' && url.hostname !== 'm.youtube.com') return;
    const isVideo = (url.pathname === '/watch' && /^[A-Za-z0-9_-]{11}$/.test(url.searchParams.get('v') || '')) ||
      /^\\/(shorts|live)\\/[A-Za-z0-9_-]{11}(?:\\/|$)/.test(url.pathname);
    if (!isVideo) return;
    event.preventDefault(); event.stopImmediatePropagation();
    location.href = 'muralop://play?url=' + encodeURIComponent(url.href);
  }, true);
})();
"""


class Jobs(QObject):
    finished = Signal(str, object, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.pool = ThreadPoolExecutor(max_workers=1, thread_name_prefix="wallpaper-ui")
        self.pending = 0

    def submit(self, name, operation):
        self.pending += 1
        try:
            future = self.pool.submit(operation)
        except RuntimeError:
            self.pending -= 1
            return  # A queued Qt callback ran while the window was closing.

        def complete(done):
            try:
                value, error = done.result(), ""
            except Exception as exc:
                value, error = None, str(exc)
            try:
                self.finished.emit(name, value, error)
            except RuntimeError:
                pass  # The window was closed while an operation was finishing.
        future.add_done_callback(complete)


class YouTubePage(QWebEnginePage):
    playRequested = Signal(str)

    def __init__(self, profile, parent=None, auto_play=lambda: True):
        super().__init__(profile, parent)
        self.auto_play = auto_play
        self.popups = []
        self.allow_test_html = False

    def acceptNavigationRequest(self, url, kind, is_main_frame):
        if url.scheme() == "muralop":
            target = parse_qs(urlsplit(url.toString()).query).get("url", [""])[0]
            video = youtube_video_url(target)
            if is_main_frame and self.auto_play() and video and self.url().host() in ("www.youtube.com", "m.youtube.com"):
                self.playRequested.emit(video)
            return False
        if self.allow_test_html and url.scheme() == "data":
            return True
        if url.scheme() not in ("https", "http", "about"):
            return False
        if is_main_frame and self.auto_play():
            video = youtube_video_url(url.toString())
            if video and kind == QWebEnginePage.NavigationType.NavigationTypeLinkClicked:
                self.playRequested.emit(video)
                return False
        return True

    def createWindow(self, _kind):
        # Login windows share this application's persistent cookie profile.
        dialog = QDialog(self.parent())
        dialog.setWindowTitle("YouTube 登录")
        dialog.resize(620, 760)
        view = QWebEngineView(dialog)
        page = YouTubePage(self.profile(), view, auto_play=lambda: False)
        view.setPage(page)
        layout = QVBoxLayout(dialog)
        layout.addWidget(view)
        self.popups.append(dialog)
        dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        dialog.destroyed.connect(lambda: self.popups.remove(dialog) if dialog in self.popups else None)
        page.windowCloseRequested.connect(dialog.close)
        dialog.show()
        return page


class MainWindow(QMainWindow):
    def __init__(self, backend, data_dir: Path, smoke=False):
        super().__init__()
        self.backend = backend
        self.data_dir = data_dir
        self.cookie_file = data_dir / "youtube-cookies.txt"
        self.cookies = {}
        self.ready = False
        self.last_auto = ""
        self.state = {}
        self.settings = QSettings(str(data_dir / "ui.ini"), QSettings.Format.IniFormat)
        self.jobs = Jobs(self)
        self.jobs.finished.connect(self.completed)
        self.setWindowTitle("Muralop · 视频壁纸")
        self.resize(1380, 900)
        self.setMinimumSize(1000, 650)

        sidebar = QWidget()
        sidebar.setObjectName("sidebar")
        sidebar.setMinimumWidth(310)
        sidebar.setMaximumWidth(390)
        panel = QVBoxLayout(sidebar)
        panel.setContentsMargins(24, 28, 24, 22)
        title = QLabel("Muralop")
        title.setObjectName("brand")
        panel.addWidget(title)
        subtitle = QLabel("让喜欢的视频成为桌面风景")
        subtitle.setObjectName("muted")
        panel.addWidget(subtitle)
        panel.addSpacing(24)
        panel.addWidget(QLabel("播放到"))
        self.monitor = QComboBox()
        self.monitor.addItem("所有显示器（使用默认视频）", "")
        panel.addWidget(self.monitor)
        self.source = QLineEdit()
        self.source.setPlaceholderText("粘贴 YouTube / 视频网址")
        self.source.returnPressed.connect(self.play_input)
        panel.addWidget(self.source)
        self.play_button = QPushButton("播放到桌面")
        self.play_button.setObjectName("primary")
        self.play_button.clicked.connect(self.play_input)
        panel.addWidget(self.play_button)
        file_button = QPushButton("选择本地视频…")
        file_button.clicked.connect(self.choose_file)
        panel.addWidget(file_button)
        reset_button = QPushButton("此显示器恢复默认视频")
        reset_button.clicked.connect(self.clear_override)
        panel.addWidget(reset_button)
        row = QHBoxLayout()
        self.pause = QPushButton("暂停")
        self.mute = QPushButton("静音")
        self.pause.clicked.connect(lambda: self.command("resume" if self.state.get("paused") else "pause"))
        self.mute.clicked.connect(lambda: self.command("unmute" if self.state.get("mute") else "mute"))
        row.addWidget(self.pause)
        row.addWidget(self.mute)
        panel.addLayout(row)
        panel.addWidget(QLabel("音量"))
        self.volume = QSlider(Qt.Orientation.Horizontal)
        self.volume.setRange(0, 100)
        self.volume.setValue(50)
        self.volume.sliderReleased.connect(lambda: self.command("volume", str(self.volume.value())))
        panel.addWidget(self.volume)
        self.auto = QCheckBox("点击网页视频，直接播放到桌面")
        self.auto.setChecked(self.settings.value("auto_play", True, type=bool))
        self.auto.toggled.connect(self.update_click_script)
        panel.addWidget(self.auto)
        panel.addSpacing(18)

        panel.addWidget(QLabel("浏览器会话"))
        self.browser = QComboBox()
        for label, value in [("Chrome", "chrome"), ("Edge", "edge"), ("Firefox", "firefox"),
                             ("Chromium", "chromium"), ("Brave", "brave")]:
            self.browser.addItem(label, value)
        self.browser.setCurrentIndex(max(0, self.browser.findData(self.settings.value("browser", "chrome"))))
        panel.addWidget(self.browser)
        self.browser_profile = QLineEdit(str(self.settings.value("browser_profile", "")))
        self.browser_profile.setPlaceholderText("配置名称或路径，可留空")
        panel.addWidget(self.browser_profile)
        self.import_button = QPushButton("导入浏览器登录会话")
        self.import_button.clicked.connect(self.import_session)
        panel.addWidget(self.import_button)
        import_file = QPushButton("从 cookies.txt 导入…")
        import_file.clicked.connect(self.import_cookie_file)
        panel.addWidget(import_file)
        hint = QLabel("内嵌网页会记住登录。会话导入失败时，可使用 cookies.txt 或在网页内登录。")
        hint.setWordWrap(True)
        hint.setObjectName("muted")
        panel.addWidget(hint)
        self.extractor = QLineEdit(str(self.settings.value("extractor", locate_ytdlp())))
        self.extractor.setPlaceholderText("yt-dlp 可执行文件路径")
        panel.addWidget(self.extractor)
        self.quality = QComboBox()
        for text, value in [("自动画质", ""), ("最高 1080p", "bestvideo[height<=1080]+bestaudio/best[height<=1080]"),
                            ("最高 2160p", "bestvideo[height<=2160]+bestaudio/best[height<=2160]")]:
            self.quality.addItem(text, value)
        self.quality.setCurrentIndex(int(self.settings.value("quality", 0)))
        panel.addWidget(self.quality)
        panel.addStretch()
        self.status = QLabel("正在连接壁纸后端…")
        self.status.setWordWrap(True)
        self.status.setTextFormat(Qt.TextFormat.PlainText)
        self.status.setObjectName("status")
        panel.addWidget(self.status)

        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(16, 20, 20, 20)
        navigation = QHBoxLayout()
        self.web = QWebEngineView()
        self.profile = QWebEngineProfile(self.web) if smoke else QWebEngineProfile("muralop", self.web)
        if not smoke:
            self.profile.setPersistentStoragePath(str(data_dir / "web-profile"))
            self.profile.setCachePath(str(data_dir / "web-cache"))
            self.profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies)
        self.page = YouTubePage(self.profile, self.web, lambda: self.auto.isChecked())
        self.page.setAudioMuted(True)
        self.page.playRequested.connect(self.auto_play)
        self.web.setPage(self.page)
        self.profile.cookieStore().cookieAdded.connect(self.cookie_added)
        self.profile.cookieStore().cookieRemoved.connect(self.cookie_removed)
        self.profile.cookieStore().loadAllCookies()
        for text, callback in [("←", self.web.back), ("→", self.web.forward), ("↻", self.web.reload),
                               ("YouTube", lambda: self.web.setUrl(QUrl("https://www.youtube.com/")))]:
            button = QPushButton(text)
            button.clicked.connect(callback)
            navigation.addWidget(button)
        self.address = QLineEdit("https://www.youtube.com/")
        self.address.returnPressed.connect(self.navigate)
        navigation.addWidget(self.address, 1)
        current = QPushButton("播放当前视频")
        current.clicked.connect(lambda: self.play(self.web.url().toString()))
        navigation.addWidget(current)
        content_layout.addLayout(navigation)
        content_layout.addWidget(self.web, 1)
        self.web.urlChanged.connect(self.url_changed)
        self.web.loadFinished.connect(self.load_finished)
        self.update_click_script()
        splitter = QSplitter()
        splitter.addWidget(sidebar)
        splitter.addWidget(content)
        splitter.setStretchFactor(1, 1)
        self.setCentralWidget(splitter)
        self.setStyleSheet("""
          QMainWindow, QWidget { background: #111820; color: #e6edf3; font-size: 13px; }
          QWidget#sidebar { background: #18222d; border-right: 1px solid #2b3845; }
          QLabel#brand { font-size: 25px; font-weight: 700; color: #f5fafc; }
          QLabel { background: transparent; }
          QLabel#muted { color: #9aaebf; font-size: 12px; }
          QLineEdit, QComboBox { background: #101923; border: 1px solid #344553; border-radius: 6px; padding: 8px; }
          QPushButton { background: #233342; border: 1px solid #344553; border-radius: 6px; padding: 8px 10px; }
          QPushButton:hover { background: #2d4355; }
          QPushButton#primary { background: #419b83; color: #fff; border: 0; font-weight: 600; padding: 12px; }
          QPushButton:disabled { color: #6d7d8b; }
          QCheckBox { spacing: 8px; padding-top: 8px; }
          QLabel#status { color: #acd8cb; padding: 10px; background: #102b29; border-radius: 6px; }
        """)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll)
        if smoke:
            self.apply_state({"ok": True, "volume": 50, "monitors": [{"id": "Demo", "width": 1920, "height": 1080}]})
            self.ready = True
            self.page.allow_test_html = True
            self.web.setHtml("<html style='background:#101820;color:#bacbd8;font-family:sans-serif'><body style='padding:70px'><h1>YouTube</h1><p>内嵌浏览区域 · 在这里浏览并选择视频</p></body></html>", QUrl("https://www.youtube.com/"))
            self.status.setText("主界面预览 · 尚未连接播放后端")
        else:
            self.web.setUrl(QUrl("https://www.youtube.com/"))
            self.jobs.submit("connect", self.backend.ensure_running)

    def completed(self, name, value, error):
        self.jobs.pending -= 1
        self.play_button.setEnabled(self.ready)
        self.import_button.setEnabled(True)
        if error:
            self.status.setText(error)
            return
        if name == "import":
            self.install_cookies(value)
            return
        if name == "connect":
            self.ready = True
            self.timer.start(2000)
            self.play_button.setEnabled(True)
        self.apply_state(value)
        if name == "play":
            self.status.setText("已交给桌面播放器；正在加载视频…")

    def apply_state(self, state):
        self.state = state
        self.pause.setText("继续" if state.get("paused") else "暂停")
        self.mute.setText("取消静音" if state.get("mute") else "静音")
        if not self.volume.isSliderDown():
            self.volume.setValue(state.get("volume", 50))
        selected = self.monitor.currentData()
        monitors = state.get("monitors", [])
        ids = [m["id"] for m in monitors]
        if ids != [self.monitor.itemData(i) for i in range(1, self.monitor.count())]:
            self.monitor.clear()
            self.monitor.addItem("所有显示器（使用默认视频）", "")
            for m in monitors:
                self.monitor.addItem(f'{m["id"]} · {m["width"]}×{m["height"]}', m["id"])
            self.monitor.setCurrentIndex(max(0, self.monitor.findData(selected)))
        if state.get("error"):
            self.status.setText("播放失败：" + state["error"])
        elif not monitors:
            self.status.setText("尚无可用的壁纸显示器，请检查桌面环境。")
        else:
            self.status.setText("已连接 · " + ("已暂停" if state.get("paused") else "播放器就绪"))

    def poll(self):
        if self.ready and not self.jobs.pending:
            self.jobs.submit("status", lambda: self.backend.request("status"))

    def command(self, command, *args):
        if self.ready:
            self.jobs.submit(command, lambda: self.backend.request(command, *args))

    def play_input(self):
        self.play(self.source.text().strip())

    def play(self, source):
        if not self.ready:
            self.status.setText("请先连接壁纸后端。")
            return
        if not source:
            self.status.setText("先选择一个视频或输入网址。")
            return
        video = youtube_video_url(source)
        source = video or source
        if source.startswith(("http://", "https://")) and not self.extractor.text().strip():
            self.status.setText("请安装 yt-dlp，或填写 yt-dlp 可执行文件路径。")
            return
        self.export_cookies()
        monitor = self.monitor.currentData() or ""
        extractor = self.extractor.text().strip()
        cookie_file = str(self.cookie_file)
        quality = self.quality.currentData()
        self.source.setText(source)
        self.status.setText("正在把视频交给桌面播放器…")
        self.play_button.setEnabled(False)

        def operation():
            self.backend.request("online", extractor, cookie_file, quality)
            return self.backend.request("set", source, monitor)
        self.jobs.submit("play", operation)

    def auto_play(self, url):
        if url == self.last_auto or not self.ready:
            return
        self.last_auto = url
        self.play(url)
        QTimer.singleShot(1500, lambda: setattr(self, "last_auto", ""))

    def choose_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择壁纸视频", "", "视频 (*.mp4 *.mkv *.webm *.avi *.mov *.wmv);;所有文件 (*)")
        if path:
            self.play(path)

    def clear_override(self):
        if self.monitor.currentData():
            self.command("clear", self.monitor.currentData())

    def navigate(self):
        url = QUrl.fromUserInput(self.address.text().strip())
        if url.scheme() in ("https", "http"):
            self.last_auto = ""
            self.web.setUrl(url)

    def url_changed(self, url):
        self.address.setText(url.toString())
        video = youtube_video_url(url.toString())
        if video and self.auto.isChecked():
            self.auto_play(video)
        elif not video:
            self.last_auto = ""

    def load_finished(self, success):
        if not success and self.web.url().scheme() in ("http", "https"):
            self.status.setText("网页加载失败，请检查网络连接。")

    def update_click_script(self, *_args):
        if not hasattr(self, "page"):
            return
        for old in self.page.scripts().find("wallpaper-video-click"):
            self.page.scripts().remove(old)
        script = QWebEngineScript()
        script.setName("wallpaper-video-click")
        script.setInjectionPoint(QWebEngineScript.InjectionPoint.DocumentReady)
        script.setWorldId(QWebEngineScript.ScriptWorldId.MainWorld)
        script.setRunsOnSubFrames(False)
        script.setSourceCode(CLICK_SCRIPT.replace("AUTO", "true" if self.auto.isChecked() else "false"))
        self.page.scripts().insert(script)
        self.page.runJavaScript("window.__muralopToDesktop = " + ("true" if self.auto.isChecked() else "false"))

    def cookie_added(self, cookie):
        if not session_domain(cookie.domain()):
            return
        key = (cookie.domain(), cookie.path(), bytes(cookie.name()))
        self.cookies[key] = {"name": bytes(cookie.name()).decode("utf-8"), "value": bytes(cookie.value()).decode("utf-8"),
            "domain": cookie.domain(), "path": cookie.path() or "/", "secure": cookie.isSecure(),
            "expires": 0 if cookie.isSessionCookie() else cookie.expirationDate().toSecsSinceEpoch(),
            "http_only": cookie.isHttpOnly()}

    def cookie_removed(self, cookie):
        self.cookies.pop((cookie.domain(), cookie.path(), bytes(cookie.name())), None)

    def export_cookies(self):
        write_youtube_cookies(self.cookie_file, list(self.cookies.values()))

    def import_session(self):
        self.import_button.setEnabled(False)
        self.status.setText("正在读取所选浏览器的会话…")
        browser, profile = self.browser.currentData(), self.browser_profile.text().strip()
        self.jobs.submit("import", lambda: read_browser_cookies(browser, profile))

    def import_cookie_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "导入 Netscape 格式 Cookie 文件", "", "Cookie 文件 (*.txt);;所有文件 (*)")
        if path:
            self.jobs.submit("import", lambda: read_cookie_file(path))

    def install_cookies(self, records):
        if not records:
            self.status.setText("文件中没有可用的 YouTube 登录会话。")
            return
        for record in records:
            cookie = QNetworkCookie(record["name"].encode(), record["value"].encode())
            cookie.setDomain(record["domain"])
            cookie.setPath(record["path"])
            cookie.setSecure(record["secure"])
            cookie.setHttpOnly(record["http_only"])
            if record["expires"]:
                cookie.setExpirationDate(QDateTime.fromSecsSinceEpoch(record["expires"]))
            self.profile.cookieStore().setCookie(cookie, QUrl("https://" + record["domain"].lstrip(".") + record["path"]))
        # Give WebEngine time to commit the asynchronous setCookie operations.
        QTimer.singleShot(500, lambda: self.web.setUrl(QUrl("https://www.youtube.com/")))
        self.status.setText("已导入会话，正在刷新 YouTube。登录是否有效以网页显示为准。")

    def closeEvent(self, event):
        self.ready = False
        self.timer.stop()
        self.settings.setValue("auto_play", self.auto.isChecked())
        self.settings.setValue("browser", self.browser.currentData())
        self.settings.setValue("browser_profile", self.browser_profile.text())
        self.settings.setValue("extractor", self.extractor.text())
        self.settings.setValue("quality", self.quality.currentIndex())
        self.jobs.pool.shutdown(wait=False, cancel_futures=True)
        event.accept()  # The daemon keeps playing and remains available in the tray.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend")
    parser.add_argument("--smoke-test", metavar="SCREENSHOT")
    options = parser.parse_args()
    app = QApplication(sys.argv[:1])
    app.setApplicationName("Muralop")
    app.setOrganizationName("Muralop")
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    signal.signal(signal.SIGTERM, lambda *_: app.quit())
    # Wake Python regularly while Qt runs its native event loop so Ctrl+C
    # reaches the handlers even before the backend's status timer starts.
    signal_timer = QTimer(app)
    signal_timer.timeout.connect(lambda: None)
    signal_timer.start(200)
    if sys.platform == "win32":
        # The offscreen test platform has no system font database on Windows.
        for filename in ("segoeui.ttf", "msyh.ttc"):
            font = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / filename
            if font.exists():
                QFontDatabase.addApplicationFont(str(font))
        app.setFont(QFont("Microsoft YaHei", 10))
    data_dir = Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.AppLocalDataLocation))
    if options.smoke_test:
        import tempfile
        temporary = tempfile.TemporaryDirectory(prefix="muralop-smoke-")
        data_dir = Path(temporary.name)
    data_dir.mkdir(parents=True, exist_ok=True)
    server = QLocalServer(app)
    if not options.smoke_test:
        server_name = "muralop-ui-" + str(os.getpid() if sys.platform == "darwin" else os.environ.get("USERNAME", os.environ.get("USER", "user")))
        socket = QLocalSocket()
        socket.connectToServer(server_name)
        if socket.waitForConnected(300):
            socket.write(b"show")
            socket.waitForBytesWritten(300)
            return 0
        QLocalServer.removeServer(server_name)
        if not server.listen(server_name):
            raise RuntimeError(server.errorString())
    backend = None if options.smoke_test else Backend(locate_backend(options.backend))
    window = MainWindow(backend, data_dir, bool(options.smoke_test))
    server.newConnection.connect(lambda: (server.nextPendingConnection().deleteLater(), window.showNormal(), window.raise_(), window.activateWindow()))
    window.show()
    if options.smoke_test:
        def capture():
            window.grab().save(str(Path(options.smoke_test).resolve()))
            window.close()
        QTimer.singleShot(1800, capture)
    result = app.exec()
    # Destroy pages before their profile (WebEngine checks this ordering).
    window.web.setPage(QWebEnginePage(window.web))
    window.page.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
