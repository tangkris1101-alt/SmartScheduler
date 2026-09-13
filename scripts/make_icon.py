#!/usr/bin/env python
# 生成 SmartScheduler 应用图标：assets/icon.png (256px) + assets/icon.ico (多尺寸)
# 设计：蓝色渐变圆角方形底 + 白色课表卡片（表头 + 3x2 课表格，一格高亮=已排课程）
from PIL import Image, ImageDraw

S = 4                      # 超采样倍数，抗锯齿
SIZE = 256 * S
RADIUS_RATIO = 0.225       # 底板圆角（macOS 风格连续圆角近似）

BG_TOP = (37, 99, 235)     # #2563eb 主题色
BG_BOT = (96, 165, 250)    # #60a5fa
CARD = (255, 255, 255)
CARD_HEADER = (219, 234, 254)   # #dbeafe
CELL = (191, 219, 254)          # #bfdbfe
ACCENT = (249, 115, 22)         # #f97316 高亮橙
RING = (30, 64, 175)            # #1e40af


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def make(size=SIZE):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # --- 底板：竖向渐变 + 圆角蒙版 ---
    grad = Image.new("RGB", (1, size))
    for y in range(size):
        grad.putpixel((0, y), lerp(BG_TOP, BG_BOT, y / (size - 1)))
    grad = grad.resize((size, size))
    mask = Image.new("L", (size, size), 0)
    md = ImageDraw.Draw(mask)
    m = size * 0.055
    md.rounded_rectangle([m, m, size - m, size - m], radius=size * RADIUS_RATIO, fill=255)
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)

    # --- 课表卡片 ---
    cx0, cy0 = size * 0.235, size * 0.255
    cx1, cy1 = size * 0.765, size * 0.775
    card_r = size * 0.05
    d.rounded_rectangle([cx0, cy0, cx1, cy1], radius=card_r, fill=CARD + (255,))

    # 卡片表头（上半圆角，下边缘补方）
    hh = size * 0.115
    d.rounded_rectangle([cx0, cy0, cx1, cy0 + hh], radius=card_r, fill=CARD_HEADER + (255,))
    d.rectangle([cx0, cy0 + hh / 2, cx1, cy0 + hh], fill=CARD_HEADER + (255,))

    # 卡片顶部两个挂环
    rw, rh = size * 0.030, size * 0.105
    for rx in (size * 0.36, size * 0.585):
        d.rounded_rectangle([rx, cy0 - size * 0.05, rx + rw, cy0 - size * 0.05 + rh],
                            radius=rw / 2, fill=RING + (255,))

    # --- 课表格 3 列 x 2 行，(行1,列2) 高亮 ---
    gx0, gy0 = size * 0.30, size * 0.45
    cw, ch = size * 0.113, size * 0.098
    gap = size * 0.026
    cell_r = size * 0.02
    accent_rc = (0, 1)
    for r in range(2):
        for c in range(3):
            x0 = gx0 + c * (cw + gap)
            y0 = gy0 + r * (ch + gap)
            color = ACCENT if (r, c) == accent_rc else CELL
            d.rounded_rectangle([x0, y0, x0 + cw, y0 + ch], radius=cell_r, fill=color + (255,))

    return img.resize((256, 256), Image.LANCZOS)


if __name__ == "__main__":
    import os
    out = os.path.join(os.path.dirname(__file__), "..", "assets")
    icon = make()
    icon.save(os.path.join(out, "icon.png"))
    icon.save(os.path.join(out, "icon.ico"),
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("written:", os.path.join(out, "icon.png"), "and icon.ico")
