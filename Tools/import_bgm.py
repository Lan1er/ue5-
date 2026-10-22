# -*- coding: utf-8 -*-
"""导入 BGM（SourceArt/Audio/BGM -> /Game/Audio/BGM/BGM）。

为什么要 ogg 而不是 wav：
  * 第一次用 wav 导入时抛 `Decoder for AudioFormat 'BINKA' not found`（工程没启用 BinkAudio 插件），
    编辑器默认把 wav 压成 BINKA 就解不出来了 ✗；
  * ogg（Vorbis）是 UE 原生支持、不依赖插件 ✓，而且体积只有 1.66MB（wav 15MB）✓。
两者都已转好放在 SourceArt/Audio/BGM/（源 mp3 也留着）。

导入后：置 **looping=True**（BGM 要全程循环）、保存、打印时长/大小/循环标记。
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, 'SourceArt', 'Audio', 'BGM')
DEST = '/Game/Audio/BGM'
NAME = 'BGM'

report = ['=== BGM 导入 ===']


def find_source():
    for ext in ('.ogg', '.wav'):
        p = os.path.join(SRC_DIR, NAME + ext)
        if os.path.exists(p):
            return p
    return None


src = find_source()
if src is None:
    report.append('!! 找不到源文件（BGM.ogg / BGM.wav）')
else:
    report.append('源文件: {}  {:.2f} MB'.format(os.path.basename(src), os.path.getsize(src) / 1024 / 1024))
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', src)
    task.set_editor_property('destination_path', DEST)
    task.set_editor_property('destination_name', NAME)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    path = '{}/{}'.format(DEST, NAME)
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        report.append('!! 导入失败（load_asset 拿不到 ' + path + '）')
    else:
        report.append('资产: {}  类={}'.format(asset.get_path_name(), asset.get_class().get_name()))
        try:
            asset.set_editor_property('looping', True)
            report.append('looping -> True')
        except Exception as exc:
            report.append('!! 设置 looping 失败: {}'.format(exc))
        try:
            report.append('时长: {:.2f} 秒'.format(float(asset.get_editor_property('duration'))))
        except Exception as exc:
            report.append('时长读取失败: {}'.format(exc))
        try:
            report.append('looping = {}'.format(asset.get_editor_property('looping')))
        except Exception:
            pass
        try:
            report.append('压缩 = {}'.format(asset.get_editor_property('sound_compression_type')))
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
        report.append('已保存')

os.makedirs(os.path.join(ROOT, 'Saved'), exist_ok=True)
open(os.path.join(ROOT, 'Saved', '_bgm_import.txt'), 'w', encoding='utf-8').write('\n'.join(report))
print('ok')
