/* ═══════════════════════════════════════════
   WPT Monitor — 历史查询
   对齐 Web history.html: 动态 metricTabs + Canvas + CSV 导出
   ═══════════════════════════════════════════ */

var OneNet = require('../../utils/onenet.js');

Page({
  data: {
    currentMetric: '', metricTabs: [], tabLabels: {},
    records: [], chartEmpty: true, currentTheme: 'theme-dark',
    tabList: getApp().getTabBarList(), _tabSelected: getApp().globalData.tabBarSelected
  },

  onLoad: function() {
    this._checkTheme();
    var model = OneNet.getDataModel();
    var tabs = model.sensors.map(function(s) { return s.id; });
    var labels = {};
    model.sensors.forEach(function(s) { labels[s.id] = s.name; });
    this.setData({ currentMetric: tabs[0] || '', metricTabs: tabs, tabLabels: labels });
    this._active = true;
    this.setData({ _tabSelected: getApp().globalData.tabBarSelected });
    this._allHistory = wx.getStorageSync('wpt_history') || [];
    this._loadAndRender();
    var that = this;
    this._pollTimer = setInterval(function() { that._syncData(); }, 30000);
  },

  onShow: function() {
    this._checkTheme();
    this.setData({ _tabSelected: getApp().globalData.tabBarSelected });
    /* 每次进入都刷新数据 */
    this._allHistory = wx.getStorageSync('wpt_history') || [];
    this._loadAndRender();
    /* 报警跳转 metric 处理 */
    var metric = wx.getStorageSync('wpt_history_metric');
    if (metric && this.data.metricTabs.indexOf(metric) !== -1 && metric !== this.data.currentMetric) {
      this.setData({ currentMetric: metric }); wx.removeStorageSync('wpt_history_metric');
      this._syncData();
    }
  },

  _checkTheme: function() { var t = wx.getStorageSync('wpt_theme') || 'theme-dark'; if (t !== this.data.currentTheme) this.setData({ currentTheme: t }); },
  onUnload: function() { this._active = false; clearInterval(this._pollTimer); },
  onToggleTheme: function() { var n = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark'; this.setData({ currentTheme: n }); wx.setStorageSync('wpt_theme', n); },
  onPullDownRefresh: function() { this._allHistory = wx.getStorageSync('wpt_history') || []; this._loadAndRender(); wx.stopPullDownRefresh(); },

  _loadAndRender: function() {
    var model = OneNet.getDataModel(), now = Date.now(), dayAgo = now - 24 * 60 * 60 * 1000;
    var filtered = this._allHistory.filter(function(h) { return h.timestamp >= dayAgo; });
    var sensor = null, cm = this.data.currentMetric;
    for (var j = 0; j < model.sensors.length; j++) { if (model.sensors[j].id === cm) { sensor = model.sensors[j]; break; } }
    var records = [];
    if (sensor && filtered.length) {
      for (var i = filtered.length - 1; i >= 0; i--) {
        var item = filtered[i];
        var val = item.data && item.data[sensor.id] !== undefined ? Number(item.data[sensor.id]) : null;
        if (val === null) continue;
        var dec = OneNet.getDecimals(sensor.dataType, sensor.step);
        var ft = item.fullTime || item.time || '';
        records.push({ time: item.time || '', fullTime: ft, value: val.toFixed(dec), ok: !(val < sensor.min || val > sensor.max), unit: sensor.unit });
      }
    }
    this.setData({ records: records });
    var that = this;
    setTimeout(function() { that._drawChart(filtered); }, 400);
  },

  onTabTap: function(e) { this.setData({ currentMetric: e.currentTarget.dataset.metric }); this._loadAndRender(); },

  _syncData: function() {
    var that = this;
    if (!that._active) return;
    OneNet.getLatestData().then(function() {
      if (!that._active) return;
      that._allHistory = wx.getStorageSync('wpt_history') || [];
      that._loadAndRender();
    }).catch(function(){});
  },

  /* ══ CSV 导出 ══ */
  onExport: function() {
    var model = OneNet.getDataModel(), sensors = model.sensors;
    var now = new Date(), dayAgo = now.getTime() - 24 * 60 * 60 * 1000;
    var displayData = this._allHistory.filter(function(h) { return h.timestamp >= dayAgo; });
    if (displayData.length === 0) { wx.showToast({ title: '暂无数据可导出', icon: 'none' }); return; }
    try {
      var csv = '﻿记录时间,完整时间,';
      for (var si = 0; si < sensors.length; si++) { csv += '"' + sensors[si].name + '(' + sensors[si].unit + ')"' + (si < sensors.length - 1 ? ',' : ''); }
      csv += '\n';
      for (var di = 0; di < displayData.length; di++) {
        var item = displayData[di];
        csv += (item.time || '') + ',' + (item.fullTime || '') + ',';
        for (var sj = 0; sj < sensors.length; sj++) {
          var v = item.data && item.data[sensors[sj].id] !== undefined ? String(item.data[sensors[sj].id]) : '';
          if (v.indexOf(',') !== -1 || v.indexOf('"') !== -1) v = '"' + v.replace(/"/g, '""') + '"';
          csv += v + (sj < sensors.length - 1 ? ',' : '');
        }
        csv += '\n';
      }
      var fs = wx.getFileSystemManager();
      var fileName = 'WPT_' + now.getFullYear() + ('0' + (now.getMonth() + 1)).slice(-2) + ('0' + now.getDate()).slice(-2) + '_' + ('0' + now.getHours()).slice(-2) + ('0' + now.getMinutes()).slice(-2) + '.csv';
      var filePath = wx.env.USER_DATA_PATH + '/' + fileName;
      fs.writeFile({ filePath: filePath, data: csv, encoding: 'utf8', success: function() { wx.showModal({ title: '导出成功', content: fileName + '\n点击"发送"可通过微信发给电脑', confirmText: '发送', cancelText: '完成', success: function(r) { if (r.confirm) wx.shareFileMessage({ filePath: filePath, fileName: fileName }); } }); }, fail: function() { wx.showToast({ title: '保存失败', icon: 'none' }); } });
    } catch (e) { wx.showToast({ title: '导出失败', icon: 'none' }); }
  },

  onTabNav: function(e) {
    var idx = parseInt(e.currentTarget.dataset.idx), path = e.currentTarget.dataset.path, app = getApp();
    if (this._tabLock || idx === app.globalData.tabBarSelected) return;
    this._tabLock = true;
    try { wx.vibrateShort({ type: 'light' }); } catch (_) {}
    app.globalData.tabBarSelected = idx; wx.setStorageSync('wpt_tab', idx);
    this.setData({ _tabSelected: idx });
    wx.switchTab({ url: '/' + path });
    var that = this; setTimeout(function() { that._tabLock = false; }, 200);
  },

  _drawChart: function(hist) {
    var that = this, model = OneNet.getDataModel();
    wx.createSelectorQuery().in(this).select('#historyCanvas').fields({ node: true, size: true }).exec(function(res) {
      if (!res[0] || !res[0].node) return;
      var canvas = res[0].node, w = res[0].width, h = res[0].height;
      var dpr = wx.getSystemInfoSync().pixelRatio;
      canvas.width = w * dpr; canvas.height = h * dpr; var ctx = canvas.getContext('2d'); ctx.scale(dpr, dpr);
      var sensor = null;
      for (var j = 0; j < model.sensors.length; j++) { if (model.sensors[j].id === that.data.currentMetric) { sensor = model.sensors[j]; break; } }
      if (!sensor) return;
      var labels = [], dataPoints = [];
      for (var i = Math.max(0, hist.length - 7); i < hist.length; i++) { if (hist[i].time) labels.push(hist[i].time); var v = hist[i].data && hist[i].data[sensor.id] !== undefined ? Number(hist[i].data[sensor.id]) : NaN; dataPoints.push(v); }
      var hasData = dataPoints.some(function(v) { return !isNaN(v); }); that.setData({ chartEmpty: !hasData });
      if (!hasData) { ctx.clearRect(0, 0, w, h); ctx.fillStyle = '#6B7280'; ctx.font = '14px sans-serif'; ctx.textAlign = 'center'; ctx.fillText('暂无历史数据', w / 2, h / 2); return; }
      var vals = dataPoints.filter(function(v) { return !isNaN(v); }), dMin = Math.min.apply(null, vals), dMax = Math.max.apply(null, vals), pad = (dMax - dMin || 1) * 0.25, yMin = Math.max(0, dMin - pad), yMax = dMax + pad, padL = 40, padR = 8, padT = 8, padB = 24, pw = w - padL - padR, ph = h - padT - padB;
      ctx.clearRect(0, 0, w, h); ctx.fillStyle = '#94a3b8'; ctx.font = '10px sans-serif'; ctx.textAlign = 'right';
      for (var yi = 0; yi <= 4; yi++) { var yv = yMin + (yMax - yMin) * yi / 4, yy = padT + ph * (1 - yi / 4);
        var dec = OneNet.getDecimals(sensor.dataType, sensor.step);
        ctx.fillText(dec === 0 ? Math.round(yv).toString() : yv.toFixed(dec), padL - 4, yy + 4); }
      ctx.textAlign = 'center';
      for (var xi = 0; xi < labels.length; xi++) { if (xi % Math.max(1, Math.floor(labels.length / 5)) !== 0 && xi !== labels.length - 1) continue; ctx.fillText(labels[xi], padL + pw * xi / Math.max(1, labels.length - 1), h - 4); }
      if (dataPoints.length > 1) { ctx.beginPath(); ctx.strokeStyle = sensor.colorHex || '#3b82f6'; ctx.lineWidth = 2; ctx.lineJoin = 'round'; for (var di = 0; di < dataPoints.length; di++) { if (isNaN(dataPoints[di])) continue; var dx = padL + pw * di / (dataPoints.length - 1), dy = padT + ph * (1 - (dataPoints[di] - yMin) / (yMax - yMin || 1)); if (di === 0 || isNaN(dataPoints[di - 1])) ctx.moveTo(dx, dy); else ctx.lineTo(dx, dy); } ctx.stroke(); for (var di2 = 0; di2 < dataPoints.length; di2++) { if (isNaN(dataPoints[di2])) continue; var dx2 = padL + pw * di2 / (dataPoints.length - 1), dy2 = padT + ph * (1 - (dataPoints[di2] - yMin) / (yMax - yMin || 1)); ctx.beginPath(); ctx.arc(dx2, dy2, 3, 0, Math.PI * 2); ctx.fillStyle = '#fff'; ctx.fill(); ctx.strokeStyle = sensor.colorHex || '#3b82f6'; ctx.lineWidth = 2; ctx.stroke(); } }
    });
  }
});
