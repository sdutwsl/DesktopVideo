// Only this application-owned host loads native code. Linux uses a plain Node
// runtime so Chromium's FFmpeg symbols and allocator cannot interpose libmpv.
const { safeError } = require('./processes.cjs');
let player;
const port = process.parentPort;
const send = value => port ? port.postMessage(value) : process.send?.(value);

function receive(data) {
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
    send({ id, result });
    if (method === 'close') setImmediate(() => process.exit(0));
  } catch (error) { send({ id, error: safeError(error.message) }); }
}

if (port) {
  port.on('message', ({ data }) => receive(data));
  port.on('close', () => process.exit(0));
} else {
  process.on('message', receive);
  process.on('disconnect', () => process.exit(0));
}
