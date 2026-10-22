# -*- coding: utf-8 -*-
"""把开始界面交付包里的【高分辨率】素材整理到 SourceArt/UI/Delivery2/Start_Design。

为什么需要：
  · 之前用的是 07_psd/layers 里的 1x 按钮文字图（136x31 那种），在 342x78 的按钮里被放大 2.5 倍
    -> 笔画发虚、看着"不像美术画的"（用户反馈"文字风格不符合"）。
  · 交付包 03_ui_assets 里同款素材有 3x 版本（462x126），用它显示就清晰。
  · 进度条也用同一套交付：HUD_BarTrack（槽）/ HUD_BarFill（填充）/ HUD_BarFrame（金框）。

用法（普通 python，不需要编辑器）：
  python Tools/stage_start_hi.py
输出：SourceArt/UI/Delivery2/Start_Design/ 下
  Txt3_Continue/Start/Settings/Quit.png + BarTrack/BarFill/BarFrame.png
"""
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PKG = r"F:\乱七八糟的文件\游戏开始界面\开始界面\变形记_UI动态交付包_20261017\03_ui_assets\3x"
DST = os.path.join(ROOT, "SourceArt", "UI", "Delivery2", "Start_Design")

# 源文件名（3x） -> 目标名
MAP = {
    "TEXT_MenuContinue.png": "Txt3_Continue.png",
    "TEXT_MenuStart.png": "Txt3_Start.png",
    "TEXT_MenuSettings.png": "Txt3_Settings.png",
    "TEXT_MenuQuit.png": "Txt3_Quit.png",
    "HUD_BarTrack.png": "BarTrack.png",
    "HUD_BarFill.png": "BarFill.png",
    "HUD_BarFrame.png": "BarFrame.png",
}


def main():
    os.makedirs(DST, exist_ok=True)
    ok = 0
    for src_name, dst_name in MAP.items():
        src = os.path.join(PKG, src_name)
        if not os.path.isfile(src):
            print("  MISSING %s" % src)
            continue
        shutil.copyfile(src, os.path.join(DST, dst_name))
        ok += 1
        print("  %-22s -> %s" % (src_name, dst_name))
    print("完成：%d/%d -> %s" % (ok, len(MAP), DST))


if __name__ == "__main__":
    main()
