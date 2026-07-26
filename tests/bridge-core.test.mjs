import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';

import {
  isApiKeyValid,
  normalizeCommand,
  parseTelemetry
} from '../安卓app/server/bridge-core.mjs';
import { createBridge } from '../安卓app/server/bridge.mjs';

test('桥接命令只接受完整命令并遵守双档步进', () => {
  assert.equal(normalizeCommand('CMD:ON'), 'CMD:ON');
  assert.equal(normalizeCommand('CMD:OFF'), 'CMD:OFF');
  assert.equal(normalizeCommand('CMD:SETFREQ:20000'), 'CMD:SETFREQ:20000');
  assert.equal(normalizeCommand('CMD:SETFREQ:99900'), 'CMD:SETFREQ:99900');
  assert.equal(normalizeCommand('CMD:SETFREQ:100000'), 'CMD:SETFREQ:100000');
  assert.equal(normalizeCommand('CMD:SETFREQ:200000'), 'CMD:SETFREQ:200000');

  assert.equal(normalizeCommand('CMD:ONLY'), null);
  assert.equal(normalizeCommand('CMD:ON:EXTRA'), null);
  assert.equal(normalizeCommand('CMD:SETFREQ:19900'), null);
  assert.equal(normalizeCommand('CMD:SETFREQ:99950'), null);
  assert.equal(normalizeCommand('CMD:SETFREQ:100100'), null);
  assert.equal(normalizeCommand('CMD:SETFREQ:201000'), null);
});

test('桥接遥测拒绝非有限值、错误状态和越界频率', () => {
  assert.deepEqual(
    parseTelemetry('{"V":12.5,"I":0.42,"F":99900,"S":2}'),
    { V: 12.5, I: 0.42, F: 99900, S: 2 }
  );
  assert.equal(parseTelemetry('{"V":"12.5","I":0.42,"F":99900,"S":2}'), null);
  assert.equal(parseTelemetry('{"V":12.5,"I":0.42,"F":19900,"S":2}'), null);
  assert.equal(parseTelemetry('{"V":12.5,"I":0.42,"F":99900,"S":4}'), null);
  assert.equal(parseTelemetry('not-json'), null);
});

test('控制接口密钥必须存在并进行完整匹配', () => {
  assert.equal(isApiKeyValid('abc123', 'abc123'), true);
  assert.equal(isApiKeyValid('abc123x', 'abc123'), false);
  assert.equal(isApiKeyValid('', ''), false);
  assert.equal(isApiKeyValid(undefined, 'abc123'), false);
});

test('桥接HTTP接口鉴权、CORS与MQTT离线状态一致', async () => {
  class FakeClient extends EventEmitter {
    connected = false;
    subscribe(_topic, _options, callback) { if (callback) callback(null); }
    publish(topic, payload, _options, callback) {
      this.lastPublish = { topic, payload };
      if (callback) callback(null);
    }
  }

  const client = new FakeClient();
  const { app } = createBridge({
    client,
    deviceName: 'dev',
    apiKey: 'secret',
    allowedOrigin: 'https://allowed.example'
  });
  const server = await new Promise((resolve) => {
    const listener = app.listen(0, '127.0.0.1', () => resolve(listener));
  });
  const { port } = server.address();
  const base = `http://127.0.0.1:${port}`;

  try {
    let response = await fetch(`${base}/cmd`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd: 'CMD:ON' })
    });
    assert.equal(response.status, 401);

    response = await fetch(`${base}/cmd`, {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'X-WPT-Key': 'secret' },
      body: JSON.stringify({ cmd: 'CMD:ON' })
    });
    assert.equal(response.status, 503);

    client.connected = true;
    response = await fetch(`${base}/cmd`, {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'X-WPT-Key': 'secret' },
      body: JSON.stringify({ cmd: 'CMD:SETFREQ:99900' })
    });
    assert.equal(response.status, 202);
    assert.deepEqual(client.lastPublish, { topic: 'wpt/dev/cmd', payload: 'CMD:SETFREQ:99900' });

    response = await fetch(`${base}/health`, { headers: { Origin: 'https://blocked.example' } });
    assert.equal(response.status, 403);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
});
