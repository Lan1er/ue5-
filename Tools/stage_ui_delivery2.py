# -*- coding: utf-8 -*-
"""把「UI 交付 2」里**实际用得到**的图整理进仓库 SourceArt/UI/Delivery2/<组>/（改 ASCII 名）。

用法（普通 python 即可，不需要 unreal）:
  python Tools/stage_ui_delivery2.py

设计要点：
  * 只挑用得到的图 —— 交付里 619 张 PNG，实际只取 ~95 张（预览图/标注/html/mp4 一律不取）。
  * 交付里同名文件可能出现在多个目录（三值 UI 的低/中/高各一套帧），所以按
    (所在目录名包含 marker, 文件名) 精确取图，不做"同名随便找一个"。
  * 落盘名统一 ASCII：引擎里的资产名干净，重导时也不受中文路径影响。
"""
import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# stage_ui_delivery2 的上游：解压后的交付目录（在仓库外，用环境变量 META_UI_STAGE 指定）
SRC_ROOT = os.environ.get("META_UI_STAGE", os.path.join(ROOT, "_staging", "ui_delivery2"))
DEST_ROOT = os.path.join(ROOT, 'SourceArt', 'UI', 'Delivery2')
REPORT_PATH = os.path.join(ROOT, 'Saved', 'ui_delivery2_stage.txt')

