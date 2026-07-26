import { timingSafeEqual } from 'node:crypto';

const LOW_FREQ_MIN_HZ = 20000;
const LOW_FREQ_MAX_HZ = 99900;
const HIGH_FREQ_MIN_HZ = 100000;
const HIGH_FREQ_MAX_HZ = 200000;

/**
 * 校验并规范化下行命令，防止前缀匹配把附加内容当作合法命令。
 * @param {unknown} input 待校验的命令
 * @returns {string|null} 合法命令，非法时返回 null
 */
export function normalizeCommand(input) {
  if (typeof input !== 'string') return null;
  const command = input.trim();
  if (command === 'CMD:ON' || command === 'CMD:OFF') return command;

  const match = /^CMD:SETFREQ:([0-9]{5,6})$/.exec(command);
  if (!match) return null;
  const frequency = Number(match[1]);
  const lowValid = frequency >= LOW_FREQ_MIN_HZ &&
    frequency <= LOW_FREQ_MAX_HZ && frequency % 100 === 0;
  const highValid = frequency >= HIGH_FREQ_MIN_HZ &&
    frequency <= HIGH_FREQ_MAX_HZ && frequency % 1000 === 0;
  return (lowValid || highValid) ? `CMD:SETFREQ:${frequency}` : null;
}

/**
 * 解析并校验 STM32 遥测，拒绝字符串数字、非有限值及越界状态。
 * @param {string|Buffer} payload MQTT 遥测负载
 * @returns {{V:number,I:number,F:number,S:number}|null} 合法遥测
 */
export function parseTelemetry(payload) {
  let value;
  try {
    value = JSON.parse(Buffer.isBuffer(payload) ? payload.toString('utf8') : payload);
  } catch {
    return null;
  }
  if (!value || typeof value !== 'object') return null;

  const { V, I, F, S } = value;
  if (![V, I, F, S].every((item) => typeof item === 'number' && Number.isFinite(item))) {
    return null;
  }
  if (V < 0 || V > 60 || I < 0 || I > 20 || !Number.isInteger(F) ||
      !Number.isInteger(S) || S < 0 || S > 3) {
    return null;
  }
  if (F !== 0 && (F < LOW_FREQ_MIN_HZ || F > HIGH_FREQ_MAX_HZ)) return null;
  return { V, I, F, S };
}

/**
 * 使用恒定时间比较控制密钥，且配置为空时默认关闭控制接口。
 * @param {unknown} candidate 请求携带的密钥
 * @param {unknown} expected 服务端配置的密钥
 * @returns {boolean} 是否通过鉴权
 */
export function isApiKeyValid(candidate, expected) {
  if (typeof candidate !== 'string' || typeof expected !== 'string' || expected.length === 0) {
    return false;
  }
  const candidateBuffer = Buffer.from(candidate, 'utf8');
  const expectedBuffer = Buffer.from(expected, 'utf8');
  if (candidateBuffer.length !== expectedBuffer.length) return false;
  return timingSafeEqual(candidateBuffer, expectedBuffer);
}
