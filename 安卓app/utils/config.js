/* ═══════════════════════════════════════════
   WPT Monitor — 数据模型配置（V5.1.2）
   已保存的纯数据配置会在读取时重新补回换算函数。
   ═══════════════════════════════════════════ */

var DATA_MODEL_VERSION = 2;

function frequencyFromCloud(value) {
  return Math.round(Number(value) / 100) / 10;
}

function frequencyToCloud(value) {
  return Math.round(Number(value) * 1000);
}

var DEFAULT_DATA_MODEL = {
  version: DATA_MODEL_VERSION,
  sensors: [
    { id: 'voltage', name: '电压', cloudKey: 'V', unit: 'V', min: 0, max: 50, dataType: 'float', step: 0.01, colorHex: '#06b6d4', colorBg: 'rgba(6,182,212,0.25)' },
    { id: 'current', name: '电流', cloudKey: 'I', unit: 'A', min: 0, max: 5, dataType: 'float', step: 0.001, colorHex: '#eab308', colorBg: 'rgba(234,179,8,0.25)' },
    { id: 'freq', name: '频率', cloudKey: 'F', unit: 'kHz', min: 20, max: 200, dataType: 'int32', step: 0.1, colorHex: '#3b82f6', colorBg: 'rgba(59,130,246,0.25)', fromCloud: frequencyFromCloud }
  ],
  controls: [
    { id: 'switch', name: '启停控制', cloudKey: 'Switch', dataType: 'bool' },
    { id: 'setfreq', name: '频率设置', cloudKey: 'SetFreq', dataType: 'int32', step: 0.1, min: 20, max: 200, toCloud: frequencyToCloud, fromCloud: frequencyFromCloud }
  ]
};

function copyFields(source) {
  var target = {}, key;
  if (!source || typeof source !== 'object') return target;
  for (key in source) {
    if (source.hasOwnProperty(key) && typeof source[key] !== 'function') target[key] = source[key];
  }
  return target;
}

function normalizeGroup(savedItems, defaults) {
  var saved = Array.isArray(savedItems) ? savedItems : [];
  var result = [], used = {}, i, j, item, merged;
  for (i = 0; i < defaults.length; i++) {
    item = null;
    for (j = 0; j < saved.length; j++) {
      if (saved[j] && saved[j].id === defaults[i].id) { item = saved[j]; used[j] = true; break; }
    }
    merged = copyFields(defaults[i]);
    if (item) {
      var savedFields = copyFields(item), key;
      for (key in savedFields) { if (savedFields.hasOwnProperty(key)) merged[key] = savedFields[key]; }
    }
    /* 固件约束字段不能被旧缓存覆盖，显示名称和配色仍允许自定义。 */
    if (merged.id === 'current') merged.max = 5;
    if (merged.id === 'freq' || merged.id === 'setfreq') {
      merged.min = 20; merged.max = 200; merged.step = 0.1;
      merged.fromCloud = frequencyFromCloud;
      if (merged.id === 'setfreq') merged.toCloud = frequencyToCloud;
    }
    result.push(merged);
  }
  for (j = 0; j < saved.length; j++) {
    if (!used[j] && saved[j] && saved[j].id) result.push(copyFields(saved[j]));
  }
  return result;
}

function normalizeDataModel(model) {
  var source = model && typeof model === 'object' ? model : {};
  return {
    version: DATA_MODEL_VERSION,
    sensors: normalizeGroup(source.sensors, DEFAULT_DATA_MODEL.sensors),
    controls: normalizeGroup(source.controls, DEFAULT_DATA_MODEL.controls)
  };
}

function getDataModel() {
  try {
    var saved = wx.getStorageSync('wpt_data_model');
    return normalizeDataModel(saved);
  } catch (e) {
    return normalizeDataModel(null);
  }
}

function saveDataModel(model) {
  /* 存储层只保存纯数据，读取时再恢复函数，避免序列化后换算逻辑消失。 */
  var serializable = JSON.parse(JSON.stringify(normalizeDataModel(model)));
  wx.setStorageSync('wpt_data_model', serializable);
}

function getDecimals(dataType, step) {
  if (step !== undefined && step !== null) {
    var text = String(step);
    if (text.indexOf('.') !== -1) return text.split('.')[1].length;
  }
  return dataType === 'int32' ? 0 : 1;
}

module.exports = {
  DATA_MODEL_VERSION: DATA_MODEL_VERSION,
  DEFAULT_DATA_MODEL: DEFAULT_DATA_MODEL,
  normalizeDataModel: normalizeDataModel,
  getDataModel: getDataModel,
  saveDataModel: saveDataModel,
  getDecimals: getDecimals
};
