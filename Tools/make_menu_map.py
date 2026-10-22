# -*- coding: utf-8 -*-
"""准备开始界面用的关卡 /Game/Maps/MainMenu：
   * 不存在就创建（空关卡）
   * 补上 PlayerStart + 平行光 + 天空光（空关卡没有视口起点和光照，
     虽然菜单背景是一张纯白 UI，但有关卡基础件更稳，也方便以后放 3D 背景/转台）

可重复执行（已存在就只补缺的东西）。

用法：
  UnrealEditor-Cmd.exe <项目>.uproject -run=PythonScript -Script="Tools/make_menu_map.py" -unattended -nosplash
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAP_PATH = "/Game/Maps/MainMenu"
REPORT = os.path.join(ROOT, "Saved", "menu_map_report.txt")

lines = []


def log(msg):
    lines.append(str(msg))


level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor_actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# 1) 关卡本身
if not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    level_subsystem.new_level(MAP_PATH)
    log("已创建关卡 %s" % MAP_PATH)
else:
    log("关卡已存在，直接打开")
    level_subsystem.load_level(MAP_PATH)

# 2) 补缺的基础 Actor
actors = editor_actor_subsystem.get_all_level_actors()
has_player_start = any(isinstance(a, unreal.PlayerStart) for a in actors)
has_light = any(isinstance(a, unreal.DirectionalLight) for a in actors)
has_sky = any(isinstance(a, unreal.SkyLight) for a in actors)

log("现有 Actor 数：%d（PlayerStart=%s 平行光=%s 天空光=%s）" % (
    len(actors), has_player_start, has_light, has_sky))

if not has_player_start:
    editor_actor_subsystem.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 100))
    log("已放置 PlayerStart")

if not has_light:
    editor_actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 400), unreal.Rotator(-45, 0, 0))
    log("已放置平行光")

if not has_sky:
    editor_actor_subsystem.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 400))
    log("已放置天空光")

# 3) 保存
try:
    level_subsystem.save_current_level()
    log("save_current_level 完成")
except Exception as e:  # noqa
    log("保存失败：%s" % e)

log("最终存在：%s" % unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH))

with open(REPORT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
