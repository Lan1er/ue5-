// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Stats/MetamorphosisStatsSubsystem.h"

#include "Config/MetamorphosisConfigSubsystem.h"
#include "Config/GameConfigTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "Story/MetamorphosisStorySubsystem.h"

namespace
{
	constexpr float MinBroadcastDelta = 0.01f;
}

// ---------------------------------------------------------------------------
// USubsystem / FTickableGameObject
// ---------------------------------------------------------------------------

void UMetamorphosisStatsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 配置表必须先载入，否则第一帧会摸到 USTRUCT 的默认值（Mood.Stages 默认是空的）
	Collection.InitializeDependency<UMetamorphosisConfigSubsystem>();

	bDayInitialized = false;
	bLoggedCaught = false;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Stats] 数值系统就绪。调试命令：Meta.Suspicion.Add / Meta.Suspicion.Set / Meta.Mood.Add / Meta.Mood.Set / Meta.Stress / Meta.Eat / Meta.LoopReset / Meta.Clock.Pause / Meta.Clock.Resume / Meta.Stats.Print"));
}

void UMetamorphosisStatsSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool UMetamorphosisStatsSubsystem::IsTickable() const
{
	return !IsTemplate() && GetGameInstance() != nullptr;
}

ETickableTickType UMetamorphosisStatsSubsystem::GetTickableTickType() const
{
	// CDO 永远不 tick；其余情况交给 IsTickable 判断
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

TStatId UMetamorphosisStatsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetamorphosisStatsSubsystem, STATGROUP_Tickables);
}

void UMetamorphosisStatsSubsystem::Tick(float DeltaTime)
{
	// 暂停（设置/暂停页）时**时间必须停**：GameInstance 子系统默认仍在 tick 列表里，
	// 不挡的话游戏时钟会继续走、怀疑/躁郁也继续算 -> 玩家暂停时会被抓（实测 bug）。
	if (UGameplayStatics::IsGamePaused(this))
	{
		return;
	}

	const FGameConfig& Config = Cfg();

	if (!bDayInitialized)
	{
		DayMinutes = Config.Global.DayStartMinutes;
		bDayInitialized = true;

		// 开局数值：等配置表真的载入之后（第一帧）才应用，否则会摸到 USTRUCT 默认值。
		// 默认躁郁 50 -> 按阈值表（阶段2 起点 40、阶段3 起点 90）正好落在二形态。
		if (!bInitialValuesApplied)
		{
			bInitialValuesApplied = true;
			SetMoodInternal(InitialMood);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 开局数值：躁郁 %.1f / 上限 %.1f -> 体型阶段 %d（体积倍率 %.2f）"),
				Mood, GetMaxMood(), GetBodyStage(), GetBodySizeMultiplier());
			BroadcastIfChanged();
		}

		// 这一行用来确认「Excel -> JSON -> 代码」整条链路真的生效了
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Stats] 配置生效：一天 %.0f→%.0f 分钟（倍率 %.2f），怀疑阈值 %.0f 搜寻 / %.0f 暴露 / %.0f 被捉，躁郁 %d 档，腿伤 %.1f /游戏小时，进食 -%.1f，周目重置 怀疑 %.0f / 躁郁 %.0f"),
			Config.Global.DayStartMinutes, Config.Global.NightMinutes, Config.Global.GameMinutesPerRealSecond,
			Config.Suspicion.ThresholdSearch, Config.Suspicion.ThresholdExposed, Config.Suspicion.CaughtValue,
			Config.Mood.Stages.Num(),
			Config.Mood.LegPainPerGameHour, Config.Mood.FoodRestore,
			Config.Suspicion.LoopResetDelta, Config.Mood.LoopResetDelta);
	}

	// ---- 夜晚跳过：23:00 触发一段剧情，剧情播完直接跳到 06:00（夜晚整段跳过）----
	if (NightSkipPhase != ENightSkipPhase::Idle)
	{
		// 跳过进行中：时间由状态机接管（停住 / 跳），不要在这里再加分钟
		UpdateNightSkip(DeltaTime);
	}
	else
	{
		// ---- 时间推进：这里只是个时间源，不做入夜结算/周目流程 ----
		if (bClockRunning && DayMinutes < Config.Global.NightMinutes)
		{
			const float Step = FMath::Max(0.f, DeltaTime) * FMath::Max(0.f, Config.Global.GameMinutesPerRealSecond);
			const float Before = DayMinutes;
			DayMinutes = FMath::Min(DayMinutes + Step, Config.Global.NightMinutes);

			// 腿伤：持续、稳定地推高躁郁值（每游戏小时 +LegPainPerGameHour）
			const float GameHours = (DayMinutes - Before) / 60.f;
			if (GameHours > 0.f && Config.Mood.LegPainPerGameHour != 0.f)
			{
				SetMoodInternal(Mood + Config.Mood.LegPainPerGameHour * GameHours);
			}
		}

		// 到 23:00（NightMinutes）-> 启动夜晚跳过
		TryBeginNightSkip();
	}

	// ---- 时段切换（配表 Periods）：跨时段就把探索次数重置成新时段配额 ----
	RefreshPeriod();

	// ---- 怀疑值衰减：不被注意时缓慢下降，增长后先等 DecayDelay 秒（防抖动） ----
	if (Suspicion > 0.f)
	{
		TimeSinceSuspicionGain += DeltaTime;
		if (TimeSinceSuspicionGain >= Config.Suspicion.DecayDelay && Config.Suspicion.DecayPerSecond > 0.f)
		{
			SetSuspicionInternal(Suspicion - Config.Suspicion.DecayPerSecond * DeltaTime);
		}
	}

	BroadcastIfChanged();
}

