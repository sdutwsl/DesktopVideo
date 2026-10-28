Linux KDE 托盘优先使用 `preferences-desktop-wallpaper` 主题图标，与原生 Wayland 托盘的 `IconName` 一致。应用通过已有的 `kiconfinder6` / `kiconfinder5` 查找当前主题资源，不修改主题设置；SVG 由现有应用渲染进程转换为透明 PNG。

- `muralop.svg`：对应 `src/wayland/Tray.cpp` 原有备用图形的蓝色圆角底与白色播放三角。
- `muralop.png`：该备用图形的 32×32 透明 PNG。
- `muralop@2x.png`：64×64 PNG，供高分屏使用。

未找到主题图标时，以及 Windows 上，使用上述备用资源。Nix 和便携包需要一并携带该目录。

`src/win/Muralop.ico`、`src/mac/Muralop.icns` 中的旧图形是历史遗留的 .NET 默认图标，不能作为 Muralop 的正式标识或托盘资源来源。
