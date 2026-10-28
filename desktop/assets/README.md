托盘图标复用项目原有的 Muralop 图形，与 `src/win/Muralop.ico`、`src/mac/Muralop.icns` 保持一致。

- `muralop.png`：从现有 ICNS 的 `icp5` 表示中直接提取的 32×32 PNG。
- `muralop@2x.png`：从现有 ICNS 的 `icp6` 表示中直接提取的 64×64 PNG，供高分屏使用。

两份资源均保留原始 PNG 数据及透明通道，不通过手工位图绘制重建。Nix 和便携包需要一并携带该目录。
