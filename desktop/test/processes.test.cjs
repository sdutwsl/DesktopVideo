const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Processes } = require('../processes.cjs');

test('a parser that never returns is killed on cancellation', async () => {
  const processes = new Processes();
  const controller = new AbortController();
  let ticks = 0;
  const timer = setInterval(() => ticks++, 5);
  const promise = processes.run(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], { signal: controller.signal });
  setTimeout(() => controller.abort(), 100);
  await assert.rejects(promise, { name: 'AbortError' });
  clearInterval(timer);
  assert.ok(ticks > 5, 'the event loop remained responsive during parsing');
  assert.equal(processes.active.size, 0);
});

test('hung IPC has a deadline and shutdown cancels all active workers', async () => {
  const processes = new Processes();
  await assert.rejects(processes.run(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], { timeout: 70 }), /超时/);
  const pending = processes.run(process.execPath, ['-e', 'setInterval(() => {}, 1000)']);
  processes.close();
  await assert.rejects(pending, { name: 'AbortError' });
  assert.equal(processes.active.size, 0);
});