// ---------------------------------------------------------------------------
// 取数
// ---------------------------------------------------------------------------

UMetamorphosisStatsSubsystem* UMetamorphosisStatsSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr || GEngine == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		return nullptr;
	}

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		return GameInstance->GetSubsystem<UMetamorphosisStatsSubsystem>();
	}
	return nullptr;
}

UMetamorphosisConfigSubsystem* UMetamorphosisStatsSubsystem::GetConfigSubsystem() const
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>();
	}
	return nullptr;
}

const FGameConfig& UMetamorphosisStatsSubsystem::Cfg() const
{
	// 不缓存：配置子系统支持 Reload()，而且初始化顺序不保证
	if (const UMetamorphosisConfigSubsystem* ConfigSubsystem = GetConfigSubsystem())
	{
		return ConfigSubsystem->GetConfig();
	}

	static const FGameConfig Fallback;
	return Fallback;
}

float UMetamorphosisStatsSubsystem::GetMaxSuspicion() const
{
	const float Max = Cfg().Suspicion.MaxValue;
	return Max > 0.f ? Max : 100.f;
}

float UMetamorphosisStatsSubsystem::GetMaxMood() const
{
	const float Max = Cfg().Mood.MaxValue;
	return Max > 0.f ? Max : 100.f;
}

float UMetamorphosisStatsSubsystem::GetSuspicionRatio() const
{
	return FMath::Clamp(Suspicion / GetMaxSuspicion(), 0.f, 1.f);
}

float UMetamorphosisStatsSubsystem::GetMoodRatio() const
{
	return FMath::Clamp(Mood / GetMaxMood(), 0.f, 1.f);
}

int32 UMetamorphosisStatsSubsystem::GetAlertLevel() const
{
	return Cfg().Suspicion.GetAlertLevel(Suspicion);
}

int32 UMetamorphosisStatsSubsystem::GetBodyStage() const
{
	const FBodyStageEntry* Stage = Cfg().Mood.GetStage(Mood);
	return Stage != nullptr ? Stage->Stage : 1;
}

float UMetamorphosisStatsSubsystem::GetBodySizeMultiplier() const
{
	const FBodyStageEntry* Stage = Cfg().Mood.GetStage(Mood);
	return Stage != nullptr ? Stage->SizeMultiplier : 1.f;
}

float UMetamorphosisStatsSubsystem::GetStageSizeMultiplier(int32 Stage) const
{
	for (const FBodyStageEntry& Entry : Cfg().Mood.Stages)
	{
		if (Entry.Stage == Stage)
		{
			return Entry.SizeMultiplier;
		}
	}
	return 1.f;
}

float UMetamorphosisStatsSubsystem::GetSpeedMultiplier() const
{
	// 下限保底：配置表要是填了 -100 这种，角色会被钉死在地上，这里夹到 0.1 倍
	return FMath::Clamp(1.f + SpeedBonusPercent / 100.f, 0.1f, 10.f);
}