# (目录名必须包含的标记, 源文件名, 目标组, 目标名)
# 目标名 = 引擎资产名（会加 T_ 前缀的贴图习惯）
PICKS = [
    # ---------------- 三值 HUD：引擎切图（已归一化的独立小图） ----------------
    ('引擎切图', 'eye_0_闭眼.png',            'Stats', 'T_Eye_0'),
    ('引擎切图', 'eye_1_睁眼.png',            'Stats', 'T_Eye_1'),
    ('引擎切图', 'eye_2_红眼.png',            'Stats', 'T_Eye_2'),
    ('引擎切图', 'cocoon_0_完整茧.png',       'Stats', 'T_Cocoon_0'),
    ('引擎切图', 'cocoon_1_裂茧.png',         'Stats', 'T_Cocoon_1'),
    ('引擎切图', 'cocoon_2_破茧.png',         'Stats', 'T_Cocoon_2'),
    ('引擎切图', 'fill_green.png',            'Stats', 'T_Fill_Green'),
    ('引擎切图', 'fill_red.png',              'Stats', 'T_Fill_Red'),
    ('引擎切图', 'pip_empty.png',             'Stats', 'T_Pip_Empty'),
    ('引擎切图', 'pip_green.png',             'Stats', 'T_Pip_Green'),
    ('引擎切图', 'pip_red.png',               'Stats', 'T_Pip_Red'),

    # ---------------- 三值 HUD：分层 PNG（三种状态共用同一套框/底板） ----------------
    ('01_低值_阶段I', '02_标题外框.png',           'Stats', 'T_TitleFrame'),
    ('01_低值_阶段I', '03_标题底板.png',           'Stats', 'T_TitlePlate'),
    ('01_低值_阶段I', '01_标题文字.png',           'Stats', 'T_TitleText'),
    ('01_低值_阶段I', '06_怀疑外框.png',           'Stats', 'T_SuspFrame'),
    ('01_低值_阶段I', '07_怀疑底板_深墨绿.png',    'Stats', 'T_SuspPlate'),
    ('01_低值_阶段I', '04_躁郁外框.png',           'Stats', 'T_MoodFrame'),
    ('01_低值_阶段I', '05_躁郁底板_深墨绿.png',    'Stats', 'T_MoodPlate'),
    ('01_低值_阶段I', '03_读条轮廓.png',           'Stats', 'T_BarOutline'),
    ('01_低值_阶段I', '05_读条槽底.png',           'Stats', 'T_BarTrack'),
    ('01_低值_阶段I', '01_文字_怀疑值.png',        'Stats', 'T_Label_Suspicion'),
    ('01_低值_阶段I', '02_文字_躁郁值.png',        'Stats', 'T_Label_Mood'),
    ('01_低值_阶段I', '03_文字_体型阶段.png',      'Stats', 'T_Label_Body'),
    ('01_低值_阶段I', '02_阶段牌_空底.png',        'Stats', 'T_StagePlate'),
    ('01_低值_阶段I', '04_体型引导细线.png',       'Stats', 'T_BodyLine'),
    ('01_低值_阶段I', '圆点01_轮廓.png',           'Stats', 'T_PipFrame'),
    ('01_低值_阶段I', '01_阶段字符_I.png',         'Stats', 'T_StageChar_1'),
    ('02_中值_阶段II', '01_阶段字符_II.png',       'Stats', 'T_StageChar_2'),
    ('03_高值_阶段III', '01_阶段字符_III.png',     'Stats', 'T_StageChar_3'),

    # ---------------- 实机按钮 ----------------
    ('buttons', 'joystick_default.png',   'Buttons', 'T_Joystick_Default'),
    ('buttons', 'joystick_hover.png',     'Buttons', 'T_Joystick_Hover'),
    ('buttons', 'up_default.png',         'Buttons', 'T_Up_Default'),
    ('buttons', 'up_hover.png',           'Buttons', 'T_Up_Hover'),
    ('buttons', 'down_default.png',       'Buttons', 'T_Down_Default'),
    ('buttons', 'down_hover.png',         'Buttons', 'T_Down_Hover'),
    ('buttons', 'pickup_default.png',     'Buttons', 'T_Pickup_Default'),
    ('buttons', 'pickup_hover.png',       'Buttons', 'T_Pickup_Hover'),
    ('buttons', 'settings_default.png',   'Buttons', 'T_Settings_Default'),
    ('buttons', 'settings_hover.png',     'Buttons', 'T_Settings_Hover'),

    # ---------------- 开始界面（1x 归一化套件） ----------------
    ('1x', 'LOGO_BumpyDay.png',        'Start', 'T_Logo'),
    ('1x', 'TEXT_MenuStart.png',       'Start', 'T_Txt_Start'),
    ('1x', 'TEXT_MenuQuit.png',        'Start', 'T_Txt_Quit'),
    ('1x', 'TEXT_MenuSettings.png',    'Start', 'T_Txt_Settings'),
    ('1x', 'TEXT_MenuContinue.png',    'Start', 'T_Txt_Continue'),
    ('1x', 'DEC_MenuShade.png',        'Start', 'T_MenuShade'),
    ('1x', 'DEC_RedScratches.png',     'Start', 'T_RedScratches'),
    ('1x', 'FX_Vignette.png',          'Start', 'T_Vignette'),
    ('1x', 'MENU_FocusLine.png',       'Start', 'T_FocusLine'),
    ('1x', 'MENU_FocusStar.png',       'Start', 'T_FocusStar'),
    ('1x', 'ICON_Bag.png',             'Start', 'T_Icon_Bag'),
    ('1x', 'ICON_Settings.png',        'Start', 'T_Icon_Settings'),

    # ---------------- 设置页面 ----------------
    ('PNG图层', '002_面板投影.png',                'Settings', 'T_PanelShadow'),
    ('PNG图层', '003_暗绿面板材质.png',            'Settings', 'T_PanelBody'),
    ('PNG图层', '004_哥特金属外框.png',            'Settings', 'T_PanelFrame'),
    ('PNG图层', '005_分隔线_1.png',                'Settings', 'T_Divider'),
    ('PNG图层', '022_标题_设置_金色.png',          'Settings', 'T_Title'),
    ('PNG图层', '023_标签_声音大小.png',           'Settings', 'T_Lbl_Volume'),
    ('PNG图层', '024_标签_摇杆固定.png',           'Settings', 'T_Lbl_JoystickFixed'),
    ('PNG图层', '025_标签_摇杆大小.png',           'Settings', 'T_Lbl_JoystickSize'),
    ('PNG图层', '026_标签_物品栏大小.png',         'Settings', 'T_Lbl_HotbarSize'),
    ('PNG图层', '027_标签_上下按钮大小.png',       'Settings', 'T_Lbl_UpDownSize'),
    ('PNG图层', '032_声音大小_Default_轨道.png',   'Settings', 'T_SliderTrack'),
    ('PNG图层', '033_声音大小_Default_填充.png',   'Settings', 'T_SliderFill'),
    ('PNG图层', '034_声音大小_Default_滑块.png',   'Settings', 'T_SliderThumb'),
    ('PNG图层', '037_声音大小_Hover_滑块高亮.png', 'Settings', 'T_SliderThumbHi'),
    ('PNG图层', '039_声音大小_末端装饰.png',       'Settings', 'T_SliderCap'),
    ('PNG图层', '012_图标底板_1_暗绿.png',         'Settings', 'T_IconPlate'),
    ('PNG图层', '013_行首符文_1.png',              'Settings', 'T_Rune'),

    # ---------------- 失败 / 周目结算界面 ----------------
    ('PNG', 'art_room_clean.png',            'Failure', 'T_Bg'),
    ('PNG', 'art_readability_shade.png',     'Failure', 'T_Shade'),
    ('PNG', 'art_failed.png',                'Failure', 'T_Failed'),
    ('PNG', 'art_beetle.png',                'Failure', 'T_Beetle'),
    ('PNG', 'art_beetle_shadow.png',         'Failure', 'T_BeetleShadow'),
    ('PNG', 'ui_failure_headline.png',       'Failure', 'T_Headline'),
    ('PNG', 'ui_failure_subline.png',        'Failure', 'T_Subline'),
    ('PNG', 'art_button_restart_plate.png',  'Failure', 'T_RestartPlate'),
    ('PNG', 'ui_button_restart_label.png',   'Failure', 'T_RestartLabel'),
    ('PNG', 'ui_button_restart_focus.png',   'Failure', 'T_RestartFocus'),
    ('PNG', 'ui_button_menu_plate.png',      'Failure', 'T_MenuPlate'),
    ('PNG', 'ui_button_menu_label.png',      'Failure', 'T_MenuLabel'),
]

