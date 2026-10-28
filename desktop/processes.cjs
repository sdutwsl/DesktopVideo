const { spawn } = require('node:child_process');
const path = require('node:path');
const { createInterface } = require('node:readline');

function abortError(message = '操作已取消') {
  return Object.assign(new Error(message), { name: 'AbortError' });
}

function safeError(text) {
  return String(text).replace(/https?:\/\/\S+/g, '[网址]').replace(/(?:cookie|authorization):[^\r\n]*/gi, '[会话信息]').slice(-1200);
}

class Processes {
  constructor() { this.active = new Set(); }

  run(executable, args, { input, signal, onLine, idleTimeout = false, timeout = 45000, maxBytes = 32 * 1024 * 1024 } = {}) {
    if (signal?.aborted) return Promise.reject(abortError());
    return new Promise((resolve, reject) => {
      const child = spawn(executable, args, {
        windowsHide: true, detached: process.platform !== 'win32',
        stdio: ['pipe', 'pipe', 'pipe'], shell: false,
      });
      let done = false, bytes = 0, output = [], errors = '', lines;
      const kill = () => {
        if (!child.pid || child.exitCode !== null) return;
        if (process.platform === 'win32') {
          const taskkill = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'taskkill.exe');
          const killer = spawn(taskkill, ['/pid', String(child.pid), '/t', '/f'], { windowsHide: true, stdio: 'ignore' });
          killer.on('error', () => child.kill());
          killer.unref();
        } else {
          try { process.kill(-child.pid, 'SIGKILL'); } catch { child.kill('SIGKILL'); }
        }
      };
      const finish = (error, value) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        lines?.close();
        signal?.removeEventListener('abort', cancel);
        this.active.delete(cancel);
        error ? reject(error) : resolve(value);
      };
      const cancel = () => { kill(); finish(abortError()); };
      const timer = setTimeout(() => {
        kill();
        finish(new Error(`操作超时（${Math.round(timeout / 1000)} 秒），可以取消后重试。`));
      }, timeout);
      this.active.add(cancel);
      signal?.addEventListener('abort', cancel, { once: true });
      if (signal?.aborted) cancel();
      if (onLine) {
        lines = createInterface({ input: child.stdout, crlfDelay: Infinity });
        lines.on('line', line => {
          if (done || !line.trim()) return;
          if (idleTimeout) timer.refresh();
          try { onLine(line); } catch (error) { kill(); finish(error); }
        });
      }
      child.stdout.on('data', chunk => {
        bytes += chunk.length;
        if (bytes > maxBytes) { kill(); finish(new Error('进程返回的数据过大')); }
        else if (!onLine) output.push(chunk);
      });
      child.stderr.on('data', chunk => { errors = (errors + chunk.toString('utf8')).slice(-4096); });
      child.on('error', error => finish(new Error(`无法启动 ${path.basename(executable)}：${error.code || error.message}`)));
      child.on('close', code => finish(code === 0 ? null : new Error(safeError(errors) || '播放后端未响应或尚未启动'), Buffer.concat(output).toString('utf8')));
      child.stdin.on('error', () => {});
      child.stdin.end(input);
    });
  }

  close() { for (const cancel of [...this.active]) cancel(); }
}

module.exports = { Processes, abortError, safeError };
