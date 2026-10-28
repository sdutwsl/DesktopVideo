# Muralop

[![build](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml/badge.svg)](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml)

把视频当作桌面壁纸播放。Linux / Windows 主界面通过原生 Node 插件使用 C++/libmpv 核心；各平台的壁纸层如下（本次插件重构待构建和实播验证）：

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
- Linux 和 Windows x64 可选主界面：内嵌 YouTube、B站、抖音、TikTok、Vimeo、Dailymotion、Twitch 浏览，支持 yt-dlp 站点网址播放、显示器选择和登录会话导入
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
muralop                    # 完整包打开应用；仅安装原生程序时启动独立播放器
muralop ~/Videos/a.mp4     # 启动并使用该视频
muralop set a.mp4 DP-2     # 只给 DP-2 换视频
muralop ui                 # 在终端前台打开主界面，Ctrl+C 可退出
muralop pause | resume | mute | unmute | volume 40 | monitors | quit
MURALOP_DEBUG=1 muralop         # 打印渲染和 mpv 的详细日志
```

应用的播放插件和独立播放器均可在 session bus 上注册 `org.muralop.Daemon`，也可以直接用 `busctl --user call ...` 控制；
Sway 用户可以在 `swayidle` 的 lock/unlock 钩子里调 `muralop pause/resume`。

### 媒体键与 playerctl

Linux 应用及独立播放器提供 [MPRIS 2](https://specifications.freedesktop.org/mpris/latest/) 接口，播放器名称为 `muralop`。使用 [playerctl](https://github.com/altdesktop/playerctl) 控制：

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
```

`volume` 的 0～1 对应应用的 0～100；设置非零音量会取消静音。`position 10+` 前进 10 秒，`position 30` 跳至第 30 秒，只在视频本身可跳转时生效。停止会取消待完成的解析，再播放会从头打开当前视频。

标题、时长和进度跟随出声显示器；播放、暂停、停止、音量和跳转控制所有显示器。合集切换只在当前出声视频属于应用已读取的合集、并有对应条目时启用，沿用该次播放的显示器目标。单视频及独立播放器不提供合集切换。媒体信息只公开原始视频网址或本地文件 URI，不公开签名播放流。还支持 `playerctl -p muralop open URI` 打开网址或 `file:///` 本地文件；KDE 等桌面的媒体控件可读取同一接口。

服务名为 `org.mpris.MediaPlayer2.muralop`，对象为 `/org/mpris/MediaPlayer2`，随应用启动、退出，复用现有会话总线。当前只提供正常倍速，不提供随机播放或循环模式切换。本次接入尚未构建和桌面联调。

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
- **主界面** `muralop-v*-windows-x64-gui.zip`：解压后运行 `muralop-ui.exe`；包含 x64 原生 Node 播放插件、libmpv、yt-dlp 和 Deno。ARM64 主界面尚未打包。

master 每次提交的构建在 [Actions](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml) 的 `windows-x64` / `windows-arm64` 产物里。安装包没有代码签名，第一次运行 SmartScreen 会提示，点“更多信息 → 仍要运行”。

运行后右键托盘图标选择视频；把视频文件拖到 exe 上也可以。

## 主界面（Linux / Windows）

应用基于 Electron，通过 `muralop.node` 调用现有 C++/libmpv 播放能力，统一管理窗口、托盘和退出。插件在应用拥有的宿主进程中加载（Linux 使用随包提供的纯 Node，Windows 使用 Electron utility process），渲染运行于专用线程；驱动崩溃时窗口保留，可重启播放。解析视频时界面保持可操作，可随时取消（本次重构待构建和实播验证）。
Linux 可通过 Nix 包中的 `muralop-ui` 启动；也可在 Ubuntu 26.04 下载 `muralop-v*-linux-gui-x64.tar.gz`，解压后运行 `muralop-ui/muralop-ui`。Linux GUI 归档内的播放插件依赖 Ubuntu 26.04 的 libmpv、Wayland、EGL 和 sdbus-c++ 2；Nix 包会管理这些依赖。Ubuntu 默认限制非特权用户命名空间，解压后需执行一次 `sudo chown root:root muralop-ui/chrome-sandbox && sudo chmod 4755 muralop-ui/chrome-sandbox`，否则 Chromium 沙箱无法启动。Windows 使用上文的 GUI zip。关闭按钮将主界面隐藏到托盘，保留网页和当前任务；最小化或再次打开不会重新加载网页。使用托盘中的“退出”同时结束壁纸播放和主界面。

在内嵌网页浏览支持的视频平台，点击视频可直接设为桌面壁纸；也可以粘贴视频网址，给不同显示器指定不同视频。点击后界面和窗口标题依次显示 **解析中**、**加载中**、**播放中**，失败时显示原因。合集会列出全部可用视频，可以选择其中任意视频播放。YouTube 可能要求登录后才能解析：可在内嵌网页直接登录，或从浏览器、`cookies.txt` 导入登录会话。具体操作、Cookie 保存位置和开发运行方式见 [主界面说明](desktop/README.md)。macOS 目前没有这个主界面。

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

