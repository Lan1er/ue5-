# -*- coding: utf-8 -*-
"""导入甲虫动作序列帧（可重复执行）。

用法（无界面）:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="Tools/import_beetle_sequences.py" -unattended -nosplash

来源：SourceArt/Character/Beetle_Sequences_Ready/<Tag>/<Tag>_01..NN.png
      （由 Tools/prepare_beetle_sequences.py 统一到 384x384 画布、按身体尺寸归一、躯干质心对齐画布中心）
目标：/Game/Characters/Beetle/Sequences/<Tag>/T_Beetle_<Tag>_01..NN
      沿用现有 M_BeetleSprite 的 SpriteTexture 参数，不新建材质
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC_ROOT = os.path.join(ROOT, "SourceArt", "Character", "Beetle_Sequences_Ready")
DEST_ROOT = '/Game/Characters/Beetle/Sequences'
SETS = [('Walk', 9), ('Fly', 9), ('Idle12', 4), ('Idle3', 9)]
REPORT_PATH = os.path.join(ROOT, "Saved", "beetle_sequences_report.txt")

report = []


def log(msg):
    report.append(str(msg))


tasks = []
expected = []
for tag, count in SETS:
    for i in range(1, count + 1):
        src = r'{}\{}\{}_{:02d}.png'.format(SRC_ROOT, tag, tag, i)
        name = 'T_Beetle_{}_{:02d}'.format(tag, i)
        dest = '{}/{}'.format(DEST_ROOT, tag)
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', src)
        task.set_editor_property('destination_path', dest)
        task.set_editor_property('destination_name', name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        tasks.append(task)
        expected.append('{}/{}'.format(dest, name))

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

ok = 0
for path in expected:
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        log('!! 缺贴图: {}'.format(path))
        continue
    ok += 1
    log('{}  {}x{}  sRGB={}'.format(
        path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
        tex.get_editor_property('srgb')))

log('导入完成：{}/{}'.format(ok, len(expected)))

# 把贴图所在目录也保存一次，避免新增资产只存在内存里
for folder in ['{}/{}'.format(DEST_ROOT, tag) for tag, _ in SETS]:
    try:
        unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=False, recursive=True)
    except Exception as e:
        log('保存目录失败 {}: {}'.format(folder, e))

try:
    with open(REPORT_PATH, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))
except Exception as e:
    unreal.log_error('写报告失败: {}'.format(e))

