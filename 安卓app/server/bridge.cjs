/**
 * WPT Bridge Server — HTTP ↔ MQTT 桥接
 * 微信小程序通过 HTTPS 连此服务器，服务器通过 MQTT 连 EMQX
 *
 * 启动: node server/bridge.js
 * 端口: 3000
 */
const express = require('express');
const mqtt    = require('mqtt');
const app     = express();

const MQTT_BROKER = 'mqtt://broker.emqx.io:1883';
const TOPIC_DATA  = 'wpt/20260001/data';
const TOPIC_CMD   = 'wpt/20260001/cmd';

let latestData = { V: 0, I: 0, F: 100000 };
let lastUpdate = 0;

/* ── MQTT 连接 EMQX ── */
const client = mqtt.connect(MQTT_BROKER, {
  clientId: 'wpt_bridge_' + Date.now(),
  clean: true,
  keepalive: 30
});

client.on('connect', () => {
  console.log('[Bridge] Connected to EMQX');
  client.subscribe(TOPIC_DATA, (err) => {
    if (err) console.error('[Bridge] Subscribe error:', err);
    else console.log('[Bridge] Subscribed to', TOPIC_DATA);
  });
});

client.on('message', (topic, msg) => {
  if (topic === TOPIC_DATA) {
    try {
      latestData = JSON.parse(msg.toString());
      lastUpdate = Date.now();
    } catch (e) { /* skip */ }
  }
});

client.on('error', (err) => console.error('[Bridge] MQTT error:', err));
client.on('close', () => console.log('[Bridge] MQTT disconnected, auto-reconnect...'));

/* ── HTTP API ── */
app.use(express.json());

/* CORS — 允许小程序和浏览器跨域 */
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  next();
});

/* GET /data — 获取最新遥测数据 */
app.get('/data', (req, res) => {
  const stale = (Date.now() - lastUpdate) > 5000;
  res.json({
    voltage:   latestData.V,
    current:   latestData.I,
    frequency: latestData.F,
    state:     latestData.S,
    updated:   lastUpdate,
    stale:     stale
  });
});

/* POST /cmd — 下发控制指令 */
app.post('/cmd', (req, res) => {
  const cmd = req.body.cmd;
  if (!cmd) return res.status(400).json({ error: 'missing cmd' });

  console.log('[Bridge] CMD publish:', cmd);
  client.publish(TOPIC_CMD, cmd, { qos: 1 });
  res.json({ ok: true, cmd });
});

/* GET /health — 健康检查 */
app.get('/health', (req, res) => {
  res.json({
    mqtt: client.connected,
    lastUpdate,
    uptime: process.uptime()
  });
});

const PORT = 3000;
app.listen(PORT, () => {
  console.log('[Bridge] Server running on http://localhost:' + PORT);
  console.log('[Bridge] Data endpoint:  GET  http://localhost:' + PORT + '/data');
  console.log('[Bridge] Cmd endpoint:   POST http://localhost:' + PORT + '/cmd');
});
