const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Playback } = require('../playback.cjs');
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

function plugin() {
  return { calls: [], status: { ok: true, monitors: [], playback: 'idle', volume: 50 },
    async request(command) { this.calls.push(command); return this.status; },
    async playResolved(media) { this.calls.push(media.source); return this.status; } };
}

test('resolving remains responsive, is cancellable and ignores late results', async () => {
  const player = plugin();
  let complete;
  const playback = new Playback(player, () => new Promise(resolve => { complete = resolve; }));
  playback.state.backendOnline = true;
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
  playback.state.backendOnline = true;
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
  playback.state.backendOnline = true;
  await playback.play('https://example.com/video');
  await playback.cancel();
  assert.ok(player.calls.includes('stop'));
  playback.resolve = async () => { throw new Error('解析失败'); };
  await playback.play('https://example.com/video');
  assert.equal(playback.state.phase, 'error');
  assert.equal(playback.state.message, '解析失败');
});

test('an unresponsive backend reports failure without blocking controls', async () => {
  const player = plugin();
  const playback = new Playback(player, async () => ({}));
  playback.state.backendOnline = true;
  player.request = async () => { await wait(20); throw new Error('超时'); };
  const polling = playback.poll();
  await playback.cancel();
  assert.equal(playback.state.phase, 'cancelled');
  await polling;
  assert.equal(playback.state.phase, 'error');
  assert.equal(playback.state.backendOnline, false);
});

test('an old backend without playback reporting is refused with a hint', async () => {
  const playback = new Playback({ connect: async () => ({ ok: true, monitors: [], volume: 50 }) }, async () => ({}));
  assert.equal(await playback.connect(), null);
  assert.equal(playback.state.backendOnline, false);
  assert.match(playback.state.message, /旧版播放后端/);
});
