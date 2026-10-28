# Muralop

[![build](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml/badge.svg)](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml)

把本地视频和在线视频设为桌面壁纸，支持 Linux Wayland、Windows 和 macOS。

## 功能

- 多显示器分别设置视频，选择出声显示器，循环播放并裁切铺满。
- 浏览 YouTube、哔哩哔哩、抖音、TikTok、Vimeo、Dailymotion、Twitch，点击视频直接播放到桌面；其他 yt-dlp 站点可粘贴链接解析。
- 读取 YouTube 合集并选择条目，后台补齐列表，支持上一条 / 下一条。
- 一次导入多个平台的登录会话，支持浏览器配置和 `cookies.txt`。
- 托盘控制播放、静音和退出；隐藏窗口后保留网页与播放任务。
- Linux 支持 MPRIS 和 `playerctl`，可通过桌面媒体控件控制播放。

## 安装与运行

从 [Releases](https://github.com/sdutwsl/Muralop/releases/latest) 下载正式版本：

| 平台 | 推荐入口 |
|---|---|
| Linux Wayland（Nix） | `nix run github:sdutwsl/Muralop -- ui` |
| Ubuntu 26.04 x64 | 解压 `muralop-v*-linux-gui-x64.tar.gz`，运行 `muralop-ui/muralop-ui` |
| Windows x64 | 解压 `muralop-v*-windows-x64-gui.zip`，运行 `muralop-ui.exe` |
| macOS 15+（Apple Silicon） | 打开 `muralop-v*-macos-arm64.dmg`，将应用拖入“应用程序” |

Linux 需要支持 layer-shell 的 Wayland 合成器，例如 KDE Plasma 6、Sway、Hyprland 或 niri；不支持 GNOME。Linux GUI 归档还需要兼容的动态库和 Chromium 沙箱配置。

其他安装包、Windows ARM64、运行依赖及 NixOS / home-manager 配置见 [安装说明](docs/installation.md)。macOS 使用原生菜单栏界面，暂未提供内嵌站点浏览。

## 使用

1. 选择播放目标显示器。
2. 在内嵌网页点击视频，或粘贴链接、选择本地文件。
3. 按需调整音量、画质和暂停状态；合集可直接切换条目。

需要登录时，可在网页中登录，或导入浏览器 / `cookies.txt` 会话。关闭窗口会隐藏到托盘；使用托盘中的“退出”结束整个应用。

详细操作、媒体键、命令行、配置与诊断见 [使用说明](docs/usage.md)。

## 开发与构建

从源码运行完整应用：

```sh
nix run . -- ui
```

源码入口、各平台构建、便携打包及提交推送流程见 [开发文档](docs/development.md)。版本历史和 Feature / Fix 清单见 [更新日志](CHANGELOG.md)。
