# Muralop

[![build](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml/badge.svg)](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml)

把视频当作桌面壁纸播放。一套 C++ 核心（libmpv），三个原生外壳：

| 平台 | 壁纸层 | 托盘 | 状态 |
|---|---|---|---|
| **Linux Wayland** | `wlr-layer-shell` background 层 + 自绘（EGL/GLES2） | StatusNotifierItem（D-Bus） | KDE Plasma 6 上验证通过 |
| **Windows** | Explorer 的 `WorkerW`（桌面图标之下） | `Shell_NotifyIcon` | 从 v1 移植，x64 / ARM64 构建由 CI 覆盖，尚未在真机验证 |
| **macOS** | 桌面层级的无边框窗口（Finder 图标之下）+ CAOpenGLLayer | 菜单栏 `NSStatusItem` | Apple Silicon，CI 的 macOS 虚拟机里冒烟测试通过，尚未在真机验证 |

A video wallpaper player: shared libmpv core, native shells for Wayland (layer-shell), Windows (WorkerW) and macOS (desktop-level windows).

v2.2.0 及更早版本仍使用 VideoWallpaper 名称；Muralop 从 v3.0.0 起使用新名称和包名。正式版发布前，可以从 [master 的 Actions 构建](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml)下载构建产物。

## 功能

- 多显示器：每个显示器一个播放器，可分别指定视频；只有主屏出声
- 循环播放、按屏幕比例裁切铺满
- 锁屏、休眠时暂停，关机前退出
- 托盘 / 菜单栏菜单：选择视频、暂停、静音、退出；Linux 另有命令行 / D-Bus 控制，macOS 菜单里可以设置登录时启动
- Linux 和 Windows x64 可选主界面：内嵌 YouTube 浏览、网址播放、显示器选择和登录会话导入
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