# 通配规则：(目录名包含的标记, 正则, 目标组, 目标名模板) —— 用于快捷栏 8 格这种批量命名
REGEX_PICKS = [
    # 快捷槽数字 1~8：58x58 正方形（buttons/ 里的 slot_*.png 是 58x74，不是正方形，所以用 png/ 这套）
    (r'^\\png$', re.compile(r'快捷槽_(\d)_独立格子\.png$'),  'Buttons', 'T_Slot{n}_Default'),
    (r'^\\png$', re.compile(r'快捷槽_(\d)_独立格子_悬停\.png$'), 'Buttons', 'T_Slot{n}_Hover'),
    # 快捷栏外框底板 614x117
    (r'^\\png$', re.compile(r'^011_051 .*外框底板\.png$'), 'Buttons', 'T_HotbarFrame'),
]


def main():
    if not os.path.isdir(SRC_ROOT):
        raise SystemExit('交付解压目录不存在：{}'.format(SRC_ROOT))

    # 建立索引：目录名 -> {文件名: 绝对路径}
    index = {}
    for dp, dn, fn in os.walk(SRC_ROOT):
        parent = os.path.basename(dp)
        bucket = index.setdefault(parent, {})
        for f in fn:
            if f.lower().endswith('.png'):
                bucket.setdefault(f, os.path.join(dp, f))

    report = []
    copied = 0
    for marker, src_name, group, dest_name in PICKS:
        bucket = index.get(marker, {})
        src = bucket.get(src_name)
        if not src:
            report.append('!! 缺源: [{}] {}'.format(marker, src_name))
            continue
        out_dir = os.path.join(DEST_ROOT, group)
        os.makedirs(out_dir, exist_ok=True)
        shutil.copyfile(src, os.path.join(out_dir, dest_name + '.png'))
        copied += 1

    # 通配规则
    for dir_pat, rx, group, name_tpl in REGEX_PICKS:
        hit = 0
        for parent, bucket in index.items():
            if not re.match(dir_pat, '\\' + parent):
                continue
            for fname, path in bucket.items():
                m = rx.search(fname)
                if not m:
                    continue
                n = m.group(1) if m.groups() else ''
                out_dir = os.path.join(DEST_ROOT, group)
                os.makedirs(out_dir, exist_ok=True)
                shutil.copyfile(path, os.path.join(out_dir, name_tpl.format(n=n) + '.png'))
                hit += 1
                copied += 1
        report.append('通配 {} 命中 {} 张'.format(name_tpl if False else group, hit))

    report.append('')
    report.append('共整理 {} 张 -> {}'.format(copied, DEST_ROOT))
    with open(REPORT_PATH, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))
    print('\n'.join(report))


if __name__ == '__main__':
    main()
