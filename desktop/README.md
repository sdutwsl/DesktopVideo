# Electron 主界面

运行 `muralop-ui.exe`（Windows）、`muralop-ui`（Linux）或 `nix run . -- ui`。
主界面使用 Electron，C++/libmpv 壁纸能力通过 `muralop.node` 原生 Node-API 插件接入，作为同一个应用管理（本次重构待构建和实播验证）。

点击视频后立即显示 **解析中**，取得播放地址后显示 **加载中**，只有播放器报告媒体开始运行才显示 **播放中**。
窗口标题同步更新；界面同时显示任务耗时，网页上方也会保留当前播放状态。
状态卡显示“解析视频 → 加载画面 → 桌面播放”三个实际阶段；网页初次打开或切换站点时显示骨架屏，超过 8 秒仍在加载会提示并提供重新加载入口。进度动画表示正在处理，不代表下载百分比。
播放、状态切换和控件操作带有轻量动效，窗口隐藏或最小化时暂停界面动画；系统请求减少动态效果时关闭动画。
单视频解析和播放器加载各有 60 秒上限；合集列表连续 60 秒没有新输出会超时（播放核心会自动重试中断的视频连接），可以随时点击取消。重新选择视频会取消上一个播放任务；选择同一合集已有条目时复用后台列表读取。

网页、解析任务和播放均由应用管理。原生插件加载于应用拥有的宿主进程，渲染运行在专用线程；每次查询状态不再创建 RPC 子进程。解析和插件请求可以取消与超时。
最小化或点击关闭按钮会保留网页、浏览位置和当前播放任务；关闭按钮隐藏到托盘，再次打开沿用原窗口。
应用只创建一个托盘和其中唯一的“退出”入口，退出会结束插件、窗口、网页和解析任务；插件停止超过 3 秒则终止其宿主进程。
播放插件崩溃或失去响应时保留窗口和托盘，显示原因并提供“重启播放”；继续使用 `hwdec=auto-safe`，没有改为软解。
“关于与诊断”显示一个应用版本、文件标识、实际运行位置、插件位置及最近 20 条错误，支持复制诊断信息和清空记录。错误保存在应用数据目录的 `errors.json`；复制时只包含诊断字段，网址及 Cookie / Authorization 请求头会被隐藏，不复制播放源或登录会话。

## 使用

- 点击网页左上角的平台小按钮，弹出列表切换 YouTube、哔哩哔哩、抖音、TikTok、Vimeo、Dailymotion、Twitch；点击视频链接即可设为壁纸。Twitch 的点击识别涵盖录像和剪辑。
- 主窗口使用无边框布局，顶部工具栏和左侧品牌区域可拖动；右上角提供最小化、最大化和隐藏到托盘操作。
- 网址输入框接受 yt-dlp 支持的其他站点，也可以选择本地视频，并指定显示器。
- 点击或粘贴 YouTube 合集链接会读取合集全部可用视频；可从下拉列表选择任意条目播放，或用“上一个 / 下一个”切换。每次只解析所选视频的播放地址。
- 合集列表逐条显示：含视频 ID 的链接直接解析所选视频，纯合集链接读到首个可用视频后开始播放，其余列表在后台补齐。界面显示已读取数量，后续列表读取失败也不会中断已开始的视频播放。
- 完整合集列表在内存中缓存 10 分钟，最多保存 10 个合集；切换画质重新解析视频流时仍可复用列表。导入或清除登录会话、更换解析器后会清空缓存。签名播放地址不参与该缓存。
- 点击网页中的视频缩略图即可直接播放到桌面，网页保留在列表位置。
- 网页默认静音，避免与壁纸声音重复。
- 网页加载或进程错误显示在网页上方，不覆盖视频的解析、加载状态；仅在网页异常时显示“重新加载网页”按钮。
- 网页使用 Muralop 自己的持久会话，不会占用现有浏览器配置。
- 部分站点或视频要求登录才能解析。可在右侧网页中登录，解析时会带上对应站点的会话。
- 一次导入所选浏览器配置或 cookies.txt 中所有已接入平台的登录会话，切换平台后直接使用。导入只替换本次读到会话的平台，其余平台已有登录保留；“清除登录会话”会退出所有内嵌站点。
- 可通过独立的会话导入工具导入 Chrome、Edge、Firefox、Chromium、Brave 或 cookies.txt。
- 浏览器的加密、数据库占用或 Google 的登录限制可能使会话导入失败；错误会显示在界面中。
- 从旧的独立播放器升级时，需要先退出旧实例，避免它继续占用同一播放控制接口。

