/* ═══════════════════════════════════════════
   WPT Monitor — OneNET Service
   完全对齐 Web ONENETapp/js/onenet.js OneNetService
   数据模型从 utils/config.js 读取 (单一来源)
   ═══════════════════════════════════════════ */

var Config = require('./config.js');

var POLL_BASE = 5000;
var POLL_MAX  = 30000;
var LOCK_MS   = 3000;

function getOneNetConfig() {
  try {
    var app = getApp();
    if (app && app.getOneNetConfig) return app.getOneNetConfig();
  } catch (e) {}
  return {
    PRODUCT_ID: '1iS397oJFL', DEVICE_NAME: '20260001',
    TOKEN: 'version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D',
    BASE_URL: 'https://iot-api.heclouds.com'
  };
}

function getDataModel() { return Config.getDataModel(); }
function getDecimals(dataType, step) { return Config.getDecimals(dataType, step); }
function getPollInterval(fail) { return Math.min(POLL_BASE * Math.pow(2, fail || 0), POLL_MAX); }

/* ═══════════════════════════════════════════
   getLatestData — 对齐 Web OneNetService.getLatestData()
   - HTTP 状态码细化错误信息
   - /device/detail 并行请求在线状态
   - Mock data fallback (未配置时)
   ═══════════════════════════════════════════ */
function getLatestData(cfg) {
  if (!cfg) cfg = getOneNetConfig();

  /* Mock data: Token 未配置时返回预览数据 */
  if (cfg.TOKEN.indexOf('YOUR_') !== -1) {
    return Promise.resolve(getMockData());
  }

  return new Promise(function(resolve, reject) {
    /* 数据请求 */
    var dataDone = false, statusDone = false;
    var dataResult = null, statusResult = null;
    var isOnline = false;

    function checkDone() {
      if (!dataDone) return;
      /* 数据到达后判断在线 */
      if (dataResult._isOnline !== undefined) {
        dataResult._isOnline = isOnline || dataResult._isOnline;
      }
      resolve(dataResult);
    }

    wx.request({
      url: cfg.BASE_URL + '/thingmodel/query-device-property?product_id=' + cfg.PRODUCT_ID + '&device_name=' + cfg.DEVICE_NAME,
      method: 'GET', header: { 'Authorization': cfg.TOKEN },
      success: function(res) {
        if (res.statusCode !== 200 || res.data.code !== 0) {
          var errMsg = httpErrorMessage(res.statusCode, res.data);
          dataResult = { _isOnline: false, _error: errMsg };
          dataDone = true;
          reject(new Error(errMsg));
          return;
        }
        var rawData = {};
        (res.data.data || []).forEach(function(item) {
          var v = item.value;
          if (v === 'true') v = true; else if (v === 'false') v = false;
          else if (!isNaN(v) && v !== '' && v !== undefined) v = Number(v);
          rawData[item.identifier] = v;
        });
        var model = getDataModel();
        var data = {};
        model.sensors.forEach(function(s) { if (rawData[s.cloudKey] !== undefined) { var v = rawData[s.cloudKey]; if (s.fromCloud) v = s.fromCloud(v); data[s.id] = v; } });
        model.controls.forEach(function(c) { if (rawData[c.cloudKey] !== undefined) { var v = rawData[c.cloudKey]; if (c.fromCloud) v = c.fromCloud(v); data[c.id] = v; } });
        /* 乐观锁保护 */
        var cachedData = wx.getStorageSync('wpt_latest') || {};
        var controlLocks = wx.getStorageSync('wpt_control_locks') || {};
        var now = Date.now();
        for (var k in data) { if (data.hasOwnProperty(k) && controlLocks[k] && (now - controlLocks[k] < LOCK_MS)) { data[k] = cachedData[k]; } }
        var newData = extend({}, cachedData, data);
        wx.setStorageSync('wpt_latest', newData);
        saveHistory(newData);
        data._raw = rawData;
        data._isOnline = (res.data.data && res.data.data.length > 0);
        dataResult = data;
        dataDone = true;
        if (statusDone) checkDone();
        else resolve(data);
      },
      fail: function() { reject(new Error('网络请求失败, 请检查网络连接')); }
    });

    /* 并行: /device/detail 获取在线状态 */
    wx.request({
      url: cfg.BASE_URL + '/device/detail?product_id=' + cfg.PRODUCT_ID + '&device_name=' + cfg.DEVICE_NAME,
      method: 'GET', header: { 'Authorization': cfg.TOKEN },
      success: function(res) {
        if (res.statusCode === 200 && res.data.code === 0 && res.data.data) {
          var st = res.data.data.status;
          isOnline = (st === 1 || st === 2 || st === '在线');
        }
        statusDone = true;
        if (dataDone && dataResult) dataResult._isOnline = isOnline || dataResult._isOnline;
      },
      fail: function() { statusDone = true; }
    });
  });
}

