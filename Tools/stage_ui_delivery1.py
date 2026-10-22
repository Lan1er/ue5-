# -*- coding: utf-8 -*-
"""把「ui交付1」里的素材整理成项目可导入的 PNG（SourceArt/UI/<组>/<英文名>.png）。

用法（普通 python 即可，不需要 unreal）：
  python Tools/stage_ui_delivery1.py

来源：$META_UI_STAGE/ui交付1/*.zip（不解压整包，直接读 zip 内的图）
产物：SourceArt/UI/{Inventory,Backpack,Pause,Dialogue}/...
      + Saved/ui_delivery1_staging.txt（映射表，供文档引用）
"""
import io
import os
import zipfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# 美术交付包的解压目录在仓库外：用环境变量 META_UI_STAGE 指定，
# 默认落在工程的 _staging/ 下（该目录不纳入版本管理）。
SRC = os.environ.get("META_UI_STAGE", os.path.join(ROOT, "_staging", "ui交付1"))
OUT_ROOT = os.path.join(ROOT, 'SourceArt', 'UI')
REPORT = os.path.join(ROOT, 'Saved', 'ui_delivery1_staging.txt')

ZIP_ICON = os.path.join(SRC, '道具库.zip')
ZIP_PAUSE = os.path.join(SRC, '暂停页面.zip')
ZIP_DLG = os.path.join(SRC, '对话框序列.zip')

# 目标: (组, 输出名, zip, 匹配后缀, 处理方式)
# 处理方式: None=原样, 'crop_alpha'=按 alpha 包围盒裁切, 'progress'=等比缩放到 800x320
JOBS = [
    # 快捷栏 / 背包格子（默认 / 悬停）
    ('Inventory', 'Slot_Default', ZIP_ICON, '状态成品PNG/格子_01_默认.png', None),
    ('Inventory', 'Slot_Hover', ZIP_ICON, '状态成品PNG/格子_01_悬停.png', None),
    # 道具库（书本式背包）
    ('Backpack', 'Bag_LeftPage', ZIP_ICON, 'PNG_裁切/01_左页_旧纸张_补全底纹.png', None),
    ('Backpack', 'Bag_RightPage', ZIP_ICON, 'PNG_裁切/02_右页_旧纸张_补全底纹.png', None),
    ('Backpack', 'Bag_Frame', ZIP_ICON, 'PNG_裁切/03_外框_不规则破损纸边.png', None),
    ('Backpack', 'Bag_Binding', ZIP_ICON, 'PNG_裁切/04_左侧_装订孔与线圈.png', None),
    ('Backpack', 'Bag_Gutter', ZIP_ICON, 'PNG_裁切/05_中缝_折痕与阴影.png', None),
    ('Backpack', 'Bag_PreviewPaper', ZIP_ICON, 'PNG_裁切/06_右侧_空白预览框_纸底.png', None),
    ('Backpack', 'Bag_PreviewFrame', ZIP_ICON, 'PNG_裁切/07_右侧_预览框_墨色边框.png', None),
    ('Backpack', 'Bag_Close', ZIP_ICON, '状态成品PNG/关闭按钮_默认.png', None),
    ('Backpack', 'Bag_CloseHover', ZIP_ICON, '状态成品PNG/关闭按钮_悬停.png', None),
    ('Backpack', 'Bag_Title', ZIP_ICON, 'PNG_裁切/45_标题_道具库.png', None),
    # 暂停页
    ('Pause', 'Pause_BG', ZIP_PAUSE, 'PNG/BG_01_room.png', None),
    ('Pause', 'Pause_BtnPlate_Normal', ZIP_PAUSE, 'PNG/BTN_01_continue_normal_plate.png', 'crop_alpha'),
    ('Pause', 'Pause_BtnPlate_Hover', ZIP_PAUSE, 'PNG/BTN_01_continue_hover_plate.png', 'crop_alpha'),
    ('Pause', 'Pause_Title', ZIP_PAUSE, 'PNG/TTL_03_pause.png', 'crop_alpha'),
    ('Pause', 'Pause_Beetle', ZIP_PAUSE, 'PNG/FG_02_beetle.png', 'crop_alpha'),
    ('Pause', 'Pause_Shadow', ZIP_PAUSE, 'PNG/FG_01_shadow.png', 'crop_alpha'),
    # 对话框静态层
    ('Dialogue', 'DLG_BackPaper_A', ZIP_DLG, '01_layers_png/DLG_BackPaper_A.png', None),
    ('Dialogue', 'DLG_BackPaper_B', ZIP_DLG, '01_layers_png/DLG_BackPaper_B.png', None),
    ('Dialogue', 'DLG_BodyText', ZIP_DLG, '01_layers_png/DLG_BodyText.png', None),
    ('Dialogue', 'DLG_ClickFX', ZIP_DLG, '01_layers_png/DLG_ClickFX.png', None),
]

DLG_TAKE_EVERY = 6       # 288 帧 -> 48 帧
DLG_TARGET_W = 800
DLG_TARGET_H = 320

report = []


def crop_alpha(img):
    bbox = img.split()[-1].getbbox()
    return img.crop(bbox) if bbox else img


def write_png(img, group, name):
    out_dir = os.path.join(OUT_ROOT, group)
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, name + '.png')
    img.save(out_path, 'PNG', optimize=True)
    return out_path


def main():
    cache = {}
    for group, name, zip_path, suffix, mode in JOBS:
        if zip_path not in cache:
            cache[zip_path] = zipfile.ZipFile(zip_path)
        zf = cache[zip_path]
        hit = [n for n in zf.namelist() if n.replace('\\', '/').endswith(suffix)]
        if not hit:
            report.append('!! 缺: {} <- {}'.format(name, suffix))
            continue
        img = Image.open(io.BytesIO(zf.read(hit[0]))).convert('RGBA')
        w0, h0 = img.width, img.height
        if mode == 'crop_alpha':
            img = crop_alpha(img)
        out = write_png(img, group, name)
        report.append('{}/{}  {}x{} -> {}x{}  <= {}'.format(
            group, name, w0, h0, img.width, img.height, hit[0]))

    # 对话框序列：每 6 帧取 1 帧，缩放到 800x320
    zf = cache.get(ZIP_DLG) or zipfile.ZipFile(ZIP_DLG)
    frames = sorted(n for n in zf.namelist()
                    if '/02_待机_循环播放/' in n.replace('\\', '/') and n.lower().endswith('.png'))
    report.append('对话框序列共 {} 帧，取每 {} 帧 1 张 -> {} 帧'.format(
        len(frames), DLG_TAKE_EVERY, len(frames[::DLG_TAKE_EVERY])))
    picked = frames[::DLG_TAKE_EVERY][:48]
    for idx, member in enumerate(picked):
        img = Image.open(io.BytesIO(zf.read(member))).convert('RGBA')
        img = img.resize((DLG_TARGET_W, DLG_TARGET_H), Image.LANCZOS)
        out = write_png(img, os.path.join('Dialogue', 'Seq'), 'DLG_Idle_{:02d}'.format(idx))
        if idx < 2 or idx == len(picked) - 1:
            report.append('  Seq/DLG_Idle_{:02d}  <= {}'.format(idx, member))
    report.append('对话框序列实际写入 {} 帧'.format(len(picked)))

    with open(REPORT, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))
    print('done: {} 条'.format(len(report)))


if __name__ == '__main__':
    main()
