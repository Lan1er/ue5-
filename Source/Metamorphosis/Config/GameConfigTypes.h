// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 数值配置表结构定义 —— 字段名必须与 ConfigSource/数值配置表.xlsx 导出的
// Content/Config/game_config.json 中的键一一对应。改任一侧需同步另一侧。
// 数值来源：策划 2026-10-06 版《数值(2).xlsx》（怀疑/躁郁三档制、周目重置 -20、时段探索机会）。

#pragma once

#include "CoreMinimal.h"
#include "GameConfigTypes.generated.h"

// ---------- 全局 / 时间循环 ----------
USTRUCT(BlueprintType)
struct FGlobalConfig
{
	GENERATED_BODY()

	/** 一天开始时刻（当天分钟数，06:00 = 360） */
	UPROPERTY(BlueprintReadOnly) float DayStartMinutes = 360.f;
	/** 入夜时刻（当天分钟数，23:00 = 1380），之后为安全窗口 */
	UPROPERTY(BlueprintReadOnly) float NightMinutes = 1380.f;
	/** 时间倍率：1 现实秒 = N 游戏分钟 */
	UPROPERTY(BlueprintReadOnly) float GameMinutesPerRealSecond = 1.f;
	/** 被捉必触发周目重置 */
	UPROPERTY(BlueprintReadOnly) bool bCaughtResetsLoop = true;
	/** 入夜自然结束周目（保留碎片与认知） */
	UPROPERTY(BlueprintReadOnly) bool bNightEndsLoop = true;
	UPROPERTY(BlueprintReadOnly) float MaxSuspicion = 100.f;
	UPROPERTY(BlueprintReadOnly) float MaxMood = 100.f;
};

// ---------- 怀疑值 ----------
USTRUCT(BlueprintType)
struct FSuspicionGainEntry
{
	GENERATED_BODY()

	/** 触发类型：SightGlance（视线扫到）/ NoiseLoud（大声响）/ LoopReset（周目重置，负值）（对应 Excel SuspicionGain sheet） */
	UPROPERTY(BlueprintReadOnly) FName TriggerType;
	/** 单次增量（负值为修正项） */
	UPROPERTY(BlueprintReadOnly) float Gain = 0.f;
};

USTRUCT(BlueprintType)
struct FSuspicionConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float MaxValue = 100.f;
	/** 藏匿且不被注意时的每秒衰减 */
	UPROPERTY(BlueprintReadOnly) float DecayPerSecond = 3.f;
	/** 最后一次增长后延迟多少秒才开始衰减（防抖动） */
	UPROPERTY(BlueprintReadOnly) float DecayDelay = 2.f;
	/** 40：0–39 忽略；40–89 搜寻（主动搜索所在区域） */
	UPROPERTY(BlueprintReadOnly) float ThresholdSearch = 40.f;
	/** 90：90–100 暴露（锁定玩家；追逐战为 P1） */
	UPROPERTY(BlueprintReadOnly) float ThresholdExposed = 90.f;
	/** 被捉线（达到即触发周目重置） */
	UPROPERTY(BlueprintReadOnly) float CaughtValue = 100.f;
	/** 周目重置一次的怀疑值变化（2026-10-06：保留周目记忆，重置不清零） */
	UPROPERTY(BlueprintReadOnly) float LoopResetDelta = -20.f;

	UPROPERTY(BlueprintReadOnly) TArray<FSuspicionGainEntry> GainEntries;

	/** 查询某触发类型的单次增量，未配置返回 0 */
	float GetGain(FName TriggerType) const
	{
		for (const FSuspicionGainEntry& E : GainEntries)
		{
			if (E.TriggerType == TriggerType)
			{
				return E.Gain;
			}
		}
		return 0.f;
	}

	/** 0 忽略 / 1 搜寻 / 2 暴露 */
	int32 GetAlertLevel(float Value) const
	{
		if (Value >= ThresholdExposed) { return 2; }
		if (Value >= ThresholdSearch)  { return 1; }
		return 0;
	}
};

// ---------- 躁郁值与体型 ----------
USTRUCT(BlueprintType)
struct FBodyStageEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Stage = 1;
	/** 进入该阶段所需的躁郁值下限 */
	UPROPERTY(BlueprintReadOnly) float MinMood = 0.f;
	/** 体积倍率（1x / 2x / 4x） */
	UPROPERTY(BlueprintReadOnly) float SizeMultiplier = 1.f;
	UPROPERTY(BlueprintReadOnly) FString Note;
};

