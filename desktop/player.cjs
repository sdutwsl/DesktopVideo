const { spawn } = require('node:child_process');

// This is the player plugin boundary. The UI never imports libmpv or native IPC
// bindings: a short-lived native client performs each bounded request.
class PlayerPlugin {
  constructor(executable, processes) { this.executable = executable; this.processes = processes; }

  async request(command, args = [], signal) {
    const fields = [command, ...args];
    if (fields.some(value => typeof value !== 'string' || /[\r\n\0]/.test(value))) throw new Error('控制参数无效');
    const input = fields.join('\n');
    if (Buffer.byteLength(input) > 32768) throw new Error('控制参数过长');
    const output = await this.processes.run(this.executable, ['rpc'], { input, signal, timeout: 4000, maxBytes: 128 * 1024 });
    const result = JSON.parse(output);
    if (!result.ok) throw new Error(result.error || '播放后端请求失败');
    return result;
  }

  async connect(signal) {
    try { return await this.request('status', [], signal); }
    catch (error) { if (signal?.aborted) throw error; }
    const child = spawn(this.executable, ['--background'], { detached: true, stdio: 'ignore', windowsHide: true });
    let startupError;
    child.on('error', error => { startupError = error; });
    child.unref();
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
      if (startupError) throw startupError;
      await new Promise(resolve => setTimeout(resolve, 250));
      try { return await this.request('status', [], signal); }
      catch (error) { if (signal?.aborted) throw error; }
    }
    throw new Error('播放后端未响应，请退出旧版后端后重连。');
  }

  playResolved(media, monitor, signal) {
    return this.request('play-resolved', [media.source, media.video, media.audio, media.userAgent, media.referrer, monitor], signal);
  }
}

module.exports = { PlayerPlugin };
