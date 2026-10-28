const { nativeImage } = require('electron');
const fs = require('node:fs/promises');
const path = require('node:path');

// Keep the same themed icon name as the original Wayland StatusNotifierItem.
// kiconfinder respects the KDE theme and inheritance without changing settings.
async function themeTrayIcon(processes, contents, signal) {
  if (process.platform !== 'linux') return null;
  for (const finder of ['kiconfinder6', 'kiconfinder5']) {
    if (signal?.aborted || contents.isDestroyed()) return null;
    try {
      const filename = (await processes.run(finder, ['preferences-desktop-wallpaper'], { signal, timeout: 2000, maxBytes: 4096 })).trim();
      if (!path.isAbsolute(filename)) continue;
      if (path.extname(filename).toLowerCase() !== '.svg') {
        const icon = nativeImage.createFromPath(filename);
        if (!icon.isEmpty()) return icon;
        continue;
      }
      if ((await fs.stat(filename)).size > 1024 * 1024) continue;
      const data = 'data:image/svg+xml;base64,' + (await fs.readFile(filename)).toString('base64');
      if (signal?.aborted || contents.isDestroyed()) return null;
      // NativeImage does not decode SVG. Use the existing trusted application
      // renderer to rasterize it, preserving transparency and both DPI scales.
      const pngs = await contents.executeJavaScript(`new Promise((resolve, reject) => {
        const image = new Image();
        const timer = setTimeout(() => reject(new Error('Icon rendering timed out')), 2000);
        image.onerror = () => { clearTimeout(timer); reject(new Error('Cannot read the themed icon')); };
        image.onload = () => {
          clearTimeout(timer);
          try {
            resolve([1, 2].map(scale => {
              const canvas = document.createElement('canvas');
              canvas.width = canvas.height = 32 * scale;
              canvas.getContext('2d').drawImage(image, 0, 0, canvas.width, canvas.height);
              return canvas.toDataURL('image/png');
            }));
          } catch (error) { reject(error); }
        };
        image.src = ${JSON.stringify(data)};
      })`);
      const icon = nativeImage.createFromDataURL(pngs[0]);
      icon.addRepresentation({ scaleFactor: 2, dataURL: pngs[1] });
      if (!icon.isEmpty()) return icon;
    } catch { /* Missing finder or unreadable theme: keep the packaged fallback. */ }
  }
  return null;
}

module.exports = { themeTrayIcon };
