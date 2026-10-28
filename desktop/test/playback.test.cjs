const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Playback } = require('../playback.cjs');
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const deferred = () => { let resolve, reject; const promise = new Promise((yes, no) => { resolve = yes; reject = no; }); return { promise, resolve, reject }; };

function plugin() {
  return { calls: [], status: { ok: true, monitors: [], playback: 'idle', volume: 50 },
    async request(command) { this.calls.push(command); return this.status; },
    async playResolved(media) { this.calls.push(media.source); return this.status; } };
}

test('resolving remains responsive, is cancellable and ignores late results', async () => {
  const player = plugin();
  let complete;
  const playback = new Playback(player, () => new Promise(resolve => { complete = resolve; }));
  playback.state.playerReady = true;
  const phases = [];
  playback.on('state', state => phases.push(state.phase));
  const operation = playback.play('https://www.youtube.com/watch?v=abcdefghijk');
  await wait(10);
  assert.equal(playback.state.phase, 'resolving');
  await playback.poll();
  assert.equal(playback.state.phase, 'resolving');
  await playback.cancel();
  assert.equal(playback.state.phase, 'cancelled');
  complete({ source: 'old', video: 'https://media.example/video' });
  await operation;
  assert.ok(!player.calls.includes('old'));
  assert.ok(!phases.includes('playing'));
});

test('loading becomes playing only when the core confirms playback', async () => {
  const player = plugin();
  const playback = new Playback(player, async source => ({ source, title: 'Demo' }));
  playback.state.playerReady = true;
  await playback.play('https://www.youtube.com/watch?v=abcdefghijk');
  assert.equal(playback.state.phase, 'loading');
  await playback.poll();
  assert.equal(playback.state.phase, 'loading');
  player.status.playback = 'playing';
  await playback.poll();
  assert.equal(playback.state.phase, 'playing');
  assert.equal(playback.state.canCancel, false);
});

test('cancelling loading stops the core and a failed resolver reports an error', async () => {
  const player = plugin();
  const playback = new Playback(player, async source => ({ source }));
  playback.state.playerReady = true;
  await playback.play('https://example.com/video');
  await playback.cancel();
  assert.ok(player.calls.includes('stop'));
  playback.resolve = async () => { throw new Error('解析失败'); };
  await playback.play('https://example.com/video');
  assert.equal(playback.state.phase, 'error');
  assert.equal(playback.state.message, '解析失败');
});

test('an unresponsive player reports failure without blocking controls', async () => {
  const player = plugin();
  const playback = new Playback(player, async () => ({}));
  playback.state.playerReady = true;
  player.request = async () => { await wait(20); throw new Error('超时'); };
  const polling = playback.poll();
  await playback.cancel();
  assert.equal(playback.state.phase, 'cancelled');
  await polling;
  assert.equal(playback.state.phase, 'error');
  assert.equal(playback.state.playerReady, false);
});

test('a plugin startup failure leaves playback unavailable and reports the cause', async () => {
  const playback = new Playback({ connect: async () => { throw new Error('播放插件加载失败'); } }, async () => ({}));
  assert.equal(await playback.connect(), null);
  assert.equal(playback.state.playerReady, false);
  assert.equal(playback.state.message, '播放插件加载失败');
});

test('a status response from the preceding video cannot clear the new loading indicator', async () => {
  const player = plugin(), pending = deferred();
  player.status.playback = 'playing';
  player.request = async () => pending.promise;
  const playback = new Playback(player, async source => ({ source }));
  playback.state.playerReady = true;
  const poll = playback.poll();
  await playback.play('https://example.com/new');
  pending.resolve(player.status);
  await poll;
  assert.equal(playback.state.phase, 'loading');
  assert.equal(playback.state.canCancel, true);
});

test('loading stays visible until the new play request is acknowledged', async () => {
  const player = plugin(), accepted = deferred();
  player.status.playback = 'playing';
  player.playResolved = () => accepted.promise;
  const playback = new Playback(player, async source => ({ source }));
  playback.state.playerReady = true;
  const play = playback.play('https://example.com/new');
  await wait(0);
  await playback.poll();
  assert.equal(playback.state.phase, 'loading');
  accepted.resolve();
  await play;
  await playback.poll();
  assert.equal(playback.state.phase, 'playing');
});

