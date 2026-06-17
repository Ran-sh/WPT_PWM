/* ═══════════════════════════════════════════
   WPT Monitor — 全局 App
   数据模型从 utils/config.js 读取 (单一来源)
   ═══════════════════════════════════════════ */

var Config = require('./utils/config.js');

var DEFAULT_CONFIG = {
  PRODUCT_ID: '1iS397oJFL',
  DEVICE_NAME: '20260001',
  TOKEN: 'version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D',
  BASE_URL: 'https://iot-api.heclouds.com'
};

App({
  globalData: {
    tabBarSelected: 0
  },

  /* Tab 列表 (对齐 Web mobile-nav.js 6项, 小程序保持5项 + 报警从横幅/设置跳转) */
  getTabBarList: function() {
    return [
      { id: 'index',    path: 'pages/index/index',       text: '首页', icon: '⌂' },
      { id: 'monitor',  path: 'pages/monitoring/monitoring', text: '监测', icon: '◉' },
      { id: 'control',  path: 'pages/control/control',    text: '控制', icon: '⊛' },
      { id: 'history',  path: 'pages/history/history',    text: '历史', icon: '🗂' },
      { id: 'settings', path: 'pages/settings/settings',  text: '设置', icon: '⚙' }
    ];
  },

  getOneNetConfig: function() {
    var user = wx.getStorageSync('wpt_onenet_config') || {};
    if (user.productId && user.deviceName && user.token) {
      return {
        PRODUCT_ID: user.productId, DEVICE_NAME: user.deviceName,
        TOKEN: user.token, BASE_URL: 'https://iot-api.heclouds.com'
      };
    }
    return DEFAULT_CONFIG;
  },

  getDataModel: function() {
    return Config.getDataModel();
  },

  saveDataModel: function(model) {
    Config.saveDataModel(model);
  }
});