bool UMetamorphosisStatsSubsystem::IsCaught() const
{
	return Suspicion >= Cfg().Suspicion.CaughtValue;
}

// ---------------------------------------------------------------------------
// 时段 / 探索机会（2026-10-19）
// ---------------------------------------------------------------------------

const FPeriodConfig* UMetamorphosisStatsSubsystem::GetCurrentPeriod() const
{
	return Cfg().GetPeriodAt(DayMinutes);
}

FText UMetamorphosisStatsSubsystem::GetCurrentPeriodDisplayName() const
{
	const FPeriodConfig* Period = GetCurrentPeriod();
	if (Period == nullptr)
	{
		return FText::FromString(TEXT("深夜"));
	}

	// 配表 PeriodId 是英文（Morning/Afternoon/Evening），这里给玩家看的中文名。
	// ⚠️ 只是**显示用**，不改配表（策划改 Id 也不影响逻辑，逻辑认 Id）。
	if (Period->PeriodId == FName(TEXT("Morning")))   { return FText::FromString(TEXT("早晨")); }
	if (Period->PeriodId == FName(TEXT("Afternoon"))) { return FText::FromString(TEXT("下午")); }
	if (Period->PeriodId == FName(TEXT("Evening")))   { return FText::FromString(TEXT("傍晚")); }
	return FText::FromName(Period->PeriodId);
}

bool UMetamorphosisStatsSubsystem::RefreshPeriod()
{
	const FGameConfig& Config = Cfg();
	const FPeriodConfig* Period = Config.GetPeriodAt(DayMinutes);

	const FName NewId = (Period != nullptr) ? Period->PeriodId : NAME_None;
	const int32 NewMax = (Period != nullptr) ? FMath::Max(0, Period->ExploreChances) : 0;

	if (NewId == CurrentPeriodId)
	{
		// 同一时段：只把配额夹一下（配表热改时也能兜住），不动剩余次数
		ExploreChancesMax = NewMax;
		ExploreChancesRemaining = FMath::Clamp(ExploreChancesRemaining, 0, ExploreChancesMax);
		return false;
	}

	// 跨时段（含开局第一帧 NAME_None -> Morning）：配额重置
	CurrentPeriodId = NewId;
	ExploreChancesMax = NewMax;
	ExploreChancesRemaining = NewMax;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Stats] 时段 -> %s（%s，%.0f~%.0f 分钟）：探索机会重置为 %d 次"),
		*GetCurrentPeriodDisplayName().ToString(), *CurrentPeriodId.ToString(),
		Period != nullptr ? Period->StartMinutes : -1.f, Period != nullptr ? Period->EndMinutes : -1.f,
		ExploreChancesRemaining);

	return true;
}

bool UMetamorphosisStatsSubsystem::ConsumeExploreChance()
{
	// 兜底：外部直接改了 DayMinutes 却没走 Tick 时，先保证时段是最新的
	RefreshPeriod();

	if (ExploreChancesRemaining <= 0)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Stats] 探索机会已用完（%s，%d/%d），拒绝交互"),
			*GetCurrentPeriodDisplayName().ToString(), ExploreChancesRemaining, ExploreChancesMax);
		return false;
	}

	--ExploreChancesRemaining;
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Stats] 消耗 1 次探索机会（%s）：剩余 %d/%d"),
		*GetCurrentPeriodDisplayName().ToString(), ExploreChancesRemaining, ExploreChancesMax);
	return true;
}

void UMetamorphosisStatsSubsystem::SetExploreChancesRemaining(int32 Value)
{
	// 读档专用：**不重算**配额，直接照存档值恢复（防止读档把次数刷满）
	if (ExploreChancesMax <= 0)
	{
		RefreshPeriod();
	}
	ExploreChancesRemaining = FMath::Clamp(Value, 0, FMath::Max(0, ExploreChancesMax));
	UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 读档恢复探索机会：%d/%d（时段 %s）"),
		ExploreChancesRemaining, ExploreChancesMax, *CurrentPeriodId.ToString());
}

void UMetamorphosisStatsSubsystem::ResetExploreChances()
{
	// 强制按当前时段重新拉满（新周目 / 新游戏）
	CurrentPeriodId = NAME_None;
	RefreshPeriod();
}