USTRUCT(BlueprintType)
struct FMoodConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float MaxValue = 100.f;
	/** 应激：家人议论对话一次（随机或进入固定区域触发） */
	UPROPERTY(BlueprintReadOnly) float StressEventGain = 20.f;
	/** 腿伤：每游戏小时 +5 */
	UPROPERTY(BlueprintReadOnly) float LegPainPerGameHour = 5.f;
	/** 进食一次降低的躁郁值 */
	UPROPERTY(BlueprintReadOnly) float FoodRestore = 10.f;
	/** 周目重置一次的躁郁值变化（2026-10-06 新增） */
	UPROPERTY(BlueprintReadOnly) float LoopResetDelta = -20.f;
	/** 风衣：体型阶段 <= 此值时可披上。披上后持续有效、不失效
	 *  （2026-10-10：原来还有个 TrenchCoatBreakStage「到达此阶段风衣失效」，
	 *    该规则在 2026-10-06-3 已删除，字段与校验一并清掉了） */
	UPROPERTY(BlueprintReadOnly) int32 TrenchCoatMaxStage = 2;
	/** 阶段2+ 无风衣时：怀疑值增量倍率 */
	UPROPERTY(BlueprintReadOnly) float Stage2PlusNoCoatSusGainMult = 2.f;
	/** 阶段2+ 无风衣时：家人视线范围倍率 */
	UPROPERTY(BlueprintReadOnly) float Stage2PlusNoCoatSightRangeMult = 1.5f;

	UPROPERTY(BlueprintReadOnly) TArray<FBodyStageEntry> Stages;

	/** 由躁郁值查当前体型阶段（配置表需按 Stage 升序） */
	const FBodyStageEntry* GetStage(float MoodValue) const
	{
		const FBodyStageEntry* Result = Stages.Num() > 0 ? &Stages[0] : nullptr;
		for (const FBodyStageEntry& S : Stages)
		{
			if (MoodValue >= S.MinMood)
			{
				Result = &S;
			}
			else
			{
				break;
			}
		}
		return Result;
	}
};

// ---------- 时段（探索机会） ----------
USTRUCT(BlueprintType)
struct FPeriodConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName PeriodId;
	UPROPERTY(BlueprintReadOnly) float StartMinutes = 0.f;
	UPROPERTY(BlueprintReadOnly) float EndMinutes = 0.f;
	/** 该时段的探索机会次数（上午 2 / 下午 3 / 晚上 3） */
	UPROPERTY(BlueprintReadOnly) int32 ExploreChances = 0;
	UPROPERTY(BlueprintReadOnly) FString Note;
};

// ---------- 家人动线 ----------
USTRUCT(BlueprintType)
struct FScheduleEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float StartMinutes = 0.f;
	UPROPERTY(BlueprintReadOnly) float EndMinutes = 0.f;
	/** 区域标签，对应关卡内的房间 Tag（Kitchen / LivingRoom / DiningRoom / Bedroom / House） */
	UPROPERTY(BlueprintReadOnly) FName Location;
	UPROPERTY(BlueprintReadOnly) FName Action;
	/** 视线危险度 0（安全）~ 3（高危） */
	UPROPERTY(BlueprintReadOnly) int32 Danger = 0;
	UPROPERTY(BlueprintReadOnly) FString Note;
};

USTRUCT(BlueprintType)
struct FFamilySchedule
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName FamilyId;
	UPROPERTY(BlueprintReadOnly) TArray<FScheduleEntry> Entries;

	/** 查询某时刻（当天分钟数）所处的动线段，无则返回 nullptr（时间空洞，家人不应处于该状态） */
	const FScheduleEntry* GetEntryAt(float Minutes) const
	{
		for (const FScheduleEntry& E : Entries)
		{
			if (Minutes >= E.StartMinutes && Minutes < E.EndMinutes)
			{
				return &E;
			}
		}
		return nullptr;
	}
};

// ---------- 道具配表 ----------
// 来源：ConfigSource/数值配置表.xlsx 的「道具」sheet（2026-10-13 定稿，共 16 件）。
// 字段名与 JSON 里的键逐字一致，改表请改 Excel 后跑 Tools/export_config.py，不要手改 JSON。
// 说明见 Docs/道具配表.md。
USTRUCT(BlueprintType)
struct FItemConfigEntry
{
	GENERATED_BODY()

