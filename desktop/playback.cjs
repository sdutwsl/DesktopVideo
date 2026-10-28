const { EventEmitter } = require('node:events');
const { httpUrl, youtubeVideo, youtubeSource, youtubePlaylist } = require('./media.cjs');
const { videoSource } = require('./sites.cjs');
const { safeError, abortError } = require('./processes.cjs');

// The core retries stalled CDN connections itself; allow for a few of them.
const LOAD_TIMEOUT = 60000;

class Playback extends EventEmitter {
  constructor(player, resolve, list) {
    super();
    this.player = player;
    this.resolve = resolve;
    this.list = list;
    this.state = { phase: 'connecting', message: '正在连接播放后端…', title: '', backendOnline: false,
      monitors: [], volume: 50, mute: false, paused: false, canCancel: false, startedAt: 0,
      playlist: null, playlistIndex: -1, source: '' };
    this.job = null;
    this.polling = false;
    this.closed = false;
    this.commands = Promise.resolve();
  }

  control(send, signal) {
    const operation = this.commands.catch(() => {}).then(() => {
      if (this.closed || signal?.aborted) throw abortError();
      return send();
    });
    this.commands = operation;
    return operation;
  }

  update(value) {
    if (this.closed) return;
    Object.assign(this.state, value);
    this.emit('state', { ...this.state });
  }

  async connect() {
    if (this.connecting || this.closed || this.job) return null;
    this.connecting = true;
    this.update({ phase: 'connecting', message: '正在连接播放后端…' });
    try {
      const state = await this.player.connect();
      // Backends from before the Electron UI cannot play resolved streams.
      if (!('playback' in state)) {
        this.update({ backendOnline: false, phase: 'error',
          message: '正在运行的是旧版播放后端。请先退出它（旧版托盘菜单或 muralop quit），再点“重连”。' });
        return null;
      }
      const phase = state.playback === 'playing' ? (state.paused ? 'paused' : 'playing')
        : state.playback === 'loading' ? 'loading' : state.playback === 'error' ? 'error' : 'ready';
      this.update({ ...state, backendOnline: true, phase,
        startedAt: phase === 'loading' ? Date.now() : 0,
        message: phase === 'loading' ? '加载中 · 播放器正在打开视频…' : phase === 'error'
          ? '播放失败：' + safeError(state.error || '无法打开视频') : phase === 'ready' ? '已连接，可以选择视频' : '播放中' });
      return state;
    } catch (error) {
      this.update({ backendOnline: false, phase: 'error', message: safeError(error.message) });
      return null;
    } finally { this.connecting = false; }
  }

