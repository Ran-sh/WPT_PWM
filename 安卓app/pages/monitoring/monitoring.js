/* ═══════════════════════════════════════════
   WPT Monitor — 实时监测
   对齐 Web monitoring.html: 动态 metricTabs + 传感器卡片
   ═══════════════════════════════════════════ */

var OneNet = require('../../utils/onenet.js');

function buildSensorList(model, data) {
  var list = [], isOffline = !(data && data._isOnline);
  model.sensors.forEach(function(s) {
    var val = '--', status = 'normal', stxt = '等待数据';
    if (!isOffline && data && data[s.id] !== undefined) {
      val = Number(data[s.id]).toFixed(OneNet.getDecimals(s.dataType, s.step));
      var n = Number(val);
      if (!isNaN(n)) {
        if (n > s.max) { status = 'alert'; stxt = '异常 (偏高)'; }
        else if (n < s.min) { status = 'alert'; stxt = '异常 (偏低)'; }
        else { status = 'normal'; stxt = '正常 (' + s.min + '-' + s.max + s.unit + ')'; }
      }
    } else if (isOffline) { stxt = '设备离线, 无实时数据'; }
    list.push({ id: s.id, name: s.name, unit: s.unit, min: s.min, max: s.max, value: val, status: status, statusText: stxt, colorHex: s.colorHex, colorBg: s.colorBg });
  });
  return list;
}

