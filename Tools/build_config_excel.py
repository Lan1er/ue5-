# -*- coding: utf-8 -*-
"""生成《数值配置表.xlsx》初始版本。
用法: python Tools/build_config_excel.py
策划后续直接改 ConfigSource/数值配置表.xlsx，改完跑 Tools/export_config.py 重新导出 JSON。
表头用英文字段名（与 UE 侧 USTRUCT 一一对应），中文说明放在 Note/说明列和「说明」sheet。
数值来源：策划 2026-10-06 版《数值(2).xlsx》。
"""
import os
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.utils import get_column_letter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "ConfigSource", "数值配置表.xlsx")

HEADER_FILL = PatternFill("solid", fgColor="4472C4")
HEADER_FONT = Font(bold=True, color="FFFFFF")
TBD_FILL = PatternFill("solid", fgColor="FFF2CC")  # 黄色 = 待定/待调

def style_sheet(ws, widths, tbd_rows=()):
    for col, w in enumerate(widths, start=1):
        ws.column_dimensions[get_column_letter(col)].width = w
    for cell in ws[1]:
        cell.fill = HEADER_FILL
        cell.font = HEADER_FONT
        cell.alignment = Alignment(horizontal="center")
    ws.freeze_panes = "A2"
    for r in tbd_rows:
        for cell in ws[r]:
            cell.fill = TBD_FILL

def kv_sheet(ws, rows):
    # rows: (key, value, note)
    ws.append(["Key", "Value", "Note"])
    for k, v, n in rows:
        ws.append([k, v, n])

wb = Workbook()

# ---------- 说明 ----------
ws = wb.active
ws.title = "说明"
ws.append(["项", "内容"])
for row in [
    ("修改流程", "只改本 Excel → 运行 Tools/export_config.py → 生成 Content/Config/game_config.json → 游戏启动时自动读取。不要手改 JSON。"),
    ("时间格式", "所有时刻写 HH:MM（24小时制），导出时转成「当天分钟数」。一天跨零点不入范围。"),
    ("时间倍率", "GameMinutesPerRealSecond = 1 游戏分钟 / 现实秒。当前值 1.0：一天 06:00–23:00 约 17 现实分钟，超出策划案「单周目 10–15 分钟」，待调。"),
    ("单位约定", "速率「每秒」；数值 0–100 浮点；SizeMultiplier 为体积倍率；腿伤为「每游戏小时」增量。"),
    ("变更 2026-10-06", "按策划《数值(2).xlsx》定稿：怀疑值改为三档（忽略/搜寻/暴露），无风衣时阶段2+怀疑增量×2、视线×1.5；躁郁值改三档体型（1x/2x/4x）；新增「周目重置 -20」（怀疑与躁郁都保留周目记忆，重置不清零）；新增「时段」表（上午2次/下午3次/晚上3次探索机会）。"),
    ("待定项-1", "怀疑值 90–100 暴露=锁定玩家，100 被捉重置周目（沿用原口径，追逐战为 P1）。"),
    ("待定项-2", "物品位移触发怀疑的规则在新版表中未列出，本表已移除；若仍需请策划补回。"),
    ("变更 2026-10-06-2", "程序确认：ScheduleWife/ScheduleFather 为程序依据玩法走读与角色表整理的初稿，策划案中无此内容，已按要求删除；10-07 动线表定稿后重新加入（导出脚本与 USTRUCT 仍支持 Schedule 类 sheet）。"),
    ("变更 2026-10-06-3", "风衣规则修正：删除「风衣失效」设定。披上后持续有效；阶段3（4倍）需风衣或深入藏点，与策划案四阶段表原文一致。"),
    ("变更 2026-10-10", "删除 TrenchCoatBreakStage 字段：「风衣失效」规则已在 10-06-3 删除，该字段与对应校验（必须小于）是历史残留，已从 Excel / 导出脚本 / USTRUCT / JSON 一并清掉。风衣现在只由 TrenchCoatMaxStage（可披阶段）决定。"),
    ("字段命名", "表头英文字段名必须与 UE 侧 USTRUCT 字段名一致，改名需同步改代码。"),
]:
    ws.append(list(row))
style_sheet(ws, [16, 110])

