# -*- coding: utf-8 -*-
"""
把「设置UI完整交付_v3」的静态层按 图层坐标清单.json 烤成一张背景图，
并抽出需要"活的"几张图（滑块填充/滑块/开关钮/按钮底板）。

为什么烤：本工程 UMG 里 75 层逐层绝对定位又慢又容易错，而静态层不参与交互，
合成一张最稳（开始界面、失败界面都是这么做的）。

输出：
  SourceArt/UI/Settings/BG_Settings.png     1672x941  静态背景（面板/外框/分隔线/标题/标签/符文/轨道/返回按钮）
  SourceArt/UI/Settings/Fill.png            动态：滑块填充（拉伸用）
  SourceArt/UI/Settings/Handle.png          动态：滑块
  SourceArt/UI/Settings/SwitchKnob.png      动态：摇杆固定开关钮
  SourceArt/UI/Settings/BtnPlate.png        动态：「返回主界面」按钮底板（复用「返回」的底板）
"""
import io
import json
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC = r"F:\乱七八糟的文件\设置页面\变形记_设置UI完整交付_v3"
DST = os.path.join(ROOT, "SourceArt", "UI", "Settings")

# 烤进背景的层（索引 -> 用 assets 顺序，见 图层坐标清单.json）
BAKE = list(range(0, 27)) + [31, 38, 39, 46, 47, 54, 55, 62, 63, 67, 68, 69, 70]

# 单独抽出的层
EXTRACT = {
    32: "Fill.png",          # 声音大小_Default_填充（宽度按百分比裁剪/拉伸）
    33: "Handle.png",        # 声音大小_Default_滑块
    64: "SwitchKnob.png",    # 摇杆固定_Default_开关钮
    67: "BtnPlate.png",      # 返回_Default_暗绿底板（给「返回主界面」复用）
}


def main():
    j = json.load(io.open(os.path.join(SRC, "图层坐标清单.json"), encoding="utf-8"))
    canvas = tuple(j["canvas"])
    assets = j["assets"]
    os.makedirs(DST, exist_ok=True)

    bg = Image.new("RGBA", canvas, (0, 0, 0, 0))
    for idx in BAKE:
        a = assets[idx]
        p = os.path.join(SRC, "PNG图层", a["file"])
        if not os.path.exists(p):
            print("  [跳过] 缺图: %s" % a["file"])
            continue
        layer = Image.open(p).convert("RGBA")
        if (layer.width, layer.height) != (a["width"], a["height"]):
            layer = layer.resize((a["width"], a["height"]), Image.LANCZOS)
        bg.alpha_composite(layer, (a["x"], a["y"]))
    bg_path = os.path.join(DST, "BG_Settings.png")
    bg.save(bg_path)
    print("  背景已烤: %s  (%dx%d, %d 层)" % (bg_path, bg.width, bg.height, len(BAKE)))

    for idx, name in EXTRACT.items():
        a = assets[idx]
        p = os.path.join(SRC, "PNG图层", a["file"])
        im = Image.open(p).convert("RGBA")
        im.save(os.path.join(DST, name))
        print("  抽出: %-16s <- %s  (%dx%d)" % (name, a["file"], im.width, im.height))


if __name__ == "__main__":
    main()
