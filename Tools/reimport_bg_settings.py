import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = os.path.join(ROOT, "SourceArt", "UI", "Settings", "BG_Settings.png")
task = unreal.AssetImportTask()
task.filename = p
task.destination_path = "/Game/UI/Settings"
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
for path in task.imported_object_paths:
    a = unreal.load_asset(path)
    if isinstance(a, unreal.Texture2D):
        a.set_editor_property("srgb", True)
        a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        a.set_editor_property("max_texture_size", 2048)
        a.set_editor_property("never_stream", True)
        a.set_editor_property("compression_no_alpha", False)
        unreal.EditorAssetLibrary.save_asset(path)
        print("  reimported:", path, a.blueprint_get_size_x() if hasattr(a, "blueprint_get_size_x") else a)
print("DONE")