# -*- coding: utf-8 -*-
"""导入失败页面图层到 /Game/UI/Settlement（UE python，用 UnrealEditor-Cmd -run=PythonScript 跑）。

用法：
  UnrealEditor-Cmd.exe <uproject> -run=PythonScript -Script="<abs path>" -unattended -nosplash
"""
import os
import unreal

SRC_DIR = os.path.join(unreal.Paths.project_dir(), "SourceArt", "UI", "Settlement")
DST_DIR = "/Game/UI/Settlement"

assign = [
    (".png", "T_", "TEXTUREGROUP_UI", True, False, unreal.TextureCompressionSettings.TC_EDITOR_ICON),
]


def import_one(path, dst_dir):
    name = os.path.splitext(os.path.basename(path))[0]
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", dst_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    # 注意：options 是 ObjectProperty（不是字符串），别去 set 它 —— 走默认导入设置，下面再统一改贴图属性。
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return os.path.join(dst_dir, name)


def main():
    if not os.path.isdir(SRC_DIR):
        print("  源目录不存在: %s" % SRC_DIR)
        return
    files = sorted(f for f in os.listdir(SRC_DIR) if f.lower().endswith(".png"))
    print("  待导入 %d 张" % len(files))

    paths = []
    for f in files:
        paths.append((os.path.join(SRC_DIR, f), import_one(os.path.join(SRC_DIR, f), DST_DIR)))

    # 统一贴图设置：UI 组 / sRGB / 无 mip / 编辑器图标压缩（和本工程其它 UI 图一致）
    ok = 0
    for _, asset_path in paths:
        tex = unreal.EditorAssetLibrary.load_asset(asset_path)
        if tex is None:
            continue
        try:
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            tex.set_editor_property("srgb", True)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
            tex.set_editor_property("never_stream", True)
            unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
            ok += 1
        except Exception as ex:
            print("  设置失败 %s: %s" % (asset_path, ex))
    print("  导入完成 %d/%d，贴图设置成功 %d" % (len(paths), len(files), ok))


if __name__ == "__main__":
    main()
