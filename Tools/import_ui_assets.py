# -*- coding: utf-8 -*-
"""导入 UI 素材（从设计稿里抠出来的元件）。

用法:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="<绝对路径>/Tools/import_ui_assets.py" -unattended -nosplash

来源：SourceArt/UI/<子目录>/*.png
目标：/Game/UI/<子目录>/<文件名>
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_ROOT = os.path.join(ROOT, 'SourceArt', 'UI')
DEST_ROOT = '/Game/UI'
REPORT_PATH = os.path.join(ROOT, 'Saved', 'ui_assets_report.txt')

report = []
tasks = []
expected = []
for sub in ('HUD',):
    src_dir = os.path.join(SRC_ROOT, sub)
    if not os.path.isdir(src_dir):
        continue
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith('.png'):
            continue
        asset_name = os.path.splitext(name)[0]
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(src_dir, name))
        task.set_editor_property('destination_path', '{}/{}'.format(DEST_ROOT, sub))
        task.set_editor_property('destination_name', asset_name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        tasks.append(task)
        expected.append('{}/{}/{}'.format(DEST_ROOT, sub, asset_name))

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = 0
for path in expected:
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        report.append('!! 缺: {}'.format(path)); continue
    ok += 1
    report.append('{}  {}x{}'.format(path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
report.append('导入完成：{}/{}'.format(ok, len(expected)))
try:
    with open(REPORT_PATH, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))
except Exception as e:
    unreal.log_error('写报告失败: {}'.format(e))