/* ═══════════════════════════════════════════
   setProperty — 对齐 Web OneNetService.setProperty()
   ═══════════════════════════════════════════ */
function setProperty(cfg, params) {
  if (!cfg) cfg = getOneNetConfig();
  return new Promise(function(resolve) {
    var retries = 3;
    var model = getDataModel();
    var reverseMap = {};
    model.controls.forEach(function(c) { reverseMap[c.id] = c.cloudKey; });
    model.sensors.forEach(function(s) { reverseMap[s.id] = s.cloudKey; });
    var mapped = {};
    for (var k in params) { if (!params.hasOwnProperty(k)) continue;
      var v = params[k];
      for (var j = 0; j < model.controls.length; j++) { if (model.controls[j].id === k && model.controls[j].toCloud) { v = model.controls[j].toCloud(v); break; } }
      mapped[reverseMap[k] || k] = v;
    }
    var UNRECOVERABLE = [401, 403];
    function _go(left) {
      wx.request({
        url: cfg.BASE_URL + '/thingmodel/set-device-property', method: 'POST',
        header: { 'Authorization': cfg.TOKEN, 'Content-Type': 'application/json' },
        data: { product_id: cfg.PRODUCT_ID, device_name: cfg.DEVICE_NAME, params: mapped },
        success: function(res) {
          if (UNRECOVERABLE.indexOf(res.statusCode) !== -1) { resolve(false); return; }
          if (res.statusCode === 200 && res.data.code === 0) {
            var cached = wx.getStorageSync('wpt_latest') || {};
            var locks = wx.getStorageSync('wpt_control_locks') || {};
            var n = Date.now();
            for (var k2 in params) { if (!params.hasOwnProperty(k2)) continue; cached[k2] = params[k2]; locks[k2] = n; }
            wx.setStorageSync('wpt_latest', cached);
            wx.setStorageSync('wpt_control_locks', locks);
            resolve(true);
          } else { if (left > 1) { setTimeout(function() { _go(left - 1); }, 500); } else { resolve(false); } }
        },
        fail: function() { if (left > 1) { setTimeout(function() { _go(left - 1); }, 800); } else { resolve(false); } }
      });
    }
    _go(retries);
  });
}

/* ═══════════════════════════════════════════
   checkAlerts — 使用 sensor.min/max 做阈值
   (不用 ALERT_CONFIG, 与 Web index.html checkAlert() 一致)
   ═══════════════════════════════════════════ */
function checkAlerts(data, isFromCache) {
  if (isFromCache || data._isOnline === false) return [];
  var model = getDataModel();
  var alerts = wx.getStorageSync('wpt_alerts') || [];
  var alarmStates = wx.getStorageSync('wpt_alarm_states') || {};
  var now = new Date(), timeStr = ('0'+now.getHours()).slice(-2)+':'+('0'+now.getMinutes()).slice(-2);
  var newAlerts = [];

  model.sensors.forEach(function(s) {
    var val = data[s.id];
    if (val === undefined) return;
    var nv = Number(val); if (isNaN(nv)) return;
    var highKey = s.id + '_high';
    if (nv > s.max) {
      if (!alarmStates[highKey]) {
        var last = null;
        for (var i = 0; i < alerts.length; i++) { if (alerts[i].title === s.name + '过高报警') { last = alerts[i]; break; } }
        if (!last || (now.getTime() - last.timestamp > 5 * 60 * 1000)) {
          alarmStates[highKey] = true;
          alerts.unshift({ id: Date.now(), title: s.name + '过高报警', type: s.id, currentValue: nv, threshold: s.max, unit: s.unit || '', time: timeStr, timestamp: now.getTime(), status: 'unread', isCritical: true });
          newAlerts.push(s.name + '过高报警');
        }
      }
    } else { alarmStates[highKey] = false; }
    var lowKey = s.id + '_low';
    if (nv < s.min && s.min > 0) {
      if (!alarmStates[lowKey]) {
        var l2 = null;
        for (var j = 0; j < alerts.length; j++) { if (alerts[j].title === s.name + '过低报警') { l2 = alerts[j]; break; } }
        if (!l2 || (now.getTime() - l2.timestamp > 5 * 60 * 1000)) {
          alarmStates[lowKey] = true;
          alerts.unshift({ id: Date.now(), title: s.name + '过低报警', type: s.id, currentValue: nv, threshold: s.min, unit: s.unit || '', time: timeStr, timestamp: now.getTime(), status: 'unread', isCritical: false });
          newAlerts.push(s.name + '过低报警');
        }
      }
    } else { alarmStates[lowKey] = false; }
  });

  if (alerts.length > 50) alerts.splice(50);
  wx.setStorageSync('wpt_alerts', alerts);
  wx.setStorageSync('wpt_alarm_states', alarmStates);
  return newAlerts;
}