## 版本与 Fix / Feature 清单

当前源码版本为 **3.0.0**，以 `CMakeLists.txt` 的 `project(Muralop VERSION x.y.z)` 为准。版本采用 `主版本.次版本.修订版本`：不兼容变更升级主版本，新增功能升级次版本，仅修复问题升级修订版本；纯文档修改不单独升级应用版本。

日常改动先累计在“未发布”中，整理新版本时再确定版本号，将本批条目移入对应版本标题并保留历史记录。提交、推送和正式版本发布是不同操作，不为每次提交自动增加版本号。

### 未发布

#### Feature

- Linux 支持 MPRIS 2 和 `playerctl -p muralop`：播放、暂停、停止、音量、跳转进度、标题与时长查询，以及当前合集的上一条 / 下一条；复用会话 D-Bus，不增加系统服务（待构建及桌面联调）。
- 播放能力改为原生 Node-API 插件，由应用管理启动、渲染线程和退出；保留自动硬解、多屏、锁屏和休眠处理，Nix 及 Windows/Linux 便携包同步携带插件（待构建和实播验证）。
- 新增“关于与诊断”：统一应用版本、文件标识、运行位置、最近 20 条错误，支持复制诊断信息和清空记录（待界面实测）。
- 支持切换 YouTube、B站、抖音、TikTok、Vimeo、Dailymotion、Twitch，点击视频直接播放；其他 yt-dlp 站点可粘贴网址解析（待逐站实播验证）。
- 一次导入浏览器配置或 cookies.txt 中所有已接入平台的登录会话，切换平台无需逐个重新导入；支持从分享文案中提取视频链接。
- 简化网页区域，移除导航栏、手动“播放当前视频”按钮和预览开关。
- 调整侧栏分组和播放状态卡，增加三步播放状态、网页骨架屏、慢加载提示、播放指示及控件过渡动效（待界面实测）。
- 移除原生窗口装饰和网页外框，将顶部收为单行工具栏；通过平台小按钮弹出站点列表，提供窗口拖动、最小化、最大化和隐藏到托盘操作（待界面实测）。

#### Fix

- Linux 使用应用提供的纯 Node 宿主加载播放插件，避免与 Electron 内置 FFmpeg 的同名符号冲突，修复 mpv 在库版本检查中直接退出的问题；硬解策略保持不变（已通过初始化、状态查询和退出定点诊断，整包及实播待验证）。
- 将新增播放插件及运行时模块纳入 Git 跟踪，修复本地 `nix run . -- ui` 因 flake 源码缺少 `src/node` 而无法求值的问题（完整构建待验证）。
- 保留窗口隐藏、最小化前的网页与播放任务，恢复窗口时沿用原网页。
- 完整应用统一由 Electron 提供一个托盘，播放插件不再创建第二个托盘（待运行验证）。
- 托盘复用原有 Muralop 图标，移除临时代码绘制的播放图案；提供透明 PNG 和高分屏资源，并纳入 Nix / 便携包（待托盘显示实测）。
- 完整包的默认启动和传入视频文件统一进入同一应用实例，保留命令行控制，避免额外启动独立播放器（待运行验证）。
- 保留 YouTube 合集参数，读取完整可用视频列表，支持选择和切换条目。
- 优化合集首播等待：逐条读取并追加列表，先解析所选视频或首个可用视频，后台补齐其余条目；完整列表缓存 10 分钟，减少重复请求（待性能实测）。
- 防止旧请求覆盖新视频的解析、加载状态；网页错误与播放状态分别显示。
- 统一为托盘中的一个“退出”入口，同时结束播放插件、窗口、网页和解析任务；退出会取消正在启动和解析的任务，插件停止超时后终止其宿主进程（待运行验证）。
- 将 Wayland 壁纸窗口标记为 KWin 可识别的桌面类型，使其参与桌面预览选择（待 KDE 实测）。
- 播放插件崩溃或失去响应时保留窗口和托盘，显示原因并支持重启播放；移除每次状态查询启动 RPC 子进程的调用链（待运行验证）。
- 解锁或休眠恢复时重置 Wayland 帧回调并主动重绘当前画面，处理有音频但画面不恢复的情况，保留硬解和播放进度（待锁屏恢复实测）。

#### 已知问题

- 已观察到 Intel iHD 的 AV1 硬解同步路径崩溃，尚未解决；保留原有自动硬解策略。

## 发布

Nix 包和 Windows exe 的版本信息从 `CMakeLists.txt` 获取，Electron 包元数据随应用版本同步。仅在明确要求发布时整理版本清单、调整版本并创建 tag：

```sh
# 先改 CMakeLists.txt 里的版本号并提交
git tag v3.0.0 && git push origin v3.0.0
```

CI 跑完全部构建后自动创建 GitHub Release，附上 Windows x64 / ARM64 的安装包和便携 zip、Windows/Linux x64 的 GUI 包、Ubuntu 26.04 的 amd64 / arm64 `.deb`、Fedora 44 的 x86_64 / aarch64 `.rpm`、macOS Apple Silicon 的 `.dmg`，以及 `SHA256SUMS`，更新说明根据提交记录自动生成。tag 和版本号对不上时发布会失败；`v3.0.0-rc1` 这类带 `-` 的 tag 发布为预发布版。

