# -*- coding: utf-8 -*-
"""导入「UI 交付 2」整理出来的素材（SourceArt/UI/Delivery2/<组> -> /Game/UI/Delivery2/<组>）。

用法:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="<绝对路径>/Tools/import_ui_delivery2.py" -unattended -nosplash

先跑 stage_ui_delivery2.py（普通 python）把图整理进 SourceArt，再跑本脚本。
UI 贴图设置：无 mip、UI 压缩（TC_EDITOR_ICON）、TEXTUREGROUP_UI、sRGB。
"""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_ROOT = os.path.join(ROOT, 'SourceArt', 'UI', 'Delivery2')
DEST_ROOT = '/Game/UI/Delivery2'
REPORT_PATH = os.path.join(ROOT, 'Saved', 'ui_delivery2_import.txt')

GROUPS = ['Stats', 'Buttons', 'Start', 'Settings', 'Failure']

report = []
tasks = []
expected = []

for rel in GROUPS:
    src_dir = os.path.join(SRC_ROOT, rel)
    if not os.path.isdir(src_dir):
        report.append('!! 源目录不存在: {}'.format(src_dir))
        continue
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith('.png'):
            continue
        asset_name = os.path.splitext(name)[0]
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(src_dir, name))
        task.set_editor_property('destination_path', '{}/{}'.format(DEST_ROOT, rel))
        task.set_editor_property('destination_name', asset_name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        tasks.append(task)
        expected.append('{}/{}/{}'.format(DEST_ROOT, rel, asset_name))

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

ok = 0
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
        ok += 1
    except Exception as exc:
        report.append('!! 设置失败 {}: {}'.format(path, exc))
    report.append('{}  {}x{}'.format(path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

report.append('')
report.append('导入完成：{}/{}'.format(ok, len(expected)))

with open(REPORT_PATH, 'w', encoding='utf-8') as f:
    f.write('\n'.join(report))

unreal.log('导入报告写入 {}'.format(REPORT_PATH))
