# -*- coding: utf-8 -*-
"""生成开始界面「新游戏」按钮的美术文字图（Txt3_NewGame.png）。

为什么需要：
  · 交付包里「开始游戏」是烘死在 L012 / TEXT_MenuStart 里的（字就是"开始游戏"）。
  · 用户要求这个按钮显示「新游戏」。
  · 用系统字体（Roboto 无衬线）做"文字块"会和另外三张宋体字明显不一致（用户反馈"文字风格不符合"）。
  · 所以按同一规格（462x126 画布、宋体、色 RGB(210,200,164)）生成一张图，替换进去当美术图用。

规格（与交付包 3x 文字图一致）：
  画布 462x126 / alpha 包围盒约 440x104 / 文字色 RGB(210,200,164) / 宋体（华文宋体 STSong 实测最接近）

用法：python Tools/render_newgame_text.py
输出：SourceArt/UI/Delivery2/Start_Design/Txt3_NewGame.png
然后重跑 Tools/import_start_design.py 导入。
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

W, H = 462, 126
COLOR = (210, 200, 164)
TEXT = "新游戏"
CANDIDATES = [
    (r"C:\Windows\Fonts\STSONG.TTF", "STSong"),
    (r"C:\Windows\Fonts\simsun.ttc", "SimSun"),
    (r"C:\Windows\Fonts\NotoSerifSC-VF.ttf", "NotoSerifSC"),
]
OUT = os.path.join(ROOT, "SourceArt", "UI", "Delivery2", "Start_Design", "Txt3_NewGame.png")
# 交付包 3x 文字图的字形高度（去掉边距后的净高）
TARGET_GLYPH_H = 104


def pick_font():
    for path, name in CANDIDATES:
        for size in range(60, 140):
            try:
                font = ImageFont.truetype(path, size)
            except Exception:
                break
            bbox = font.getbbox(TEXT)
            if bbox[3] - bbox[1] >= TARGET_GLYPH_H:
                return font, name, size, bbox
    raise RuntimeError("找不到合适的宋体字体")


def main():
    font, name, size, bbox = pick_font()
    print("  选用字体：%s size=%d bbox=%s" % (name, size, bbox))
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (W - gw) // 2 - bbox[0]
    y = (H - gh) // 2 - bbox[1]
    draw.text((x, y), TEXT, font=font, fill=COLOR + (255,))
    im.save(OUT)
    print("  已生成：%s  bbox=%s" % (OUT, im.getbbox()))


if __name__ == "__main__":
    main()
