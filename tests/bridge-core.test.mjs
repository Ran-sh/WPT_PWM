import test from 'node:test';
import assert from 'node:assert/strict';

import {
  isApiKeyValid,
  normalizeCommand,
  parseTelemetry
} from '../安卓app/server/bridge-core.mjs';

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
