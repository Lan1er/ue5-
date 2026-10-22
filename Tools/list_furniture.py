# -*- coding: utf-8 -*-
"""列出关卡里家具/大件的坐标与尺寸（厘米），方便调试时把角色瞬移到指定位置。

用法：
  UnrealEditor-Cmd.exe <项目>.uproject -run=PythonScript -Script="Tools/list_furniture.py" -unattended -nosplash
输出：Saved/furniture_report.txt
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAP_PATH = "/Game/mymap/map/Untitled"
OUT = os.path.join(ROOT, "Saved", "furniture_report.txt")

# 名字里含这些关键词的就算"家具/大件"
KEYS = ["sofa", "chair", "table", "bed", "desk", "cabinet", "fridge", "wardrobe",
        "tv", "oven", "door", "stair", "shelf",
        "\u6c99\u53d1", "\u684c", "\u5e8a", "\u6905", "\u67dc", "\u7bb1", "\u95e8"]  # 沙发 桌 床 椅 柜 箱 门

lines = []


def log(msg):
    lines.append(str(msg))


unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = subsystem.get_all_level_actors()

rows = []
for actor in actors:
    name = actor.get_name()
    if not any(key.lower() in name.lower() for key in KEYS):
        continue
    try:
        origin, extent = actor.get_actor_bounds(False)
    except Exception:  # noqa
        continue
    center = origin
    size = extent * 2.0                     # get_actor_bounds 返回的是"中心到角"的向量
    spawn = unreal.Vector(-680.0, -1920.0, 20.0)
    rows.append((name, center, size, (center - spawn).length()))

rows.sort(key=lambda r: r[3])   # 按离出生点距离排序

log("关卡 %s：候选家具/大件 %d 件（坐标与尺寸单位 cm）" % (MAP_PATH, len(rows)))
log("")
log("%-44s %-34s %-30s %s" % ("actor", "center(X,Y,Z)", "size(X,Y,Z)", "距出生点"))
for name, center, size, dist in rows[:40]:
    log("%-44s (%8.0f,%8.0f,%8.0f)   (%6.0f,%6.0f,%6.0f)   %6.0f" % (
        name, center.x, center.y, center.z, size.x, size.y, size.z, dist))

log("")
log("提示：把角色放到某件家具旁边（bbox 外 20~30cm、Z 取地面高度）就能测「贴近物体是否被误隐藏」")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")