void UMetamorphosisStatsSubsystem::SetDayMinutes(float Value)
{
	DayMinutes = FMath::Clamp(Value, 0.f, 24.f * 60.f);

	// 新的一天（时间被拉回到深夜之前）-> 夜晚跳过重新可用（周目重置 / 新游戏 / 读档都会走到这里）
	// 同时把状态机复位：读档/重置之后不应该继续沿用上一段跳过的状态 ✗
	//（若读到的档恰好停在 23:00 且剧情节点是 NightSkip01，TryBeginNightSkip 会重新"接管"它 ✓）
	if (DayMinutes < Cfg().Global.NightMinutes)
	{
		bNightSkipDoneThisDay = false;
		NightSkipPhase = ENightSkipPhase::Idle;
	}

	// 跨时段则重置配额；同段内保持（读档随后会用 SetExploreChancesRemaining 覆盖成存档值）
	RefreshPeriod();
}

// ---------------------------------------------------------------------------
// 夜晚跳过（23:00 触发一段剧情 -> 播完跳到 06:00）
// ---------------------------------------------------------------------------

void UMetamorphosisStatsSubsystem::TryBeginNightSkip()
{
	const FGameConfig& Config = Cfg();

	// 关闭/已经跳过了/时间没到 -> 什么都不做
	if (bNightSkipDoneThisDay || NightSkipStoryNode.IsNone())
	{
		return;
	}
	if (!bClockRunning || DayMinutes < Config.Global.NightMinutes)
	{
		return;
	}

	UMetamorphosisStorySubsystem* Story = GetStorySubsystem();
	if (Story == nullptr)
	{
		return;
	}

	// 已经有剧情在播：
	//   * 若播的正好是夜晚跳过那个节点（典型场景：读了一份"23:00 + 节点 NightSkip01"的档，
	//     StartStoryRestored 把它当普通剧情恢复了 ✗）-> 直接"接管"成跳过状态 ✓，
	//     这样它播完就会跳时间和"期间不许存盘"都会生效 ✓；
	//   * 若是别的剧情/玩法门 -> 不能启动，等它播完下一帧再试（时间停在 23:00）。
	if (Story->IsPlaying())
	{
		if (Story->GetCurrentNodeId() == NightSkipStoryNode)
		{
			NightSkipPhase = ENightSkipPhase::PlayingStory;
			DayMinutes = Config.Global.NightMinutes;
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Stats] 夜晚跳过：接管了正在播放的跳过剧情 %s（读档恢复的），播完即跳到 %.0f 分钟。"),
				*NightSkipStoryNode.ToString(), Config.Global.DayStartMinutes);
		}
		return;
	}

	if (!Story->StartStory(NightSkipStoryNode))
	{
		// 配表里没有这个节点（或者被拒）—— 打印一次 Warning 并放弃本次跳过，避免每帧刷屏
		bNightSkipDoneThisDay = true;
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Stats] 23:00 触发夜晚跳过失败：剧情节点 '%s' 播不了（检查配表 Story）。本日跳过取消。"),
			*NightSkipStoryNode.ToString());
		return;
	}

	NightSkipPhase = ENightSkipPhase::PlayingStory;
	DayMinutes = Config.Global.NightMinutes;   // 卡在 23:00，不再前进

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Stats] 23:00 -> 触发夜晚跳过：播放剧情 %s；跳过期间时间停住（%.0f 分钟）。"),
		*NightSkipStoryNode.ToString(), DayMinutes);
}

