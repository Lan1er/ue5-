# -*- coding: utf-8 -*-
import os
import unreal, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "SourceArt", "UI", "Icons")
DST = "/Game/UI/Items"      # 与 InventoryComponent::ItemIconFolder 的约定一致
OLD = "/Game/UI/Icons"
tools = unreal.AssetToolsHelpers.get_asset_tools()
ok=0; total=0
for f in sorted(os.listdir(SRC)):
    if not f.lower().endswith(".png"): continue
    total += 1
    name = os.path.splitext(f)[0]
    p = os.path.join(SRC, f)
    if unreal.EditorAssetLibrary.does_asset_exist(DST + "/" + name):
        unreal.EditorAssetLibrary.delete_asset(DST + "/" + name)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", p)
    t.set_editor_property("destination_path", DST)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tools.import_asset_tasks([t])
    tex = unreal.EditorAssetLibrary.load_asset(DST + "/" + name)
    if tex:
        try:
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            tex.set_editor_property("srgb", True)
            unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
        except Exception as e:
            print("prop fail", name, e)
        ok += 1
# 清掉导错位置的旧目录
if unreal.EditorAssetLibrary.does_directory_exist(OLD):
    for a in unreal.EditorAssetLibrary.list_assets(OLD, recursive=True):
        unreal.EditorAssetLibrary.delete_asset(a)
    try: unreal.EditorAssetLibrary.delete_directory(OLD)
    except Exception: pass
print("ICONS_IMPORT %d/%d -> %s" % (ok, total, DST))
