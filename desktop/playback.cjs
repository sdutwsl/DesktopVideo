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
    this.state = { phase: 'connecting', message: '正在启动播放…', title: '', playerReady: false,
      monitors: [], volume: 50, mute: false, paused: false, canCancel: false, startedAt: 0,
      playlist: null, playlistIndex: -1, source: '' };
    this.playlistCache = new Map();
    this.listTask = null;
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

  stopPlaylist() {
    this.listTask?.controller.abort();
    this.listTask?.rejectFirst(abortError());
    this.listTask = null;
  }

  clearPlaylistCache() {
    this.playlistCache.clear();
    this.stopPlaylist();
    this.update({ playlist: null, playlistIndex: -1, playlistLoading: false, playlistError: '' });
  }

  playerFailed(message) {
    this.job?.controller.abort();
    this.job = null;
    this.stopPlaylist();
    this.update({ playerReady: false, phase: 'error', canCancel: false, playlistLoading: false, message });
  }

  startPlaylist(source) {
    const id = new URL(source).searchParams.get('list');
    if (this.listTask?.id === id && (!this.listTask.completeAt || Date.now() - this.listTask.completeAt < 10 * 60 * 1000)) return this.listTask;
    this.stopPlaylist();
    let resolveFirst, rejectFirst;
    const first = new Promise((resolve, reject) => { resolveFirst = resolve; rejectFirst = reject; });
    // Watch links already identify a video, so they need not await this promise.
    first.catch(() => {});
    const task = { id, controller: new AbortController(), first, rejectFirst };
    this.listTask = task;
    const current = () => this.listTask === task && !task.controller.signal.aborted && !this.closed;
    const accept = playlist => {
      if (!current()) return;
      const selected = youtubeVideo(this.state.source) || this.state.source;
      const requestedIndex = Number(new URL(source).searchParams.get('index'));
      let index = playlist.entries.findIndex(entry => entry.source === selected && (!requestedIndex || entry.index === requestedIndex));
      if (index < 0) index = playlist.entries.findIndex(entry => entry.source === selected);
      this.update({ playlist, playlistIndex: index, playlistLoading: !playlist.complete, playlistError: '' });
      if (playlist.entries.length) resolveFirst(playlist.entries[0].source);
    };
    const cached = this.playlistCache.get(id);
    if (cached && Date.now() - cached.at < 10 * 60 * 1000) {
      task.completeAt = cached.at;
      accept(cached.playlist);
      return task;
    }
    // Normal playlists can be fetched directly, avoiding the watch-page lookup.
    const listSource = id.startsWith('RD') ? source : `https://www.youtube.com/playlist?list=${encodeURIComponent(id)}`;
    this.update({ playlistLoading: true, playlistError: '' });
    void Promise.resolve().then(() => {
      if (!current()) throw abortError();
      return this.list(listSource, task.controller.signal, accept);
    }).then(result => {
      if (!current()) return;
      const playlist = { ...result, complete: true };
      task.completeAt = Date.now();
      this.playlistCache.delete(id);
      this.playlistCache.set(id, { at: Date.now(), playlist });
      if (this.playlistCache.size > 10) this.playlistCache.delete(this.playlistCache.keys().next().value);
      accept(playlist);
    }).catch(error => {
      rejectFirst(error);
      if (current()) {
        this.listTask = null;
        this.update({ playlistLoading: false, playlistError: safeError(error.message) });
      }
    });
    return task;
  }

  async connect() {
    if (this.connecting || this.closed || this.job) return null;
    this.connecting = true;
    this.connection = new AbortController();
    this.update({ phase: 'connecting', message: '正在启动播放…' });
    try {
      const state = await this.player.connect(this.connection.signal);
      const phase = state.playback === 'playing' ? (state.paused ? 'paused' : 'playing')
        : state.playback === 'loading' ? 'loading' : state.playback === 'error' ? 'error' : 'ready';
      this.update({ ...state, playerReady: true, phase,
        startedAt: phase === 'loading' ? Date.now() : 0,
        message: phase === 'loading' ? '加载中 · 播放器正在打开视频…' : phase === 'error'
          ? '播放失败：' + safeError(state.error || '无法打开视频') : phase === 'ready' ? '播放已就绪，可以选择视频' : '播放中' });
      return state;
    } catch (error) {
      this.update({ playerReady: false, phase: 'error', message: safeError(error.message) });
      return null;
    } finally { this.connecting = false; }
  }

  async play(source, monitor = '', quality = '') {
    source = String(source || '').trim();
    // Share sheets often copy a caption together with the actual video URL.
    if (!httpUrl(source) && /https?:\/\//.test(source)) source = source.match(/https?:\/\/[^\s<>"，。]+/)[0];
    source = youtubeSource(source) || videoSource(source) || source;
    if (!source) throw new Error('先选择视频或输入网址');
    if (!this.state.playerReady) throw new Error('播放尚未就绪，请先重启播放');
    const previous = this.job;
    previous?.controller.abort();
    const job = { controller: new AbortController(), monitor, source, sent: false, accepted: false, deadline: 0 };
    this.job = job;
    const current = () => this.job === job && !job.controller.signal.aborted && !this.closed;
    const collection = youtubePlaylist(source);
    const existing = this.state.playlist;
    const entryIndex = existing?.entries.findIndex(entry => entry.source === (youtubeVideo(source) || source)) ?? -1;
    const sameCollection = collection && this.listTask?.id === new URL(collection).searchParams.get('list');
    if (!sameCollection && entryIndex < 0) this.stopPlaylist();
    this.update({ phase: httpUrl(source) ? 'resolving' : 'loading', canCancel: true, startedAt: Date.now(), source,
      playlist: entryIndex >= 0 || sameCollection ? existing : null, playlistIndex: entryIndex,
      playlistLoading: Boolean(this.listTask && this.state.playlistLoading), playlistError: '',
      message: httpUrl(source) ? '解析中 · 正在获取视频信息和播放地址…' : '加载中 · 正在打开本地视频…', title: '' });
    try {
      if (previous?.sent) {
        await this.control(() => this.player.request('stop', [previous.monitor]));
        if (!current()) return;
      }
      if (collection) {
        const video = youtubeVideo(source);
        this.update({ message: video ? '解析中 · 正在获取所选视频，合集列表在后台读取…' : '解析中 · 正在读取合集首个视频…' });
        const task = this.startPlaylist(collection);
        source = video || await task.first;
        if (!current()) return;
        job.source = source;
        this.update({ source, playlistIndex: this.state.playlist?.entries.findIndex(entry => entry.source === source) ?? -1 });
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
    const listingOnly = !job && this.state.playlistLoading;
    this.job = null;
    job?.controller.abort();
    this.stopPlaylist();
    this.update({ playlistLoading: false, ...(listingOnly ? { playlistError: '已停止读取' } : {}) });
    if (show && !listingOnly) this.update({ phase: 'cancelled', canCancel: false, message: '已取消，可以重新选择视频' });
    if (job?.sent) {
      try { await this.control(() => this.player.request('stop', [job.monitor])); } catch { /* cancellation still completes if the plugin is offline */ }
    }
  }

  async poll() {
    if (this.polling || !this.state.playerReady || this.closed) return;
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
      if (this.job !== polledJob || this.closed || !this.state.playerReady) return;
      this.playerFailed('播放插件失去响应：' + safeError(error.message));
    } finally { this.polling = false; }
  }

  close() { this.closed = true; this.connection?.abort(); this.job?.controller.abort(); this.stopPlaylist(); this.job = null; }
}

module.exports = { Playback };
