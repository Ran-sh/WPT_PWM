import express from 'express';
import mqtt from 'mqtt';

const MQTT_BROKER = 'mqtt://broker.emqx.io:1883';
const TOPIC_DATA  = 'wpt/20260001/data';
const TOPIC_CMD   = 'wpt/20260001/cmd';

let latestData = { V: 0, I: 0, F: 100000 };
let lastUpdate = 0;

const client = mqtt.connect(MQTT_BROKER, {
  clientId: 'wpt_bridge_' + Date.now(),
  clean: true,
  keepalive: 30
});

client.on('connect', () => {
  console.log('[Bridge] Connected to EMQX');
  client.subscribe(TOPIC_DATA);
});

client.on('message', (topic, msg) => {
  if (topic === TOPIC_DATA) {
    try { latestData = JSON.parse(msg.toString()); lastUpdate = Date.now(); } catch {}
  }
});

const app = express();
app.use(express.json());
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  next();
});

app.get('/data', (req, res) => res.json({
  voltage: latestData.V, current: latestData.I,
  frequency: latestData.F, updated: lastUpdate
}));

app.post('/cmd', (req, res) => {
  const cmd = req.body.cmd;
  if (!cmd) return res.status(400).json({ error: 'missing cmd' });
  client.publish(TOPIC_CMD, cmd, { qos: 1 });
  res.json({ ok: true });
});

app.get('/health', (req, res) => res.json({ mqtt: client.connected }));

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => console.log('[Bridge] Listening on', PORT));
