# -*- coding: utf-8 -*-
"""把「失败页面」交付里的 26 张裁切图层整理进 SourceArt/UI/Settlement/（普通 python，不用 unreal）。

用法： python Tools/stage_settlement.py
源目录： $META_UI_STAGE/失败页面_同步v2/PNG（未设置时用 _staging/失败页面_同步v2/PNG）
输出：   SourceArt/UI/Settlement/T_<id>.png
"""
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# 美术交付包的解压目录在仓库外：用环境变量 META_UI_STAGE 指定，
# 默认落在工程的 _staging/ 下（该目录不纳入版本管理）。
SRC = os.environ.get("META_UI_STAGE", os.path.join(ROOT, "_staging", "失败页面_同步v2", "PNG"))
DST = os.path.join(ROOT, "SourceArt", "UI", "Settlement")

# 只要这 26 张（ui_button_restart_focus 默认隐藏，也一起带上备用 -> 27）
LAYERS = [
    "art_room_clean", "art_readability_shade", "art_failed", "art_beetle", "art_beetle_shadow",
    "art_button_restart_plate", "ui_button_restart_label", "ui_button_restart_focus",
    "ui_button_menu_plate", "ui_button_menu_label",
    "ui_failure_headline", "ui_failure_subline",
    "ui_hud_title", "ui_hud_divider", "ui_hud_cycle", "ui_hud_time",
    "ui_hud_eye_user_smartobject", "ui_hud_suspicion_label", "ui_hud_suspicion_track", "ui_hud_suspicion_fill",
    "ui_memory_beetle", "ui_memory_label",
    "ui_memory_slot_01", "ui_memory_slot_02", "ui_memory_slot_03", "ui_memory_slot_04", "ui_memory_slot_05",
]


def main():
    os.makedirs(DST, exist_ok=True)
    copied = 0
    for name in LAYERS:
        src = os.path.join(SRC, name + ".png")
        if not os.path.exists(src):
            print("  [缺] %s" % name)
            continue
        dst = os.path.join(DST, "T_" + name + ".png")
        shutil.copyfile(src, dst)
        copied += 1
    print("  已整理 %d/%d 张 -> %s" % (copied, len(LAYERS), DST))


if __name__ == "__main__":
    main()
