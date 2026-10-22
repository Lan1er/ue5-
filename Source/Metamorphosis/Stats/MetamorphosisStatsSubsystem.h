// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 怀疑值 / 躁郁值 数值系统（P0 骨架，2026-10-07）
//
// 职责边界：
//   * 只维护「怀疑值」「躁郁值」两个数值，外加一个单纯的游戏时间计数，并按
//     Content/Config/game_config.json（源头是 ConfigSource/数值配置表.xlsx）驱动它们；
//   * 不做家人动线、不做视线/躲藏判定、不做被捉结算、不做周目流程/结局——
//     这些等 10-07 动线表定稿后再接，接入点是下面几个 public 函数；
//   * 数值挂 GameInstance，跨关卡/跨周目不丢；ResetForNewLoop() 已按策划规则 -20。
//
// 取数：UMetamorphosisStatsSubsystem::Get(WorldContext)
// 改表：改 Excel -> python Tools/export_config.py -> 重启游戏（不要手改 JSON）
// 调试：控制台 Meta.Suspicion.Add / Meta.Mood.Add / Meta.Stress / Meta.Eat / Meta.Stats.Print

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "MetamorphosisStatsSubsystem.generated.h"

class UMetamorphosisConfigSubsystem;
struct FGameConfig;
struct FPeriodConfig;

/** 数值变化广播：UI 订阅这个即可，不需要每帧轮询。
 *  AlertLevel：0 忽略 / 1 搜寻 / 2 暴露（阈值来自配置表）
 *  BodyStage ：1 虫子 / 2 倍大 / 4 倍大（阈值来自配置表） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMetamorphosisStatsChanged, float, Suspicion, float, Mood, int32, AlertLevel, int32, BodyStage);

/** 体型阶段变化（阶段 2 起才有额外惩罚；后续追逐战/美术换形象可以挂这里） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMetamorphosisBodyStageChanged, int32, NewStage);

UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisStatsSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

	/** 夜晚跳过状态机（23:00 触发剧情 -> 播完跳到 06:00） */
	enum class ENightSkipPhase : uint8
	{
		Idle,          // 没在跳过
		PlayingStory,  // 正在播夜晚跳过的剧情（时间停住）
	};

