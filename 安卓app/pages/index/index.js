/* OneNET 直连 — 与网页端完全相同的后端逻辑 */
const ONENET = {
  PRODUCT_ID: '1iS397oJFL',
  DEVICE_NAME: '20260001',
  TOKEN: 'version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D',
  BASE_URL: 'https://iot-api.heclouds.com'
};
const POLL_MS = 2000;
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
/* PWM 停机时当前频率回零, 复位需从值域快照恢复 */
let s_last_display_freq = 100;

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
    this.fetchAll();
    setTimeout(() => this.setData({ spinning: false }), 1000);
  },

  onLoad() {
    const saved = wx.getStorageSync('wpt_theme');
    if (saved) this.setData({ currentTheme: saved });
    this.fetchAll();
    this._pollTimer = setInterval(() => this.fetchAll(), POLL_MS);
  },
  onToggleTheme() {
    const next = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark';
    this.setData({ currentTheme: next });
    wx.setStorageSync('wpt_theme', next);
  },
  onUnload() {
    if (this._pollTimer) clearInterval(this._pollTimer);
  },

  /* ── 合并轮询: V/I/F + Switch/SetFreq + 在线判断 (单次请求, 2s 间隔) ── */
  fetchAll() {
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
        let latestTime = 0;
        (res.data.data || []).forEach(item => {
          let v = item.value;
          if (v === 'true') v = true; else if (v === 'false') v = false;
          else if (!isNaN(v) && v !== '' && v !== undefined) v = Number(v);
          raw[item.identifier] = v;
          if (item.time && item.time > latestTime) latestTime = item.time;
        });

        /* 在线判断: 优先用 OneNET 返回的 time 字段 (毫秒时间戳), 若无则数据存在即在线 */
        let online;
        if (latestTime > 0) {
          online = (Date.now() - latestTime) < 30000;  /* 30s 内有数据 → 在线 */
        } else {
          online = (res.data.data && res.data.data.length > 0);  /* 无时间戳兜底 */
        }
        const now = Date.now();
        const lock = that._cmdLock || {};

        /* 数据 */
        const v = raw.V !== undefined ? Number(raw.V).toFixed(2) : that.data.voltage;
        const i = raw.I !== undefined ? Number(raw.I).toFixed(2) : that.data.current;
        const fHz = raw.F;
        const rawFreq = fHz !== undefined ? Math.floor(fHz / 1000) : 0;
        /* PWM 停机 → F=0 (STM32 遥测 S<DONE 时 V/I=0, 频率也显示 0), 停机期间保持最近已知频率供用户选频 */
        const isRunning = (raw.Switch === true);
        const fNum = isRunning ? (rawFreq > 0 ? rawFreq : that.data.frequency) : 0;
        if (isRunning && fNum > 0) s_last_display_freq = fNum;
        const displayFreq = isRunning ? fNum : 0;
        const fIdx = isRunning ? ((fNum > 0 && fNum >= 95) ? FREQ_LIST.indexOf(fNum) : -1) : -1;

        /* 控制状态 (乐观锁仅保护 Switch) */
        const sw = (lock.switch && (now - lock.switch < 5000)) ? that.data.isOn : (raw.Switch === true);

        let state, label;
        if (sw === true) { state = 'DONE';  label = '运行中'; }
        else             { state = 'IDLE';  label = '待机'; }

        /* 首次成功拿到数据 → 同步一次 swiper，之后永不再自动改 */
        if (!that._freqInited && online && fIdx >= 0 && isRunning) {
          that._freqInited = true;
          that.setData({ selectedFreq: fNum, freqIdx: fIdx });
        }

        that.setData({
          voltage: v, current: i, frequency: displayFreq, connected: online,
          isOn: isRunning, systemState: state, stateLabel: label
        });
      },
      fail() { that.setData({ connected: false }); }
    });
  },

  /* ── 发送命令 (通用, 支持网络抖动重试) ── */
  _sendCmd(params, cb, retry) {
    if (!retry) retry = 0;
    const that = this;
    const payload = JSON.stringify({
      product_id: ONENET.PRODUCT_ID,
      device_name: ONENET.DEVICE_NAME,
      params: params
    });
    wx.request({
      url: ONENET.BASE_URL + '/thingmodel/set-device-property',
      method: 'POST',
      header: { 'Authorization': ONENET.TOKEN, 'Content-Type': 'application/json' },
      data: payload,
      success(res) {
        if (res.statusCode === 200 && res.data.code === 0) {
          if (cb) cb();
        } else if (retry < 2) {
          setTimeout(() => that._sendCmd(params, cb, retry + 1), 600);
        } else {
          wx.showToast({ title: '下发失败', icon: 'none' });
        }
      },
      fail() {
        if (retry < 2) {
          setTimeout(() => that._sendCmd(params, cb, retry + 1), 800);
        } else {
          wx.showToast({ title: '发送失败', icon: 'none' });
        }
      }
    });
  },

  /* ── 启停开关 — 带验证重发 ── */
  onSwitch(e) {
    const on = e.detail.value;
    if (!this._cmdLock) this._cmdLock = {};
    this._cmdLock.switch = Date.now();
    const that = this;
    this._sendCmd({ Switch: on }, function() {
      that.setData({ isOn: on, systemState: on ? 'SWEEP' : 'IDLE', stateLabel: on ? '扫频中' : '待机' });
      /* 下发后 3s 只验证一次, 失败则弹 toast 提示而非静默忽略 */
      setTimeout(() => {
        wx.request({
          url: ONENET.BASE_URL + '/thingmodel/query-device-property?product_id=' + ONENET.PRODUCT_ID + '&device_name=' + ONENET.DEVICE_NAME,
          method: 'GET',
          header: { 'Authorization': ONENET.TOKEN },
          success(res) {
            const raw = {};
            (res.data.data || []).forEach(item => {
              let v = item.value;
              if (v === 'true') v = true; else if (v === 'false') v = false;
              raw[item.identifier] = v;
            });
            if (raw.Switch !== on) {
              that._sendCmd({ Switch: on });  /* 自动重发一次 */
              wx.showToast({ title: '指令未生效, 已重发', icon: 'none', duration: 1500 });
            }
          }
        });
      }, 3000);
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
