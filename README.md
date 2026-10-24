# DesktopVideo / VideoWallpaper

[![build](https://github.com/sdutwsl/DesktopVideo/actions/workflows/build.yml/badge.svg)](https://github.com/sdutwsl/DesktopVideo/actions/workflows/build.yml)

把视频当作桌面壁纸播放。一套 C++ 核心（libmpv），两个原生外壳：

| 平台 | 壁纸层 | 托盘 | 状态 |
|---|---|---|---|
| **Linux Wayland** | `wlr-layer-shell` background 层 + 自绘（EGL/GLES2） | StatusNotifierItem（D-Bus） | KDE Plasma 6 上验证通过 |
| **Windows** | Explorer 的 `WorkerW`（桌面图标之下） | `Shell_NotifyIcon` | 从 v1 移植，MinGW 构建由 CI 覆盖，尚未在真机验证 |

A video wallpaper player: shared libmpv core, native shells for Wayland (layer-shell) and Windows (WorkerW).

## 功能

- 主界面：内嵌 YouTube 网页，点击视频直接播放到桌面；支持网址输入、本地文件、多屏选择和播放控制
- yt-dlp 在线解析，支持导入 Chrome / Edge / Firefox 等浏览器的登录 Cookie，内嵌网页独立保存登录状态

- 多显示器：每个显示器一个播放器，可分别指定视频；只有主屏出声
- 循环播放、按屏幕比例裁切铺满
- 锁屏、休眠时暂停，关机前退出
- 托盘菜单：选择视频、暂停、静音、退出；Linux 另有命令行 / D-Bus 控制
- 配置持久化（`key=value` 文本文件）

## 主界面与 YouTube

Windows 完整包运行 `videowallpaper-ui.exe`；Linux GUI 便携包运行 `videowallpaper-ui`，
Nix 包运行 `videowallpaper ui`。主界面会自动启动 C++ 后端，关闭窗口后壁纸继续播放。
Ubuntu 的 `.deb` 提供后端，图形界面另见同一 Release 的 `linux-gui-x64.tar.gz`。

内嵌网页支持在结果列表点击视频后直接投到桌面，也可以取消“点击网页视频”选项，进入视频后点“播放当前视频”。
会话导入使用 yt-dlp，**不能保证读取 Windows Chrome/Edge 的加密 Cookie**；遇到限制可以导入 `cookies.txt`，
或在应用自己的网页里登录。应用不会直接复用或修改正在运行的浏览器配置目录。

开发运行、Cookie 保存位置和打包方式见 [gui/README.md](gui/README.md)。

## Linux（Wayland）

### 合成器支持

需要合成器实现 `zwlr_layer_shell_v1`：

| 合成器 | 壁纸 |
|---|---|
| KDE Plasma 6 (KWin) | ✅ |
| Sway / Hyprland / river / niri 等 wlroots 系 | ✅ |
| GNOME (Mutter) | ❌ 不支持 layer-shell |

GNOME 用户请用 [Hanabi](https://github.com/jeffshee/gnome-ext-hanabi) 之类的 Shell 扩展，或走 XWayland 方案（本项目未实现）。

托盘图标需要面板支持 StatusNotifierItem（Plasma、Waybar、GNOME 的 AppIndicator 扩展）。

### 构建

Ubuntu 24.04 可以直接装 [Releases](https://github.com/sdutwsl/DesktopVideo/releases) 里的 `.deb`（`sudo apt install ./videowallpaper-*.deb`，依赖会自动装上）。

依赖：`libmpv`、`wayland-client`、`wayland-egl`、`egl`、`glesv2`、`sdbus-c++`、`wayland-scanner`、CMake ≥ 3.20、C++20 编译器。

NixOS / 装了 Nix：

```sh
nix develop            # 进入带全部依赖的 shell
cmake -S . -B build -G Ninja && ninja -C build
```

Debian/Ubuntu：

```sh
sudo apt install cmake ninja-build pkg-config libmpv-dev libwayland-dev libegl-dev libgles-dev libsdbus-c++-dev libwayland-bin g++
cmake -S . -B build -G Ninja && ninja -C build
sudo ninja -C build install   # 可选，装到 /usr/local
```

### 使用

```sh
videowallpaper                    # 启动守护进程（托盘里选视频）
videowallpaper ~/Videos/a.mp4     # 启动并使用该视频
videowallpaper set a.mp4 DP-2     # 只给 DP-2 换视频
videowallpaper set 'https://www.youtube.com/watch?v=VIDEO_ID' # 网络视频（需要 yt-dlp）
videowallpaper ui                 # 打开已安装的图形界面
videowallpaper pause | resume | mute | unmute | volume 40 | monitors | quit
VW_DEBUG=1 videowallpaper         # 打印渲染和 mpv 的详细日志
```

守护进程在 session bus 上注册 `org.videowallpaper.Daemon`，也可以直接用 `busctl --user call ...` 控制；
Sway 用户可以在 `swayidle` 的 lock/unlock 钩子里调 `videowallpaper pause/resume`。

开机自启：把 `data/videowallpaper.desktop` 复制到 `~/.config/autostart/`。

### 在 NixOS / home-manager 中引用

```nix
# flake.nix
inputs.videowallpaper = {
  url = "github:sdutwsl/DesktopVideo";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

```nix
# NixOS 模块：装包，并作为 systemd 用户服务随图形会话启动
imports = [ inputs.videowallpaper.nixosModules.default ];
programs.videowallpaper.enable = true;
# programs.videowallpaper.autostart = false;   # 只装包，不自启
```

home-manager 用户换成 `inputs.videowallpaper.homeModules.default`，选项相同。
只要包本身的话：`inputs.videowallpaper.overlays.default`（提供 `pkgs.videowallpaper`）或
`inputs.videowallpaper.packages.${system}.default`；临时试用：`nix run github:sdutwsl/DesktopVideo`。

模块默认用系统自己的 `pkgs` 编译，保证和 `/run/opengl-driver` 里的显卡驱动用同一份 glibc；
否则会在启动时报 `EGL initialisation failed`。

## Windows

依赖：MinGW-w64（GCC ≥ 13），CMake，libmpv 的 Windows 开发包。不支持 MSVC：libmpv 的 Windows 包只带 MinGW 导入库。

现成的 exe：[Releases](https://github.com/sdutwsl/DesktopVideo/releases) 里的 `videowallpaper-v*-windows-x64.zip`（`videowallpaper.exe` + `libmpv-2.dll`，解压即用）；master 每次提交的构建在 [Actions](https://github.com/sdutwsl/DesktopVideo/actions/workflows/build.yml) 的 `videowallpaper-windows-x64` 产物里。

自己构建：

1. 从 <https://sourceforge.net/projects/mpv-player-windows/files/libmpv/> 下载 `mpv-dev-x86_64-*.7z` 并解压（含 `include/`、`libmpv.dll.a`、`libmpv-2.dll`；CI 固定用 `mpv-dev-x86_64-20261018-git-01e2533f31.7z`）。
2. 构建（MSYS2 的 MinGW64 环境，或在 Linux 上 `nix develop .#mingw` 交叉编译）：
   ```sh
   cmake -S . -B build-win -G Ninja -DMPV_SDK_DIR=/path/to/mpv-dev \
         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake   # 仅交叉编译时需要
   ninja -C build-win
   ```
   `libmpv-2.dll` 会自动复制到输出目录。
3. 运行 `videowallpaper.exe`，右键托盘图标选择视频。把视频文件拖到 exe 上也可以。

## 配置文件

Linux：`~/.config/videowallpaper/config.ini`；Windows：`%APPDATA%\VideoWallpaper\config.ini`。

```ini
video=/path/default.mp4        # 默认视频
video.DP-2=/path/other.mp4     # 某个显示器的覆盖（Windows 里显示器名形如 \\.\DISPLAY1）
volume=50
mute=0
pause_when_locked=1
audio_monitor=DP-1             # 哪个显示器出声，留空用主屏
ytdlp_path=/path/to/yt-dlp     # 在线解析器，留空使用 PATH
cookies_file=/path/cookies.txt # 由主界面维护，只含 YouTube 域的 Cookie
ytdlp_format=                 # 可选的 yt-dlp 画质表达式
```

## 发布

版本号只在 `CMakeLists.txt` 的 `project(VideoWallpaper VERSION x.y.z)` 里写一次，Nix 包和 Windows exe 的版本信息都从这里取。发布新版本：

```sh
# 先改 CMakeLists.txt 里的版本号并提交
git tag v2.1.0 && git push origin v2.1.0
```

CI 跑完三路构建后自动创建 GitHub Release，附上 `videowallpaper-v2.1.0-windows-x64.zip`、`videowallpaper-v2.1.0-ubuntu-24.04-amd64.deb` 和 `SHA256SUMS`，更新说明根据提交记录自动生成。tag 和版本号对不上时发布会失败；`v2.1.0-rc1` 这类带 `-` 的 tag 发布为预发布版。

## 代码结构

```
src/core/       平台无关：libmpv 封装、配置、多屏控制器、菜单模型
src/wayland/    layer-shell 表面 + EGL 渲染、事件循环、SNI 托盘、D-Bus 会话事件
src/win/        WorkerW 表面、Shell_NotifyIcon 托盘、锁屏检测
cmake/          FindMpv、MinGW 工具链
nix/            Nix 包、NixOS / home-manager 模块
.github/        CI：Nix、Ubuntu apt（打 .deb）、MinGW 交叉三路构建，打 tag 时发布 release
Archived/       v1（C# WinForms 客户端 + C++ socket 服务端），仅存档
```

核心通过两个接口和平台打交道：`Platform`（枚举显示器、创建表面）和 `Surface`（把播放器的输出接到一块屏幕上）。
Windows 上 mpv 直接渲染进壁纸窗口（`wid`）；Wayland 上用 mpv 的 render API 画到我们自己的 EGL 表面，传入 `wl_display` 后 VAAPI 硬解的帧可以直接交给 GL。
循环播放、裁切铺满用的是 mpv 选项（`loop-file=inf`、`panscan=1.0`），不读用户的 `mpv.conf`。
