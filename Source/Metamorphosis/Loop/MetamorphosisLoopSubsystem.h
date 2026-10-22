// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 周目结算（被捉 / 入夜）：把"玩家被发现"从一句日志变成一段有反馈的流程。
//
// 为什么需要它：妻子的追逐战抓到主控后，原本只做了两件事 —— 把怀疑值拉到被捉线、
// 调 Stats->ResetForNewLoop()。玩家屏幕上**什么都没有**，体验就是"莫名其妙重开了"。
//
// 本子系统的四阶段（阶段计时用 **FTSTicker 真实时间**，不是 FTimerManager：
// 后者是游戏时间，一旦中途被暂停（例如玩家在结算中点开弹窗）就永远不会走完）：
//   1. Freeze     —— 冻结输入（控制器进入"结算中"）+ 停住主角 + 通知妻子收手
//   2. Feedback   —— 显示全屏反馈（暗化遮罩 + 大字提示），持续 FeedbackSeconds
//   3. ApplyReset —— Stats->ResetForNewLoop() + 把主角放回安全起点（LoopStartLocation）
//   4. Resume     —— 恢复输入、隐藏反馈、计数 +1、进入 CooldownSeconds 冷却
//
// ⚠️ 重生点**不能用 PlayerStart**：本图（/Game/mymap/map/Untitled）的 PlayerStart 埋在地形下 3 米多，
//    用它会把主角放进地里（详见 Docs/换地图与资源清理.md）。所以用 ini 的 LoopStartLocation。
//
// 入夜（NightEnds）：流程与参数都已接好（Meta.Loop.NightEnd 可手动触发），
// 但**自动触发还没接** —— Stats 目前只是个时间源（它自己注释写着"不做入夜结算/周目流程"）。
// 接线点见 Docs/被捉与周目结算.md 的 TODO。

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MetamorphosisLoopSubsystem.generated.h"

class AMetamorphosisPlayerController;

/** 周目为什么结束 */
UENUM(BlueprintType)
enum class ELoopEndReason : uint8
{
	/** 被家人（妻子）抓住 */
	Caught UMETA(DisplayName = "被捉"),
	/** 入夜（这一天结束） */
	NightEnds UMETA(DisplayName = "入夜")
};

/** 结算当前处在哪个阶段 */
UENUM(BlueprintType)
enum class ELoopPhase : uint8
{
	Idle UMETA(DisplayName = "不在结算中"),
	Freeze UMETA(DisplayName = "冻结输入"),
	Feedback UMETA(DisplayName = "显示反馈"),
	/** 2026-10-19：反馈播完后**停在这里等玩家点按钮**（用户："还没点重新开始就自动重新开始了"） */
	AwaitChoice UMETA(DisplayName = "等待选择"),
	ApplyReset UMETA(DisplayName = "应用重置"),
	Resume UMETA(DisplayName = "恢复")
};

class FLoopSettlementTicker;

/**
 * 周目结算流程。GameInstance 级（跨关卡保留，结算次数等数据不会因换关丢失）。
 * 取用：UMetamorphosisLoopSubsystem::Get(WorldContext)
 */
UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisLoopSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 析构在 .cpp 里定义：TUniquePtr<FLoopSettlementTicker> 需要完整类型才能实例化删除器 */
	virtual ~UMetamorphosisLoopSubsystem() override;

	static UMetamorphosisLoopSubsystem* Get(const UObject* WorldContextObject);

	/** 唯一入口：请求结束本周目。结算中 / 冷却中 / **已经通关**的请求会被忽略并打日志。
	 *  @return 是否真的开始了结算 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	bool RequestLoopEnd(ELoopEndReason Reason);

	/** 在接下来 Seconds 秒内**忽略所有结算请求** ✓（2026-10-19：读档保护窗口）
	 *  用途：读档瞬间如果还有残留状态想结算（门的超时 / 妻子抓人 / 剧情 Death）✗，
	 *  会走 ApplyFullLoopReset 把**背包清空 + 玩家传回出生点** ✗✗
	 *  —— 玩家看到的就是"读档后位置不对、背包消失"✗。
	 *  调用方：SaveSubsystem 读档时（默认 3 秒）✓。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void SuppressLoopReset(float Seconds, const FString& Reason);

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	bool IsSettling() const { return bActive; }

	// ------------------------------------------------------------------
	// 通关（2026-10-18：从门缝出去 = 直接通关）
	//
	// 为什么要"直接通关"：原剧情里"出去"是一条长分支（蜘蛛网 / 道具判定 / 大雪 / 强风操控），
	// 用户因时间原因要求**全部删除**，改成"走到缝隙旁 -> 交互「从缝隙出去」-> 通关"。
	// 所以这里没有任何道具前置条件（WinRequiredItems 默认空），要加就配 ini。
	// ------------------------------------------------------------------

	/** 请求通关：会先播通关剧情（WinStoryNode，节点里用 Effect=Win 收尾），没有就立刻显示通关界面。
	 *  @return 是否接受了这次请求（已经通关 / 结算中 / 缺少必需道具时返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	bool RequestWin(const FString& Source);

	/** 直接显示通关界面（不播剧情）。剧情节点的 Effect=Win 也走这里。
	 *  @return 是否真的显示了（重复调用返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	bool ShowWinEnding(const FString& Source);

	/** 通关界面上的「重新开始」：收起界面 + 恢复运行 + 走一次完整周目重置（数值/任务/背包/剧情/位置） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void RestartAfterWin();

	/** 通关界面上的「返回标题」：收起界面 + 回到开始界面（并清掉"已进入游戏"标志，让「开始游戏」还能用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void ReturnToTitleAfterWin();

	// ------------------------------------------------------------------
	// 结算界面玩家选择（2026-10-19）
	// 反馈播完后进入 AwaitChoice 阶段**停住**，等玩家点「重新开始」或「返回主菜单」。
	// 两个方法只在 AwaitChoice 阶段有效，返回 false 表示当前不在等待选择（调用方可以忽略）。
	// ------------------------------------------------------------------

	/** 结算界面「重新开始」：走完整周目重置 + 恢复运行 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	bool ChooseSettlementRestart();

	/** 结算界面「返回主菜单」：先完整重置（不把"被抓住"的脏状态留给存档）再回标题 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	bool ChooseSettlementReturnToTitle();

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	bool IsAwaitingSettlementChoice() const { return Phase == ELoopPhase::AwaitChoice; }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	bool HasWon() const { return bWon; }

	// ------------------------------------------------------------------
	// 手动跳周目 / 新的一天（2026-10-19 用户要求）
	// ------------------------------------------------------------------

	/** 设置界面「下一周目」：不播被捉反馈，直接做一次完整周目重置（周目 +1、回出生点、数值/任务/背包/剧情全重置）。
	 *  @return 是否执行了（正在结算/通关界面时返回 false，避免打架） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	bool RequestManualNextLoop();

	/** 夜晚跳过结束（23:00 -> 06:00）后调：**只把周目数 +1**，不做任何重置（时间已经跳了、玩家还在原地） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void IncrementLoopCountForNewDay(const FString& Reason);

	/**
	 * 进入"第二天"（策划语义：**周目 = 家中一天**）。
	 * 三条入口共用：被捉结算后重新开始 / 23:00 夜晚跳过 / 暂停菜单「下一周目」。
	 * 行为：周目 +1；数值按配表 LoopResetDelta（怀疑/躁郁 -20）；时间回 DayStartMinutes(06:00)；
	 * **保留剧情道具（配表 Category=Story 的格子）**，其余清空；回出生点；**不播开场**，改播 DayStartStoryNode。
	 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void BeginNextDay(const FString& Reason);

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	bool IsWinEndingVisible() const { return bWinEndingShown; }

	/** 通关状态一行诊断串（Meta.Loop.WinInfo 用） */
	FString GetWinInfoText() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	ELoopPhase GetPhase() const { return Phase; }

	/** 本次运行已经结算过几次 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	int32 GetLoopCount() const { return LoopCount; }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	ELoopEndReason GetLastReason() const { return LastReason; }

	/** 冷却剩余秒数（>0 时新的结算请求会被忽略） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	float GetCooldownRemaining() const { return CooldownRemaining; }

	FString GetPhaseText() const;
	FString GetReasonText(ELoopEndReason Reason) const;

	// ------------------------------------------------------------------
	// 参数（Config=Game，段名 [/Script/Metamorphosis.MetamorphosisLoopSubsystem]）
	// 注意：UE 是先跑构造函数再套用 ini，所以这些值只在构造函数之后的代码里可信
	// ------------------------------------------------------------------

	/** 冻结阶段时长（秒）：只做"立刻收手"的动作，给一小段时间让状态落定 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Timing")
	float FreezeSeconds = 0.15f;

	/** 反馈（暗化 + 大字）停留时长（秒） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Timing")
	float FeedbackSeconds = 2.2f;

	/** 遮罩淡入时长（秒） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Timing")
	float FadeInSeconds = 0.3f;

	/** 重置阶段时长（秒） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Timing")
	float ResetSeconds = 0.2f;

	/** 「等待玩家选择」的兜底超时（秒）：0 = 永不超时。
	 *  超时后自动走「重新开始」，防止玩家放着不管卡在这里。 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Timing")
	float AwaitChoiceTimeoutSeconds = 60.f;

	/** 结算结束后的冷却（秒）：防止刚结算完又立刻触发一次 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Timing")
	float CooldownSeconds = 2.f;

	/** 是否把主角放回 LoopStartLocation（关掉就只重置数值、不动位置） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Start")
	bool bResetPlayerLocation = true;

	/** 周目重生点（世界坐标，厘米）。
	 *  ⚠️ 不要用 PlayerStart：本图的 PlayerStart 埋在地形下 3 米多。
	 *  默认值是之前实测"虫子能站稳"的卧室地面点。 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Start")
	FVector LoopStartLocation = FVector(-320.f, -630.f, 10.f);

	/** 遮罩最大不透明度（0~1） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|UI")
	float OverlayMaxOpacity = 0.75f;

	/** 「进第二天」时播的日开端剧情节点 id（配表 Story 表；留空 = 不播任何剧情）。
	 *  用途：周目 = 家中一天（策划案 [031][039]），第二天**不重播开场**，改播这段短旁白。
	 *  ini 段：[/Script/Metamorphosis.MetamorphosisLoopSubsystem] DayStartStoryNode=DayStart01 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|NextDay")
	FString DayStartStoryNode = TEXT("DayStart01");

	/** 是否打结算流程日志 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Log")
	bool bLogLoop = true;

	/** 结算中要显示的文案（可在 Project Settings / ini 里改） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Text")
	FText CaughtTitle = FText::FromString(TEXT("你被抓住了"));

	UPROPERTY(EditAnywhere, Config, Category = "Loop|Text")
	FText NightEndTitle = FText::FromString(TEXT("这一天结束了"));

	// ------------------------------------------------------------------
	// 通关参数（Config=Game，段名 [/Script/Metamorphosis.MetamorphosisLoopSubsystem]）
	// ------------------------------------------------------------------

	/** 通关剧情起始节点（配表 Story 的 NodeId；留空 = 不播剧情，交互后直接显示通关界面）。
	 *  建议节点结构：Win01(文本) -> Win02(文本, Effect=Win) -> Next 空。
	 *  Effect=Win 只是"标记待通关"，**剧情结束（EndStory）时才真正显示通关界面** —— 这样最后一句
	 *  文本玩家能读完，不会一进节点就被通关界面盖住。 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Win")
	FName WinStoryNode = FName("Win01");

	/** 通关必需道具（ItemId 列表；默认**空** = 不需要任何道具）。
	 *  原剧情里的"棉棒/铁丝/长条发卡"判定因时间原因删除了，这里留一个空的可配项，
	 *  以后策划想加"必须带某件东西才能出去"时填上即可（缺道具时交互会被拒绝并打日志）。 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Win")
	TArray<FString> WinRequiredItems;

	/** 通关界面的大标题 */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Win")
	FText WinTitle = FText::FromString(TEXT("通关"));

	/** 通关界面的副标题（固定文案；数据行是自动生成的"第 N 周目 · 怀疑值 X · 当天 H:MM"） */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Win")
	FText WinSubtitle = FText::FromString(TEXT("你从门缝钻了出去，寒风扑面，你钻进了冻土。"));

	/** 通关时是否把游戏暂停（冻结时钟/妻子/玩法门）—— 终局界面下应该冻结，默认 true */
	UPROPERTY(EditAnywhere, Config, Category = "Loop|Win")
	bool bFreezeGameOnWin = true;