Page({
  data: {
    sensors: [], currentMetric: '', metricTabs: [], tabLabels: {},
    chartEmpty: true, currentTheme: 'theme-dark'
  },

  onLoad: function() {
    this._checkTheme();
    var model = OneNet.getDataModel();
    var tabs = model.sensors.map(function(s) { return s.id; });
    var labels = {};
    model.sensors.forEach(function(s) { labels[s.id] = s.name; });
    this.setData({ sensors: buildSensorList(model), currentMetric: tabs[0] || '', metricTabs: tabs, tabLabels: labels });
    this._active = true; this._latestData = null;
    this._syncData();
    var that = this;
    this._pollTimer = setInterval(function() { that._syncData(); }, 10000);
    setTimeout(function() { that._drawChart(); }, 600);
  },

  onShow: function() { this._checkTheme(); if (!this._active) { this._active = true; this._syncData(); } },
  onHide: function() { this._active = false; clearInterval(this._pollTimer); },

  onUnload: function() { this._active = false; clearInterval(this._pollTimer); },

  _checkTheme: function() {
    var t = wx.getStorageSync('wpt_theme') || 'theme-dark';
    if (t !== this.data.currentTheme) this.setData({ currentTheme: t });
  },

  _syncData: function() {
    var that = this;
    if (!that._active) return;
    OneNet.getLatestData().then(function(data) {
      if (!that._active) return;
      that._latestData = data;
      that.setData({ sensors: buildSensorList(OneNet.getDataModel(), data) });
      that._drawChart();
    }).catch(function(){});
  },

  onTabTap: function(e) {
    this.setData({ currentMetric: e.currentTarget.dataset.metric });
    this._drawChart();
  },

  onToggleTheme: function() { var n = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark'; this.setData({ currentTheme: n }); wx.setStorageSync('wpt_theme', n); },
  onPullDownRefresh: function() { var that = this; that._syncData(); setTimeout(function() { wx.stopPullDownRefresh(); }, 1500); },

  _drawChart: function() {
    var that = this, model = OneNet.getDataModel();
    wx.createSelectorQuery().in(this).select('#trendCanvas').fields({ node: true, size: true }).exec(function(res) {
      if (!res[0] || !res[0].node) return;
      var canvas = res[0].node, w = res[0].width, h = res[0].height;
      var dpr = wx.getSystemInfoSync().pixelRatio;
      canvas.width = w * dpr; canvas.height = h * dpr;
      var ctx = canvas.getContext('2d'); ctx.scale(dpr, dpr);
      var metric = that.data.currentMetric, sensor = null;
      for (var j = 0; j < model.sensors.length; j++) { if (model.sensors[j].id === metric) { sensor = model.sensors[j]; break; } }
      if (!sensor) { ctx.clearRect(0,0,w,h); ctx.fillStyle='#6B7280'; ctx.font='14px sans-serif'; ctx.textAlign='center'; ctx.fillText('无传感器配置', w/2, h/2); return; }
      var hist = (wx.getStorageSync('wpt_history') || []).slice(-7), labels = [], dataPoints = [];
      for (var i = 0; i < hist.length; i++) {
        if (hist[i].time) labels.push(hist[i].time);
        var v = hist[i].data && hist[i].data[sensor.id] !== undefined ? Number(hist[i].data[sensor.id]) : NaN;
        dataPoints.push(v);
      }
      var hasData = dataPoints.some(function(v) { return !isNaN(v); });
      that.setData({ chartEmpty: !hasData });
      if (!hasData) { ctx.clearRect(0,0,w,h); ctx.fillStyle='#6B7280'; ctx.font='14px sans-serif'; ctx.textAlign='center'; ctx.fillText('数据收集中...', w/2, h/2); return; }
      var vals = dataPoints.filter(function(v) { return !isNaN(v); });
      var dMin = Math.min.apply(null, vals), dMax = Math.max.apply(null, vals);
      var pad = (dMax - dMin || 1) * 0.25, yMin = Math.max(0, dMin - pad), yMax = dMax + pad;
      var padL = 36, padR = 8, padT = 8, padB = 24, pw = w - padL - padR, ph = h - padT - padB;
      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = '#94a3b8'; ctx.font = '10px sans-serif'; ctx.textAlign = 'right';
      for (var yi = 0; yi <= 4; yi++) { var yv = yMin + (yMax - yMin) * yi / 4, yy = padT + ph * (1 - yi / 4);
        var dec = OneNet.getDecimals(sensor.dataType, sensor.step);
        ctx.fillText(dec === 0 ? Math.round(yv).toString() : yv.toFixed(dec), padL - 4, yy + 4); }
      ctx.textAlign = 'center';
      for (var xi = 0; xi < labels.length; xi++) { if (xi % Math.max(1, Math.floor(labels.length / 5)) !== 0 && xi !== labels.length - 1) continue; ctx.fillText(labels[xi], padL + pw * xi / Math.max(1, labels.length - 1), h - 4); }
      if (dataPoints.length > 1) {
        var grd = ctx.createLinearGradient(0, padT, 0, h - padB); grd.addColorStop(0, sensor.colorBg); grd.addColorStop(1, 'rgba(255,255,255,0)');
        ctx.beginPath(); var fx;
        for (var di = 0; di < dataPoints.length; di++) { if (isNaN(dataPoints[di])) continue; var dx = padL + pw * di / (dataPoints.length - 1), dy = padT + ph * (1 - (dataPoints[di] - yMin) / (yMax - yMin || 1)); if (di === 0 || isNaN(dataPoints[di - 1])) { ctx.moveTo(dx, dy); fx = dx; } else ctx.lineTo(dx, dy); }
        ctx.lineTo(dx, padT + ph); ctx.lineTo(fx, padT + ph); ctx.closePath(); ctx.fillStyle = grd; ctx.fill();
        ctx.beginPath(); ctx.strokeStyle = sensor.colorHex; ctx.lineWidth = 2.5; ctx.lineJoin = 'round';
        for (var di2 = 0; di2 < dataPoints.length; di2++) { if (isNaN(dataPoints[di2])) continue; var dx2 = padL + pw * di2 / (dataPoints.length - 1), dy2 = padT + ph * (1 - (dataPoints[di2] - yMin) / (yMax - yMin || 1)); if (di2 === 0 || isNaN(dataPoints[di2 - 1])) ctx.moveTo(dx2, dy2); else ctx.lineTo(dx2, dy2); }
        ctx.stroke();
        for (var di3 = 0; di3 < dataPoints.length; di3++) { if (isNaN(dataPoints[di3])) continue; var dx3 = padL + pw * di3 / (dataPoints.length - 1), dy3 = padT + ph * (1 - (dataPoints[di3] - yMin) / (yMax - yMin || 1)); ctx.beginPath(); ctx.arc(dx3, dy3, 3.5, 0, Math.PI * 2); ctx.fillStyle = '#fff'; ctx.fill(); ctx.strokeStyle = sensor.colorHex; ctx.lineWidth = 2; ctx.stroke(); }
      }
    });
  }
});