	/** 程序内唯一 ID（如 BrownSugar） */
	UPROPERTY(BlueprintReadOnly) FString ItemId;
	/** 显示名（如 红糖） */
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	/** 分类：Tool / Food / Medicine / Story（字符串，表里就这么写的） */
	UPROPERTY(BlueprintReadOnly) FString Category;
	/** 单格最大堆叠数 */
	UPROPERTY(BlueprintReadOnly) int32 MaxStack = 1;
	/** 能否放进快捷栏。false = 只能就地用（红糖/面条/蔬菜） */
	UPROPERTY(BlueprintReadOnly) bool bCarryable = true;
	/** 用完是否消耗（true 才扣数量/清格） */
	UPROPERTY(BlueprintReadOnly) bool bConsumable = false;
	/** 使用后躁郁值变化（负 = 缓解） */
	UPROPERTY(BlueprintReadOnly) float MoodDelta = 0.f;
	/** 使用后怀疑值变化（负 = 降低） */
	UPROPERTY(BlueprintReadOnly) float SuspicionDelta = 0.f;
	/** 永久速度加成（百分比，20 = +20%）。只对可消耗道具生效，见 UseSelectedSlot */
	UPROPERTY(BlueprintReadOnly) float SpeedBonusPercent = 0.f;
	/** 防蜘蛛强度（系统未做，先只存参数） */
	UPROPERTY(BlueprintReadOnly) float SpiderDefense = 0.f;
	/** 卡门延迟秒数（系统未做，先只存参数） */
	UPROPERTY(BlueprintReadOnly) float DoorBlockSeconds = 0.f;
	/** 储存类型：None / Durable / Perishable / Unstorable（储存系统未做，先只存参数） */
	UPROPERTY(BlueprintReadOnly) FString StorageType;
	/** 占位色（6 位十六进制，无 #，如 9AA5B1）；美术图到位前 UI 用它染色 */
	UPROPERTY(BlueprintReadOnly) FString PlaceholderColor;
	/** 外观备注（美术参考，程序不用） */
	UPROPERTY(BlueprintReadOnly) FString AppearanceNote;
	/** 关卡内的出现位置（关卡摆放用） */
	UPROPERTY(BlueprintReadOnly) FString SpawnLocation;
	/** 描述文本（拾取时展示/物品说明） */
	UPROPERTY(BlueprintReadOnly) FString Description;
	/** 剧情道具的"后续剧情节点"（2026-10-18，来自策划对话表 B 组）：
	 *  读完回忆文本 -> 播该节点（人物台词，如 Sam「我肚子疼，妈妈。」）-> 剧情结束**才**拾取。
	 *  留空 = 读完回忆文本直接拾取。 */
	UPROPERTY(BlueprintReadOnly) FString FollowUpStoryNode;
	/** 策划备注（含"程序暂定"标注） */
	UPROPERTY(BlueprintReadOnly) FString Note;
};

// ---------- 任务配表（当前任务 HUD） ----------
// 来源：ConfigSource/数值配置表.xlsx 的「Tasks」sheet（2026-10-14 草案，待策划确认）。
// TriggerItemId 填了就等于「拾取或吃掉该道具即自动完成此任务」——这是"事件推进"的唯一数据来源。
USTRUCT(BlueprintType)
struct FTaskConfigEntry
{
	GENERATED_BODY()

	/** 任务唯一 ID（如 Task02） */
	UPROPERTY(BlueprintReadOnly) FString TaskId;
	/** 顺序，从 1 连续递增；决定推进次序 */
	UPROPERTY(BlueprintReadOnly) int32 Order = 1;
	/** 短标题（HUD 上那一行大字，如「找到能缓解躁郁的东西」） */
	UPROPERTY(BlueprintReadOnly) FString Title;
	/** 补充说明（HUD 上的小字，可为空） */
	UPROPERTY(BlueprintReadOnly) FString Detail;
	/** 触发道具：拾取/吃掉该道具即完成此任务；空 = 无触发（靠剧情或调试推进）。
	 *  2026-10-18：支持**逗号分隔多件**（如 "BrownSugar,Payslip"），含义=**全部拥有**才完成，
	 *  而且**顺序无关**（先拿到哪件都行）——进入该任务时会做一次"回溯判定"，
	 *  已经在背包里的触发道具立即算数（见 UMetamorphosisTaskSubsystem::TryCompleteCurrentByInventory）。 */
	UPROPERTY(BlueprintReadOnly) FString TriggerItemId;
	/** 本任务完成时自动播放的剧情节点（空 = 不播）。用于"任务做完 -> 播一段剧情 -> 进入下一阶段"，
	 *  例如第一章"找到工资条"完成后播第二章转场（Ch2_01）。 */
	UPROPERTY(BlueprintReadOnly) FString OnCompleteStory;
	/** 策划备注 */
	UPROPERTY(BlueprintReadOnly) FString Note;