# ---------- Global ----------
ws = wb.create_sheet("Global")
kv_sheet(ws, [
    ("DayStartMinutes",          "06:00", "一天开始时刻（2026-10-06 策划版：上午 6 点开始）"),
    ("NightMinutes",             "23:00", "入夜时刻（2026-10-06 策划版：晚上到 23 点）"),
    ("GameMinutesPerRealSecond", 1.0,   "时间倍率：1 现实秒 = N 游戏分钟【待调：当前一天约 17 分钟】"),
    ("CaughtResetsLoop",       "TRUE",  "被捉必触发周目重置"),
    ("NightEndsLoop",          "TRUE",  "入夜自然结束周目（保留碎片与认知）"),
    ("MaxSuspicion",           100,     "怀疑值上限"),
    ("MaxMood",                100,     "躁郁值上限"),
])
style_sheet(ws, [28, 12, 60], tbd_rows=(3,))

# ---------- Suspicion ----------
ws = wb.create_sheet("Suspicion")
kv_sheet(ws, [
    ("MaxValue",       100,  "达到即被捉，触发周目重置"),
    ("DecayPerSecond", 3,    "藏匿且不被注意时的每秒衰减（策划案 M2: −3/秒；新版数值表未列此项，暂保留）"),
    ("DecayDelay",     2.0,  "最后一次增长后延迟多少秒才开始衰减（防抖动，程序保护项）"),
    ("ThresholdSearch",  40, "0–39 忽略；40–89 搜寻：主动搜索所在区域【2026-10-06 策划版】"),
    ("ThresholdExposed", 90, "90–100 暴露：锁定玩家（追逐战为 P1）【2026-10-06 策划版】"),
    ("CaughtValue",    100,  "被捉线（= MaxValue）"),
    ("LoopResetDelta", -20,  "周目重置一次的怀疑值变化（2026-10-06 新增：保留周目记忆，重置不清零）"),
])
style_sheet(ws, [20, 10, 70])

# ---------- SuspicionGain ----------
ws = wb.create_sheet("SuspicionGain")
ws.append(["TriggerType", "Gain", "Note"])
for row in [
    ("SightGlance", 10, "被视线范围扫到（2026-10-06 策划版；搜寻状态下周围半径内经过即判定，半径待场景定）"),
    ("NoiseLoud",   5,  "发出过大声响（地板缝隙触发，2026-10-06 策划版）"),
    ("LoopReset",  -20, "周目重置一次（负值为修正项）"),
]:
    ws.append(list(row))
style_sheet(ws, [16, 8, 70])

# ---------- Mood ----------
ws = wb.create_sheet("Mood")
kv_sheet(ws, [
    ("MaxValue",                        100, "拉满 → 攻击破坏行为，周目结束"),
    ("StressEventGain",                 20,  "应激：触发家人议论对话（随机或玩家进入固定区域触发）【2026-10-06 策划版】"),
    ("LegPainPerGameHour",              5,   "腿伤：每游戏小时 +5（2026-10-06 策划版）"),
    ("FoodRestore",                     10,  "进食一次降低的躁郁值【2026-10-06 策划版】"),
    ("LoopResetDelta",                  -20, "周目重置一次的躁郁值变化（2026-10-06 新增）"),
    ("TrenchCoatMaxStage",              2,   "风衣：体型阶段 ≤ 此值时可披上；披上后持续有效，不失效"),
    ("Stage2PlusNoCoatSusGainMult",     2.0, "阶段2+ 无风衣时：怀疑值增量倍率【2026-10-06 策划版】"),
    ("Stage2PlusNoCoatSightRangeMult",  1.5, "阶段2+ 无风衣时：家人视线范围倍率【2026-10-06 策划版】"),
])
style_sheet(ws, [32, 10, 70])

# ---------- MoodStages ----------
ws = wb.create_sheet("MoodStages")
ws.append(["Stage", "MinMood", "SizeMultiplier", "Note"])
for row in [
    (1, 0,  1, "虫子：无效果"),
    (2, 40, 2, "2倍虫子：有风衣则无效果；无风衣怀疑增量×2、视线范围×1.5"),
    (3, 90, 4, "4倍虫子：大到无法被忽视，触发追逐战（追逐战为 P1）；需风衣或深入藏点，移动更易被察觉——风衣不失效"),
]:
    ws.append(list(row))
style_sheet(ws, [8, 10, 14, 70])

# ---------- Periods ----------
ws = wb.create_sheet("Periods")
ws.append(["PeriodId", "Start", "End", "ExploreChances", "Note"])
for row in [
    ("Morning",   "06:00", "11:00", 2, "上午：探索机会 2 次"),
    ("Afternoon", "11:00", "17:00", 3, "下午：探索机会 3 次"),
    ("Evening",   "17:00", "23:00", 3, "晚上：探索机会 3 次"),
]:
    ws.append(list(row))
style_sheet(ws, [12, 8, 8, 15, 40])

os.makedirs(os.path.dirname(OUT), exist_ok=True)
wb.save(OUT)
print("saved:", OUT)
