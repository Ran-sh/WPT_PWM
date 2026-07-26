/**
 * WPT HTTP-MQTT 桥接服务。
 * 遥测接口只返回经过边界校验的数据，控制接口必须携带 X-WPT-Key。
 */
import express from 'express';
import mqtt from 'mqtt';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

import { isApiKeyValid, normalizeCommand, parseTelemetry } from './bridge-core.mjs';

const DEFAULT_DATA = Object.freeze({ V: 0, I: 0, F: 0, S: 0 });

/**
 * 创建桥接服务，参数允许测试或部署环境注入。
 * @param {object} options 服务配置
 * @returns {{app: import('express').Express, client: import('mqtt').MqttClient}}
 */
export function createBridge(options = {}) {
  const broker = options.broker || process.env.WPT_MQTT_BROKER || 'mqtt://broker.emqx.io:1883';
  const deviceName = options.deviceName || process.env.WPT_DEVICE_NAME || 'YOUR_DEVICE_NAME';
  const topicData = options.topicData || `wpt/${deviceName}/data`;
  const topicCmd = options.topicCmd || `wpt/${deviceName}/cmd`;
  const apiKey = options.apiKey ?? process.env.WPT_BRIDGE_API_KEY ?? '';
  const allowedOrigin = options.allowedOrigin ?? process.env.WPT_ALLOWED_ORIGIN ?? '';
  const client = options.client || mqtt.connect(broker, {
    clientId: `wpt_bridge_${Date.now()}_${Math.random().toString(16).slice(2, 10)}`,
    clean: true,
    keepalive: 30,
    reconnectPeriod: 5000,
    connectTimeout: 10000
  });

  let latestData = { ...DEFAULT_DATA };
  let lastUpdate = 0;

  client.on('connect', () => {
    client.subscribe(topicData, { qos: 1 }, (error) => {
      if (error) console.error('[桥接] 遥测订阅失败:', error.message);
    });
  });
  client.on('message', (topic, payload) => {
    if (topic !== topicData) return;
    const parsed = parseTelemetry(payload);
    if (!parsed) return;
    latestData = parsed;
    lastUpdate = Date.now();
  });
  client.on('error', (error) => console.error('[桥接] MQTT错误:', error.message));

  const app = express();
  app.disable('x-powered-by');
  app.use(express.json({ limit: '1kb', strict: true }));
  app.use((request, response, next) => {
    const requestOrigin = request.headers.origin;
    if (requestOrigin && allowedOrigin && requestOrigin !== allowedOrigin) {
      response.status(403).json({ error: 'origin denied' });
      return;
    }
    if (allowedOrigin && requestOrigin === allowedOrigin) {
      response.setHeader('Access-Control-Allow-Origin', allowedOrigin);
      response.setHeader('Vary', 'Origin');
      response.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
      response.setHeader('Access-Control-Allow-Headers', 'Content-Type, X-WPT-Key');
      if (request.method === 'OPTIONS') {
        response.status(204).end();
        return;
      }
    }
    next();
  });

  app.get('/data', (_request, response) => {
    response.json({
      voltage: latestData.V,
      current: latestData.I,
      frequency: latestData.F,
      state: latestData.S,
      updated: lastUpdate,
      stale: lastUpdate === 0 || Date.now() - lastUpdate > 5000
    });
  });

  app.post('/cmd', (request, response) => {
    if (!isApiKeyValid(request.get('X-WPT-Key'), apiKey)) {
      response.status(apiKey ? 401 : 503).json({ error: apiKey ? 'unauthorized' : 'command disabled' });
      return;
    }
    const command = normalizeCommand(request.body?.cmd);
    if (!command) {
      response.status(400).json({ error: 'invalid command' });
      return;
    }
    if (!client.connected) {
      response.status(503).json({ error: 'mqtt offline' });
      return;
    }
    client.publish(topicCmd, command, { qos: 1 }, (error) => {
      if (error) response.status(502).json({ error: 'mqtt publish failed' });
      else response.status(202).json({ ok: true, cmd: command });
    });
  });

  app.get('/health', (_request, response) => {
    response.json({ mqtt: client.connected, lastUpdate, uptime: process.uptime() });
  });

  app.use((error, _request, response, _next) => {
    if (error instanceof SyntaxError) response.status(400).json({ error: 'invalid json' });
    else response.status(500).json({ error: 'internal error' });
  });

  return { app, client };
}

/** 启动独立桥接进程。 */
export function startBridge() {
  const port = Number.parseInt(process.env.PORT || '3000', 10);
  const host = process.env.HOST || '127.0.0.1';
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error('PORT 必须是 1-65535 的整数');
  }
  const { app, client } = createBridge();
  const server = app.listen(port, host, () => {
    console.log(`[桥接] HTTP服务已监听 http://${host}:${port}`);
  });

  const shutdown = () => {
    server.close(() => client.end(false, () => process.exit(0)));
    setTimeout(() => process.exit(1), 5000).unref();
  };
  process.once('SIGINT', shutdown);
  process.once('SIGTERM', shutdown);
  return { server, client };
}

const entryPath = process.argv[1] ? resolve(process.argv[1]) : '';
if (entryPath && fileURLToPath(import.meta.url) === entryPath) {
  try {
    startBridge();
  } catch (error) {
    console.error('[桥接] 启动失败:', error.message);
    process.exitCode = 1;
  }
}
/* WPT Monitor V5.1.3：本地桥接服务入口。 */
