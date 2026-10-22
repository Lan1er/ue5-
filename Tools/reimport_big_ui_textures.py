"""
把所有"可疑"的 UI / 角色贴图按已验证的参数重导一遍。

背景（已验证 ✓）：
  /Game/UI/Settings/BG_Settings 原本画不出来（CompressionSettings=TC_EditorIcon）
  -> 重导成 TC_Default + MaxTextureSize=2048 + sRGB + NoMipmaps + TextureGroup=UI + NeverStream 之后【能画出来了】

本脚本做两件事：
  1) 找出 /Game/UI 与 /Game/Characters 下所有 CompressionSettings == TC_EditorIcon 且宽或高 > 512 的贴图，全部重导；
  2) 显式点名那 6 张"历史上画不出来"的图（暂停页按钮底板 / 拾取提示板身 / 妻子阴影 / 怀疑值填充 / 背包书本 / 设置大图），
     即使它们已经不是 TC_EditorIcon 也强制重新 import 一次（重新生成 mip/格式）。

用法（编辑器关闭时）：
  UnrealEditor-Cmd.exe <工程目录>/Metamorphosis.uproject -run=pythonscript `
      -script="<工程目录>/Tools/reimport_big_ui_textures.py" -unattended -nosplash
"""

import unreal
import os

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT_ROOT, "SourceArt")

# ---- 参数（与已验证的 reimport_bg_settings.py 完全一致）----
def apply_params(a: unreal.Texture2D):
    a.set_editor_property("srgb", True)
    a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    a.set_editor_property("max_texture_size", 2048)
    a.set_editor_property("never_stream", True)
    a.set_editor_property("compression_no_alpha", False)


def size_of(a):
    try:
        return (a.blueprint_get_size_x(), a.blueprint_get_size_y())
    except Exception:
        return (0, 0)


def reimport(asset_path: str, source_png: str, reason: str) -> bool:
    """用源 PNG 重新 import（覆盖），再设参数。返回是否成功。"""
    if not source_png or not os.path.isfile(source_png):
        unreal.log_warning("[Tex] 跳过（找不到源图）：%s <- %s" % (asset_path, source_png))
        return False
    dst = os.path.dirname(asset_path).replace("/Game", "")
    dest_path = "/Game" + dst
    task = unreal.AssetImportTask()
    task.filename = source_png
    task.destination_path = dest_path
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    ok = False
    for p in task.imported_object_paths:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.Texture2D):
            apply_params(a)
            unreal.EditorAssetLibrary.save_asset(p)
            w, h = size_of(a)
            unreal.log("[Tex] 重导 %s（%s）：%dx%d，TC_DEFAULT / 2048 / NoMip / UI / NeverStream" % (p, reason, w, h))
            ok = True
    return ok


def find_source_png(asset_name: str):
    """在 SourceArt 下按文件名找源 PNG（忽略大小写）。"""
    target = asset_name.lower() + ".png"
    for dirpath, _, files in os.walk(ROOT):
        for f in files:
            if f.lower() == target:
                return os.path.join(dirpath, f)
    return None


def main():
    eas = unreal.EditorAssetLibrary
    candidates = {}   # asset_path -> reason

    # ---- 1) 扫描：TC_EditorIcon 且大图 ----
    for folder in ("/Game/UI", "/Game/Characters"):
        for path in eas.list_assets(folder, recursive=True, include_folder=False):
            try:
                a = unreal.load_asset(path)
                if not isinstance(a, unreal.Texture2D):
                    continue
                cs = a.get_editor_property("compression_settings")
                w, h = size_of(a)
                if cs == unreal.TextureCompressionSettings.TC_EDITOR_ICON and max(w, h) > 512:
                    candidates[path] = "TC_EditorIcon 大图 %dx%d" % (w, h)
            except Exception as e:
                unreal.log_warning("[Tex] 读失败 %s：%s" % (path, e))

    # ---- 2) 显式点名那几张"历史上画不出来"的 ----
    named = ["PromptPlate", "BtnPlate", "Pause_BtnPlate", "shadow", "Shadow",
             "suspicion_fill", "SuspicionFill", "Book", "book", "BackPaper"]
    for folder in ("/Game/UI", "/Game/Characters"):
        for path in eas.list_assets(folder, recursive=True, include_folder=False):
            name = path.split("/")[-1]
            if any(k in name for k in named):
                candidates.setdefault(path, "历史画不出来的图（点名）")

    unreal.log("[Tex] 候选 %d 张：" % len(candidates))
    for p, r in sorted(candidates.items()):
        unreal.log("    %s  <- %s" % (p, r))

    # ---- 3) 逐张重导 ----
    done, failed = [], []
    for path, reason in sorted(candidates.items()):
        name = path.split("/")[-1]
        src = find_source_png(name)
        if reimport(path, src, reason):
            done.append(path)
        else:
            failed.append((path, reason))

    unreal.log("==============================================")
    unreal.log("[Tex] 完成：成功 %d 张，失败 %d 张" % (len(done), len(failed)))
    for p in done:
        unreal.log("    OK   %s" % p)
    for p, r in failed:
        unreal.log_warning("    FAIL %s（%s）" % (p, r))
    unreal.log("==============================================")


main()