## 开发与构建流程

Muralop 按一个应用打包：Electron 管理窗口、网页、托盘和生命周期，原生 Node 插件承接壁纸播放。应用文件和插件必须来自同一份源码，版本统一读取 `CMakeLists.txt`；启动时检查包内版本是否一致。

| 部分 | 修改位置 | 构建或打包入口 | 产物 |
|---|---|---|---|
| 原生 Node 播放插件 | `src/node/`、`src/core/`、`src/wayland/`、`src/win/` | CMake 的 `MURALOP_BUILD_NODE_PLUGIN=ON` | `build/desktop/muralop.node`、生成的 `package.json` |
| 应用窗口、网页、托盘、解析与诊断 | `desktop/` | `nix/package.nix` 或 `desktop/package.py` | `muralop-ui` / `muralop-ui.exe` |
| 浏览器登录会话导入 | `gui/session.py` | Nix Python 包装器或 PyInstaller | `muralop-session` |
| 独立原生播放器与命令行 | `src/core/`、平台目录 | CMake + Ninja | `muralop` / `muralop.exe` / `Muralop.app` |

应用入口为 `desktop/main.cjs`。CJS、HTML、CSS 直接复制进包，没有 `npm run build`。`desktop/player-host.cjs` 在应用拥有的宿主进程中加载 `.node`：Linux 使用纯 Node 运行时，Windows 使用 Electron utility process，插件内部将命令交给渲染线程执行；界面不再启动 `muralop --background` 或 `muralop rpc`。独立原生播放器仍用于不带 Electron 的发行包，macOS 继续使用原生外壳。

**Nix 构建链路：** `flake.nix` → `nix/package.nix` → CMake 编译 `.node` → 安装 `desktop/` 源码和生成的版本元数据 → 包装 Electron、Node、yt-dlp、Deno、会话导入工具。包装器通过 `MURALOP_NODE` 指定同一包依赖的 Node 运行时，并指定包内的插件；完整包的 `muralop` 默认入口转入这个应用，`nix run . -- ui` 也打开同一个入口。应用改动必须进入该包的源码集合，不能向消费仓库添加临时补丁。

**便携包构建链路：** `.github/workflows/build.yml` 先在 Ubuntu / Windows 构建中启用 `MURALOP_BUILD_NODE_PLUGIN`，GUI 任务取得 `.node` 和生成的 `package.json`，运行 `python desktop/package.py --player-dir player`。脚本将 Electron、应用源码、播放插件、会话导入工具、yt-dlp 和 Deno 组装进 `dist/gui/muralop-ui/`；Linux 同时包含构建环境的纯 Node 可执行文件 `node`，Windows 同时包含 `libmpv-2.dll`，最终归档为 zip 或 tar.gz。GUI 包无需单独的 `muralop` 播放程序。

本地开发使用与当前平台对应的 Node 头文件和 libmpv；Linux 还需前述 Wayland 依赖。在 Nix 开发环境中：

```sh
nix develop
cmake -S . -B build -G Ninja -DMURALOP_BUILD_NODE_PLUGIN=ON \
  -DNODE_INCLUDE_DIR="$(node -p "require('path').resolve(process.execPath, '../../include/node')")"
cmake --build build --target muralop_player
npm ci --prefix desktop
npm start --prefix desktop -- --player-plugin "$PWD/build/desktop/muralop.node"
```

这里使用稳定的 Node-API v8，不直接依赖 V8 接口；Windows 从当前 Electron 可执行文件解析 Node-API 符号。Linux 使用纯 Node 的进程 IPC，Windows 使用 [Electron utilityProcess](https://www.electronjs.org/docs/latest/api/utility-process)；两者均由应用创建和回收。默认只做源码修改，不主动执行上述构建、测试或发布命令。

完成修改后，同步更新上面的 Fix / Feature 清单，可以创建本地提交。新增源码必须加入 Git 跟踪，否则本地 Git flake 的 `nix run .` 会遗漏这些文件。每次推送仍需当次明确要求；允许本地提交不代表允许推送。明确要求推送后，将实际修改的源码、相关打包文件和变更清单完整提交，再推送到 `origin/master`，并给出提交号。推送会触发上述 CI，生成各平台构建产物；日常修复无需打版本 tag。

```sh
# 修改完成后可创建本地提交
git add -p
# 新增文件需另用 git add 文件路径加入提交
git commit -m "说明本次修复"
# 仅在当次明确要求推送时执行
git push origin master
```

## 代码结构

```
src/node/       Node-API 播放插件、专用渲染线程与平台生命周期
desktop/        Electron 应用、浏览与解析、托盘、诊断、插件宿主和便携打包
src/core/       平台无关：libmpv 封装、配置、多屏控制器、菜单模型
src/wayland/    layer-shell 表面 + EGL 渲染、事件循环、SNI 托盘、D-Bus 会话事件、MPRIS
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
