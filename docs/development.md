# 开发与构建

[返回首页](../README.md) · [安装说明](installation.md) · [更新日志](../CHANGELOG.md)

## 应用入口与构建链路

Electron 管理窗口、网页、托盘和应用生命周期，`muralop.node` 复用 C++/libmpv 壁纸播放核心。应用文件和插件来自同一份源码，版本统一读取 `CMakeLists.txt`，启动时检查包内版本是否一致。

| 内容 | 修改位置 | 构建入口 | 产物 |
|---|---|---|---|
| 播放插件 | `src/node/`、`src/core/`、平台目录 | CMake，启用 `MURALOP_BUILD_NODE_PLUGIN` | `build/desktop/muralop.node` 和生成的 `package.json` |
| 窗口、网页、解析、托盘和诊断 | `desktop/` | `nix/package.nix` 或 `desktop/package.py` | `muralop-ui` / `muralop-ui.exe` |
| 登录会话导入 | `gui/session.py` | Nix Python 包装器或 PyInstaller | `muralop-session` |
| 独立原生播放器 | `src/core/`、平台目录 | CMake + Ninja | `muralop` / `muralop.exe` / `Muralop.app` |

应用入口为 `desktop/main.cjs`。CJS、HTML、CSS 直接复制进包，没有 `npm run build`。`desktop/player-host.cjs` 在应用拥有的宿主进程中加载 `.node`，插件将命令交给专用渲染线程；应用不通过 `muralop --background` 或反复执行 RPC 子进程驱动播放。

Linux 使用纯 Node 宿主，隔离 Electron 内置 FFmpeg 的同名符号和内存分配器；不能直接在 Electron 中加载 libmpv，也不能对整个依赖树使用深度绑定。Windows 使用 Electron utility process。两者均由应用创建和回收，保持相同插件接口。

### Nix 包

`flake.nix` → `nix/package.nix` → CMake 编译插件 → 安装 `desktop/` 源码、资源和生成的版本元数据 → 包装 Electron、Node、yt-dlp、Deno 及会话导入工具。

包装器通过 `MURALOP_NODE` 指定包内依赖的纯 Node 运行时，通过 `MURALOP_PLAYER_PLUGIN` 指定插件。完整包的 `muralop` 默认进入应用，`nix run . -- ui` 使用同一个入口。

新增应用源码必须加入 Git 跟踪：本地 Git flake 不包含未跟踪文件。源码修复应进入本仓库的实际打包路径，不向消费仓库添加临时 Nix 补丁。

### 便携包

[构建工作流](../.github/workflows/build.yml) 在 Ubuntu / Windows 构建中启用插件，GUI 任务取得 `.node` 和生成的 `package.json`，再运行 `desktop/package.py`。

脚本把 Electron、应用源码、插件、会话导入工具、yt-dlp 和 Deno 组装进 `dist/gui/muralop-ui/`。Linux 另外包含纯 Node 可执行文件 `node`，Windows 包含 `libmpv-2.dll`，最终归档为 tar.gz 或 zip，无需单独的原生播放程序。

## 构建主界面

在仓库根目录直接运行完整 Nix 包：

```sh
nix run . -- ui
```

修改插件时，可在 Nix 开发环境中单独构建并运行：

```sh
nix develop
cmake -S . -B build -G Ninja -DMURALOP_BUILD_NODE_PLUGIN=ON \
  -DNODE_INCLUDE_DIR="$(node -p "require('path').resolve(process.execPath, '../../include/node')")"
cmake --build build --target muralop_player
npm ci --prefix desktop
npm start --prefix desktop -- --player-plugin "$PWD/build/desktop/muralop.node"
```

使用稳定的 Node-API v8，不直接依赖 V8。Windows 插件从当前 Electron 可执行文件解析 Node-API 符号，插件架构必须与 Electron 一致。

| 参数或环境变量 | 用途 |
|---|---|
| `--player-plugin` / `MURALOP_PLAYER_PLUGIN` | 指定本地构建的 `muralop.node` |
| `MURALOP_NODE` | 指定 Linux 纯 Node 宿主；源码开发默认使用 PATH 中的 `node` |
| `MURALOP_YTDLP` | 指定 yt-dlp，当前打包版本为 2026.10.21，需配合 Deno |
| `MURALOP_SESSION_HELPER` | 指定已打包的 `muralop-session`；Nix 和便携包已提供 |

从仓库根目录打便携包：

```sh
python -m pip install -r desktop/requirements.txt pyinstaller
python desktop/package.py --player-dir build/desktop
```

指定目录需要包含 CMake 生成的 `muralop.node` 和 `package.json`；Windows 还需 `libmpv-2.dll`。Linux 打包时复制当前构建环境的纯 Node 可执行文件为 `node`。

## 独立原生播放器

### Linux

依赖 CMake ≥ 3.20、C++20 编译器、Ninja、pkg-config、libmpv、Wayland、EGL/GLES、wayland-scanner 和 sdbus-c++ ≥ 2.0。sdbus-c++ 1.x 与 2.x 的 API 不兼容。

Ubuntu 26.04：

```sh
sudo apt install cmake ninja-build pkg-config libmpv-dev libwayland-dev libegl-dev libgles-dev libsdbus-c++-dev libwayland-bin g++
```

Fedora 44：

