# Muralop 快速使用

Windows 运行 `muralop-ui.exe`，Linux 运行 `muralop-ui`。在源码仓库中可用 `nix run . -- ui` 启动完整应用。

1. 选择目标显示器，在内嵌网页点击视频，或粘贴链接、选择本地文件。
2. 通过网页左上角的平台小按钮切换站点；需要登录时可直接登录，或一次导入浏览器 / `cookies.txt` 会话。
3. YouTube 合集会逐条列出可用视频，可选择条目或使用“上一个 / 下一个”。
4. 关闭窗口会隐藏到托盘并保留当前网页和播放任务；通过托盘中的“退出”结束整个应用。

界面依次显示“解析中 → 加载中 → 播放中”，处理期间可取消或重新选择视频。播放插件异常时可点击“重启播放”。“关于与诊断”可查看版本、运行位置和最近错误，并复制诊断信息。

Linux 支持 `playerctl -p muralop`，例如 `playerctl -p muralop play-pause` 或 `playerctl -p muralop volume 0.5`。Linux GUI 归档需要兼容的 libmpv、Wayland、EGL 和 sdbus-c++ 2 动态库及 Chromium 沙箱配置；具体见安装文档。

应用数据目录中的 `youtube-cookies.txt` 和 `Partitions` 包含登录信息，不要分享或加入 Git。

详细文档：

- [安装与运行依赖](https://github.com/sdutwsl/Muralop/blob/master/docs/installation.md)
- [使用、登录会话、媒体控制和配置](https://github.com/sdutwsl/Muralop/blob/master/docs/usage.md)
- [开发、构建和打包](https://github.com/sdutwsl/Muralop/blob/master/docs/development.md)
- [更新日志与已知问题](https://github.com/sdutwsl/Muralop/blob/master/CHANGELOG.md)
