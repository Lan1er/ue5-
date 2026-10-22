# -*- coding: utf-8 -*-
"""生成妻子的柔光阴影材质（一次性，可重复执行）。

用法（必须用 UE 的 python）:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="Tools/make_wife_assets.py" -unattended -nosplash

产物：/Game/Characters/Wife/M_WifeShadow
  - Unlit + Translucent + 双面
  - 参数 ShadowTexture（Texture2D）：其 alpha 直接接到 Opacity，RGB 输出黑色
  -> 用在脚下一块压扁的 Plane 上，就是"贴地的软阴影"，不会像 2D 角色那样飘着。
"""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAT_DIR = '/Game/Characters/Wife'
MAT_NAME = 'M_WifeShadow'
MAT_PATH = '{}/{}'.format(MAT_DIR, MAT_NAME)
REPORT_PATH = os.path.join(ROOT, 'Saved', 'wife_assets_report.txt')

report = []


def log(msg):
    report.append(str(msg))


lib = unreal.MaterialEditingLibrary

existing = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
if existing is not None:
    log('材质已存在，删除重建：{}'.format(MAT_PATH))
    unreal.EditorAssetLibrary.delete_asset(MAT_PATH)

factory = unreal.MaterialFactoryNew()
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    MAT_NAME, MAT_DIR, unreal.Material, factory)
if mat is None:
    log('!! 创建材质失败')
else:
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)

    tex = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    tex.set_editor_property('parameter_name', 'ShadowTexture')
    # 默认给一个引擎白图，避免未设置参数时是黑的
    default_tex = unreal.EditorAssetLibrary.load_asset('/Engine/EngineResources/WhiteSquareTexture')
    if default_tex is not None:
        tex.set_editor_property('texture', default_tex)

    black = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 120)
    black.set_editor_property('constant', unreal.LinearColor(0.02, 0.02, 0.02, 1.0))

    lib.connect_material_property(black, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(tex, 'A', unreal.MaterialProperty.MP_OPACITY)

    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    log('已生成材质：{}'.format(MAT_PATH))
    log('  blend=Translucent, shading=Unlit, two_sided=True, 参数 ShadowTexture -> Opacity, Emissive=0.02 黑')

with open(REPORT_PATH, 'w', encoding='utf-8') as f:
    f.write('\n'.join(report))
print('\n'.join(report))
