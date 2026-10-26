# DesktopVideo / VideoWallpaper

[![build](https://github.com/sdutwsl/DesktopVideo/actions/workflows/build.yml/badge.svg)](https://github.com/sdutwsl/DesktopVideo/actions/workflows/build.yml)

把视频当作桌面壁纸播放。一套 C++ 核心（libmpv），三个原生外壳：

| 平台 | 壁纸层 | 托盘 | 状态 |
|---|---|---|---|
| **Linux Wayland** | `wlr-layer-shell` background 层 + 自绘（EGL/GLES2） | StatusNotifierItem（D-Bus） | KDE Plasma 6 上验证通过 |
| **Windows** | Explorer 的 `WorkerW`（桌面图标之下） | `Shell_NotifyIcon` | 从 v1 移植，x64 / ARM64 构建由 CI 覆盖，尚未在真机验证 |
| **macOS** | 桌面层级的无边框窗口（Finder 图标之下）+ CAOpenGLLayer | 菜单栏 `NSStatusItem` | Apple Silicon，CI 的 macOS 虚拟机里冒烟测试通过，尚未在真机验证 |

A video wallpaper player: shared libmpv core, native shells for Wayland (layer-shell), Windows (WorkerW) and macOS (desktop-level windows).

## 功能

- 多显示器：每个显示器一个播放器，可分别指定视频；只有主屏出声
- 循环播放、按屏幕比例裁切铺满
- 锁屏、休眠时暂停，关机前退出
- 托盘 / 菜单栏菜单：选择视频、暂停、静音、退出；Linux 另有命令行 / D-Bus 控制，macOS 菜单里可以设置登录时启动
- 配置持久化（`key=value` 文本文件）

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

现成的包在 [Releases](https://github.com/sdutwsl/DesktopVideo/releases)，依赖会自动装上：

- Ubuntu 26.04 / Debian 13：`sudo apt install ./videowallpaper-*-ubuntu-26.04-amd64.deb`（ARM 机器用 `-arm64.deb`）
- Fedora 44：`sudo dnf install ./videowallpaper-*-fedora-44-x86_64.rpm`（ARM 机器用 `-aarch64.rpm`）

依赖：`libmpv`、`wayland-client`、`wayland-egl`、`egl`、`glesv2`、`sdbus-c++` ≥ 2.0、`wayland-scanner`、CMake ≥ 3.20、C++20 编译器。
sdbus-c++ 2 的 API 和 1.x 不兼容，所以 Ubuntu 24.04（只有 1.4）不能直接构建。

NixOS / 装了 Nix：

```sh
nix develop            # 进入带全部依赖的 shell
cmake -S . -B build -G Ninja && ninja -C build
```

Ubuntu 26.04 / Debian 13：

```sh
sudo apt install cmake ninja-build pkg-config libmpv-dev libwayland-dev libegl-dev libgles-dev libsdbus-c++-dev libwayland-bin g++
cmake -S . -B build -G Ninja && ninja -C build
sudo ninja -C build install   # 可选，装到 /usr/local
```

Fedora：

```sh
sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config mpv-devel wayland-devel libglvnd-devel sdbus-cpp-devel
cmake -S . -B build -G Ninja && ninja -C build
```

`cd build && cpack -G DEB`（或 `-G RPM`）可以自己打包。

### 使用

```sh
videowallpaper                    # 启动守护进程（托盘里选视频）
videowallpaper ~/Videos/a.mp4     # 启动并使用该视频
videowallpaper set a.mp4 DP-2     # 只给 DP-2 换视频
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

[Releases](https://github.com/sdutwsl/DesktopVideo/releases) 里每个版本有 x64 和 ARM64（骁龙等 Windows on ARM 设备）两套：

- **安装包** `videowallpaper-v*-windows-<arch>-setup.exe`：装到 `%LOCALAPPDATA%\Programs\VideoWallpaper`，不需要管理员权限；有开始菜单快捷方式，可选开机自启，可在“设置 → 应用”里卸载。安装和卸载前会让正在运行的壁纸先正常退出。
- **便携版** `videowallpaper-v*-windows-<arch>-portable.zip`：解压出 `VideoWallpaper` 文件夹直接运行。文件夹里的 `config.ini` 让设置保存在它旁边，不写 `%APPDATA%`，也不碰注册表；删掉它就改用 `%APPDATA%\VideoWallpaper\config.ini`。

master 每次提交的构建在 [Actions](https://github.com/sdutwsl/DesktopVideo/actions/workflows/build.yml) 的 `windows-x64` / `windows-arm64` 产物里。安装包没有代码签名，第一次运行 SmartScreen 会提示，点“更多信息 → 仍要运行”。

运行后右键托盘图标选择视频；把视频文件拖到 exe 上也可以。

### 构建

依赖：MinGW-w64（x64 用 GCC ≥ 13，ARM64 用 LLVM/clang），CMake，libmpv 的 Windows 开发包；打安装包还要 NSIS（`makensis`）。不支持 MSVC：libmpv 的 Windows 包只带 MinGW 导入库。

1. 从 <https://sourceforge.net/projects/mpv-player-windows/files/libmpv/> 下载 `mpv-dev-x86_64-*.7z` 或 `mpv-dev-aarch64-*.7z`，用 7-Zip 解压（含 `include/`、`libmpv.dll.a`、`libmpv-2.dll`；ARM64 的包要较新的 7-Zip，p7zip 解不开。CI 固定用 `20261018-git-01e2533f31` 这一版）。
2. 构建。在 Linux 上交叉编译：x64 用 `nix develop .#mingw`，ARM64 用 `nix develop .#mingw-arm64`。在 Windows 上用 MSYS2 的 MinGW64 / CLANGARM64 环境，不需要工具链文件。
   ```sh
   cmake -S . -B build-win -G Ninja -DMPV_SDK_DIR=/path/to/mpv-dev \
         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake   # ARM64：cmake/mingw-aarch64.cmake
   ninja -C build-win                       # videowallpaper.exe，libmpv-2.dll 会复制到旁边
   ninja -C build-win installer portable    # videowallpaper-v<版本>-windows-<arch>-setup.exe / -portable.zip
   ```

## macOS

[Releases](https://github.com/sdutwsl/DesktopVideo/releases) 里的 `videowallpaper-v*-macos-arm64.dmg`：Apple Silicon（M 系列芯片），macOS 15 及以上。
没有 Intel 版：Homebrew 已经不给 Intel Mac 出 mpv 的预编译包。

打开 dmg，把 `VideoWallpaper` 拖进“应用程序”。这个包没有 Apple 开发者签名和公证，第一次打开会提示无法验证开发者：点“完成”，再到“系统设置 → 隐私与安全性”底部点“仍要打开”。
也可以在终端里去掉下载标记：`xattr -dr com.apple.quarantine /Applications/VideoWallpaper.app`。

它只在菜单栏显示一个图标（不进 Dock），点开选择视频、暂停、静音、登录时启动、退出。把视频拖到程序图标上，或在 Finder 里“打开方式 → VideoWallpaper”，也会设为壁纸。
锁屏、切换用户、睡眠和显示器关闭时暂停。

### 构建

```sh
brew install cmake ninja pkgconf mpv dylibbundler
cmake -S . -B build -G Ninja && ninja -C build   # build/VideoWallpaper.app，依赖 Homebrew 里的 libmpv
ninja -C build dmg                                # videowallpaper-v<版本>-macos-arm64.dmg，带上 libmpv 和它依赖的全部库
```

`ninja dmg` 用 dylibbundler 把 libmpv 及其依赖（ffmpeg、libass、libplacebo 等）复制进 `Contents/Frameworks`，检查没有残留指向 Homebrew 的路径，按这些库要求的系统版本设置 `LSMinimumSystemVersion`，再做 ad-hoc 签名。

## 配置文件

Linux：`~/.config/videowallpaper/config.ini`；Windows：`%APPDATA%\VideoWallpaper\config.ini`；macOS：`~/Library/Application Support/VideoWallpaper/config.ini`。

```ini
video=/path/default.mp4        # 默认视频
video.DP-2=/path/other.mp4     # 某个显示器的覆盖（Windows 里显示器名形如 \\.\DISPLAY1，macOS 里是显示器的名字）
volume=50
mute=0
pause_when_locked=1
audio_monitor=DP-1             # 哪个显示器出声，留空用主屏
```

## 发布

版本号只在 `CMakeLists.txt` 的 `project(VideoWallpaper VERSION x.y.z)` 里写一次，Nix 包和 Windows exe 的版本信息都从这里取。发布新版本：

```sh
# 先改 CMakeLists.txt 里的版本号并提交
git tag v2.2.0 && git push origin v2.2.0
```

CI 跑完全部构建后自动创建 GitHub Release，附上 Windows x64 / ARM64 的安装包和便携 zip、Ubuntu 26.04 的 amd64 / arm64 `.deb`、Fedora 44 的 x86_64 / aarch64 `.rpm`、macOS Apple Silicon 的 `.dmg`，以及 `SHA256SUMS`，更新说明根据提交记录自动生成。tag 和版本号对不上时发布会失败；`v2.2.0-rc1` 这类带 `-` 的 tag 发布为预发布版。

## 代码结构

```
src/core/       平台无关：libmpv 封装、配置、多屏控制器、菜单模型
src/wayland/    layer-shell 表面 + EGL 渲染、事件循环、SNI 托盘、D-Bus 会话事件
src/win/        WorkerW 表面、Shell_NotifyIcon 托盘、锁屏检测、NSIS 安装脚本
src/mac/        桌面层窗口 + CAOpenGLLayer、菜单栏图标、系统事件、Info.plist 和 dmg 打包脚本
cmake/          FindMpv、MinGW 工具链（x86_64 / aarch64）
nix/            Nix 包、NixOS / home-manager 模块
.github/        CI：Nix、Ubuntu 26.04（.deb）、Fedora 44（.rpm）各在 x86_64 和 aarch64 上构建，Windows x64 / ARM64 交叉构建，macOS arm64 构建 + 冒烟测试，打 tag 时发布 release
Archived/       v1（C# WinForms 客户端 + C++ socket 服务端），仅存档
```

核心通过两个接口和平台打交道：`Platform`（枚举显示器、创建表面）和 `Surface`（把播放器的输出接到一块屏幕上）。
Windows 上 mpv 直接渲染进壁纸窗口（`wid`）；Wayland 上用 mpv 的 render API 画到我们自己的 EGL 表面，传入 `wl_display` 后 VAAPI 硬解的帧可以直接交给 GL；
macOS 上同样用 render API，在主线程里画进每块屏幕一个的 CAOpenGLLayer，VideoToolbox 硬解。
循环播放、裁切铺满用的是 mpv 选项（`loop-file=inf`、`panscan=1.0`），不读用户的 `mpv.conf`。
