# 主界面与 YouTube

运行 `videowallpaper-ui.exe`（Windows）或 `videowallpaper-ui`（Linux）。程序会启动同目录的壁纸后端。
关闭主界面后视频继续播放，从托盘的“打开主界面”可以再次打开；退出壁纸请使用托盘的“退出”。
升级时请先退出旧版壁纸后端。图形界面需要 Windows 10/11 x64，或支持 Qt WebEngine 的 Linux 桌面。

1. 在右侧内嵌 YouTube 网页里搜索、浏览视频。
2. 勾选“点击网页视频，直接播放到桌面”后，点击视频缩略图即可播放到所选显示器，网页保留在列表位置。
3. 取消勾选可以正常浏览、登录和预览，再点“播放当前视频”。网页声音默认关闭，避免和壁纸声音重叠。
4. 也可以粘贴视频网址、选择本地视频，或按显示器设置不同视频。

## 登录与现有浏览器会话

主界面的 WebEngine 使用应用自己的持久配置目录，不会直接占用、修改 Chrome/Edge 的用户配置目录。
可以选择 Chrome、Edge、Firefox、Chromium 或 Brave，再点“导入浏览器登录会话”。配置项可留空，或填写
`Default`、`Profile 1`、Firefox 配置目录等 yt-dlp 支持的配置名称/路径。导入只保留 YouTube 和 Google 登录域的 Cookie。

Windows Chrome/Edge 的数据库占用、应用绑定加密可能阻止读取，应用会显示失败信息，不会关闭浏览器或修改其安全设置。
可改用浏览器导出的 Netscape 格式 `cookies.txt`，或在内嵌网页登录。Google 有时拒绝嵌入式浏览器登录，导入会话是此时的替代方式。
会话是否有效以 YouTube 页面显示为准；Cookie 可能过期或轮换，需要重新导入。仅支持账号本来有权观看的视频，不提供 DRM 解密。

传给 yt-dlp 的临时会话文件只包含 YouTube 域 Cookie，位于应用数据目录的 `youtube-cookies.txt`；Google Cookie 仅供内嵌网页使用。
不要分享这个文件或 `web-profile` 目录，也不要把它们加入 Git。Cookie 不会写入日志。清除登录态时退出程序后删除该应用数据目录。

## 开发运行

先按根目录 README 构建 C++ 后端，然后：

```sh
python -m venv .venv
# Linux: source .venv/bin/activate；Windows: .venv\Scripts\activate
python -m pip install -r gui/requirements.txt
python gui/main.py --backend /absolute/path/to/videowallpaper
```

界面中可以指定 yt-dlp 可执行文件路径。现代 YouTube 解析还需要 Deno；便携包已包含 Deno，开发环境可自行安装。
原始网页地址会被保存在后端配置中，流地址由 mpv 的 ytdl hook 每次重新解析。部分直播流、地区限制或平台变化可能导致解析失败，
状态栏会显示播放器错误；需要时更新 yt-dlp。

```sh
python -m pytest tests/test_session.py
python gui/main.py --smoke-test /absolute/path/to/preview.png
# 可选：打包含后端、QtWebEngine、yt-dlp 和 Deno 的便携包
python -m pip install pyinstaller
python gui/package.py --backend-dir /path/to/backend-and-libmpv
```

Linux 便携 GUI 在 Ubuntu 24.04 构建，仍需系统安装后端的 libmpv、Wayland、EGL、sdbus-c++ 等动态库。
Qt WebEngine、Python 和 yt-dlp 由便携包提供；Nix 版本由 Nix 管理所有依赖。
