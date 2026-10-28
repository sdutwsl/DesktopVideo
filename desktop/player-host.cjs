// Only this application-owned utility process loads native code.
const { safeError } = require('./processes.cjs');
let player;

process.parentPort.on('message', ({ data }) => {
  const { id, method, value } = data || {};
  if (!Number.isInteger(id)) return;
  try {
    let result;
    if (method === 'start') {
      if (player) throw new Error('播放插件已启动');
      player = require(value.pluginPath);
      result = JSON.parse(player.start(value.configPath, value.legacyPath || ''));
    } else if (method === 'request') {
      if (!player) throw new Error('播放插件尚未启动');
      result = JSON.parse(player.request(value));
    } else if (method === 'close') {
      player?.close();
      result = { ok: true };
    } else throw new Error('未知播放操作');
    process.parentPort.postMessage({ id, result });
    if (method === 'close') setImmediate(() => process.exit(0));
  } catch (error) { process.parentPort.postMessage({ id, error: safeError(error.message) }); }
});

process.parentPort.on('close', () => process.exit(0));
