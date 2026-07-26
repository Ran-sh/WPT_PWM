const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const vm = require('node:vm');

const root = path.resolve(__dirname, '..');
const configPath = path.join(root, '安卓app', 'utils', 'config.js');
const oneNetPath = path.join(root, '安卓app', 'utils', 'onenet.js');
const webConfigPath = path.join(root, 'ONENETapp', 'js', 'config.js');

function loadMiniModules(initialStorage, oneNetConfig) {
  const storage = Object.assign({}, initialStorage || {});
  global.wx = {
    getStorageSync: (key) => storage[key] === undefined ? '' : storage[key],
    setStorageSync: (key, value) => { storage[key] = value; },
    removeStorageSync: (key) => { delete storage[key]; },
    request: () => { throw new Error('本测试不应发起网络请求'); }
  };
  global.getApp = () => ({
    getOneNetConfig: () => oneNetConfig || ({ PRODUCT_ID: '', DEVICE_NAME: '', TOKEN: '', BASE_URL: 'https://iot-api.heclouds.com' })
  });
  delete require.cache[require.resolve(configPath)];
  delete require.cache[require.resolve(oneNetPath)];
  return {
    config: require(configPath),
    oneNet: require(oneNetPath),
    storage
  };
}

test('小程序数据模型与固件的 20–200kHz 和 5A 边界一致', () => {
  const { config } = loadMiniModules();
  const current = config.DEFAULT_DATA_MODEL.sensors.find((item) => item.id === 'current');
  const freq = config.DEFAULT_DATA_MODEL.sensors.find((item) => item.id === 'freq');
  const setFreq = config.DEFAULT_DATA_MODEL.controls.find((item) => item.id === 'setfreq');
  assert.equal(current.max, 5);
  assert.deepEqual([freq.min, freq.max, freq.step], [20, 200, 0.1]);
  assert.deepEqual([setFreq.min, setFreq.max, setFreq.step], [20, 200, 0.1]);
  assert.equal(config.getDecimals('int32', 0.1), 1);
});

test('旧缓存模型迁移后仍保留云端频率换算函数', () => {
  const legacy = {
    sensors: [
      { id: 'current', name: '电流', cloudKey: 'I', unit: 'A', min: 0, max: 10, dataType: 'float', step: 0.001 },
      { id: 'freq', name: '频率', cloudKey: 'F', unit: 'kHz', min: 95, max: 150, dataType: 'int32', step: 1 }
    ],
    controls: [
      { id: 'setfreq', name: '频率设置', cloudKey: 'SetFreq', min: 95, max: 150, dataType: 'int32', step: 1 }
    ]
  };
  const { config } = loadMiniModules({ wpt_data_model: legacy });
  const model = config.getDataModel();
  const freq = model.sensors.find((item) => item.id === 'freq');
  const setFreq = model.controls.find((item) => item.id === 'setfreq');
  assert.equal(freq.fromCloud(99900), 99.9);
  assert.equal(setFreq.toCloud(99.9), 99900);
  assert.deepEqual([freq.min, freq.max, freq.step], [20, 200, 0.1]);
});

test('频率列表按低档 0.1kHz、高档 1kHz 生成', () => {
  const { oneNet } = loadMiniModules();
  const list = oneNet.buildFreqList().list;
  assert.equal(list[0], 20);
  assert.equal(list[list.length - 1], 200);
  assert.equal(list.includes(99.9), true);
  assert.equal(list.includes(100), true);
  assert.equal(list.includes(100.1), false);
});

test('连续越界但处于抑制窗口时也会锁存报警状态', () => {
  const now = Date.now();
  const initial = {
    wpt_alerts: [{ id: now - 1000, title: '电压过高报警', timestamp: now - 1000, status: 'unread' }],
    wpt_alarm_states: {}
  };
  const { oneNet, storage } = loadMiniModules(initial);
  assert.deepEqual(oneNet.checkAlerts({ voltage: 60, _isOnline: true }, false), []);
  assert.equal(storage.wpt_alarm_states.voltage_high, true);
  assert.equal(storage.wpt_alerts.length, 1);
});

test('跨日期的同一分钟仍会保存一条新历史记录', () => {
  const now = new Date();
  const old = new Date(now.getTime() - 24 * 60 * 60 * 1000);
  const hhmm = ('0' + now.getHours()).slice(-2) + ':' + ('0' + now.getMinutes()).slice(-2);
  const initial = {
    wpt_history: [{ time: hhmm, fullTime: old.toISOString(), timestamp: old.getTime(), data: { voltage: 1 } }]
  };
  const { oneNet, storage } = loadMiniModules(initial);
  oneNet.saveHistory({ voltage: 2 });
  assert.equal(storage.wpt_history.length, 2);
});

test('小程序在发起网络请求前拒绝越界或非步进频率', async () => {
  const cfg = { PRODUCT_ID: 'p', DEVICE_NAME: 'd', TOKEN: 't', BASE_URL: 'https://iot-api.heclouds.com' };
  const { oneNet } = loadMiniModules({}, cfg);
  assert.equal(await oneNet.setProperty(null, { setfreq: 19.9 }), false);
  assert.equal(await oneNet.setProperty(null, { setfreq: 99.95 }), false);
  assert.equal(await oneNet.setProperty(null, { setfreq: 100.1 }), false);
  assert.equal(await oneNet.setProperty(null, { switch: 'true' }), false);
});

test('网页端旧数据模型也会迁移并恢复频率换算', () => {
  const legacy = JSON.stringify({
    sensors: [
      { id: 'current', name: '电流', max: 10 },
      { id: 'freq', name: '频率', min: 95, max: 150, step: 1 }
    ],
    controls: [{ id: 'setfreq', min: 95, max: 150, step: 1 }]
  });
  const context = {
    localStorage: {
      getItem: (key) => key === 'iot_data_model' ? legacy : null,
      setItem: () => {}
    },
    document: { createElement: () => ({ textContent: '', innerHTML: '' }) },
    Set, Object, Array, JSON, Math, Number, String
  };
  vm.createContext(context);
  const source = fs.readFileSync(webConfigPath, 'utf8') +
    '\n;globalThis.__test = { getDataModel, getDecimals };';
  vm.runInContext(source, context, { filename: webConfigPath });
  const model = context.__test.getDataModel();
  const current = model.sensors.find((item) => item.id === 'current');
  const freq = model.sensors.find((item) => item.id === 'freq');
  const setFreq = model.controls.find((item) => item.id === 'setfreq');
  assert.equal(current.max, 5);
  assert.equal(freq.fromCloud(99900), 99.9);
  assert.equal(setFreq.toCloud(99.9), 99900);
  assert.equal(context.__test.getDecimals('int32', 0.1), 1);
});