public:
	// ---------- USubsystem ----------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---------- FTickableGameObject ----------
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;

	/** 从任意 WorldContext 取本系统（拿不到返回 nullptr，调用方自行兜底） */
	static UMetamorphosisStatsSubsystem* Get(const UObject* WorldContextObject);

	// ---------- 查询 ----------
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetSuspicion() const { return Suspicion; }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetMood() const { return Mood; }

	/** 开局躁郁值（调试/策划调起点用）。0 = 一形态（正常开局）；50 = 二形态（×2）；90 = 三形态（×4，带翅膀） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Metamorphosis|Stats")
	float InitialMood = 0.f;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetMaxSuspicion() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetMaxMood() const;

	/** 0~1，给进度条直接用 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetSuspicionRatio() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetMoodRatio() const;

	/** 0 忽略 / 1 搜寻 / 2 暴露 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	int32 GetAlertLevel() const;

	/** 1 / 2 / 3（与怀疑值三档一一对应） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	int32 GetBodyStage() const;

	/** 当前体型阶段的体积倍率（配置表 SizeMultiplier，默认 1） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetBodySizeMultiplier() const;

	/** 指定体型阶段（1/2/3）的体积倍率，来自配置表；查不到返回 1 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetStageSizeMultiplier(int32 Stage) const;

	/** 累计速度加成（百分比，20 = +20%）。来源是道具配表的 SpeedBonusPercent */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetSpeedBonusPercent() const { return SpeedBonusPercent; }

	/** 速度倍率 = 1 + 累计百分比/100，已夹到 [0.1, 10]（角色拿它乘 WalkSpeed / FlySpeed） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetSpeedMultiplier() const;

	/** 怀疑值达到被捉线。周目结算尚未接入，这里只做查询 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	bool IsCaught() const;

	/** 当天分钟数（06:00 = 360） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	float GetDayMinutes() const { return DayMinutes; }

	/** 读档用：直接设定当天分钟数（不触发时间流逝事件）。
	 *  若因此跨了时段，会把探索次数重置成新时段的配额（读档时随后会用 SetExploreChancesRemaining 覆盖成存档值）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void SetDayMinutes(float Value);

	// ------------------------------------------------------------------
	// 时段 / 探索机会（2026-10-19，严格按配表 Periods）
	//   * 每个时段有 ExploreChances 次"探索机会"= 该时段允许的成功交互次数；
	//   * 时段切换 -> 次数重置成新时段配额；
	//   * 用完 -> 交互组件拒绝交互并给玩家提示（见 UMetamorphosisInteractionComponent）。
	// ------------------------------------------------------------------

	/** 当前时段（配表 Periods）；查不到返回 nullptr（例如时间已过 23:00） */
	const FPeriodConfig* GetCurrentPeriod() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	FName GetCurrentPeriodId() const { return CurrentPeriodId; }

	/** 当前时段的名字，给 UI/日志用（配表没有则返回"未知"） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	FText GetCurrentPeriodDisplayName() const;

	/** 当前时段的名义配额 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	int32 GetExploreChancesMax() const { return ExploreChancesMax; }

	/** 本时段还剩几次探索机会 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	int32 GetExploreChancesRemaining() const { return ExploreChancesRemaining; }

	/** 还能不能继续翻找东西（还剩 > 0） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	bool HasExploreChanceLeft() const { return ExploreChancesRemaining > 0; }

	/** 消耗一次探索机会（交互**成功**时调用）。返回 false 表示已经用完、调用方应拒绝交互。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	bool ConsumeExploreChance();

	/** 读档用：直接设定剩余次数（不重算、不改时段） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void SetExploreChancesRemaining(int32 Value);

	/** 新周目/新游戏用：按当前时段把次数拉满 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void ResetExploreChances();

	/** 当天时间格式化 HH:MM（24 小时制，游戏内分钟） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	FText GetClockText() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	bool IsClockRunning() const { return bClockRunning; }

	// ------------------------------------------------------------------
	// 夜晚跳过（2026-10-19 策划）：23:00 触发一段剧情 -> 剧情播完直接跳到 06:00
	//   * 跳过期间时间停住（不再 +分钟）；
	//   * 跳完时段回到 Morning（时段切换会自动把探索机会重置成配额）；
	//   * 剧情播放期间**拒绝存盘**（状态不完整，见 SaveSubsystem::SaveNow）。
	// ------------------------------------------------------------------

	/** 夜晚跳过是否正在进行（剧情播放中或正在跳时间） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	bool IsNightSkipActive() const { return NightSkipPhase != ENightSkipPhase::Idle; }

	/** 夜晚跳过用的剧情入口节点（配表 Story 的 NodeId，默认 NightSkip01） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Stats")
	FName GetNightSkipStoryNode() const { return NightSkipStoryNode; }

	/** 调试用：换一个夜晚跳过的入口节点（空 = 关闭夜晚跳过） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void SetNightSkipStoryNode(FName NodeId) { NightSkipStoryNode = NodeId; }

	// ---------- 修改 ----------

	/** 怀疑值增减（正数=被察觉）。会重置衰减延迟计时 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void AddSuspicion(float Amount);

	/** 按配置表 SuspicionGain 的 TriggerType 增减，如 SightGlance / NoiseLoud / LoopReset */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void AddSuspicionByTrigger(FName TriggerType);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void SetSuspicion(float Value);

	/** 躁郁值增减（正数=变躁，负数=缓解） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void AddMood(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void SetMood(float Value);

	/** 永久速度加成（道具配表 SpeedBonusPercent 调用）。累计到 SpeedBonusPercent，跨关卡保留。
	 *  周目重置**不清空**：道具表写的是"永久获得"，且 GameInstance 子系统本来就跨关卡/跨周目，
	 *  身体能力属于角色自身成长，不该被剧情重置回滚（ResetForNewLoop 里也打了一行日志说明）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void AddSpeedBonusPercent(float Percent);

	/** 应激：家人议论一次（+StressEventGain） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void ApplyStressEvent();

	/** 觅食一次（-FoodRestore） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void EatFood();

	/** 周目重置：怀疑与躁郁都按配置 -LoopResetDelta，时间回到一天开始。
	 *  被捉/入夜的实际流程等动线表定稿后再接。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void ResetForNewLoop();

	/** 时间源开关（腿伤是按游戏时间持续涨躁郁的，调试时可以先停表） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Stats")
	void SetClockRunning(bool bRunning) { bClockRunning = bRunning; }

	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Stats")
	FMetamorphosisStatsChanged OnStatsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Stats")
	FMetamorphosisBodyStageChanged OnBodyStageChanged;

private:
	const FGameConfig& Cfg() const;
	UMetamorphosisConfigSubsystem* GetConfigSubsystem() const;

	void SetSuspicionInternal(float Value);
	void SetMoodInternal(float Value);
	void BroadcastIfChanged();

	/** 按当前 DayMinutes 重算时段；**跨时段**时把探索次数重置成新时段配额。
	 *  返回 true 表示时段发生了变化。 */
	bool RefreshPeriod();

	/** 时间到 23:00 时尝试启动夜晚跳过（剧情若能播就进 PlayingStory，否则下一帧再试） */
	void TryBeginNightSkip();

	/** 夜晚跳过状态机推进：玩家在播剧情时时间停住；剧情播完 -> 跳到 DayStartMinutes（06:00） */
	void UpdateNightSkip(float DeltaTime);

	/** 取剧情子系统（本项目的 Story 是 GameInstance 子系统，必须走 GetGameInstance()->GetSubsystem<>()） */
	class UMetamorphosisStorySubsystem* GetStorySubsystem() const;

	/** 夜晚跳过用的剧情入口节点（配表 Story；空 = 关闭夜晚跳过） */
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Time")
	FName NightSkipStoryNode = TEXT("NightSkip01");

	/** 夜晚跳过状态 */
	ENightSkipPhase NightSkipPhase = ENightSkipPhase::Idle;
	/** 防御：夜晚跳过错过后不要再触发（只有新周目/新游戏/读档回 23:00 才允许） */
	bool bNightSkipDoneThisDay = false;

	float Suspicion = 0.f;
	float Mood = 0.f;

	/** 当前时段 Id（NAME_None = 还没算过） */
	FName CurrentPeriodId = NAME_None;
	/** 当前时段的名义配额 */
	int32 ExploreChancesMax = 0;
	/** 本时段剩余探索机会 */
	int32 ExploreChancesRemaining = 0;

	/** 累计速度加成百分比（道具给的"永久"加成，跨关卡保留，周目重置不清） */
	float SpeedBonusPercent = 0.f;

	/** 开局数值是否已应用（等配置表载入后、第一帧才应用） */
	bool bInitialValuesApplied = false;
	/** 当天分钟数，从 Global.DayStartMinutes 开始 */
	float DayMinutes = 0.f;
	/** 距上次怀疑值增长过了多久（用于 DecayDelay） */
	float TimeSinceSuspicionGain = 0.f;

	bool bClockRunning = true;
	bool bDayInitialized = false;
	bool bLoggedCaught = false;

	// 广播去抖：只有在数值/档位真的变了才通知 UI
	float LastBroadcastSuspicion = -1.f;
	float LastBroadcastMood = -1.f;
	int32 LastBroadcastLevel = INDEX_NONE;
	int32 LastBroadcastStage = INDEX_NONE;
};



