# -*- coding: utf-8 -*-
"""把开始界面的静态分层（07_psd/layers）烤成一张 1583x993 的背景图。

为什么要烤：
  · 本项目用 VerticalBox 摆内容，没有 Canvas，逐层定位风险大且难对齐；
  · 静态层（房间/道具/甲虫/压暗/划痕）位置固定、不参与交互 -> 一次性合成最稳（失败界面也是"按清单坐标摆"的思路）。

输出：SourceArt/UI/Delivery2/Start_Design/BG_Composed.png
"""
import os
from PIL import Image
import io, json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BASE = os.path.join(ROOT, "Saved", "_design")
LAYERS = os.path.join(BASE, "layers")
OUT = os.path.join(ROOT, "SourceArt", "UI", "Delivery2", "Start_Design", "BG_Composed.png")

# 图层清单：id -> (x, y)  ；顺序 = 从下往上
STACK = [
    ("L001", 0, 0),      # 房间净底 1583x993
    ("L002", 859, 171),  # 门后人影
    ("L003", 907, 269),  # 门口桌椅（前景遮挡）
    ("L004", 969, 125),  # 花束
    ("L005", 1049, 265), # 花瓶口（前景遮挡）
    ("L006", 840, 684),  # 虫子地面投影（正片叠底 -> 这里用普通 alpha 叠，观感接近）
    ("L007", 835, 581),  # 甲虫
    ("L008", 0, 0),      # 菜单压暗（左半 616x993）
    ("L009", 24, 246),   # 红色划痕 447x669
]

canvas = Image.new("RGBA", (1583, 993), (0, 0, 0, 255))
log = []
for lid, x, y in STACK:
    cand = [f for f in os.listdir(LAYERS) if f.startswith(lid + "_") and f.lower().endswith(".png")]
    if not cand:
        log.append("缺失 %s ✗" % lid)
        continue
    p = os.path.join(LAYERS, cand[0])
    img = Image.open(p).convert("RGBA")
    canvas.alpha_composite(img, (x, y))
    log.append("%s @(%d,%d) %dx%d <- %s" % (lid, x, y, img.width, img.height, cand[0]))

canvas.save(OUT)
print("输出:", OUT, canvas.size)
for l in log:
    print("  " + l)
