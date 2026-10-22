# -*- coding: utf-8 -*-
"""在编辑器里把「游戏关卡依赖的资源」烘焙成预加载清单，写进 Config/DefaultGame.ini。

为什么要烘焙：运行时（尤其是打包后）AssetRegistry 是空的，
MetamorphosisPreloadSubsystem 没法在运行时查出关卡依赖了哪些包。
所以在编辑器里查好，写进配置，运行时照着这份清单异步加载即可。

只写 Config/DefaultGame.ini 里被标记包围的那一段，手写的内容不动。

用法：
  UnrealEditor-Cmd.exe <项目>.uproject -run=PythonScript -Script="Tools/bake_preload_list.py" -unattended -nosplash
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAP_PACKAGE = "/Game/mymap/map/Untitled"
INI_PATH = os.path.join(ROOT, "Config", "DefaultGame.ini")
REPORT = os.path.join(ROOT, "Saved", "preload_bake_report.txt")

BEGIN_MARK = "; ==== BEGIN AUTO-BAKED PRELOAD LIST (Tools/bake_preload_list.py) ===="
END_MARK = "; ==== END AUTO-BAKED PRELOAD LIST ===="

lines = []


def log(msg):
    lines.append(str(msg))


asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()

# 场景资源被整包换过（地编发了新工程），缓存里的注册表是旧的 ->
# 先强制同步扫描一遍 /Game，否则 get_dependencies 会返回 None（拿不到新地图）
try:
    asset_registry.scan_paths_synchronous(["/Game"], True)
    log("已强制重新扫描 /Game")
except Exception as e:  # noqa
    log("强制扫描失败（继续尝试）：%s" % e)

deps = []
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=False,
    include_soft_management_references=False,
    include_hard_management_references=False,
)

# 不同版本 Python 暴露的签名略有差别，逐个试
for attempt in range(4):
    try:
        if attempt == 0:
            deps = asset_registry.get_dependencies(unreal.Name(MAP_PACKAGE), options)
        elif attempt == 1:
            deps = asset_registry.get_dependencies(MAP_PACKAGE, options)
        elif attempt == 2:
            deps = asset_registry.get_dependencies(unreal.Name(MAP_PACKAGE))
        else:
            deps = asset_registry.get_dependencies(MAP_PACKAGE)
        log("get_dependencies 第 %d 种写法成功，返回 %d 条" % (attempt + 1, len(deps)))
        break
    except Exception as e:  # noqa
        log("get_dependencies 第 %d 种写法失败：%s" % (attempt + 1, e))

packages = []
for dep in deps:
    name = str(dep)
    if not name.startswith("/Game/"):
        continue
    if name == MAP_PACKAGE:
        continue
    if name not in packages:
        packages.append(name)

packages.sort()
log("关卡 %s 依赖的 /Game 包：%d 个" % (MAP_PACKAGE, len(packages)))

# 统计一下体量
total_bytes = 0
for pkg in packages:
    try:
        path = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    except Exception:
        path = ""
for pkg in packages[:5]:
    log("  例：%s" % pkg)

# 写配置：替换标记之间的内容
block = [BEGIN_MARK]
block.append("; 由 Tools/bake_preload_list.py 自动生成（%d 个包）。要重新生成就再跑一次那个脚本。" % len(packages))
block.append("; 这些是游戏关卡依赖的资源：开始界面会把它们提前异步加载好，进关时不会再有加载卡顿。")
block.append("[/Script/Metamorphosis.MetamorphosisPreloadSubsystem]")
for pkg in packages:
    block.append("+PreloadAssets=%s" % pkg)
block.append(END_MARK)

try:
    with open(INI_PATH, "r", encoding="utf-8") as f:
        content = f.read()
except Exception as e:  # noqa
    content = ""
    log("读配置失败：%s" % e)

if BEGIN_MARK in content and END_MARK in content:
    head = content.split(BEGIN_MARK)[0]
    tail = content.split(END_MARK)[-1]
    content = head + "\n".join(block) + tail
else:
    if not content.endswith("\n"):
        content += "\n"
    content += "\n" + "\n".join(block) + "\n"

try:
    with open(INI_PATH, "w", encoding="utf-8") as f:
        f.write(content)
    log("已写入 %s（%d 行）" % (INI_PATH, len(block)))
except Exception as e:  # noqa
    log("写配置失败：%s" % e)

with open(REPORT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

