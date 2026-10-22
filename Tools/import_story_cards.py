# -*- coding: utf-8 -*-
"""导入剧情头像（SourceArt/UI/Dialogue/Portraits -> /Game/UI/Dialogue/Portraits）。

用法（UE Python，不能用普通 python）：
  UnrealEditor-Cmd.exe <uproject> -run=PythonScript -Script="<本文件绝对路径>" -unattended -nosplash
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'SourceArt', 'UI', 'Dialogue', 'Cards')
DEST = '/Game/UI/Dialogue/Cards'
report = ['=== 剧情头像（预合成卡片层）导入 ===']
tasks, expected = [], []
for name in sorted(os.listdir(SRC)):
    if not name.lower().endswith('.png'):
        continue
    asset = os.path.splitext(name)[0]
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', os.path.join(SRC, name))
    t.set_editor_property('destination_path', DEST)
    t.set_editor_property('destination_name', asset)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', True)
    tasks.append(t)
    expected.append(asset)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = 0
for asset in expected:
    path = '{}/{}'.format(DEST, asset)
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        report.append('!! 缺: ' + path)
        continue
    try:
        tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        # 2026-10-21：按 Docs/贴图画不出来的解法.md 的标准参数（TC_EDITOR_ICON 对本项目某些图会画不出来）
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property('srgb', True)
        tex.set_editor_property('never_stream', True)
        tex.set_editor_property('max_texture_size', 2048)
        unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
        ok += 1
    except Exception as exc:
        report.append('!! 设置失败 {}: {}'.format(path, exc))
report.append('导入 {}/{}'.format(ok, len(expected)))
report.append('  资源: ' + ', '.join(expected))
open(os.path.join(ROOT, 'Saved', 'story_cards_import.txt'), 'w', encoding='utf-8').write('\n'.join(report))
print('done')

