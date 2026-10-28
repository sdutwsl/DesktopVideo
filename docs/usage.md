# 使用

[返回首页](../README.md) · [安装说明](installation.md) · [开发文档](development.md)

## 播放视频

选择目标显示器，再从内嵌网页点击视频、粘贴视频网址，或选择本地文件。可以为不同显示器指定不同视频；出声显示器通过配置中的 `audio_monitor` 指定。

网页左上角的平台小按钮可切换 YouTube、哔哩哔哩、抖音、TikTok、Vimeo、Dailymotion、Twitch。点击视频缩略图直接播放到桌面，网页保留在列表位置；网页默认静音，避免重复出声。其他 yt-dlp 站点使用网址输入框，也可以粘贴包含视频链接的分享文案。

支持的站点取决于所用 yt-dlp 版本，以及站点、登录和地区限制。完整解析器列表见 [yt-dlp 支持列表](https://github.com/yt-dlp/yt-dlp/blob/master/supportedsites.md)。macOS 使用菜单栏选择本地视频，暂未提供内嵌站点浏览。

### 播放状态

点击后依次显示“解析中 → 加载中 → 播放中”，窗口标题同步更新。只有播放器确认开始播放才显示“播放中”；动画表示正在处理，不代表下载百分比。

解析和加载时可点击取消，重新选择视频会取消旧任务。单视频解析和加载各有 60 秒上限；合集列表连续 60 秒没有新输出会超时。网页错误单独显示，网页加载超过 8 秒会给出提示和重新加载入口。

### 合集

点击或粘贴 YouTube 合集链接后，列表逐条显示可用视频，可从下拉列表选择条目，或使用“上一个 / 下一个”。链接已选定视频时优先播放该视频；纯合集链接读到首个可用视频后开始播放，其余列表在后台补齐。

每次只解析所选视频的播放地址；后续列表读取失败不会中断已开始的播放。完整列表在内存中缓存 10 分钟，最多保存 10 个合集。导入或清除会话、更换解析器后会清空缓存；签名播放流地址不参与缓存。

## 登录会话

应用使用自己的持久网页会话，不占用浏览器的配置目录。可在内嵌网页中登录，或一次导入 Chrome、Edge、Firefox、Chromium、Brave 的配置或 Netscape 格式 `cookies.txt`，切换已接入平台后无需逐个导入。

导入只替换本次读到会话的平台，其余平台已有会话会保留。“清除登录会话”会退出所有内嵌站点。浏览器加密、数据库占用或站点的嵌入式登录限制可能使导入失败，原因会显示在界面中。

应用数据目录为 Linux 的 `~/.config/muralop/desktop`、Windows 的 `%APPDATA%\Muralop\desktop`。其中 `youtube-cookies.txt` 沿用旧文件名，包含已接入平台的会话；该文件和 `Partitions` 目录包含登录信息，不要分享或加入 Git。

## 窗口、托盘与诊断

顶部工具栏和左侧品牌区域可拖动窗口。最小化或关闭窗口会保留网页、浏览位置和播放任务；关闭按钮将窗口隐藏到托盘，再次打开沿用原窗口。使用托盘中唯一的“退出”结束应用、播放插件及解析任务。

播放插件异常退出或失去响应时，窗口和托盘会保留，可点击“重启播放”。自动硬解保持启用。

“关于与诊断”显示应用版本、文件标识、运行位置、插件位置和最近 20 条错误，可复制诊断信息或清空记录。错误保存在应用数据目录的 `errors.json`；复制内容不含播放源和登录会话，网址以及 Cookie / Authorization 请求头会隐藏。

各项修复的实机验证状态及已知问题见 [更新日志](../CHANGELOG.md)。

## 媒体键与 playerctl

Linux 提供 MPRIS 2 接口，播放器名称为 `muralop`：

```sh
playerctl -p muralop status
playerctl -p muralop play-pause
playerctl -p muralop play
playerctl -p muralop pause
playerctl -p muralop stop
playerctl -p muralop volume 0.5
playerctl -p muralop position 10+
playerctl -p muralop position 30
playerctl -p muralop metadata --format '{{title}}'
playerctl -p muralop next
playerctl -p muralop previous
playerctl -p muralop open 'file:///path/to/video.mp4'
```

`volume` 的 0～1 对应应用的 0～100，非零音量会取消静音。`position 10+` 前进 10 秒，`position 30` 跳到第 30 秒，仅在媒体可跳转时生效。停止会取消未完成的解析，再播放会从头打开当前视频。

标题、时长和进度跟随出声显示器；播放、暂停、停止、音量和跳转控制所有显示器。上一条 / 下一条只在当前出声视频属于已读取的合集、且存在对应条目时启用，沿用该次播放的显示器目标。独立原生播放器不提供合集切换。

服务名为 `org.mpris.MediaPlayer2.muralop`，对象为 `/org/mpris/MediaPlayer2`，随应用启动和退出，不增加系统服务。媒体信息只公开原始网址或本地文件 URI，不公开签名流地址。KDE 等桌面的媒体控件使用同一接口；当前不支持倍速、随机播放或切换循环模式。

## Linux 命令行

```sh
muralop                     # 完整包打开应用，原生包启动托盘播放器
muralop ~/Videos/a.mp4       # 启动并使用该视频
muralop set a.mp4 DP-2       # 给 DP-2 换视频
muralop ui                  # 在终端前台打开主窗口
muralop pause
muralop resume
muralop mute
muralop unmute
muralop volume 40
muralop monitors
muralop quit
```

`MURALOP_DEBUG=1 muralop` 可打印渲染及 mpv 日志。应用播放插件和原生播放器都提供会话 D-Bus 控制接口 `org.muralop.Daemon`；Sway 可在 `swayidle` 的 lock/unlock 钩子中调用 `muralop pause` / `muralop resume`。

## 壁纸配置

| 平台 | 配置路径 |
|---|---|
| Linux | `~/.config/muralop/config.ini` |
| Windows | `%APPDATA%\Muralop\config.ini`，原生便携版优先使用程序旁的 `config.ini` |
| macOS | `~/Library/Application Support/Muralop/config.ini` |

```ini
video=/path/default.mp4
video.DP-2=/path/other.mp4
volume=50
mute=0
pause_when_locked=1
audio_monitor=DP-1
```

`video` 是默认视频，`video.<显示器名>` 覆盖单个显示器；Windows 显示器名形如 `\\.\DISPLAY1`，macOS 使用显示器名称。`audio_monitor` 留空时主屏出声。配置只保存原始视频网址或文件路径，解析后的签名流地址只驻留在播放进程内存中。
