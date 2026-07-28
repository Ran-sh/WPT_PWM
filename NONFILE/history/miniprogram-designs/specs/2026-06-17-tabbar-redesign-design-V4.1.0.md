# 底部选择栏重构设计文档

**日期**: 2026-06-17  
**版本**: V25  
**参考**: 小红书/Douyin/TikTok 移动端交互模式

## 背景

当前底部栏使用微信原生 `custom-tab-bar` Component 方案，存在三个已验证的根因：

1. **逻辑竞态** — Component `pageLifetimes.show` 与页面 `onShow` 中 `getTabBar().setData()` 三重覆盖 `selected`
2. **页面栈读取延迟** — `switchTab` 后 `getCurrentPages()` 未立即更新，读到旧路由
3. **点击区域不一致** — 文字与图标独立渲染，真机上触摸面积不均

## 架构

### Template + app.globalData 替代 Component + getCurrentPages()

```
数据流:
  app.globalData.tabBarSelected = 0   (单一权威来源)
       ↓
  Template onTabTap(e)
       → app.globalData.tabBarSelected = idx
       → wx.setStorageSync('wpt_tab', idx)   (冷启动恢复)
       → wx.switchTab({ url: '/' + path })
       ↓
  目标页面 onShow
       → 读 app.globalData.tabBarSelected
       → setData({ _tabSelected: idx })
       → Template 自动高亮
```

关键差异:
- **不再读 getCurrentPages()** —— 数据驱动，不依赖框架页面栈同步
- **单一写入者** —— 只有 Template `onTabTap` 写 `selected`
- **页面 onShow 只读不写** —— 消除三重竞态

### Template 文件结构

```
安卓app/
├── templates/
│   └── tab-bar.wxml    ← 底部栏模板 (新建)
│   └── tab-bar.wxss    ← 模板样式 (新建)
├── app.js              ← 新增 globalData.tabBarSelected
├── app.wxss            ← 移除旧的 .page padding-bottom 逻辑
└── pages/
    ├── index/index.wxml    ← <import src="../../templates/tab-bar.wxml"/>
    ├── index/index.wxss    ← @import "../../templates/tab-bar.wxss"
    └── ... (其余 4 页同样)
```

不再需要的文件:
- `custom-tab-bar/index.js`  → 删除
- `custom-tab-bar/index.wxml` → 删除
- `custom-tab-bar/index.wxss` → 删除
- `custom-tab-bar/index.json` → 删除
- `app.json` 中 `"tabBar": { "custom": true }` → 移除

## 视觉设计

### 尺寸与间距

| 元素 | 值 |
|:---|:---|
| 栏高度 | 100rpx |
| 图标字号 | 40rpx |
| 标签字号 | 18rpx |
| 图标-标签间距 | 4rpx |
| Pill 指示器 | 28rpx × 5rpx，顶部 2rpx |
| 图标容器 (活跃态) | 48rpx × 48rpx, border-radius 14rpx |
| 触摸区域 | 等宽 flex:1, min-height: 100rpx |
| 安全区 | `env(safe-area-inset-bottom)` |

### 图标

| Tab | 图标 | Unicode | 真机兼容性 |
|:---|:---|:---|:---|
| 首页 | ⌂ | U+2302 | 全平台 ✅ |
| 监测 | ◉ | U+25C9 | 全平台 ✅ |
| 控制 | ⊛ | U+229B | 全平台 ✅ |
| 历史 | 🗂 | U+1F5C2 | Emoji 范围，全平台渲染一致 ✅ |
| 设置 | ⚙ | U+2699 | 全平台 ✅ |

### 背景

磨砂玻璃半透明 + 模糊。微信小程序不支持 CSS `backdrop-filter`: 用 `rgba(10,12,24,0.95)` 实色背景 + 顶部 1px 发光渐变线替代。

### 动效

| 交互 | 效果 | 时长 / 缓动 |
|:---|:---|:---|
| 点击 | 弹性缩放 scale(0.92) + vibrateShort('light') | 0.15s cubic-bezier(0.34,1.56,0.64,1) |
| Pill 滑移 | 旧位置消失 + 新位置弹入 scaleX(0→1) | 0.22s cubic-bezier(0.34,1.56,0.64,1) |
| 图标激活 | 颜色渐变 + 微上浮 translateY(-2rpx) | 0.18s ease |
| 图标容器 | 背景淡入 rgba(0,229,255,0→0.08) | 0.18s ease |
| 标签 | 颜色 + 字重 (600→800) 渐变 | 0.18s ease |

### 防连点

`onTabTap` 内 `_tabLock` 布尔锁 + 200ms throttle。锁期间忽略所有点击。`switchTab` 完成后 `onShow` 解锁。

## 功能对齐 Web 端

| 特性 | 网页端 | 小程序新方案 |
|:---|:---|:---|
| Tab 数量 | 6 (含报警) | 5 (报警从横幅跳转) |
| 高亮来源 | `location.pathname` 匹配 | `app.globalData.tabBarSelected` |
| 切换方式 | `<a href>` 原生跳转 | `wx.switchTab` |
| 状态持久化 | URL 即真相 | `wx.setStorageSync` 冷启动恢复 |

## 页面 padding-bottom

每个页面统一:
```css
.page {
  padding: 0 24rpx calc(100rpx + env(safe-area-inset-bottom, 0px));
}
```

## 实现步骤

1. 创建 `templates/tab-bar.wxml` + `templates/tab-bar.wxss`
2. 修改 `app.js` 添加 `globalData.tabBarSelected`
3. 修改 `app.json` 移除 `"tabBar": { "custom": true }`
4. 修改 5 个页面的 WXML (import template + 底部引用)
5. 修改 5 个页面的 WXSS (@import + 更新 padding-bottom)
6. 修改 5 个页面的 JS (onShow 读 globalData, 删除 getTabBar 调用)
7. 删除 custom-tab-bar 目录
8. 编译验证