v3.0.0 起，安装包在 [Releases](https://github.com/sdutwsl/Muralop/releases)，依赖会自动装上：

- Ubuntu 26.04 / Debian 13：`sudo apt install ./muralop-*-ubuntu-26.04-amd64.deb`（ARM 机器用 `-arm64.deb`）
- Fedora 44：`sudo dnf install ./muralop-*-fedora-44-x86_64.rpm`（ARM 机器用 `-aarch64.rpm`）

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
muralop                    # 启动守护进程（托盘里选视频）
muralop ~/Videos/a.mp4     # 启动并使用该视频
muralop set a.mp4 DP-2     # 只给 DP-2 换视频
muralop ui                 # 打开已安装的主界面
muralop pause | resume | mute | unmute | volume 40 | monitors | quit
MURALOP_DEBUG=1 muralop         # 打印渲染和 mpv 的详细日志
```

守护进程在 session bus 上注册 `org.muralop.Daemon`，也可以直接用 `busctl --user call ...` 控制；
Sway 用户可以在 `swayidle` 的 lock/unlock 钩子里调 `muralop pause/resume`。

开机自启：把 `data/muralop.desktop` 复制到 `~/.config/autostart/`。

### 在 NixOS / home-manager 中引用

```nix
# flake.nix
inputs.muralop = {
  url = "github:sdutwsl/Muralop";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

```nix
# NixOS 模块：装包，并作为 systemd 用户服务随图形会话启动
imports = [ inputs.muralop.nixosModules.default ];
programs.muralop.enable = true;
# programs.muralop.autostart = false;   # 只装包，不自启
```

home-manager 用户换成 `inputs.muralop.homeModules.default`，选项相同。
只要包本身的话：`inputs.muralop.overlays.default`（提供 `pkgs.muralop`）或
`inputs.muralop.packages.${system}.default`；临时试用：`nix run github:sdutwsl/Muralop`。

模块默认用系统自己的 `pkgs` 编译，保证和 `/run/opengl-driver` 里的显卡驱动用同一份 glibc；
否则会在启动时报 `EGL initialisation failed`。

## Windows

v3.0.0 起，[Releases](https://github.com/sdutwsl/Muralop/releases) 里每个版本有 x64 和 ARM64（骁龙等 Windows on ARM 设备）两套：

- **安装包** `muralop-v*-windows-<arch>-setup.exe`：装到 `%LOCALAPPDATA%\Programs\Muralop`，不需要管理员权限；有开始菜单快捷方式，可选开机自启，可在“设置 → 应用”里卸载。安装和卸载前会让正在运行的壁纸先正常退出。
- **便携版** `muralop-v*-windows-<arch>-portable.zip`：解压出 `Muralop` 文件夹直接运行。文件夹里的 `config.ini` 让设置保存在它旁边，不写 `%APPDATA%`，也不碰注册表；删掉它就改用 `%APPDATA%\Muralop\config.ini`。
- **YouTube 主界面** `muralop-v*-windows-x64-gui.zip`：解压后运行 `muralop-ui.exe`；包含 x64 后端、libmpv、yt-dlp 和 Deno。ARM64 主界面尚未打包。

master 每次提交的构建在 [Actions](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml) 的 `windows-x64` / `windows-arm64` 产物里。安装包没有代码签名，第一次运行 SmartScreen 会提示，点“更多信息 → 仍要运行”。

运行后右键托盘图标选择视频；把视频文件拖到 exe 上也可以。

## YouTube 主界面（Linux / Windows x64）

Linux 可通过 Nix 包中的 `muralop-ui` 启动；也可在 Ubuntu 26.04 下载 `muralop-v*-linux-gui-x64.tar.gz`，解压后运行 `muralop-ui/muralop-ui`。Linux GUI 归档内的后端依赖 Ubuntu 26.04 的 libmpv、Wayland、EGL 和 sdbus-c++ 2；Nix 包会管理这些依赖。Windows 使用上文的 GUI zip。主界面关闭后壁纸继续运行，托盘菜单可以再次打开它。

在内嵌网页浏览 YouTube，点击视频可直接设为桌面壁纸；也可以粘贴视频网址，给不同显示器指定不同视频。登录会话可从浏览器或 `cookies.txt` 导入。具体操作、Cookie 保存位置和开发运行方式见 [GUI 说明](gui/README.md)。macOS 目前没有这个主界面。

### 构建

依赖：MinGW-w64（x64 用 GCC ≥ 13，ARM64 用 LLVM/clang），CMake，libmpv 的 Windows 开发包；打安装包还要 NSIS（`makensis`）。不支持 MSVC：libmpv 的 Windows 包只带 MinGW 导入库。

1. 从 <https://sourceforge.net/projects/mpv-player-windows/files/libmpv/> 下载 `mpv-dev-x86_64-*.7z` 或 `mpv-dev-aarch64-*.7z`，用 7-Zip 解压（含 `include/`、`libmpv.dll.a`、`libmpv-2.dll`；ARM64 的包要较新的 7-Zip，p7zip 解不开。CI 固定用 `20261018-git-01e2533f31` 这一版）。
2. 构建。在 Linux 上交叉编译：x64 用 `nix develop .#mingw`，ARM64 用 `nix develop .#mingw-arm64`。在 Windows 上用 MSYS2 的 MinGW64 / CLANGARM64 环境，不需要工具链文件。
   ```sh
   cmake -S . -B build-win -G Ninja -DMPV_SDK_DIR=/path/to/mpv-dev \
         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake   # ARM64：cmake/mingw-aarch64.cmake
   ninja -C build-win                       # muralop.exe，libmpv-2.dll 会复制到旁边
   ninja -C build-win installer portable    # muralop-v<版本>-windows-<arch>-setup.exe / -portable.zip
   ```

## macOS

v3.0.0 起，[Releases](https://github.com/sdutwsl/Muralop/releases) 里的 `muralop-v*-macos-arm64.dmg`：Apple Silicon（M 系列芯片），macOS 15 及以上。
没有 Intel 版：Homebrew 已经不给 Intel Mac 出 mpv 的预编译包。

打开 dmg，把 `Muralop` 拖进“应用程序”。这个包没有 Apple 开发者签名和公证，第一次打开会提示无法验证开发者：点“完成”，再到“系统设置 → 隐私与安全性”底部点“仍要打开”。
也可以在终端里去掉下载标记：`xattr -dr com.apple.quarantine /Applications/Muralop.app`。

它只在菜单栏显示一个图标（不进 Dock），点开选择视频、暂停、静音、登录时启动、退出。把视频拖到程序图标上，或在 Finder 里“打开方式 → Muralop”，也会设为壁纸。
锁屏、切换用户、睡眠和显示器关闭时暂停。

### 构建

```sh
brew install cmake ninja pkgconf mpv dylibbundler
cmake -S . -B build -G Ninja && ninja -C build   # build/Muralop.app，依赖 Homebrew 里的 libmpv
ninja -C build dmg                                # muralop-v<版本>-macos-arm64.dmg，带上 libmpv 和它依赖的全部库
```

`ninja dmg` 用 dylibbundler 把 libmpv 及其依赖（ffmpeg、libass、libplacebo 等）复制进 `Contents/Frameworks`，检查没有残留指向 Homebrew 的路径，按这些库要求的系统版本设置 `LSMinimumSystemVersion`，再做 ad-hoc 签名。

## 配置文件

首次启动时，若新路径没有配置，Muralop 会读取旧版 VideoWallpaper 的配置并写入新路径；旧文件会保留。Windows 安装包会卸载旧版安装包。macOS 用户先在旧版菜单里关闭“登录时启动”，再移除 `VideoWallpaper.app`，避免两个应用同时启动。

Linux：`~/.config/muralop/config.ini`；Windows：`%APPDATA%\Muralop\config.ini`；macOS：`~/Library/Application Support/Muralop/config.ini`。

```ini
video=/path/default.mp4        # 默认视频
video.DP-2=/path/other.mp4     # 某个显示器的覆盖（Windows 里显示器名形如 \\.\DISPLAY1，macOS 里是显示器的名字）
volume=50
mute=0
pause_when_locked=1
audio_monitor=DP-1             # 哪个显示器出声，留空用主屏
```

## 发布

版本号只在 `CMakeLists.txt` 的 `project(Muralop VERSION x.y.z)` 里写一次，Nix 包和 Windows exe 的版本信息都从这里取。发布新版本：

```sh
# 先改 CMakeLists.txt 里的版本号并提交
git tag v3.0.0 && git push origin v3.0.0
```

CI 跑完全部构建后自动创建 GitHub Release，附上 Windows x64 / ARM64 的安装包和便携 zip、Windows/Linux x64 的 GUI 包、Ubuntu 26.04 的 amd64 / arm64 `.deb`、Fedora 44 的 x86_64 / aarch64 `.rpm`、macOS Apple Silicon 的 `.dmg`，以及 `SHA256SUMS`，更新说明根据提交记录自动生成。tag 和版本号对不上时发布会失败；`v3.0.0-rc1` 这类带 `-` 的 tag 发布为预发布版。

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
