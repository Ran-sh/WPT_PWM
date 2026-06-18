/* ═══════════════════════════════════════════
   WPT Monitor — 首页仪表盘
   完全对齐 Web index.html: renderDashboard + updateUI
   ═══════════════════════════════════════════ */

var OneNet = require('../../utils/onenet.js');

function formatValue(val, dataType, step) {
  var n = Number(val);
  if (isNaN(n)) return '--';
  return n.toFixed(OneNet.getDecimals(dataType, step));
}

function buildCards(model, data, isOffline) {
  var sensors = [], controls = [];
  /* 传感器: 离线时全部显示 "--" */
  (model.sensors || []).forEach(function(s) {
    var val = '--', status = 'normal', stxt = '等待数据';
    if (!isOffline && data && data[s.id] !== undefined) {
      val = formatValue(data[s.id], s.dataType, s.step);
      var n = Number(val);
      if (!isNaN(n)) {
        if (n > s.max) { status = 'alert'; stxt = '过高'; }
        else if (n < s.min && s.min > 0) { status = 'alert'; stxt = '过低'; }
        else { status = 'normal'; stxt = '正常'; }
      }
    }
    sensors.push({ id: s.id, name: s.name, unit: s.unit, value: val, status: status, statusText: stxt, min: s.min, max: s.max });
  });
  /* 控制: 离线时全部显示 "--" */
  (model.controls || []).forEach(function(c) {
    var displayVal = '--';
    if (!isOffline && data && data[c.id] !== undefined) {
      if (c.dataType === 'bool') displayVal = data[c.id] === true ? '已开启' : '已关闭';
      else displayVal = formatValue(data[c.id], c.dataType, c.step);
    }
    controls.push({ id: c.id, name: c.name, value: displayVal });
  });
  return { sensors: sensors, controls: controls };
}

Page({
  data: {
    dashTitle: 'WPT Monitor', sensors: [], controls: [],
    connState: 3, connLabel: '连接中',
    currentTheme: 'theme-dark', alertVisible: false, alertMessages: []
  },

  onLoad: function() {
    this.setData({ currentTheme: wx.getStorageSync('wpt_theme') || 'theme-dark' });
    var title = wx.getStorageSync('wpt_dashboard_title');
    if (title) this.setData({ dashTitle: title });
    var model = OneNet.getDataModel();
    this.setData(buildCards(model, null, true));  /* 无数据 → 离线条 */
    var cached = wx.getStorageSync('wpt_latest');
    if (cached) this._applyData(cached, true);
    this._pollTimer = null; this._retryTimer = null; this._active = true;
    this._pollFailures = 0;
    this._fetchAndSchedule();
  },

  onShow: function() {
    var saved = wx.getStorageSync('wpt_theme') || 'theme-dark';
    if (saved !== this.data.currentTheme) this.setData({ currentTheme: saved });
    var model = OneNet.getDataModel();
    if (JSON.stringify(model) !== this._lastModelJson) {
      this._lastModelJson = JSON.stringify(model);
      var cached2 = wx.getStorageSync('wpt_latest');
      this.setData(buildCards(model, cached2, !(cached2 && cached2._isOnline)));
    }
    if (!this._active) { this._active = true; this._pollFailures = 0; this._fetchAndSchedule(); }
  },

  onHide: function() { this._active = false; clearTimeout(this._pollTimer); clearTimeout(this._retryTimer); },
  onUnload: function() {
    this._active = false;
    this._clearTimers();
  },

  _clearTimers: function() {
    if (this._pollTimer) { clearTimeout(this._pollTimer); this._pollTimer = null; }
    if (this._retryTimer) { clearTimeout(this._retryTimer); this._retryTimer = null; }
  },

  _fetchAndSchedule: function() {
    var that = this;
    if (!this._active) return;
    this._doFetch().then(function(data) {
      that._pollFailures = 0;
      if (that._active && data) { var on = !!data._isOnline; that.setData({ connState: on ? 0 : 1, connLabel: on ? '在线' : '离线' }); }
    }).catch(function() {
      that._pollFailures++;
      if (that._pollFailures === 1 && that._active) {
        clearTimeout(that._retryTimer);
        that._retryTimer = setTimeout(function() {
          if (!that._active) return;
          that._doFetch().then(function(data) { that._pollFailures = 0; if (that._active && data) { var on2 = !!data._isOnline; that.setData({ connState: on2 ? 0 : 1, connLabel: on2 ? '在线' : '离线' }); } }).catch(function() {});
        }, 2000);
      } else if (that._active) { that.setData({ connState: 2, connLabel: '连接失败' }); }
    }).then(function() {
      if (!that._active) return;
      that._clearTimers();
      that._pollTimer = setTimeout(function() { that._fetchAndSchedule(); }, OneNet.getPollInterval(that._pollFailures));
    });
  },

  _doFetch: function() {
    var that = this;
    return OneNet.getLatestData().then(function(data) {
      if (that._active) that._applyData(data, false);
      return data;
    });
  },

  _applyData: function(data, fromCache) {
    var model = OneNet.getDataModel();
    var isOffline = !data._isOnline;  /* true=在线, false/undefined=离线 */
    var cards = buildCards(model, data, isOffline);
    var newAlerts = OneNet.checkAlerts(data, fromCache);
    var online = !isOffline;
    this.setData({
      sensors: cards.sensors, controls: cards.controls,
      alertVisible: newAlerts.length > 0, alertMessages: newAlerts,
      connState: online ? 0 : 1, connLabel: online ? '在线' : '离线'
    });
    this._lastModelJson = JSON.stringify(model);
  },

  onToggleTheme: function() { var n = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark'; this.setData({ currentTheme: n }); wx.setStorageSync('wpt_theme', n); },
  onPullDownRefresh: function() { var that = this; this._doFetch().then(function() { wx.stopPullDownRefresh(); }, function() { wx.stopPullDownRefresh(); }); },
  onAlertTap: function() { wx.switchTab({ url: '/pages/alerts/alerts' }); },
  onConnTap: function() { this._doFetch().catch(function(){}); }
});
