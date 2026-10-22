# -*- coding: utf-8 -*-
"""检查 mymap 里静态网格的碰撞设置，判断"相机挡墙"能不能靠碰撞查询抓到。

输出：Saved/collision_report.txt
"""
import os
import unreal
import collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAP_PATH = "/Game/mymap/map/Untitled"
OUT = os.path.join(ROOT, "Saved", "collision_report.txt")

lines = []


def log(msg):
    lines.append(str(msg))


try:
    unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    log("map loaded: %s" % MAP_PATH)
except Exception as e:  # noqa
    log("load_map failed: %s" % e)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()
log("actors: %d" % len(actors))

profile_counter = collections.Counter()
profiles_examples = {}
no_collision = []
total_smc = 0

for actor in actors:
    try:
        comps = actor.get_components_by_class(unreal.StaticMeshComponent)
    except Exception:
        comps = []
    for smc in comps:
        total_smc += 1
        try:
            profile = smc.get_editor_property("collision_profile_name")
        except Exception:
            profile = "?"
        profile_counter[str(profile)] += 1
        profiles_examples.setdefault(str(profile), "%s / %s" % (actor.get_name(), smc.get_name()))
        try:
            if smc.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                no_collision.append("%s / %s" % (actor.get_name(), smc.get_name()))
        except Exception:
            pass

log("")
log("=== StaticMeshComponent 数量：%d ===" % total_smc)
log("")
log("=== 按 CollisionProfile 分组 ===")
for name, cnt in profile_counter.most_common():
    log("%-32s %5d   e.g. %s" % (name, cnt, profiles_examples.get(name, "")))

log("")
log("=== 完全没有碰撞的构件（%d 个，最多列 40）===" % len(no_collision))
for item in no_collision[:40]:
    log("  " + item)

# 抽查几个"看起来像墙"的构件，看看它对 Camera / Visibility 通道的响应
log("")
log("=== 抽查名字里像墙/门的构件对通道的响应 ===")
keys = ("wall", "Wall", "WALL", "墙", "door", "Door", "floor", "Floor", "floor_", "ceiling")
checked = 0
for actor in actors:
    name = actor.get_name()
    if not any(k in name for k in keys):
        continue
    try:
        comps = actor.get_components_by_class(unreal.StaticMeshComponent)
    except Exception:
        comps = []
    for smc in comps:
        if checked >= 25:
            break
        checked += 1
        try:
            resp = smc.get_editor_property("body_instance")  # 不是我们要的，跳过
        except Exception:
            resp = None
        try:
            profile = str(smc.get_editor_property("collision_profile_name"))
        except Exception:
            profile = "?"
        try:
            enabled = str(smc.get_collision_enabled())
        except Exception:
            enabled = "?"
        log("  %-40s profile=%-28s enabled=%s" % ("%s/%s" % (name, smc.get_name()), profile, enabled))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

