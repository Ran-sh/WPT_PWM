/* OneNET 直连 — 与网页端完全相同的后端逻辑 */
const ONENET = {
  PRODUCT_ID: '1iS397oJFL',
  DEVICE_NAME: '20260001',
  TOKEN: 'version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D',
  BASE_URL: 'https://iot-api.heclouds.com'
};
const POLL_MS = 3000;
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
    currentTheme: 'theme-dark'
  },

  onLoad() {
    const saved = wx.getStorageSync('wpt_theme');
    if (saved) this.setData({ currentTheme: saved });
    this.fetchData();
    this._timer = setInterval(() => this.fetchData(), POLL_MS);
  },
  onToggleTheme() {
    const next = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark';
    this.setData({ currentTheme: next });
    wx.setStorageSync('wpt_theme', next);
  },
  onUnload() { if (this._timer) clearInterval(this._timer); },

  /* ── 数据获取 — 直连 OneNET HTTP API (与网页端一致) ── */
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

        const now = Date.now();
        const lock = that._cmdLock || {};

        /* 乐观锁: 3秒内刚下发的指令不覆盖 (与网页端一致) */
        const sw = (lock.switch && (now - lock.switch < 3000)) ? that.data.isOn : (raw.Switch === true);

        const v = raw.V !== undefined ? Number(raw.V).toFixed(2) : that.data.voltage;
        const i = raw.I !== undefined ? Number(raw.I).toFixed(2) : that.data.current;
        const fHz = raw.F;
        const fNum = fHz !== undefined ? Math.floor(fHz / 1000) : that.data.frequency;

        let state, label;
        if (sw === true && fNum > 0) { state = 'DONE'; label = '运行中'; }
        else if (sw === true)        { state = 'SWEEP'; label = '扫频中'; }
        else                         { state = 'IDLE';  label = '待机'; }

        /* 乐观锁: 3秒内刚设的频率不覆盖 */
        const freqLocked = lock.freq && (now - lock.freq < 3000);
        const freqKHz = freqLocked ? that.data.selectedFreq : fNum;

        /* 首次连接成功 → 强制同步频率选取器 */
        const justConnected = !that.data.connected;
        const idx = FREQ_LIST.indexOf(freqKHz);
        const syncIdx = justConnected ? (idx >= 0 ? idx : INIT_IDX)
                      : (freqLocked ? that.data.freqIdx : (idx >= 0 ? idx : that.data.freqIdx));

        that.setData({
          voltage: v, current: i, frequency: fNum,
          systemState: state, stateLabel: label,
          isOn: sw, isFault: false, connected: true,
          selectedFreq: freqKHz,
          freqIdx: syncIdx
        });
      },
      fail() { that.setData({ connected: false }); }
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

  /* ── 确认设置 — 使用 FREQ_HZ 精确映射 ── */
  onSetFreq() {
    if (!this.data.isOn) { wx.showToast({ title: '请先启动设备', icon: 'none' }); return; }
    if (!this._cmdLock) this._cmdLock = {};
    this._cmdLock.freq = Date.now();  /* 乐观锁: 3秒内不回弹 */
    const kHz = this.data.selectedFreq;
    const hz  = FREQ_HZ[this.data.freqIdx];
    const that = this;
    wx.request({
      url: ONENET.BASE_URL + '/thingmodel/set-device-property',
      method: 'POST',
      header: { 'Authorization': ONENET.TOKEN, 'Content-Type': 'application/json' },
      data: { product_id: ONENET.PRODUCT_ID, device_name: ONENET.DEVICE_NAME, params: { SetFreq: hz } },
      success() { wx.showToast({ title: 'Set ' + kHz + 'kHz', icon: 'none', duration: 800 }); },
      fail() { wx.showToast({ title: '发送失败', icon: 'none' }); }
    });
  }
});
