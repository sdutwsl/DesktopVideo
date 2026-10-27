# Electron 主界面

运行 `muralop-ui.exe`（Windows）、`muralop-ui`（Linux）或 `nix run . -- ui`。
主界面使用 Electron，C++/libmpv 壁纸核心作为独立进程插件接入。

点击视频后立即显示 **解析中**，取得播放地址后显示 **加载中**，只有播放器报告媒体开始运行才显示 **播放中**。
窗口标题同步更新；界面同时显示任务耗时，网页上方也会保留当前播放状态。
合集列表读取、单视频解析和播放器加载各有 60 秒上限（播放核心会自动重试中断的视频连接），可以随时点击取消。重新选择视频会取消上一个任务。

YouTube 网页、控制界面、yt-dlp 解析进程、播放核心分别运行。
解析和后端通信都可以独立取消与超时；后端失联不会阻止关闭界面。
最小化或点击关闭按钮会保留网页、浏览位置和当前播放任务；关闭按钮将界面隐藏到托盘，再次打开不会重新加载网页。
托盘图标统一由播放后端提供，使用它的“打开主界面”恢复窗口。界面底部的“退出界面”会真正退出界面并取消尚未完成的解析，已开始播放的壁纸继续运行。

## 使用

- 可以浏览 YouTube、粘贴视频网址或选择本地视频，并指定显示器。
- 点击或粘贴 YouTube 合集链接会读取合集全部可用视频；可从下拉列表选择任意条目播放，或用“上一个 / 下一个”切换。每次只解析所选视频的播放地址。
- 勾选“点击网页视频，直接播放到桌面”后，点击视频缩略图即可直接播放；取消勾选后可以正常预览。
- 网页默认静音，避免与壁纸声音重复。
- 网页加载或进程错误显示在地址栏下方，不覆盖视频的解析、加载状态；网页进程异常后可点击刷新恢复。
- 网页使用 Muralop 自己的持久会话，不会占用现有浏览器配置。
- YouTube 对部分网络要求登录才能解析（提示“要求登录后才能解析”）。可在右侧网页直接登录 Google 账号，之后每次解析都会使用该登录会话。
- 导入会话会替换内嵌网页现有的登录。Google 提示“Cookie 设定有问题”时，点“清除登录会话”后重新登录。
- 可通过独立的会话导入工具导入 Chrome、Edge、Firefox、Chromium、Brave 或 cookies.txt。
- 浏览器的加密、数据库占用或 Google 的登录限制可能使会话导入失败；错误会显示在界面中。
- 第一次从旧版升级时，先退出旧版播放后端；旧进程不会自动替换成新二进制。

只有原始视频网址会保存到壁纸配置。解析产生的签名流地址只保留在播放进程内存中。
应用数据目录（Linux `~/.config/muralop/desktop`，Windows `%APPDATA%\Muralop\desktop`）中的 `youtube-cookies.txt`、`Partitions` 包含登录信息，不要分享或加入 Git。

## 开发

NixOS 可直接运行：

```sh
nix run . -- ui
```

其他环境先构建 C++ 后端，再安装 Electron：

```sh
cd desktop
npm ci
npm start -- --backend /absolute/path/to/muralop
```

需要可运行的 yt-dlp 2026.10.21 和 Deno。可以通过 `MURALOP_YTDLP` 指定解析器，或在界面设置路径。
开发环境的会话导入可使用 `MURALOP_SESSION_HELPER` 指向已打包的 `muralop-session`；Nix 和便携包已包含该工具。

```sh
npm test
# 从仓库根目录打包（Windows/Linux x64，与当前 Electron 运行平台一致）
python -m pip install -r desktop/requirements.txt pyinstaller
python desktop/package.py --backend-dir /path/to/backend
```

## 播放插件协议

主界面启动 `muralop --background`，通过短期子进程 `muralop rpc` 通信。
stdin 是一个以换行分隔的请求，stdout 返回一条 JSON；本机 D-Bus / Windows 命名管道的客户端藏在该子进程中。
主界面为每次调用设置 4 秒上限。

`status` 返回显示器、音量、暂停状态、错误和 `playback`（`idle`、`loading`、`playing`、`error`）。
`play-resolved` 的字段依次是原始网址、视频流网址、音频流网址（可空）、User-Agent、Referer、显示器（可空）。
`set` 用于本地文件，`stop [显示器]` 取消加载或停止该显示器的播放；`online` 同步 yt-dlp 路径、Cookie 文件和画质；
还支持 `pause`、`resume`、`mute`、`unmute`、`volume`、`clear` 和 `quit`。

后端只保存原始网址。没有主界面提供的新鲜播放地址时（例如登录后自动启动），后端用自带的 yt-dlp 自行解析。
插件接口集中在 `player.cjs`，可以替换播放后端而不改界面。

旧版 PySide6 界面源码保留在 `gui/`，默认运行和发布入口已切换为 Electron。
