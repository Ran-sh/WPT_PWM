/* ═══════════════════════════════════════════
   WPT Monitor — 报警记录
   对齐 Web alerts.html: 过滤 + 一键已读 + 清空 + 查看详情
   ═══════════════════════════════════════════ */

var OneNet = require('../../utils/onenet.js');

Page({
  data: { filter: 'all', alerts: [], currentTheme: 'theme-dark',
    tabList: getApp().getTabBarList(), _tabSelected: getApp().globalData.tabBarSelected },

  onLoad: function() { this._checkTheme(); this.setData({ _tabSelected: getApp().globalData.tabBarSelected }); },

  onShow: function() { this._checkTheme(); this.setData({ _tabSelected: getApp().globalData.tabBarSelected }); this._load(); },

  _checkTheme: function() { var t = wx.getStorageSync('wpt_theme') || 'theme-dark'; if (t !== this.data.currentTheme) this.setData({ currentTheme: t }); },

  _load: function() {
    var all = wx.getStorageSync('wpt_alerts') || [];
    var model = OneNet.getDataModel();
    var filter = this.data.filter;
    var filtered = (filter === 'all') ? all : all.filter(function(a) { return a.status === (filter === 'unread' ? 'unread' : 'read'); });
    var display = [];
    for (var i = 0; i < filtered.length; i++) {
      var a = filtered[i];
      var sensorCfg = {};
      for (var j = 0; j < model.sensors.length; j++) { if (model.sensors[j].id === a.type) { sensorCfg = model.sensors[j]; break; } }
      display.push({ id: a.id, title: a.title, time: a.time,
        currentValue: Number(a.currentValue).toFixed(OneNet.getDecimals(sensorCfg.dataType, sensorCfg.step)),
        threshold: Number(a.threshold).toFixed(OneNet.getDecimals(sensorCfg.dataType, sensorCfg.step)),
        unit: a.unit || '', isUnread: a.status === 'unread', isCritical: a.isCritical, type: a.type });
    }
    display.sort(function(a, b) { return b.id - a.id; });
    this.setData({ alerts: display });
  },

  onFilter: function(e) { this.setData({ filter: e.currentTarget.dataset.filter }); this._load(); },

  onMarkAllRead: function() {
    var all = wx.getStorageSync('wpt_alerts') || [];
    all.forEach(function(a) { a.status = 'read'; });
    wx.setStorageSync('wpt_alerts', all);
    wx.removeStorageSync('wpt_alarm_states');
    this._load(); wx.showToast({ title: '全部已读', icon: 'none' });
  },

  onClearAll: function() {
    var that = this;
    wx.showModal({ title: '清空报警记录', content: '确定清空所有报警记录吗？此操作无法恢复。', success: function(res) {
      if (res.confirm) {
        wx.removeStorageSync('wpt_alerts'); wx.removeStorageSync('wpt_alarm_states');
        /* 同时清除最新数据缓存, 防止 stale 缓存重触发报警 (对齐 Web) */
        wx.removeStorageSync('wpt_latest');
        that._load(); wx.showToast({ title: '已清空', icon: 'none' });
      }
    }});
  },

  onIgnore: function(e) {
    var id = e.currentTarget.dataset.id, all = wx.getStorageSync('wpt_alerts') || [];
    for (var i = 0; i < all.length; i++) { if (all[i].id === id) { all[i].status = 'read'; break; } }
    wx.setStorageSync('wpt_alerts', all); this._load();
  },

  onViewDetail: function(e) {
    wx.setStorageSync('wpt_history_metric', e.currentTarget.dataset.type);
    wx.switchTab({ url: '/pages/history/history' });
  },

  onToggleTheme: function() { var n = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark'; this.setData({ currentTheme: n }); wx.setStorageSync('wpt_theme', n); },
  onPullDownRefresh: function() { this._load(); wx.stopPullDownRefresh(); },

  onTabNav: function(e) {
    var idx = parseInt(e.currentTarget.dataset.idx), path = e.currentTarget.dataset.path, app = getApp();
    if (this._tabLock || idx === app.globalData.tabBarSelected) return;
    this._tabLock = true;
    try { wx.vibrateShort({ type: 'light' }); } catch (_) {}
    app.globalData.tabBarSelected = idx; wx.setStorageSync('wpt_tab', idx);
    this.setData({ _tabSelected: idx });
    wx.switchTab({ url: '/' + path });
    var that = this; setTimeout(function() { that._tabLock = false; }, 200);
  }
});
/* WPT Monitor V5.1.3：报警记录页逻辑。 */