void UMetamorphosisStatsSubsystem::UpdateNightSkip(float DeltaTime)
{
	const FGameConfig& Config = Cfg();

	// 跳过期间时间必须停住（剧情里玩家可能还在走动）
	DayMinutes = Config.Global.NightMinutes;

	UMetamorphosisStorySubsystem* Story = GetStorySubsystem();
	if (Story != nullptr && Story->IsPlaying())
	{
		return;   // 剧情还在播，等它结束
	}

	// 剧情播完（或被显式停止）-> 跳到 06:00
	NightSkipPhase = ENightSkipPhase::Idle;
	bNightSkipDoneThisDay = true;

	const float Before = DayMinutes;
	DayMinutes = Config.Global.DayStartMinutes;   // 06:00
	CurrentPeriodId = NAME_None;                  // 强制 RefreshPeriod 重算（时段回 Morning、探索机会按配额拉满）
	RefreshPeriod();

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Stats] 夜晚跳过结束：时间 %.0f -> %.0f 分钟（%s），时段 %s，探索机会 %d/%d。"),
		Before, DayMinutes, *GetClockText().ToString(), *GetCurrentPeriodDisplayName().ToString(),
		ExploreChancesRemaining, ExploreChancesMax);

	// 用户要求（2026-10-19，策划案 [039]）：夜晚跳过结束 = 进入第二天 -> 周目数 +1，并按"进第二天"的完整规则处理
	// （保留记忆碎片、清其它道具、怀疑/躁郁各 -20、回出生点、播日开端剧情）。
	// ⚠️ 周目计数由调用方 +1（见 IncrementLoopCountForNewDay），BeginNextDay 自己不再加 ✗。
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMetamorphosisLoopSubsystem* Loop = GameInstance->GetSubsystem<UMetamorphosisLoopSubsystem>())
		{
			Loop->IncrementLoopCountForNewDay(TEXT("夜晚跳过 23:00 -> 06:00"));
			Loop->BeginNextDay(TEXT("夜晚跳过 23:00 -> 06:00"));
		}
	}
}

UMetamorphosisStorySubsystem* UMetamorphosisStatsSubsystem::GetStorySubsystem() const
{
	// ⚠️ 本项目 Stats/Story/Loop 都是 **GameInstance 子系统** -> 必须走 GetGameInstance()->GetSubsystem<>()
	const UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr)
	{
		return nullptr;
	}
	return GameInstance->GetSubsystem<UMetamorphosisStorySubsystem>();
}


FText UMetamorphosisStatsSubsystem::GetClockText() const
{
	const int32 TotalMinutes = FMath::Clamp(FMath::FloorToInt(DayMinutes), 0, 24 * 60 - 1);
	const int32 Hours = TotalMinutes / 60;
	const int32 Minutes = TotalMinutes % 60;
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Hours, Minutes));
}

// ---------------------------------------------------------------------------
// 修改
// ---------------------------------------------------------------------------

void UMetamorphosisStatsSubsystem::SetSuspicionInternal(float Value)
{
	Suspicion = FMath::Clamp(Value, 0.f, GetMaxSuspicion());

	if (!bLoggedCaught && IsCaught())
	{
		bLoggedCaught = true;
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Stats] 怀疑值到达被捉线（%s）。周目结算尚未接入，只做提示。"), *FString::SanitizeFloat(Cfg().Suspicion.CaughtValue));
	}
}

void UMetamorphosisStatsSubsystem::SetMoodInternal(float Value)
{
	Mood = FMath::Clamp(Value, 0.f, GetMaxMood());
}

void UMetamorphosisStatsSubsystem::AddSuspicion(float Amount)
{
	if (Amount == 0.f)
	{
		return;
	}

	SetSuspicionInternal(Suspicion + Amount);

	if (Amount > 0.f)
	{
		// 刚被察觉：重置衰减延迟计时
		TimeSinceSuspicionGain = 0.f;
	}

	BroadcastIfChanged();
}

void UMetamorphosisStatsSubsystem::AddSuspicionByTrigger(FName TriggerType)
{
	AddSuspicion(Cfg().Suspicion.GetGain(TriggerType));
}

void UMetamorphosisStatsSubsystem::SetSuspicion(float Value)
{
	SetSuspicionInternal(Value);
	BroadcastIfChanged();
}

void UMetamorphosisStatsSubsystem::AddMood(float Amount)
{
	if (Amount == 0.f)
	{
		return;
	}

	SetMoodInternal(Mood + Amount);
	BroadcastIfChanged();
}

void UMetamorphosisStatsSubsystem::SetMood(float Value)
{
	SetMoodInternal(Value);
	BroadcastIfChanged();
}

void UMetamorphosisStatsSubsystem::AddSpeedBonusPercent(float Percent)
{
	if (FMath::IsNearlyZero(Percent))
	{
		return;
	}

	SpeedBonusPercent = FMath::Clamp(SpeedBonusPercent + Percent, -90.f, 900.f);

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Stats] 速度加成 %+.0f%% -> 累计 %+.0f%%（倍率 %.2f，角色下一帧同步 MaxWalkSpeed/MaxFlySpeed）"),
		Percent, SpeedBonusPercent, GetSpeedMultiplier());
}