```sh
sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config mpv-devel wayland-devel libglvnd-devel sdbus-cpp-devel
```

Nix 环境可直接使用 `nix develop`，然后构建：

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

在 `build/` 中使用 `cpack -G DEB` 或 `cpack -G RPM` 打包。

### Windows

依赖 MinGW-w64（x64 使用 GCC ≥ 13，ARM64 使用 LLVM/clang）、CMake 和 libmpv 开发包；安装包还需要 NSIS。libmpv 的 Windows 包使用 MinGW 导入库，不支持 MSVC。

从 [libmpv 开发包](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/) 下载对应架构的 SDK，使用较新的 7-Zip 解压，目录应包含 `include/`、`libmpv.dll.a` 和 `libmpv-2.dll`。CI 固定使用 `20261018-git-01e2533f31` 版本。

Linux 交叉编译 x64 使用 `nix develop .#mingw`，ARM64 使用 `nix develop .#mingw-arm64`。以下以 x64 为例：

```sh
cmake -S . -B build-win -G Ninja -DMPV_SDK_DIR=/path/to/mpv-dev \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake
cmake --build build-win
cmake --build build-win --target installer portable
```

ARM64 使用 `cmake/mingw-aarch64.cmake`。Windows 本机可使用 MSYS2 的 MinGW64 / CLANGARM64 环境，省略工具链参数。

### macOS

```sh
brew install cmake ninja pkgconf mpv dylibbundler
cmake -S . -B build -G Ninja
cmake --build build
cmake --build build --target dmg
```

`build/Muralop.app` 使用 Homebrew 的 libmpv。DMG 任务把 libmpv 和依赖复制到应用的 `Contents/Frameworks`，检查残留的 Homebrew 路径，设置最低系统版本并做 ad-hoc 签名。

## 插件接口

`src/node/PlayerAddon.cpp` 导出 `start(configPath, legacyPath)`、`request(command)` 和 `close()`。同步入口只在专用宿主进程中调用；显示器、mpv 实例和平台连接均在渲染线程创建、使用和释放。

`desktop/player.cjs` 管理请求取消和超时：启动上限 15 秒，普通请求 4 秒，退出清理 3 秒。`desktop/player-host.cjs` 负责加载模块、传递结果，并在应用退出时回收宿主。

内部命令复用 `AppControl`：`status`、`play-resolved`、`set`、`stop`、`online`、`pause`、`resume`、`mute`、`unmute`、`volume`、`clear`、`quit`，以及 MPRIS 使用的 `play`、`stop-all`、`media-context`、`events`。参数以换行分隔，返回 JSON。

`status` 返回显示器、音量、暂停状态、错误和播放状态。`play-resolved` 接收原始网址、视频流、音频流、User-Agent、Referer、显示器及可选标题。签名流地址只在播放期间驻留内存。

Linux MPRIS 对象复用会话 D-Bus，并通过 mpv 事件缓存媒体属性；宿主每 250 毫秒读取原生事件队列，把窗口、合集及控制动作交回应用命令队列。属性变化按协议发送 `PropertiesChanged` / `Seeked`，进度不逐帧广播。

## 代码结构

```text
src/node/       Node-API 插件、渲染线程和平台生命周期
desktop/        应用窗口、浏览解析、托盘、诊断、插件宿主及便携打包
src/core/       libmpv 封装、配置、多屏控制器和菜单模型
src/wayland/    layer-shell、EGL、事件循环、SNI、D-Bus 和 MPRIS
src/win/        WorkerW、原生托盘、会话检测和 NSIS 安装脚本
src/mac/        桌面层窗口、CAOpenGLLayer、菜单栏和 DMG 打包
cmake/          FindMpv、MinGW 工具链
nix/            Nix 包、NixOS / home-manager 模块
.github/        各平台构建与发布 CI
docs/           安装、使用和开发文档
gui/            会话导入工具与旧 Qt 界面
Archived/       v1 存档
```

核心通过 `Platform` 枚举显示器、创建表面，通过 `Surface` 把播放器输出接到屏幕。Windows 使用 mpv 的 `wid`，Wayland 使用 render API 和 EGL，macOS 在主线程的 CAOpenGLLayer 中渲染。循环和裁切使用 mpv 选项，不读取用户的 `mpv.conf`。

## 提交、推送与发布

版本以 `CMakeLists.txt` 的 `project(Muralop VERSION x.y.z)` 为准。变更先记录到 [CHANGELOG.md](../CHANGELOG.md) 的“未发布”，按 `Feature` / `Fix` 分类；发布时确定版本号、归档条目，并同步 `desktop/package.json` 和锁文件。纯文档修改不升级应用版本。

提交时包含实际源码、打包文件和变更记录，新增文件也要加入 Git：

```sh
git add -p
git add path/to/new-file
git commit -m "说明本次修改"
# 仅在当次明确要求推送时执行
git push origin master
```

`master` 推送触发各平台构建。正式发布使用与源码版本一致的 `v<版本号>` 标签，例如：

```sh
git tag -a vX.Y.Z -m "Muralop vX.Y.Z"
git push origin vX.Y.Z
```

标签触发 CI，全部任务通过后自动创建 Release，附上各平台安装包、GUI 归档和 `SHA256SUMS`。标签必须与源码版本一致；带 `-` 后缀的标签会标为预发布。
