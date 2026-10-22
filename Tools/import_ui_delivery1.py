# -*- coding: utf-8 -*-
"""导入「ui交付1」整理出来的素材（SourceArt/UI/<组> -> /Game/UI/<组>）。

用法:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="<绝对路径>/Tools/import_ui_delivery1.py" -unattended -nosplash

UI 贴图设置：无 mip、UI 压缩（TC_EDITOR_ICON）、TEXTUREGROUP_UI、sRGB。
"""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_ROOT = os.path.join(ROOT, 'SourceArt', 'UI')
DEST_ROOT = '/Game/UI'
REPORT_PATH = os.path.join(ROOT, 'Saved', 'ui_delivery1_import.txt')

# (相对 SourceArt/UI 的子目录, 目标子目录)
GROUPS = [
    (os.path.join('Inventory'), 'Inventory'),
    (os.path.join('Backpack'), 'Backpack'),
    (os.path.join('Pause'), 'Pause'),
    (os.path.join('Dialogue'), 'Dialogue'),
    (os.path.join('Dialogue', 'Seq'), 'Dialogue/Seq'),
]

report = []
tasks = []
expected = []

for src_rel, dest_rel in GROUPS:
    src_dir = os.path.join(SRC_ROOT, src_rel)
    if not os.path.isdir(src_dir):
        report.append('!! 源目录不存在: {}'.format(src_dir))
        continue
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith('.png'):
            continue
        asset_name = os.path.splitext(name)[0]
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(src_dir, name))
        task.set_editor_property('destination_path', '{}/{}'.format(DEST_ROOT, dest_rel))
        task.set_editor_property('destination_name', asset_name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        tasks.append(task)
        expected.append('{}/{}/{}'.format(DEST_ROOT, dest_rel, asset_name))

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

# 贴图设置：UI 用无 mip + UI 压缩
for path in expected:
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        report.append('!! 缺: {}'.format(path))
        continue
    try:
        tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property('srgb', True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
    except Exception as exc:
        report.append('!! 设置失败 {}: {}'.format(path, exc))
    report.append('{}  {}x{}'.format(path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

report.append('导入完成：{}/{}'.format(
    len([r for r in report if r.startswith('/Game/')]), len(expected)))

with open(REPORT_PATH, 'w', encoding='utf-8') as f:
    f.write('\n'.join(report))

unreal.log('导入报告写入 {}'.format(REPORT_PATH))
