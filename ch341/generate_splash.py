#!/usr/bin/env python3
"""
generate_splash.py — 生成 WPT-PWM 开机动画 splash.bin (5帧 fade-in)
V1.0  2026-06-24

输出: splash.bin (200KB, 5帧 × 40KB 160x128 RGB565)
SPLASH Header: magic="SP"(0x5350) + version + n_frames + width + height + frame_bytes
"""

import struct, os
from PIL import Image, ImageDraw, ImageFont

SPLASH_BIN   = os.path.join(os.path.dirname(__file__), "splash.bin")
W, H         = 160, 128
N_FRAMES     = 5
FRAME_BYTES  = W * H * 2  # 40960
MAGIC        = 0x5350      # "SP"

def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def render_frame(brightness):
    """渲染一帧 160×128 RGB565, brightness 0.0~1.0"""
    img = Image.new('RGB', (W, H), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    try:
        font_title = ImageFont.truetype("simsun.ttc", 20)
    except:
        font_title = ImageFont.load_default()
    try:
        font_sub = ImageFont.truetype("simsun.ttc", 14)
    except:
        font_sub = ImageFont.load_default()

    title = "WPT-PWM"
    bbox = draw.textbbox((0, 0), title, font=font_title)
    tw = bbox[2] - bbox[0]
    draw.text(((W - tw)//2, 25), title, font=font_title,
              fill=(int(255*brightness), int(200*brightness), 0))

    sub = "Wireless Power Transfer"
    bbox2 = draw.textbbox((0, 0), sub, font=font_sub)
    sw = bbox2[2] - bbox2[0]
    draw.text(((W - sw)//2, 52), sub, font=font_sub,
              fill=(int(180*brightness), int(180*brightness), int(180*brightness)))

    ver = "V4.3.2"
    bbox3 = draw.textbbox((0, 0), ver, font=font_sub)
    vw = bbox3[2] - bbox3[0]
    draw.text(((W - vw)//2, 72), ver, font=font_sub,
              fill=(int(120*brightness), int(120*brightness), int(120*brightness)))

    hint = "STM32 + ESP8266 + OneNET"
    bbox4 = draw.textbbox((0, 0), hint, font=font_sub)
    hw = bbox4[2] - bbox4[0]
    draw.text(((W - hw)//2, 100), hint, font=font_sub,
              fill=(int(100*brightness), int(100*brightness), int(100*brightness)))

    frame = bytearray(FRAME_BYTES)
    for y in range(H):
        for x in range(W):
            r, g, b = img.getpixel((x, y))
            rgb565 = rgb888_to_rgb565(r, g, b)
            frame[(y*W + x)*2]     = rgb565 & 0xFF
            frame[(y*W + x)*2 + 1] = (rgb565 >> 8) & 0xFF
    return bytes(frame)

def main():
    buf = bytearray()
    buf.extend(struct.pack('<H', MAGIC))
    buf.append(1)
    buf.append(N_FRAMES)
    buf.extend(struct.pack('<HH', W, H))
    buf.extend(struct.pack('<I', FRAME_BYTES))
    buf.extend(b'\x00' * 20)
    for i in range(N_FRAMES):
        brightness = 0.2 + 0.8 * (i / max(N_FRAMES-1, 1))
        print(f"  Frame {i+1}/{N_FRAMES}: brightness={brightness:.1f}")
        buf.extend(render_frame(brightness))
    with open(SPLASH_BIN, 'wb') as f:
        f.write(buf)
    print(f"[OK] 生成 {SPLASH_BIN} ({len(buf)} 字节 = {len(buf)//1024}KB)")

if __name__ == '__main__':
    main()