private:
	/** 由 FLoopSettlementTicker 每帧调用（真实时间）。返回 false 表示结算与冷却都结束、可以停表 */
	bool TickSettlement(float DeltaTime);

	bool BeginSettlement(ELoopEndReason Reason);
	void EnterPhase(ELoopPhase NewPhase);
	void StartTicker();

	/** 完整周目重置（数值/时间、任务、背包、位置、剧情重播）。
	 *  抽出来是因为「结算的 ApplyReset 阶段」和「通关界面的重新开始」要做同一件事。 */
	void ApplyFullLoopReset(const TCHAR* Context);

	AMetamorphosisPlayerController* FindLocalController() const;

	ELoopPhase Phase = ELoopPhase::Idle;
	ELoopEndReason LastReason = ELoopEndReason::Caught;

	/** 是否正在结算（Phase != Idle） */
	bool bActive = false;

	/** 已完成的结算次数 */
	int32 LoopCount = 0;

	/** 当前阶段已过去的时间 */
	float PhaseTimer = 0.f;

	/** 冷却剩余 */
	float CooldownRemaining = 0.f;

	/** 本周目是否已经通关（通关后禁止再触发周目结算/被捉结算） */
	bool bWon = false;

	/** 读档保护窗口剩余秒数（>0 时 RequestLoopEnd 一律忽略 ✓ 防止读档瞬间被清背包/传回出生点） */
	float SuppressResetRemaining = 0.f;

	/** 通关界面是否已经显示（防止重复显示） */
	bool bWinEndingShown = false;

	/** 通关来源（日志/诊断用：交互 / 剧情 Effect / 调试命令） */
	FString WinSource;

	/** 请求结算那一刻的怀疑值（写进副标题，方便玩家理解"为什么被抓"） */
	float SuspicionAtRequest = 0.f;

	/** 真实时间计时器（结算期间与冷却期间存在）。
	 *  用裸指针而不是 TUniquePtr：TUniquePtr 的删除器会在头文件里被实例化，
	 *  而 FLoopSettlementTicker 只有在 .cpp 里才是完整类型（踩过 C4150）。 */
	FLoopSettlementTicker* Ticker = nullptr;

	friend class FLoopSettlementTicker;
};
