# -*- coding: utf-8 -*-
"""把 /Game/Audio/BGM/BGM 的压缩格式从 BINK_AUDIO 改成 OPUS（并复核 looping）。

背景（UE 5.6 实测）：
  * SoundWave 的压缩属性名是 `sound_asset_compression_type`（不是 sound_compression_type ✗）
    枚举 `SoundAssetCompressionType` = ADPCM / BINK_AUDIO / OPUS / PCM / PLATFORM_SPECIFIC / PROJECT_DEFINED / RAD_AUDIO
    —— **没有 Vorbis**（5.4 起被 Opus 取代）；
  * 默认是 PROJECT_DEFINED -> 工程默认落到 **BINK_AUDIO**，而本工程没启用 BinkMedia 插件，
    运行时 `Decoder for AudioFormat 'BINKA' not found` -> 播不出声 ✗；
  * 改成 **OPUS**：引擎内建解码、手机也支持、体积只有 PCM 的 ~1/10 ✓。
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'Saved', '_bgm_fix.txt')
PATH = '/Game/Audio/BGM/BGM'
lines = []


def step(msg):
    lines.append(str(msg))
    try:
        open(OUT, 'w', encoding='utf-8').write('\n'.join(lines))
    except Exception:
        pass
    print(msg)


step('=== BGM 压缩格式修正（-> OPUS）===')
asset = unreal.EditorAssetLibrary.load_asset(PATH)
step('资产: %s' % asset)
if asset is None:
    step('!! 加载失败')
else:
    enum = unreal.SoundAssetCompressionType
    try:
        step('改前: %s  质量=%s' % (asset.get_editor_property('sound_asset_compression_type'),
                                  asset.get_editor_property('compression_quality')))
    except Exception as exc:
        step('读改前状态失败: %s' % exc)

    try:
        asset.set_editor_property('sound_asset_compression_type', enum.OPUS)
        step('已设为 OPUS')
    except Exception as exc:
        step('!! 设置 OPUS 失败: %s' % exc)
        try:
            asset.set_editor_property('sound_asset_compression_type', enum.PCM)
            step('回退设为 PCM')
        except Exception as exc2:
            step('!! 设置 PCM 也失败: %s' % exc2)

    try:
        asset.set_editor_property('looping', True)
        step('looping -> True')
    except Exception as exc:
        step('!! looping 失败: %s' % exc)

    try:
        unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
        step('已保存')
    except Exception as exc:
        step('!! 保存失败: %s' % exc)

    try:
        step('复核: 压缩=%s  looping=%s  时长=%.2f 秒' % (
            asset.get_editor_property('sound_asset_compression_type'),
            asset.get_editor_property('looping'),
            float(asset.get_editor_property('duration'))))
    except Exception as exc:
        step('复核失败: %s' % exc)

step('=== 结束 ===')
