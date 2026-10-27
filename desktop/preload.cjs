const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('muralop', {
  invoke: (action, value) => ipcRenderer.invoke('muralop', action, value),
  layout: bounds => ipcRenderer.send('browser-layout', bounds),
  onState: callback => {
    const listener = (_event, value) => callback(value);
    ipcRenderer.on('state', listener);
    return () => ipcRenderer.removeListener('state', listener);
  },
});
