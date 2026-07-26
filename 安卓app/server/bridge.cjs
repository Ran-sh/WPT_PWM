#!/usr/bin/env node
/* 兼容旧启动命令；实际实现统一由 bridge.mjs 提供。 */
import('./bridge.mjs')
  .then(({ startBridge }) => startBridge())
  .catch((error) => {
    console.error('[桥接] 启动失败:', error.message);
    process.exitCode = 1;
  });
