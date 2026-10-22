# -*- coding: utf-8 -*-
"""导入妻子精灵图（SourceArt/Characters/Wife -> /Game/Characters/Wife）。

用法（必须用 UE 的 python）:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="Tools/import_wife_sprites.py" -unattended -nosplash
"""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_ROOT = os.path.join(ROOT, 'SourceArt', 'Characters', 'Wife')
DEST_ROOT = '/Game/Characters/Wife'
REPORT_PATH = os.path.join(ROOT, 'Saved', 'wife_sprites_import.txt')

SETS = ['Front', 'Side', 'Back']

report = []


def log(msg):
    report.append(str(msg))


tasks = []
expected = []
for sub in SETS:
    src_dir = os.path.join(SRC_ROOT, sub)
    if not os.path.isdir(src_dir):
        log('!! 缺目录: {}'.format(src_dir))
        continue
    dest = '{}/{}'.format(DEST_ROOT, sub)
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith('.png'):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(src_dir, name))
        task.set_editor_property('destination_path', dest)
        task.set_editor_property('destination_name', os.path.splitext(name)[0])
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        tasks.append(task)
        expected.append('{}/{}'.format(dest, os.path.splitext(name)[0]))

# 阴影贴图放根目录
shadow_src = os.path.join(SRC_ROOT, 'T_Wife_Shadow.png')
if os.path.exists(shadow_src):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', shadow_src)
    task.set_editor_property('destination_path', DEST_ROOT)
    task.set_editor_property('destination_name', 'T_Wife_Shadow')
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    tasks.append(task)
    expected.append('{}/T_Wife_Shadow'.format(DEST_ROOT))

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

ok = 0
for path in expected:
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        log('!! 缺贴图: {}'.format(path))
        continue
    ok += 1

log('导入 {}/{}'.format(ok, len(expected)))
for path in expected:
    log('  ' + path)

# 保存目录，避免新资产只存在内存里
for folder in ['{}/{}'.format(DEST_ROOT, s) for s in SETS] + [DEST_ROOT]:
    try:
        unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=False, recursive=True)
    except Exception as exc:
        log('保存目录失败 {}: {}'.format(folder, exc))

with open(REPORT_PATH, 'w', encoding='utf-8') as f:
    f.write('\n'.join(report))
print('done')
