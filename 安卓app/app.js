/* WPT Monitor V5.1.3：小程序全局入口。 */
/* ═══════════════════════════════════════════
   WPT Monitor — 全局 App
   数据模型从 utils/config.js 读取 (单一来源)
   安全: 无硬编码凭证
   ═══════════════════════════════════════════ */

var Config = require('./utils/config.js');

function safeStorageGet(key, fallback) { try { var v = wx.getStorageSync(key); return (v !== '' && v !== undefined && v !== null) ? v : fallback; } catch (e) { return fallback; } }

App({
  globalData: {
    tabBarSelected: 0,
    projectVersion: 'V5.1.3'
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
    var user = safeStorageGet('wpt_onenet_config', {});
    if (user.productId && user.deviceName && user.token) {
      return {
        PRODUCT_ID: user.productId, DEVICE_NAME: user.deviceName,
        TOKEN: user.token, BASE_URL: 'https://iot-api.heclouds.com'
      };
    }
    /* 安全: 未配置时返回空 Token, 触发 Mock 模式 */
    return { PRODUCT_ID: '', DEVICE_NAME: '', TOKEN: '', BASE_URL: 'https://iot-api.heclouds.com' };
  },

  getDataModel: function() {
    return Config.getDataModel();
  },

  saveDataModel: function(model) {
    Config.saveDataModel(model);
  }
});
