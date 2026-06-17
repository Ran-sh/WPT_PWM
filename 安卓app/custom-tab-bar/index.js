Component({
  data: {
    selected: -1,
    list: [
      { pagePath: '/pages/index/index',       text: '首页', icon: '⌂' },
      { pagePath: '/pages/monitoring/monitoring', text: '监测', icon: '◉' },
      { pagePath: '/pages/control/control',    text: '控制', icon: '⊛' },
      { pagePath: '/pages/history/history',    text: '历史', icon: '🗂' },
      { pagePath: '/pages/settings/settings',  text: '设置', icon: '⚙' }
    ]
  },

  methods: {
    onSwitchTab: function(e) {
      var path = e.currentTarget.dataset.path;
      try { wx.vibrateShort({ type: 'light' }); } catch (_) {}
      wx.switchTab({ url: path });
    }
  }
});