  async play(source, monitor = '', quality = '') {
    source = String(source || '').trim();
    // Share sheets often copy a caption together with the actual video URL.
    if (!httpUrl(source) && /https?:\/\//.test(source)) source = source.match(/https?:\/\/[^\s<>"，。]+/)[0];
    source = youtubeSource(source) || videoSource(source) || source;
    if (!source) throw new Error('先选择视频或输入网址');
    if (!this.state.backendOnline) throw new Error('请先连接播放后端');
    const previous = this.job;
    previous?.controller.abort();
    const job = { controller: new AbortController(), monitor, source, sent: false, accepted: false, deadline: 0 };
    this.job = job;
    const current = () => this.job === job && !job.controller.signal.aborted && !this.closed;
    const collection = youtubePlaylist(source);
    const existing = this.state.playlist;
    const entryIndex = existing?.entries.findIndex(entry => entry.source === source) ?? -1;
    this.update({ phase: httpUrl(source) ? 'resolving' : 'loading', canCancel: true, startedAt: Date.now(), source,
      playlist: entryIndex >= 0 ? existing : null, playlistIndex: entryIndex,
      message: httpUrl(source) ? '解析中 · 正在获取视频信息和播放地址…' : '加载中 · 正在打开本地视频…', title: '' });
    try {
      if (previous?.sent) {
        await this.control(() => this.player.request('stop', [previous.monitor]));
        if (!current()) return;
      }
      if (collection) {
        this.update({ message: '解析中 · 正在读取合集全部视频…' });
        const playlist = await this.list(collection, job.controller.signal);
        if (!current()) return;
        const video = youtubeVideo(source);
        const requestedIndex = Number(new URL(source).searchParams.get('index'));
        let index = playlist.entries.findIndex(entry => entry.source === video && (!requestedIndex || entry.index === requestedIndex));
        if (index < 0 && video) index = playlist.entries.findIndex(entry => entry.source === video);
        if (index < 0) index = 0;
        source = playlist.entries[index].source;
        job.source = source;
        this.update({ playlist, playlistIndex: index, source,
          message: `解析中 · 合集共 ${playlist.entries.length} 个视频，正在获取所选视频…` });
      }
      if (httpUrl(source)) {
        const media = await this.resolve(source, quality, job.controller.signal);
        if (!current()) return;
        this.update({ phase: 'loading', title: media.title, message: '加载中 · 正在缓冲音视频…' });
        job.sent = true;
        job.deadline = Date.now() + LOAD_TIMEOUT;
        await this.control(() => this.player.playResolved(media, monitor, job.controller.signal), job.controller.signal);
      } else {
        job.sent = true;
        job.deadline = Date.now() + LOAD_TIMEOUT;
        await this.control(() => this.player.request('set', [source, monitor], job.controller.signal), job.controller.signal);
      }
      if (current()) job.accepted = true;
      // Only a subsequent player status can promote loading to playing.
    } catch (error) {
      if (!current() || error.name === 'AbortError') return;
      this.job = null;
      this.update({ phase: 'error', canCancel: false, message: safeError(error.message) });
    }
  }

  async playEntry(index, monitor = '', quality = '') {
    if (!Number.isInteger(index) || !this.state.playlist?.entries[index]) throw new Error('先解析合集，再选择视频');
    return this.play(this.state.playlist.entries[index].source, monitor, quality);
  }

  async cancel(show = true) {
    const job = this.job;
    this.job = null;
    job?.controller.abort();
    if (show) this.update({ phase: 'cancelled', canCancel: false, message: '已取消，可以重新选择视频' });
    if (job?.sent) {
      try { await this.control(() => this.player.request('stop', [job.monitor])); } catch { /* cancellation still completes if the plugin is offline */ }
    }
  }

  async poll() {
    if (this.polling || !this.state.backendOnline || this.closed) return;
    this.polling = true;
    // A status request already in flight may describe the preceding video.
    const polledJob = this.job, accepted = polledJob?.accepted;
    try {
      const status = await this.player.request('status');
      if (this.job !== polledJob || this.closed) return;
      this.update({ monitors: status.monitors, volume: status.volume, mute: status.mute, paused: status.paused });
      const job = this.job;
      if (job && !accepted) return;
      const targets = job ? (status.monitors || []).filter(m => job.monitor ? m.id === job.monitor : m.video === job.source) : [];
      const playback = targets.length ? (targets.some(m => m.playback === 'loading') ? 'loading'
        : targets.some(m => m.playback === 'error') ? 'error' : targets.every(m => m.playback === 'playing') ? 'playing' : 'idle')
        : job && status.monitors?.length ? 'loading' : status.playback;
      if ((playback === 'error' || status.error && !targets.length) && (job || ['playing', 'paused', 'loading'].includes(this.state.phase))) {
        this.job = null;
        this.update({ phase: 'error', canCancel: false, message: '播放失败：' + safeError(status.error || '无法打开视频') });
      } else if (playback === 'playing' && (job || ['playing', 'paused'].includes(this.state.phase))) {
        this.job = null;
        this.update({ phase: status.paused ? 'paused' : 'playing', canCancel: false,
          message: (status.paused ? '已暂停' : '播放中') + (this.state.title ? ' · ' + this.state.title : '') });
      } else if (job?.sent && Date.now() > job.deadline) {
        await this.cancel(false);
        if (this.job || this.closed) return;
        this.update({ phase: 'error', canCancel: false, message: '视频加载超时（60 秒），可以重试或更换画质。' });
      } else if (!job && playback === 'loading' && !['cancelled', 'error'].includes(this.state.phase)) {
        this.update({ phase: 'loading', message: '加载中 · 播放器正在缓冲视频…' });
      } else if (!job && playback === 'playing' && this.state.phase === 'loading') {
        this.update({ phase: status.paused ? 'paused' : 'playing', message: status.paused ? '已暂停' : '播放中' });
      }
    } catch (error) {
      if (this.job !== polledJob || this.closed) return;
      this.job?.controller.abort();
      this.job = null;
      this.update({ backendOnline: false, phase: 'error', canCancel: false, message: '播放后端失去响应：' + safeError(error.message) });
    } finally { this.polling = false; }
  }

  close() { this.closed = true; this.job?.controller.abort(); this.job = null; }
}

module.exports = { Playback };