/* ═══════════════════════════════════════════
   saveHistory — 每分钟一条, 最多 1440 条
   ═══════════════════════════════════════════ */
function saveHistory(data) {
  var model = getDataModel();
  var now = new Date();
  var timeStr = ('0'+now.getHours()).slice(-2)+':'+('0'+now.getMinutes()).slice(-2);
  var fullTimeStr = now.getFullYear()+'-'+('0'+(now.getMonth()+1)).slice(-2)+'-'+('0'+now.getDate()).slice(-2)+' '+timeStr+':'+('0'+now.getSeconds()).slice(-2);
  var rec = {};
  model.sensors.forEach(function(s) { if (data[s.id] !== undefined) rec[s.id] = data[s.id]; });
  model.controls.forEach(function(c) { if (data[c.id] !== undefined) rec[c.id] = data[c.id]; });
  var h = wx.getStorageSync('wpt_history') || [];
  if (h.length === 0 || h[h.length - 1].time !== timeStr) {
    h.push({ time: timeStr, fullTime: fullTimeStr, timestamp: now.getTime(), data: rec });
    if (h.length > 1440) h.shift();
    wx.setStorageSync('wpt_history', h);
  }
  return h;
}

/* ══ buildFreqList ══ */
var FREQ_CACHE = null;
function buildFreqList() {
  if (FREQ_CACHE) return FREQ_CACHE;
  var map = {}, hz, ticks, kHz;
  for (hz = 95000; hz <= 150000; hz += 1000) { ticks = Math.floor(72000000 / hz); if (ticks % 2 !== 0) ticks += 1; kHz = Math.floor(72000000 / ticks / 1000); if (kHz < 95) continue; if (map[kHz] === undefined) map[kHz] = hz; }
  var e = Object.keys(map).map(Number).sort(function(a, b) { return a - b; });
  FREQ_CACHE = { list: e, hzMap: e.map(function(k) { return map[k]; }) };
  return FREQ_CACHE;
}

/* ══ Mock Data (未配置时预览用) ══ */
function getMockData() {
  var model = getDataModel();
  var data = { _isMock: true };
  model.sensors.forEach(function(s) {
    var range = s.max - s.min;
    var mid = s.min + range / 2;
    var rawVal = mid + (Math.random() * (range * 0.2) - (range * 0.1));
    data[s.id] = Number(rawVal.toFixed(getDecimals(s.dataType, s.step)));
  });
  model.controls.forEach(function(c) {
    if (c.dataType === 'int32') { data[c.id] = Math.floor(Math.random() * 100); }
    else if (c.dataType === 'bool') { data[c.id] = Math.random() > 0.5; }
    else { data[c.id] = Number((Math.random() * 100).toFixed(2)); }
  });
  return data;
}

/* ══ HTTP 错误信息细化 ══ */
function httpErrorMessage(code, body) {
  if (code === 401) return '鉴权失败(401): 请检查 Token';
  if (code === 403) return '拒绝访问(403): 检查产品/设备名';
  if (code === 404) return '服务未找到(404): 检查 BASE_URL';
  if (code === 429) return '请求过于频繁(429): 请稍后刷新';
  if (code === 503) return '服务暂不可用(503): 服务器维护中';
  if (body && body.msg) return 'API错误(' + body.code + '): ' + body.msg;
  return 'HTTP Error: ' + code;
}

function extend(target) { for (var i = 1; i < arguments.length; i++) { var src = arguments[i]; if (src) for (var k in src) { if (src.hasOwnProperty(k)) target[k] = src[k]; } } return target; }

module.exports = {
  getOneNetConfig: getOneNetConfig, getDataModel: getDataModel, getDecimals: getDecimals,
  getLatestData: getLatestData, setProperty: setProperty,
  checkAlerts: checkAlerts, saveHistory: saveHistory,
  buildFreqList: buildFreqList, getPollInterval: getPollInterval,
  getMockData: getMockData, httpErrorMessage: httpErrorMessage,
  LOCK_MS: LOCK_MS
};
