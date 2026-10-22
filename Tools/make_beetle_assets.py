# -*- coding: utf-8 -*-
"""生成虫形主角的贴图与精灵材质（可重复执行）。
用法（无界面）:
  UnrealEditor-Cmd.exe <project>.uproject -run=PythonScript -Script="<本文件>" -unattended -nosplash -nullrhi
材质：Unlit + Masked，贴图 RGB -> 自发光，贴图 A -> Opacity Mask，参数名 SpriteTexture。
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC_DIR = os.path.join(ROOT, "SourceArt", "Character")
DEST_DIR = '/Game/Characters/Beetle'
TEX_DIR = DEST_DIR + '/Textures'
STAGES = ['T_Beetle_Stage1', 'T_Beetle_Stage2', 'T_Beetle_Stage3']


def log(msg):
    # commandlet 里 unreal.log 不输出，所以同时写文件，方便排查
    report.append(str(msg))


REPORT_PATH = os.path.join(ROOT, "Saved", "beetle_assets_report.txt")
report = []


# ---------- 1. 导入贴图 ----------
tasks = []
for name in STAGES:
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', '{}\\{}.png'.format(SRC_DIR, name))
    task.set_editor_property('destination_path', TEX_DIR)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for name in STAGES:
    path = '{}/{}'.format(TEX_DIR, name)
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        log('!! 贴图缺失: {}'.format(path))
        continue
    log('贴图 {} {}x{} no_alpha={}'.format(
        name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
        tex.get_editor_property('compression_no_alpha')))

# ---------- 2. 精灵材质（最小图） ----------
mat_path = '{}/M_BeetleSprite'.format(DEST_DIR)
if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
    unreal.EditorAssetLibrary.delete_asset(mat_path)

mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_BeetleSprite', DEST_DIR, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    raise RuntimeError('创建材质失败')

mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property('two_sided', True)

param = unreal.MaterialEditingLibrary.create_material_expression(
    mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
param.set_editor_property('parameter_name', 'SpriteTexture')
param.set_editor_property('texture', unreal.EditorAssetLibrary.load_asset('{}/{}'.format(TEX_DIR, STAGES[0])))

unreal.MaterialEditingLibrary.connect_material_property(
    param, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(
    param, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)

log('recompile -> {}'.format(unreal.MaterialEditingLibrary.recompile_material(mat)))
unreal.EditorAssetLibrary.save_loaded_asset(mat)
log('材质已保存: {}'.format(mat_path))

# ---------- 3. 回读验证：到底连上了什么 ----------
check = unreal.EditorAssetLibrary.load_asset(mat_path)
log('回读 shading={} blend={} two_sided={}'.format(
    check.get_editor_property('shading_model'),
    check.get_editor_property('blend_mode'),
    check.get_editor_property('two_sided')))

for prop in ['MP_EMISSIVE_COLOR', 'MP_OPACITY_MASK', 'MP_BASE_COLOR']:
    node = unreal.MaterialEditingLibrary.get_material_property_input_node(
        check, getattr(unreal.MaterialProperty, prop))
    if node is None:
        log('  {} -> None'.format(prop))
    else:
        try:
            pname = node.get_editor_property('parameter_name')
        except Exception:
            pname = '-'
        log('  {} -> {} param={}'.format(prop, node.get_class().get_name(), pname))

try:
    with open(REPORT_PATH, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))
except Exception as e:
    unreal.log_error('写报告失败: {}'.format(e))