只有原始视频网址会保存到壁纸配置。解析产生的签名流地址只保留在播放进程内存中。
应用数据目录（Linux `~/.config/muralop/desktop`，Windows `%APPDATA%\Muralop\desktop`）中的 `youtube-cookies.txt`（沿用旧文件名，包含已接入视频站点的会话）、`Partitions` 包含登录信息，不要分享或加入 Git。

完整站点列表见 [yt-dlp 官方支持列表](https://github.com/yt-dlp/yt-dlp/blob/master/supportedsites.md)。内嵌点击入口覆盖上面列出的站点；其他站点使用网址输入框。实际解析取决于所捆绑的 yt-dlp 版本、站点变化、登录和地区限制，不能将支持列表视为每个视频均可播放的保证。

## 开发

NixOS 可直接运行：

```sh
nix run . -- ui
```

本地插件构建与运行见 [仓库构建流程](../README.md#开发与构建流程)。开发时通过 `--player-plugin /absolute/path/to/muralop.node` 或 `MURALOP_PLAYER_PLUGIN` 指定插件；完整包默认从应用目录加载。Linux 的 `MURALOP_NODE` 可指定纯 Node 运行时；Nix 已设置，便携包已携带，源码开发时默认使用 PATH 中的 `node`。
需要可运行的 yt-dlp 2026.10.21 和 Deno。可以通过 `MURALOP_YTDLP` 指定解析器，或在界面设置路径。
开发环境的会话导入可使用 `MURALOP_SESSION_HELPER` 指向已打包的 `muralop-session`；Nix 和便携包已包含该工具。

从仓库根目录打包（Windows/Linux x64，插件架构与 Electron 一致）：

```sh
python -m pip install -r desktop/requirements.txt pyinstaller
python desktop/package.py --player-dir build/desktop
```

该目录必须包含 CMake 生成的 `muralop.node` 和 `package.json`，Windows 还需 `libmpv-2.dll`。Linux 打包时还会复制当前构建环境的纯 Node 可执行文件为 `node`；不再复制独立播放程序。

## 原生播放插件

`src/node/PlayerAddon.cpp` 导出 `start(configPath, legacyPath)`、`request(command)` 和 `close()`。这些同步 Node-API 入口只在专用宿主进程中调用，不阻塞应用窗口。插件内部将命令排到原生渲染线程，所有显示器、mpv 实例和平台连接均在该线程创建、使用和释放。

`desktop/player.cjs` 统一管理插件宿主、请求取消和超时；启动上限 15 秒，普通请求 4 秒，退出清理 3 秒。`desktop/player-host.cjs` 只负责加载模块和传递结果；应用退出时一并回收宿主。
Linux 通过随应用提供的纯 Node 运行时及 Node IPC 加载插件，隔离 Electron 内置 FFmpeg 的同名符号和内存分配器；不直接在 Electron 中加载 libmpv，也不对整个依赖树进行深度绑定。Windows 继续使用 Electron utility process。两者共用相同的插件命令接口和退出清理。

内部命令复用 `AppControl`：`status`、`play-resolved`、`set`、`stop`、`online`、`pause`、`resume`、`mute`、`unmute`、`volume`、`clear`、`quit`。命令参数使用换行分隔，返回 JSON；这是插件内的调用格式，不再由外部 `muralop rpc` 转发。
`status` 返回显示器、音量、暂停状态、错误和 `playback`（`idle`、`loading`、`playing`、`error`）。`play-resolved` 依次接收原始网址、视频流网址、音频流网址、User-Agent、Referer、显示器。配置只保存原始网址；签名流地址只在播放期间驻留内存。

Linux 插件复用 layer-shell/EGL、D-Bus 锁屏和休眠通知，并保留已有命令行控制接口；Windows 插件复用 WorkerW 和会话/电源消息处理，不创建原生托盘。独立原生发行包继续使用原有平台外壳；旧版 PySide6 界面保留在 `gui/`，当前应用入口为 Electron。
