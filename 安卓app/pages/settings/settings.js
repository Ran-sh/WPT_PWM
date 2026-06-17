/* ═══════════════════════════════════════════
   WPT Monitor — 系统设置
   对齐 Web settings.html: OneNET配置 + 系统名称 + 声音 + 清除缓存
   ═══════════════════════════════════════════ */

Page({
  data: { productId: '', deviceName: '', token: '', configured: false, dashboardTitle: 'WPT Monitor', soundAlert: true, currentTheme: 'theme-dark',
    tabList: getApp().getTabBarList(), _tabSelected: getApp().globalData.tabBarSelected },

  onLoad: function() {
    this._checkTheme();
    var config = wx.getStorageSync('wpt_onenet_config') || {};
    var title = wx.getStorageSync('wpt_dashboard_title') || 'WPT Monitor';
    var sound = wx.getStorageSync('wpt_sound_alert');
    if (sound === undefined || sound === null) sound = true;
    this.setData({ productId: config.productId || '', deviceName: config.deviceName || '', token: config.token || '', configured: !!(config.productId && config.deviceName && config.token), dashboardTitle: title, soundAlert: sound });
    this.setData({ _tabSelected: getApp().globalData.tabBarSelected });
  },

  onShow: function() { this._checkTheme(); this.setData({ _tabSelected: getApp().globalData.tabBarSelected }); },

  _checkTheme: function() { var t = wx.getStorageSync('wpt_theme') || 'theme-dark'; if (t !== this.data.currentTheme) this.setData({ currentTheme: t }); },

  onProductIdInput: function(e) { this.setData({ productId: e.detail.value }); },
  onDeviceNameInput: function(e) { this.setData({ deviceName: e.detail.value }); },
  onTokenInput: function(e) { this.setData({ token: e.detail.value }); },

  onSaveConfig: function() {
    var c = { productId: this.data.productId, deviceName: this.data.deviceName, token: this.data.token };
    wx.setStorageSync('wpt_onenet_config', c);
    this.setData({ configured: !!(c.productId && c.deviceName && c.token) });
    wx.showToast({ title: '保存成功', icon: 'success' });
  },

  onTitleInput: function(e) { this.setData({ dashboardTitle: e.detail.value }); },
  onSaveTitle: function() { wx.setStorageSync('wpt_dashboard_title', this.data.dashboardTitle); wx.showToast({ title: '已保存', icon: 'success' }); },

  onToggleSound: function(e) { this.setData({ soundAlert: e.detail.value }); wx.setStorageSync('wpt_sound_alert', e.detail.value); },

  onClearCache: function() {
    var that = this;
    wx.showModal({ title: '恢复默认', content: '将清除所有本地数据：配置、历史记录、报警记录。此操作无法恢复。', success: function(res) {
      if (res.confirm) { wx.clearStorageSync(); that.setData({ productId: '', deviceName: '', token: '', configured: false, dashboardTitle: 'WPT Monitor', soundAlert: true }); wx.showToast({ title: '已清除', icon: 'none' }); }
    }});
  },

  onToggleTheme: function() { var n = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark'; this.setData({ currentTheme: n }); wx.setStorageSync('wpt_theme', n); },
  onPullDownRefresh: function() { wx.stopPullDownRefresh(); },

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