	/** 拆 TriggerItemId（逗号分隔）-> 道具 Id 列表 */
	TArray<FString> GetTriggerItemIds() const
	{
		TArray<FString> Out;
		if (TriggerItemId.IsEmpty())
		{
			return Out;
		}
		TArray<FString> Parts;
		TriggerItemId.ParseIntoArray(Parts, TEXT(","), true);
		for (FString& P : Parts)
		{
			P.TrimStartAndEndInline();
			if (!P.IsEmpty())
			{
				Out.Add(P);
			}
		}
		return Out;
	}
};

// ---------- 剧情配表（Story） ----------
// 来源：ConfigSource/数值配置表.xlsx 的「Story」sheet（2026-10-16 开场竖切，文案为草稿待策划定稿）。
// **所有剧情文本都在表里**（不硬编码在 C++），策划只改 Excel 再跑 Tools/export_config.py 即可换文案。
// Effect 语法（分号分隔，顺序执行）见 MetamorphosisStorySubsystem 的注释与 Docs/剧情系统与开场.md。
USTRUCT(BlueprintType)
struct FStoryNodeConfig
{
	GENERATED_BODY()

	/** 节点唯一 ID（如 Open01） */
	UPROPERTY(BlueprintReadOnly) FString NodeId;
	/** 章节（如 Opening） */
	UPROPERTY(BlueprintReadOnly) FString Scene;
	/** 说话人（旁白 / 主控 / 妻子 / Kim…）；空 = 无前缀 */
	UPROPERTY(BlueprintReadOnly) FString Speaker;
	/** 正文（可含换行） */
	UPROPERTY(BlueprintReadOnly) FString Text;
	/** 下一节点；空 = 本节点后剧情结束 */
	UPROPERTY(BlueprintReadOnly) FString Next;
	/** 选项 A 的按钮文字；空 = 本节点不是分支 */
	UPROPERTY(BlueprintReadOnly) FString ChoiceA_Text;
	UPROPERTY(BlueprintReadOnly) FString ChoiceA_Next;
	UPROPERTY(BlueprintReadOnly) FString ChoiceB_Text;
	UPROPERTY(BlueprintReadOnly) FString ChoiceB_Next;
	/** 进入/离开本节点时结算的效果，如 "Mood+5;Give:Cereal:1" */
	UPROPERTY(BlueprintReadOnly) FString Effect;
	/** 可选条件，如 "Stage>=2" / "HasItem:Cereal"；不满足则本节点被跳过 */
	UPROPERTY(BlueprintReadOnly) FString Condition;

	// ---------- 玩法门（2026-10-16：分支抉择不再是文字菜单，而是"回到游戏里做一件事"） ----------
	// 流程：节点先按普通文本播（全黑 + 文字），玩家点一下推进 -> 门激活：
	//   黑幕收起、HUD 恢复、任务 HUD 显示 ObjectiveText、玩家自由行动；
	//   系统轮询检测，满足 -> GateNext；超时 -> GateTimeoutNext（例如"没躲起来 -> 被看见 -> 死"）。
	/** 门类型：Spot（进入某区域）/ Region（进入配表显式 AABB）/ Distance（离参照点足够远或足够近）/
	 *  Interact（对被标记物按交互）/ UnderMesh（藏在某物件正下方）/ Timeout（纯等待）；空 = 普通文本节点 */
	UPROPERTY(BlueprintReadOnly) FString GateKind;
	/** Region 门的判定盒：格式 "XMin,XMax,YMin,YMax,ZMin,ZMax"（世界坐标 cm）。
	 *  2026-10-17 新增：用户要求"后退"必须退到某矩形区域的一侧，显式 AABB 最直观、最好调。 */
	UPROPERTY(BlueprintReadOnly) FString GateRegionBox;
	/** 目标标识：Spot/Interact 用场景标签 Meta.Spot:<Id>（也支持按网格资产名或坐标盒兜底）；Distance 留空 = 以门激活时玩家位置为参照 */
	UPROPERTY(BlueprintReadOnly) FString GateTarget;
	/** 参数：Spot=判定外扩 cm；Distance=距离 cm（>0 表示至少这么远，<0 表示靠近到 |N| 以内）；Interact=可交互距离 cm */
	UPROPERTY(BlueprintReadOnly) float GateParam = 0.f;
	/** 限时秒数（0 = 不限时） */
	UPROPERTY(BlueprintReadOnly) float GateTimeLimit = 0.f;
	/** 满足条件后进入的节点 */
	UPROPERTY(BlueprintReadOnly) FString GateNext;
	/** 超时后进入的节点（留空 = 用 Next） */
	UPROPERTY(BlueprintReadOnly) FString GateTimeoutNext;
	/** 门激活期间显示在任务 HUD 上的目标文字（如「躲到床下」）；走配表，可随时改 */
	UPROPERTY(BlueprintReadOnly) FString ObjectiveText;
	/** 满足后还要"连续保持"多少秒才算完成（0 = 一满足就完成）。中途条件失效则计时清零并重新累计。 */
	UPROPERTY(BlueprintReadOnly) float GateHoldSeconds = 0.f;
	/** 保持阶段显示的任务 HUD 文案，支持 {N} = 剩余秒数（只支持这一个占位符）；留空则沿用 ObjectiveText */
	UPROPERTY(BlueprintReadOnly) FString ObjectiveHoldText;

