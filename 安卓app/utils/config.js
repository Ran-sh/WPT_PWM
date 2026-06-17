/* ═══════════════════════════════════════════
   WPT Monitor — 数据模型配置 (唯一权威来源)
   对齐 Web 端 js/config.js DEFAULT_DATA_MODEL
   所有模块通过 getDataModel() / saveDataModel() 读写
   ═══════════════════════════════════════════ */

var DEFAULT_DATA_MODEL = {
  _version: 2,
  sensors: [
    { id: 'voltage', name: '电压', cloudKey: 'V', unit: 'V', min: 0, max: 50, dataType: 'float', step: 0.01, colorHex: '#06b6d4', colorBg: 'rgba(6,182,212,0.25)' },
    { id: 'current', name: '电流', cloudKey: 'I', unit: 'A', min: 0, max: 10, dataType: 'float', step: 0.001, colorHex: '#eab308', colorBg: 'rgba(234,179,8,0.25)' },
    { id: 'freq',    name: '频率', cloudKey: 'F', unit: 'kHz', min: 95, max: 150, dataType: 'int32', step: 1, colorHex: '#3b82f6', colorBg: 'rgba(59,130,246,0.25)',
      fromCloud: function(v) { return Math.floor(v / 1000); } }
  ],
  controls: [
    { id: 'switch',  name: '启停控制', cloudKey: 'Switch',  dataType: 'bool' },
    { id: 'Switch_WIFI', name: 'WiFi开关', cloudKey: 'Switch_WIFI', dataType: 'bool' },
    { id: 'setfreq', name: '频率设置', cloudKey: 'SetFreq', dataType: 'int32', step: 1, min: 95, max: 150,
      toCloud: function(v) { return v * 1000; },
      fromCloud: function(v) { return Math.floor(v / 1000); } }
  ]
};

function getDataModel() {
  try {
    var saved = wx.getStorageSync('wpt_data_model');
    if (saved && saved.sensors && saved.controls) {
      /* 版本更新时自动刷新 */
      if (!saved._version || saved._version < DEFAULT_DATA_MODEL._version) {
        wx.removeStorageSync('wpt_data_model');
        return DEFAULT_DATA_MODEL;
      }
      return saved;
    }
  } catch (e) {}
  return DEFAULT_DATA_MODEL;
}

function saveDataModel(model) {
  wx.setStorageSync('wpt_data_model', model);
}

function getDecimals(dataType, step) {
  if (dataType === 'int32') return 0;
  if (step === undefined || step === null) return 1;
  var s = String(step);
  if (s.indexOf('.') !== -1) return s.split('.')[1].length;
  return 0;
}

module.exports = {
  DEFAULT_DATA_MODEL: DEFAULT_DATA_MODEL,
  getDataModel: getDataModel,
  saveDataModel: saveDataModel,
  getDecimals: getDecimals
};
