# -*- coding: utf-8 -*-
"""把 开始界面设计分层（SourceArt/UI/Delivery2/Start_Design）导入到 /Game/UI/Delivery2/Start_Design。

用法（编辑器关闭状态，命令行导入）：
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/import_start_design.py" -unattended -nosplash
说明：
  · 逐张 ImportAssets，导入后统一改 Texture 属性（无 mip / UI 组 / sRGB / TC_EDITOR_ICON / NeverStream）；
  · 不用 AssetImportTask.options（它是 ObjectProperty，塞字符串会 TypeError）；
  · 已存在的资产会先删掉再导，保证可重复执行。
"""
import unreal
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC_DIR = os.path.join(ROOT, "SourceArt", "UI", "Delivery2", "Start_Design")
DST = "/Game/UI/Delivery2/Start_Design"

FILES = [
    "BG_Room", "Beetle", "MenuShade", "RedScratches", "Logo",
    "Txt_Continue", "Txt_Continue_Hover", "Txt_Start", "Txt_Start_Hover",
    "Txt_Settings", "Txt_Settings_Hover", "Txt_Quit", "Txt_Quit_Hover",
    "HoverStar", "HoverLine", "BG_Composed",
    # 2026-10-19：交付包的 3x 文字图（1x 在按钮里被放大 2.5 倍会发虚）+ 进度条三件套
    "Txt3_Continue", "Txt3_Start", "Txt3_Settings", "Txt3_Quit",
    "BarTrack", "BarFill", "BarFrame",
    "Txt3_NewGame",
]

def log(msg):
    unreal.log("[StartDesign] " + msg)

def import_one(name):
    src = os.path.join(SRC_DIR, name + ".png")
    dst_name = DST + "/" + name
    if not os.path.isfile(src):
        log("跳过（源文件不存在）：" + src)
        return False
    # 已存在则先删
    if unreal.EditorAssetLibrary.does_asset_exist(dst_name):
        unreal.EditorAssetLibrary.delete_asset(dst_name)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", DST)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(dst_name)
    if not tex:
        log("导入失败：" + name)
        return False
    # 统一 Texture 属性
    try:
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("never_stream", True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    except Exception as ex:
        log("改属性失败（%s）：%s" % (name, ex))
    log("导入成功：%s" % name)
    return True

def main():
    log("开始导入，源目录=" + SRC_DIR)
    ok = 0
    for n in FILES:
        if import_one(n):
            ok += 1
    log("完成：%d/%d" % (ok, len(FILES)))

main()
