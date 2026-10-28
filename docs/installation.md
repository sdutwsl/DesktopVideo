# 安装

[返回首页](../README.md) · [使用说明](usage.md) · [从源码构建](development.md)

正式版本下载见 [Releases](https://github.com/sdutwsl/Muralop/releases/latest)，开发版产物见 [Actions](https://github.com/sdutwsl/Muralop/actions/workflows/build.yml)。

## Linux Wayland

需要合成器支持 `zwlr_layer_shell_v1`：

| 合成器 | 支持情况 |
|---|---|
| KDE Plasma 6（KWin） | 支持 |
| Sway、Hyprland、river、niri | 支持 |
| GNOME（Mutter） | 不支持 layer-shell |

托盘需要面板支持 StatusNotifierItem，例如 Plasma 或 Waybar。

### Nix

临时运行：

```sh
nix run github:sdutwsl/Muralop -- ui
```

Nix 包包含应用、播放插件、Node 运行时、yt-dlp、Deno 和登录会话导入工具。

在 NixOS / home-manager 中引用：

```nix
# flake.nix
inputs.muralop = {
  url = "github:sdutwsl/Muralop";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

```nix
# NixOS 模块：安装，并随图形会话启动
imports = [ inputs.muralop.nixosModules.default ];
programs.muralop.enable = true;
# programs.muralop.autostart = false; # 只安装，不自启
```

home-manager 使用 `inputs.muralop.homeModules.default`，选项相同。只引用软件包时可使用 `inputs.muralop.packages.${system}.default`，或通过 `inputs.muralop.overlays.default` 提供 `pkgs.muralop`。

模块默认使用系统自己的 `pkgs` 编译，使 glibc 与 `/run/opengl-driver` 中的显卡驱动一致，避免启动时的 EGL 初始化错误。

### GUI 归档

Ubuntu 26.04 x64 下载 `muralop-v*-linux-gui-x64.tar.gz`，解压后运行 `muralop-ui/muralop-ui`。

播放插件依赖 Ubuntu 26.04 的 libmpv、Wayland、EGL 和 sdbus-c++ 2 等动态库；归档包含 Electron、Node、yt-dlp、Deno 和会话导入工具。其他发行版需要提供兼容的动态库，或使用 Nix 包。

Ubuntu 的非特权用户命名空间限制可能影响 Chromium 沙箱。归档中的 `chrome-sandbox` 需设置为 root 所有并具有 setuid 权限：

```sh
sudo chown root:root muralop-ui/chrome-sandbox
sudo chmod 4755 muralop-ui/chrome-sandbox
```

### 原生安装包

不使用内嵌站点浏览时，可安装原生播放器，通过托盘或命令行选择视频：

- Ubuntu 26.04：`sudo apt install ./muralop-v*-ubuntu-26.04-amd64.deb`，ARM64 使用 `-arm64.deb`。
- Fedora 44：`sudo dnf install ./muralop-v*-fedora-44-x86_64.rpm`，ARM64 使用 `-aarch64.rpm`。

手动设置登录自启时，可将 `data/muralop.desktop` 复制到 `~/.config/autostart/`；NixOS / home-manager 模块已提供自启选项。

## Windows

带内嵌视频站点浏览的应用使用 `muralop-v*-windows-x64-gui.zip`，解压后运行 `muralop-ui.exe`。包内包含播放插件、libmpv、yt-dlp 和 Deno；暂未提供 ARM64 GUI 包。

原生托盘播放器提供 x64 和 ARM64 两种架构：

| 文件 | 用法 |
|---|---|
| `muralop-v*-windows-<arch>-setup.exe` | 安装到 `%LOCALAPPDATA%\Programs\Muralop`，提供开始菜单快捷方式、可选自启和卸载入口，无需管理员权限 |
| `muralop-v*-windows-<arch>-portable.zip` | 解压后直接运行，配置默认保存在随包的 `config.ini` 中 |

原生播放器可从托盘选择视频，也可把视频文件拖到程序图标上。便携版删除旁边的 `config.ini` 后改用 `%APPDATA%\Muralop\config.ini`。安装和卸载前会请求正在运行的壁纸正常退出。

安装包未签名，SmartScreen 可能提示；确认下载来源后，可通过“更多信息 → 仍要运行”打开。

## macOS

下载 `muralop-v*-macos-arm64.dmg`，支持 macOS 15 及以上的 Apple Silicon。当前没有 Intel Mac 安装包。

打开 dmg，将 `Muralop` 拖到“应用程序”。应用没有 Apple 开发者签名和公证，首次打开可能需要在“系统设置 → 隐私与安全性”中选择“仍要打开”。

macOS 使用菜单栏图标，支持选择视频、暂停、静音、登录时启动和退出，不显示 Dock 图标。也可将视频拖到应用图标上，或在 Finder 中选择“打开方式 → Muralop”。

## 从 VideoWallpaper 升级

如果新路径没有配置，首次启动会读取旧版 VideoWallpaper 配置并写入新路径，保留旧文件。Windows 安装包会卸载旧版安装包。

macOS 升级前先关闭旧版的“登录时启动”，再移除 `VideoWallpaper.app`，避免重复启动。切换到带主窗口的应用前，也应退出仍在运行的旧播放器，释放播放控制接口。