void UMetamorphosisStatsSubsystem::ApplyStressEvent()
{
	AddMood(Cfg().Mood.StressEventGain);
}

void UMetamorphosisStatsSubsystem::EatFood()
{
	AddMood(-Cfg().Mood.FoodRestore);
}

void UMetamorphosisStatsSubsystem::ResetForNewLoop()
{
	const FGameConfig& Config = Cfg();

	// 策划 2026-10-06 规则：周目重置不清零，怀疑与躁郁都保留周目记忆
	SetSuspicionInternal(Suspicion + Config.Suspicion.LoopResetDelta);
	SetMoodInternal(Mood + Config.Mood.LoopResetDelta);
	DayMinutes = Config.Global.DayStartMinutes;
	TimeSinceSuspicionGain = 0.f;
	bLoggedCaught = false;

	// 新周目：时段回到早晨，探索次数按第一个时段的配额拉满（2026-10-19 时间/次数系统）
	ResetExploreChances();

	// 速度加成（道具给的"永久"加成）在周目重置里**清空**（2026-10-16 用户要求"每次新周目全部从头开始"）。
	// 历史：之前刻意保留（理由：道具原文"永久获得"、且加成跨关卡保留）。
	// 现在改为清空；角色下一帧 SyncSpeedBonus 会把 MaxWalkSpeed / MaxFlySpeed 恢复成基础值。
	// 若策划以后想保留永久加成：把下面这行删掉即可（只改这一处）。
	const float SpeedBonusBefore = SpeedBonusPercent;
	SpeedBonusPercent = 0.f;

	UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 周目重置：怀疑 %s / 躁郁 %s（%s / %s）；速度加成 %+.0f%% -> 0%%（新周目全部从头）"),
		*FString::SanitizeFloat(Suspicion), *FString::SanitizeFloat(Mood),
		*FString::SanitizeFloat(Config.Suspicion.LoopResetDelta), *FString::SanitizeFloat(Config.Mood.LoopResetDelta),
		SpeedBonusBefore);

	BroadcastIfChanged();
}

void UMetamorphosisStatsSubsystem::BroadcastIfChanged()
{
	const int32 Level = GetAlertLevel();
	const int32 Stage = GetBodyStage();

	const bool bChanged =
		!FMath::IsNearlyEqual(Suspicion, LastBroadcastSuspicion, MinBroadcastDelta) ||
		!FMath::IsNearlyEqual(Mood, LastBroadcastMood, MinBroadcastDelta) ||
		Level != LastBroadcastLevel ||
		Stage != LastBroadcastStage;

	if (!bChanged)
	{
		return;
	}

	LastBroadcastSuspicion = Suspicion;
	LastBroadcastMood = Mood;

	// 档位/阶段变化单独打一行日志：后续追逐战、家人非人化、体型换模型都挂在这些边界上，方便调
	if (Level != LastBroadcastLevel)
	{
		LastBroadcastLevel = Level;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 怀疑档位变化 -> %d（怀疑 %.1f）"), Level, Suspicion);
	}

	if (Stage != LastBroadcastStage)
	{
		LastBroadcastStage = Stage;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 体型阶段变化 -> %d（躁郁 %.1f，倍率 %.2f）"), Stage, Mood, GetBodySizeMultiplier());
		OnBodyStageChanged.Broadcast(Stage);
	}

	OnStatsChanged.Broadcast(Suspicion, Mood, Level, Stage);
}

// ---------------------------------------------------------------------------
// 调试命令（只在非 Shipping 构建里存在）
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
namespace MetamorphosisStatsConsole
{
	static UMetamorphosisStatsSubsystem* Resolve(UWorld* World)
	{
		return World != nullptr ? UMetamorphosisStatsSubsystem::Get(World) : nullptr;
	}

	static float Amount(const TArray<FString>& Args, float Default)
	{
		return Args.Num() > 0 ? FCString::Atof(*Args[0]) : Default;
	}