test('rapid selections resolve only the last video and report progress immediately', async () => {
  const player = plugin(), first = deferred();
  const playback = new Playback(player, source => source.endsWith('/old') ? first.promise : Promise.resolve({ source }));
  playback.state.playerReady = true;
  const old = playback.play('https://example.com/old');
  assert.equal(playback.state.phase, 'resolving');
  const next = playback.play('https://example.com/new');
  first.resolve({ source: 'https://example.com/old' });
  await Promise.all([old, next]);
  assert.ok(!player.calls.includes('https://example.com/old'));
  assert.ok(player.calls.includes('https://example.com/new'));
});

test('a delayed cancellation cannot stop the video selected after it', async () => {
  const player = plugin(), stop = deferred();
  player.request = async command => { player.calls.push(command); if (command === 'stop') await stop.promise; return player.status; };
  const playback = new Playback(player, async source => ({ source }));
  playback.state.playerReady = true;
  await playback.play('https://example.com/old');
  const cancel = playback.cancel();
  const next = playback.play('https://example.com/new');
  await wait(0);
  assert.ok(!player.calls.includes('https://example.com/new'));
  stop.resolve();
  await Promise.all([cancel, next]);
  assert.equal(player.calls.at(-1), 'https://example.com/new');
  assert.equal(playback.state.phase, 'loading');
});

test('a selected monitor is not affected by errors or playing state on other monitors', async () => {
  const player = plugin();
  const playback = new Playback(player, async source => ({ source }));
  playback.state.playerReady = true;
  await playback.play('https://example.com/new', 'A');
  player.status = { ...player.status, playback: 'error', error: 'other monitor failed', monitors: [
    { id: 'A', video: 'https://example.com/new', playback: 'loading' }, { id: 'B', playback: 'error' },
  ] };
  await playback.poll();
  assert.equal(playback.state.phase, 'loading');
  player.status.monitors[0].playback = 'playing';
  await playback.poll();
  assert.equal(playback.state.phase, 'playing');
});

test('connecting to a loading player retains loading progress until playback begins', async () => {
  const player = plugin();
  player.status.playback = 'loading';
  player.connect = async () => player.status;
  const playback = new Playback(player, async () => ({}));
  await playback.connect();
  assert.equal(playback.state.phase, 'loading');
  player.status.playback = 'playing';
  await playback.poll();
  assert.equal(playback.state.phase, 'playing');
});

test('collections expose every video and switching entries resolves only the selected stream', async () => {
  const player = plugin(), resolved = [];
  const entries = ['abcdefghijk', 'lmnopqrstuv', '12345678901'].map((id, i) => ({ source: `https://www.youtube.com/watch?v=${id}`, index: i + 1, title: `Video ${i + 1}` }));
  const playback = new Playback(player, async source => { resolved.push(source); return { source }; },
    async source => ({ source, title: 'Collection', entries }));
  playback.state.playerReady = true;
  await playback.play('https://www.youtube.com/watch?v=lmnopqrstuv&list=PLdemo&index=2');
  assert.equal(playback.state.playlist.entries.length, 3);
  assert.equal(playback.state.playlistIndex, 1);
  assert.deepEqual(resolved, [entries[1].source]);
  await playback.playEntry(2);
  assert.equal(playback.state.playlistIndex, 2);
  assert.deepEqual(resolved, [entries[1].source, entries[2].source]);
  await assert.rejects(playback.playEntry(-1), /选择视频/);
});

test('collection discovery is cancellable and ignores late list results', async () => {
  const player = plugin(), list = deferred();
  const playback = new Playback(player, async source => ({ source }), () => list.promise);
  playback.state.playerReady = true;
  const play = playback.play('https://www.youtube.com/playlist?list=PLdemo');
  assert.match(playback.state.message, /读取合集/);
  await playback.cancel();
  list.resolve({ entries: [{ source: 'https://example.com/late' }] });
  await play;
  assert.equal(playback.state.phase, 'cancelled');
  assert.equal(playback.state.playlist, null);
  assert.ok(!player.calls.includes('https://example.com/late'));
});
