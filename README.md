# DesktopVideo / VideoWallpaper

把视频当作桌面壁纸播放。一套 C++ 核心（libvlc），两个原生外壳：

| 平台 | 壁纸层 | 托盘 | 状态 |
|---|---|---|---|
| **Linux Wayland** | `wlr-layer-shell` background 层 + 自绘（EGL/GLES2） | StatusNotifierItem（D-Bus） | KDE Plasma 6 上验证通过 |
| **Windows** | Explorer 的 `WorkerW`（桌面图标之下） | `Shell_NotifyIcon` | 从 v1 移植，MinGW 交叉编译通过 |

A video wallpaper player: shared libvlc core, native shells for Wayland (layer-shell) and Windows (WorkerW).

## 功能

- 多显示器：每个显示器一个播放器，可分别指定视频；只有主屏出声
- 循环播放、按屏幕比例裁切铺满
- 有窗口最大化 / 全屏盖住壁纸时自动暂停（省 CPU），锁屏、休眠时暂停，关机前退出
- 托盘菜单：选择视频、暂停、静音、退出；Linux 另有命令行 / D-Bus 控制
- 配置持久化（`key=value` 文本文件）

## Linux（Wayland）

### 合成器支持

需要合成器实现 `zwlr_layer_shell_v1`：

| 合成器 | 壁纸 | 全屏检测 |
|---|---|---|
| KDE Plasma 6 (KWin) | ✅ | ✅ 通过注入 KWin 脚本（`src/wayland/kwin/coverage.js`） |
| Sway / Hyprland / river / niri 等 wlroots 系 | ✅ | ✅ `wlr-foreign-toplevel-management` |
| GNOME (Mutter) | ❌ 不支持 layer-shell | — |

GNOME 用户请用 [Hanabi](https://github.com/jeffshee/gnome-ext-hanabi) 之类的 Shell 扩展，或走 XWayland 方案（本项目未实现）。

托盘图标需要面板支持 StatusNotifierItem（Plasma、Waybar、GNOME 的 AppIndicator 扩展）。

### 构建

依赖：`libvlc`、`wayland-client`、`wayland-egl`、`egl`、`glesv2`、`sdbus-c++`、`wayland-scanner`、CMake ≥ 3.20、C++20 编译器。

NixOS / 装了 Nix：

```sh
nix develop            # 进入带全部依赖的 shell
cmake -S . -B build -G Ninja && ninja -C build
```

Debian/Ubuntu：

```sh
sudo apt install cmake ninja-build pkg-config libvlc-dev libwayland-dev libegl-dev libgles-dev libsdbus-c++-dev wayland-scanner++
cmake -S . -B build -G Ninja && ninja -C build
sudo ninja -C build install   # 可选，装到 /usr/local
```

### 使用

```sh
videowallpaper                    # 启动守护进程（托盘里选视频）
videowallpaper ~/Videos/a.mp4     # 启动并使用该视频
videowallpaper set a.mp4 DP-2     # 只给 DP-2 换视频
videowallpaper pause | resume | mute | unmute | volume 40 | monitors | quit
VW_DEBUG=1 videowallpaper         # 打印遮挡检测等调试信息
```

守护进程在 session bus 上注册 `org.videowallpaper.Daemon`，也可以直接用 `busctl --user call ...` 控制；
Sway 用户可以在 `swayidle` 的 lock/unlock 钩子里调 `videowallpaper pause/resume`。

开机自启：把 `data/videowallpaper.desktop` 复制到 `~/.config/autostart/`。

## Windows

依赖：Visual Studio 2019+（或 MinGW-w64），CMake，VLC 的 SDK。

1. 从 <https://get.videolan.org/vlc/> 下载 `vlc-3.0.x-win64.7z` 并解压，里面有 `sdk/` 目录。
2. 构建：
   ```bat
   cmake -S . -B build -DLIBVLC_SDK_DIR=C:\path\to\vlc-3.0.21\sdk
   cmake --build build --config Release
   ```
   `libvlc.dll`、`libvlccore.dll` 和 `plugins/` 会自动复制到输出目录。
3. 运行 `videowallpaper.exe`，右键托盘图标选择视频。把视频文件拖到 exe 上也可以。

在 Linux 上交叉编译：`nix develop .#mingw`，然后
`cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake -DLIBVLC_SDK_DIR=...`。

## 配置文件

Linux：`~/.config/videowallpaper/config.ini`；Windows：`%APPDATA%\VideoWallpaper\config.ini`。

```ini
video=/path/default.mp4        # 默认视频
video.DP-2=/path/other.mp4     # 某个显示器的覆盖（Windows 里显示器名形如 \\.\DISPLAY1）
volume=50
mute=0
pause_when_covered=1           # 有最大化/全屏窗口时暂停
pause_when_locked=1
audio_monitor=DP-1             # 哪个显示器出声，留空用主屏
```

## 代码结构

```
src/core/       平台无关：libvlc 封装、帧队列、配置、多屏控制器、菜单模型
src/wayland/    layer-shell 表面 + EGL 渲染、事件循环、SNI 托盘、D-Bus 会话事件、KWin 脚本桥
src/win/        WorkerW 表面、Shell_NotifyIcon 托盘、锁屏/遮挡检测
cmake/          FindLibVLC、MinGW 工具链
Archived/       v1（C# WinForms 客户端 + C++ socket 服务端），仅存档
```

核心通过两个接口和平台打交道：`Platform`（枚举显示器、创建表面）和 `Surface`（把播放器的输出接到一块屏幕上）。
Windows 走 `libvlc_media_player_set_hwnd`；Wayland 因为 libvlc 3 没有 wl_surface 输出，走 `libvlc_video_set_callbacks` 拿 I420 帧自己上传 GL 纹理。