	static void SuspicionAdd(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->AddSuspicion(Amount(Args, 10.f));
		}
	}

	static void SuspicionSet(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->SetSuspicion(Amount(Args, 0.f));
		}
	}

	static void SuspicionTrigger(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->AddSuspicionByTrigger(Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("SightGlance")));
		}
	}

	static void MoodAdd(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->AddMood(Amount(Args, 20.f));
		}
	}

	static void MoodSet(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->SetMood(Amount(Args, 0.f));
		}
	}

	static void Stress(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->ApplyStressEvent();
		}
	}

	static void Eat(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->EatFood();
		}
	}

	static void LoopReset(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->ResetForNewLoop();
		}
	}

	static void ClockPause(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->SetClockRunning(false);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 时间已暂停（定时腿伤不再累加躁郁值）"));
		}
	}

	static void ClockResume(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			Stats->SetClockRunning(true);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Stats] 时间继续"));
		}
	}

	static void Print(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Stats] 怀疑 %.1f/%.0f（档位 %d）  躁郁 %.1f/%.0f（体型阶段 %d，倍率 %.2f）  当天 %.0f 分钟  速度倍率 %.2f（加成 %+.0f%%）  时间 %s"),
				Stats->GetSuspicion(), Stats->GetMaxSuspicion(), Stats->GetAlertLevel(),
				Stats->GetMood(), Stats->GetMaxMood(), Stats->GetBodyStage(), Stats->GetBodySizeMultiplier(),
				Stats->GetDayMinutes(), Stats->GetSpeedMultiplier(), Stats->GetSpeedBonusPercent(),
				Stats->IsClockRunning() ? TEXT("运行中") : TEXT("已暂停"));
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Stats] 取不到数值系统（当前 World 没有 GameInstance？）"));
		}
	}

	// ---------------- 时间 / 时段 / 探索机会（2026-10-19） ----------------

	static void TimeInfo(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			const FPeriodConfig* Period = Stats->GetCurrentPeriod();
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Time] 当天 %.0f 分钟（%s）｜时段 %s（%s）%s｜探索机会 %d/%d｜时间%s"),
				Stats->GetDayMinutes(), *Stats->GetClockText().ToString(),
				*Stats->GetCurrentPeriodDisplayName().ToString(), *Stats->GetCurrentPeriodId().ToString(),
				Period != nullptr
					? *FString::Printf(TEXT("（%.0f~%.0f 分钟）"), Period->StartMinutes, Period->EndMinutes)
					: TEXT("（超出配表时段）"),
				Stats->GetExploreChancesRemaining(), Stats->GetExploreChancesMax(),
				Stats->IsClockRunning() ? TEXT("运行中") : TEXT("已暂停"));
		}
	}

	static void TimeSet(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			const float Minutes = Amount(Args, 360.f);
			Stats->SetDayMinutes(Minutes);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Time] 时间设为 %.0f 分钟（%s）-> 时段 %s，探索机会 %d/%d"),
				Stats->GetDayMinutes(), *Stats->GetClockText().ToString(),
				*Stats->GetCurrentPeriodDisplayName().ToString(),
				Stats->GetExploreChancesRemaining(), Stats->GetExploreChancesMax());
		}
	}

	/** Meta.Time.SetClock 7:40 -> 460 分钟（比直接填分钟好用） */
	static void TimeSetClock(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			if (Args.Num() > 0)
			{
				FString Left;
				FString Right;
				if (Args[0].Split(TEXT(":"), &Left, &Right))
				{
					const float Minutes = FCString::Atof(*Left) * 60.f + FCString::Atof(*Right);
					Stats->SetDayMinutes(Minutes);
				}
			}
			UE_LOG(LogMetamorphosis, Log, TEXT("[Time] 时间设为 %s -> 时段 %s，探索机会 %d/%d"),
				*Stats->GetClockText().ToString(), *Stats->GetCurrentPeriodDisplayName().ToString(),
				Stats->GetExploreChancesRemaining(), Stats->GetExploreChancesMax());
		}
	}

	static void TimePeriod(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			const FName Target = Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("Morning"));

			// 直接把时间跳到该时段的起点，让 RefreshPeriod 走"跨时段 -> 重置配额"的正规路径
			UGameInstance* GameInstance = Stats->GetGameInstance();
			const UMetamorphosisConfigSubsystem* Cfg = GameInstance != nullptr
				? GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>()
				: nullptr;
			if (Cfg != nullptr)
			{
				for (const FPeriodConfig& P : Cfg->GetConfig().Periods)
				{
					if (P.PeriodId == Target)
					{
						Stats->SetDayMinutes(P.StartMinutes);
						break;
					}
				}
			}

			UE_LOG(LogMetamorphosis, Log, TEXT("[Time] 强制时段 -> %s（时间 %s，探索机会 %d/%d）"),
				*Stats->GetCurrentPeriodDisplayName().ToString(), *Stats->GetClockText().ToString(),
				Stats->GetExploreChancesRemaining(), Stats->GetExploreChancesMax());
		}
	}

	static void TimeOpps(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStatsSubsystem* Stats = Resolve(World))
		{
			const int32 Value = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : Stats->GetExploreChancesMax();
			Stats->SetExploreChancesRemaining(Value);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Time] 探索机会设为 %d/%d"),
				Stats->GetExploreChancesRemaining(), Stats->GetExploreChancesMax());
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMetaSuspicionAdd(
	TEXT("Meta.Suspicion.Add"),
	TEXT("怀疑值 +N（默认 10）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::SuspicionAdd));

static FAutoConsoleCommandWithWorldAndArgs GMetaSuspicionSet(
	TEXT("Meta.Suspicion.Set"),
	TEXT("怀疑值直接设为 N（默认 0）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::SuspicionSet));

static FAutoConsoleCommandWithWorldAndArgs GMetaSuspicionTrigger(
	TEXT("Meta.Suspicion.Trigger"),
	TEXT("按配置表 TriggerType 增减怀疑值，如 SightGlance / NoiseLoud"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::SuspicionTrigger));

static FAutoConsoleCommandWithWorldAndArgs GMetaMoodAdd(
	TEXT("Meta.Mood.Add"),
	TEXT("躁郁值 +N（默认 20，填负数可缓解）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::MoodAdd));

static FAutoConsoleCommandWithWorldAndArgs GMetaMoodSet(
	TEXT("Meta.Mood.Set"),
	TEXT("躁郁值直接设为 N（默认 0）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::MoodSet));

static FAutoConsoleCommandWithWorldAndArgs GMetaStress(
	TEXT("Meta.Stress"),
	TEXT("触发一次应激（家人议论），躁郁值 +StressEventGain"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::Stress));

static FAutoConsoleCommandWithWorldAndArgs GMetaEat(
	TEXT("Meta.Eat"),
	TEXT("觅食一次，躁郁值 -FoodRestore"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::Eat));

static FAutoConsoleCommandWithWorldAndArgs GMetaLoopReset(
	TEXT("Meta.LoopReset"),
	TEXT("周目重置：怀疑/躁郁各按配置 -20，时间回到一天开始"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::LoopReset));

static FAutoConsoleCommandWithWorldAndArgs GMetaClockPause(
	TEXT("Meta.Clock.Pause"),
	TEXT("暂停游戏时间（腿伤不再持续推高躁郁值）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::ClockPause));

static FAutoConsoleCommandWithWorldAndArgs GMetaClockResume(
	TEXT("Meta.Clock.Resume"),
	TEXT("继续游戏时间"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::ClockResume));

static FAutoConsoleCommandWithWorldAndArgs GMetaStatsPrint(
	TEXT("Meta.Stats.Print"),
	TEXT("把当前怀疑值/躁郁值/时间打到日志"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::Print));

// ---------------- 时间 / 时段 / 探索机会（2026-10-19） ----------------

static FAutoConsoleCommandWithWorldAndArgs GMetaTimeInfo(
	TEXT("Meta.Time.Info"),
	TEXT("打印当天分钟/时段/剩余探索机会"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::TimeInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaTimeSet(
	TEXT("Meta.Time.Set"),
	TEXT("当天时间设为 N 分钟（06:00 = 360，默认 360）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::TimeSet));

static FAutoConsoleCommandWithWorldAndArgs GMetaTimeSetClock(
	TEXT("Meta.Time.SetClock"),
	TEXT("当天时间设为 HH:MM（例如 7:40）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::TimeSetClock));

static FAutoConsoleCommandWithWorldAndArgs GMetaTimePeriod(
	TEXT("Meta.Time.Period"),
	TEXT("强制切到某个时段：Morning / Afternoon / Evening（时间会跳到该时段起点，配额随之重置）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::TimePeriod));

static FAutoConsoleCommandWithWorldAndArgs GMetaTimeOpps(
	TEXT("Meta.Time.Opps"),
	TEXT("探索机会设为 N（默认=当前时段配额）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisStatsConsole::TimeOpps));
#endif // !UE_BUILD_SHIPPING


