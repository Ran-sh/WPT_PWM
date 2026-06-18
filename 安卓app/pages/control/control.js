/* ═══════════════════════════════════════════
   WPT Monitor — 设备控制
   对齐 Web control.html
   ═══════════════════════════════════════════ */

var OneNet = require('../../utils/onenet.js');
var FREQ = OneNet.buildFreqList();
var FREQ_LIST = FREQ.list;
var INIT_IDX = FREQ_LIST.indexOf(100);

Page({
  data: {
    isOn: false, isFault: false, systemState: 'IDLE', stateLabel: '待机',
    freqList: FREQ_LIST, selectedFreq: 100, freqIdx: INIT_IDX >= 0 ? INIT_IDX : 0,
    logs: [], currentTheme: 'theme-dark'
  },

  onLoad: function() {
    this._checkTheme();
    this._active = true;
    this._cmdLock = {}; this._switchPending = false;
    this._logs = wx.getStorageSync('wpt_ctrl_logs') || [];
    this.setData({ logs: this._logs.slice(0, 10) });
    this._syncStatus();
    var that = this;
    this._pollTimer = setInterval(function() { that._syncStatus(); }, 5000);
  },

  onShow: function() { this._checkTheme(); },

  _checkTheme: function() { var t = wx.getStorageSync('wpt_theme') || 'theme-dark'; if (t !== this.data.currentTheme) this.setData({ currentTheme: t }); },
  onUnload: function() { this._active = false; clearInterval(this._pollTimer); },
  onToggleTheme: function() { var n = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark'; this.setData({ currentTheme: n }); wx.setStorageSync('wpt_theme', n); },
  onPullDownRefresh: function() { this._syncStatus(); wx.stopPullDownRefresh(); },

  _syncStatus: function() {
    var that = this;
    if (!that._active) return;
    OneNet.getLatestData().then(function(data) {
      if (!that._active) return;
      /* 设备离线: 强制安全默认值, 忽略 OneNET 缓存的旧数据 */
      if (data._isOnline === false) {
        that.setData({ isOn: false, isFault: false, systemState: 'IDLE', stateLabel: '离线' });
        return;
      }
      var raw = data._raw || {}, sState = raw.S, isRunning, isFault, systemState, stateLabel;
      if (sState === 3)      { isRunning=false; isFault=true;  systemState='FAULT'; stateLabel='故障'; }
      else if (sState === 2) { isRunning=true;  isFault=false; systemState='DONE';  stateLabel='运行中'; }
      else if (sState === 1) { isRunning=true;  isFault=false; systemState='SWEEP'; stateLabel='扫频中'; }
      else                   { isRunning=false; isFault=false; systemState='IDLE';  stateLabel='待机'; }
      var swOn = (data.switch !== undefined) ? (data.switch === true) : isRunning;
      var freqKHz = raw.F !== undefined ? Math.floor(raw.F / 1000) : that.data.selectedFreq;
      var lock = that._cmdLock || {}, now = Date.now();
      if (!(lock.freq && (now - lock.freq < OneNet.LOCK_MS)) && freqKHz >= 95 && freqKHz <= 150) {
        var idx = FREQ_LIST.indexOf(freqKHz); if (idx >= 0) that.setData({ selectedFreq: freqKHz, freqIdx: idx });
      }
      that.setData({ isOn: swOn, isFault: isFault, systemState: systemState, stateLabel: stateLabel });
    }).catch(function(){
      /* API 失败 → 从缓存回填, 避免显示安全默认值 (OFF/100) */
      var cached = wx.getStorageSync('wpt_latest') || {};
      var raw = cached._raw || {};
      var sState = raw.S, isRunning, isFault, systemState, stateLabel;
      if (sState === 3)      { isRunning=false; isFault=true;  systemState='FAULT'; stateLabel='故障'; }
      else if (sState === 2) { isRunning=true;  isFault=false; systemState='DONE';  stateLabel='运行中'; }
      else if (sState === 1) { isRunning=true;  isFault=false; systemState='SWEEP'; stateLabel='扫频中'; }
      else                   { isRunning=false; isFault=false; systemState='IDLE';  stateLabel='待机'; }
      var swOn = cached.switch !== undefined ? cached.switch : isRunning;
      var fRaw = raw.F !== undefined ? Math.floor(raw.F / 1000) : undefined;
      var upd = { isOn: swOn, isFault: isFault, systemState: systemState, stateLabel: stateLabel };
      if (fRaw !== undefined && fRaw >= 95 && fRaw <= 150) {
        var idx = FREQ_LIST.indexOf(fRaw); if (idx >= 0) { upd.selectedFreq = fRaw; upd.freqIdx = idx; }
      }
      if (that._active) that.setData(upd);
    });
  },

  onSwiperChange: function(e) { var idx = e.detail.current; this.setData({ freqIdx: idx, selectedFreq: FREQ_LIST[idx] }); },

  onSwitch: function(e) {
    var on = e.detail.value, that = this;
    if (this._switchPending) return;
    this._switchPending = true;
    if (!this._cmdLock) this._cmdLock = {};
    this._cmdLock.switch = Date.now();
    this.setData({ isOn: on, systemState: on ? 'SWEEP' : 'IDLE', stateLabel: on ? '扫频中' : '待机', isFault: false });
    OneNet.setProperty(null, { switch: on }).then(function(ok) {
      if (ok) { that._addLog('Switch', on ? '开启设备' : '关闭设备'); }
      else { that.setData({ isOn: !on, systemState: !on ? 'SWEEP' : 'IDLE', stateLabel: !on ? '扫频中' : '待机' }); wx.showToast({ title: '下发失败', icon: 'none' }); }
      that._switchPending = false;
    });
  },

  onSetFreq: function() {
    if (!this.data.isOn) { wx.showToast({ title: '请先启动设备', icon: 'none' }); return; }
    var kHz = this.data.selectedFreq;
    if (!this._cmdLock) this._cmdLock = {};
    this._cmdLock.freq = Date.now();
    OneNet.setProperty(null, { setfreq: kHz }).then(function(ok) {
      wx.showToast({ title: ok ? 'Set ' + kHz + 'kHz' : '下发失败', icon: 'none', duration: 800 });
    });
  },

  _addLog: function(device, status) {
    var now = new Date();
    var ts = now.getFullYear()+'-'+('0'+(now.getMonth()+1)).slice(-2)+'-'+('0'+now.getDate()).slice(-2)+' '+('0'+now.getHours()).slice(-2)+':'+('0'+now.getMinutes()).slice(-2)+':'+('0'+now.getSeconds()).slice(-2);
    this._logs.unshift({ device: device, status: status, time: ts });
    if (this._logs.length > 20) this._logs.pop();
    wx.setStorageSync('wpt_ctrl_logs', this._logs);
    this.setData({ logs: this._logs.slice(0, 10) });
  },

  onClearLogs: function() {
    var that = this;
    wx.showModal({ title: '清空操作记录', content: '确定清空所有操作记录吗？', success: function(r) { if (r.confirm) { that._logs = []; wx.setStorageSync('wpt_ctrl_logs', []); that.setData({ logs: [] }); } } });
  }
});