	/** 策划备注 */
	UPROPERTY(BlueprintReadOnly) FString Note;

	bool HasChoice() const { return !ChoiceA_Text.IsEmpty(); }
	bool HasGate() const { return !GateKind.IsEmpty(); }
};

// ---------- 总配置 ----------
USTRUCT(BlueprintType)
struct FGameConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FGlobalConfig Global;
	UPROPERTY(BlueprintReadOnly) FSuspicionConfig Suspicion;
	UPROPERTY(BlueprintReadOnly) FMoodConfig Mood;
	UPROPERTY(BlueprintReadOnly) TArray<FPeriodConfig> Periods;
	UPROPERTY(BlueprintReadOnly) TArray<FFamilySchedule> Schedules;
	UPROPERTY(BlueprintReadOnly) TArray<FItemConfigEntry> Items;
	UPROPERTY(BlueprintReadOnly) TArray<FTaskConfigEntry> Tasks;
	UPROPERTY(BlueprintReadOnly) TArray<FStoryNodeConfig> Story;

	/** 按 NodeId 查剧情节点，查不到返回 nullptr */
	const FStoryNodeConfig* GetStoryNode(FName NodeId) const
	{
		const FString Id = NodeId.ToString();
		for (const FStoryNodeConfig& N : Story)
		{
			if (N.NodeId == Id)
			{
				return &N;
			}
		}
		return nullptr;
	}

	/** 按 ItemId 查道具配表，查不到返回 nullptr（风格对齐 GetSchedule / GetPeriodAt）。
	 *  配置表里的 ItemId 是字符串，这里用 FName 只是为了调用方便（比较忽略大小写）。 */
	const FItemConfigEntry* GetItem(FName ItemId) const
	{
		const FString Id = ItemId.ToString();
		for (const FItemConfigEntry& Item : Items)
		{
			if (Item.ItemId == Id)
			{
				return &Item;
			}
		}
		return nullptr;
	}

	const FFamilySchedule* GetSchedule(FName FamilyId) const
	{
		for (const FFamilySchedule& S : Schedules)
		{
			if (S.FamilyId == FamilyId)
			{
				return &S;
			}
		}
		return nullptr;
	}

	/** 按 TaskId 查任务配表，查不到返回 nullptr */
	const FTaskConfigEntry* GetTask(FName TaskId) const
	{
		const FString Id = TaskId.ToString();
		for (const FTaskConfigEntry& T : Tasks)
		{
			if (T.TaskId == Id)
			{
				return &T;
			}
		}
		return nullptr;
	}

	/** 按 Order 查任务（Order 从 1 开始）。越界返回 nullptr */
	const FTaskConfigEntry* GetTaskByOrder(int32 Order) const
	{
		for (const FTaskConfigEntry& T : Tasks)
		{
			if (T.Order == Order)
			{
				return &T;
			}
		}
		return nullptr;
	}

	/** 查询某时刻（当天分钟数）所属的时段，无则返回 nullptr */
	const FPeriodConfig* GetPeriodAt(float Minutes) const
	{
		for (const FPeriodConfig& P : Periods)
		{
			if (Minutes >= P.StartMinutes && Minutes < P.EndMinutes)
			{
				return &P;
			}
		}
		return nullptr;
	}
};
