import unreal, os
def imp(png, folder):
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = folder
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for p in task.imported_object_paths:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.Texture2D):
            a.set_editor_property("srgb", True)
            a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
            a.set_editor_property("never_stream", True)
            unreal.EditorAssetLibrary.save_asset(p)
        print("  imported:", p)
pairs = [
    (r"SourceArt/UI/Settings/BG_Settings.png",  "/Game/UI/Settings"),
    (r"SourceArt/UI/Settings/Fill.png",         "/Game/UI/Settings"),
    (r"SourceArt/UI/Settings/Handle.png",       "/Game/UI/Settings"),
    (r"SourceArt/UI/Settings/SwitchKnob.png",   "/Game/UI/Settings"),
    (r"SourceArt/UI/Settings/BtnPlate.png",     "/Game/UI/Settings"),
    (r"SourceArt/UI/HUD/T_Icon_Backpack.png",   "/Game/UI/HUD"),
]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for rel, folder in pairs:
    p = os.path.join(root, rel.replace("/", os.sep))
    imp(p, folder)
print("DONE")

import os
