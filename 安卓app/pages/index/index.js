/* OneNET 直连 — 与网页端完全相同的后端逻辑 */
const ONENET = {
  PRODUCT_ID: '1iS397oJFL',
  DEVICE_NAME: '20260001',
  TOKEN: 'version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D',
  BASE_URL: 'https://iot-api.heclouds.com'
};
const POLL_MS = 5000;
const TIM1_CLK = 72000000;

function buildFreqMap() {
  const map = new Map();
  for (let hz = 95000; hz <= 150000; hz += 1000) {
    let ticks = Math.floor(TIM1_CLK / hz);
    if (ticks % 2 !== 0) ticks += 1;
    const displayKHz = Math.floor(TIM1_CLK / ticks / 1000);
    if (displayKHz < 95) continue;
    if (!map.has(displayKHz)) map.set(displayKHz, hz);
  }
  const entries = Array.from(map.entries()).sort((a, b) => a[0] - b[0]);
  return { list: entries.map(e => e[0]), hzMap: entries.map(e => e[1]) };
}
const FREQ = buildFreqMap();
const FREQ_LIST = FREQ.list;
const FREQ_HZ   = FREQ.hzMap;
const INIT_IDX  = FREQ_LIST.indexOf(100);

Page({
  data: {
    voltage: '--', current: '--', frequency: '--',
    systemState: 'IDLE', stateLabel: '待机',
    isOn: false, isFault: false, connected: false,
    freqList: FREQ_LIST, selectedFreq: 100, freqIdx: INIT_IDX,
    currentTheme: 'theme-dark',
    spinning: false
  },

  onRefresh() {
    if (this.data.spinning) return;
    this.setData({ spinning: true });
    this.fetchData();
    this.fetchControlState();
    setTimeout(() => this.setData({ spinning: false }), 1000);
  },

  onLoad() {
    const saved = wx.getStorageSync('wpt_theme');
    if (saved) this.setData({ currentTheme: saved });
    this.fetchData();           /* 数据: 5s (对标网页首页) */
    this.fetchControlState();   /* 控制状态: 60s (对标网页控制页) */
    this._timerData  = setInterval(() => this.fetchData(), 5000);
    this._timerCtrl  = setInterval(() => this.fetchControlState(), 60000);
  },
  onToggleTheme() {
    const next = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark';
    this.setData({ currentTheme: next });
    wx.setStorageSync('wpt_theme', next);
  },
  onUnload() {
    if (this._timerData) clearInterval(this._timerData);
    if (this._timerCtrl) clearInterval(this._timerCtrl);
  },

  /* ── 数据获取: V/I/F 每5s (对标网页首页) ── */
  fetchData() {
    const that = this;
    wx.request({
      url: ONENET.BASE_URL + '/thingmodel/query-device-property?product_id=' + ONENET.PRODUCT_ID + '&device_name=' + ONENET.DEVICE_NAME,
      method: 'GET',
      header: { 'Authorization': ONENET.TOKEN },
      success(res) {
        if (res.statusCode !== 200 || res.data.code !== 0) {
          that.setData({ connected: false });
          return;
        }
        const raw = {};
        (res.data.data || []).forEach(item => {
          let v = item.value;
          if (v === 'true') v = true; else if (v === 'false') v = false;
          else if (!isNaN(v) && v !== '' && v !== undefined) v = Number(v);
          raw[item.identifier] = v;
        });

        const v = raw.V !== undefined ? Number(raw.V).toFixed(2) : that.data.voltage;
        const i = raw.I !== undefined ? Number(raw.I).toFixed(2) : that.data.current;
        const fHz = raw.F;
        const fNum = fHz !== undefined ? Math.floor(fHz / 1000) : that.data.frequency;

        const justConnected = !that.data.connected;
        const idx = FREQ_LIST.indexOf(fNum);

        that.setData({
          voltage: v, current: i, frequency: fNum,
          connected: true,
          selectedFreq: justConnected && idx >= 0 ? fNum : that.data.selectedFreq,
          freqIdx: justConnected && idx >= 0 ? idx : that.data.freqIdx
        });
      },
      fail() { that.setData({ connected: false }); }
    });
  },

  /* ── 控制状态同步: Switch/SetFreq 每60s (对标网页控制页) ── */
  fetchControlState() {
    const that = this;
    wx.request({
      url: ONENET.BASE_URL + '/thingmodel/query-device-property?product_id=' + ONENET.PRODUCT_ID + '&device_name=' + ONENET.DEVICE_NAME,
      method: 'GET',
      header: { 'Authorization': ONENET.TOKEN },
      success(res) {
        if (res.statusCode !== 200 || res.data.code !== 0) return;
        const raw = {};
        (res.data.data || []).forEach(item => {
          let v = item.value;
          if (v === 'true') v = true; else if (v === 'false') v = false;
          else if (!isNaN(v) && v !== '' && v !== undefined) v = Number(v);
          raw[item.identifier] = v;
        });

        const now = Date.now();
        const lock = that._cmdLock || {};

        /* 乐观锁保护 */
        const sw = (lock.switch && (now - lock.switch < 5000)) ? that.data.isOn : (raw.Switch === true);
        const setFreqHz = raw.SetFreq;
        const setFreqKHz = setFreqHz !== undefined ? Math.floor(setFreqHz / 1000) : null;
        const freqLocked = lock.freq && (now - lock.freq < 5000);

        let state, label;
        if (sw === true) { state = 'DONE';  label = '运行中'; }
        else             { state = 'IDLE';  label = '待机'; }

        const idx = FREQ_LIST.indexOf(setFreqKHz);

        that.setData({
          isOn: sw, systemState: state, stateLabel: label,
          selectedFreq: freqLocked ? that.data.selectedFreq : (idx >= 0 ? setFreqKHz : that.data.selectedFreq),
          freqIdx: freqLocked ? that.data.freqIdx : (idx >= 0 ? idx : that.data.freqIdx)
        });
      }
    });
  },

  /* ── 启停开关 — 与网页端完全一致 ── */
  onSwitch(e) {
    const on = e.detail.value;
    if (!this._cmdLock) this._cmdLock = {};
    this._cmdLock.switch = Date.now();  /* 乐观锁: 3秒内不回弹 */
    const that = this;
    wx.request({
      url: ONENET.BASE_URL + '/thingmodel/set-device-property',
      method: 'POST',
      header: { 'Authorization': ONENET.TOKEN, 'Content-Type': 'application/json' },
      data: { product_id: ONENET.PRODUCT_ID, device_name: ONENET.DEVICE_NAME, params: { Switch: on } },
      success() {
        that.setData({ isOn: on, systemState: on ? 'SWEEP' : 'IDLE', stateLabel: on ? '扫频中' : '待机' });
      },
      fail() { wx.showToast({ title: '发送失败', icon: 'none' }); }
    });
  },

  _debounce(ts) {
    const now = Date.now();
    if (now - (this._lastTap || 0) < ts) return true;
    this._lastTap = now; return false;
  },

  /* ── 滑动选频 — 与网页端一致 ── */
  onSwiperChange(e) {
    const idx = e.detail.current;
    if (idx !== this.data.freqIdx) {
      this.setData({ freqIdx: idx, selectedFreq: FREQ_LIST[idx] });
    }
  },

  /* ── 确认设置 — 与网页端 toCloud 一致: kHz × 1000 ── */
  onSetFreq() {
    if (!this.data.isOn) { wx.showToast({ title: '请先启动设备', icon: 'none' }); return; }
    if (!this._cmdLock) this._cmdLock = {};
    this._cmdLock.freq = Date.now();
    const kHz = this.data.selectedFreq;
    const that = this;
    wx.request({
      url: ONENET.BASE_URL + '/thingmodel/set-device-property',
      method: 'POST',
      header: { 'Authorization': ONENET.TOKEN, 'Content-Type': 'application/json' },
      data: { product_id: ONENET.PRODUCT_ID, device_name: ONENET.DEVICE_NAME, params: { SetFreq: kHz * 1000 } },
      success() { wx.showToast({ title: 'Set ' + kHz + 'kHz', icon: 'none', duration: 800 }); },
      fail() { wx.showToast({ title: '发送失败', icon: 'none' }); }
    });
  }
});
