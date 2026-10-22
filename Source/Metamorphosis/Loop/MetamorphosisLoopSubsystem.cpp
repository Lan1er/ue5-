// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/UnrealMemory.h"

#include "AI/MetamorphosisWifeSubsystem.h"
#include "Character/MetamorphosisBeetleCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Metamorphosis.h"
#include "MetamorphosisPlayerController.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Story/MetamorphosisStorySubsystem.h"
#include "Tasks/MetamorphosisTaskSubsystem.h"

// ---------------------------------------------------------------------------
// 真实时间计时器：**不能用 FTimerManager**（那是游戏时间，暂停就不走，
// 万一结算过程中被暂停，流程会永远卡住）。同 MetamorphosisPlayerController.cpp 的 FRealTimeDelay。
// ---------------------------------------------------------------------------
class FLoopSettlementTicker : public FTSTickerObjectBase
{
public:
	explicit FLoopSettlementTicker(UMetamorphosisLoopSubsystem* InOwner)
		: FTSTickerObjectBase(0.f, FTSTicker::GetCoreTicker())
		, Owner(InOwner)
	{
	}

	virtual bool Tick(float DeltaTime) override
	{
		UMetamorphosisLoopSubsystem* Sys = Owner.Get();
		if (Sys == nullptr)
		{
			return false;   // 子系统没了，自己摘掉
		}
		return Sys->TickSettlement(DeltaTime);
	}

private:
	TWeakObjectPtr<UMetamorphosisLoopSubsystem> Owner;
};

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

UMetamorphosisLoopSubsystem::~UMetamorphosisLoopSubsystem()
{
	// 这里才能安全地删掉计时器：FLoopSettlementTicker 在本文件里才是完整类型
	delete Ticker;
	Ticker = nullptr;
}

void UMetamorphosisLoopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 这里读 ini 是安全的（构造已完成、配置已套用到实例）
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Loop] 周目结算就绪：冻结 %.2fs / 反馈 %.2fs / 淡入 %.2fs / 重置 %.2fs / 冷却 %.2fs；重生点 %s（%s）；遮罩 %.2f"),
		FreezeSeconds, FeedbackSeconds, FadeInSeconds, ResetSeconds, CooldownSeconds,
		bResetPlayerLocation ? *LoopStartLocation.ToCompactString() : TEXT("不改位置"),
		bResetPlayerLocation ? TEXT("不用 PlayerStart：本图的它埋在地形下") : TEXT("已关闭位置重置"),
		OverlayMaxOpacity);
}

void UMetamorphosisLoopSubsystem::Deinitialize()
{
	// 摘掉计时器（析构 FTSTickerObjectBase 会自动从 ticker 列表移除）
	delete Ticker;
	Ticker = nullptr;
	bActive = false;
	Phase = ELoopPhase::Idle;

	Super::Deinitialize();
}

UMetamorphosisLoopSubsystem* UMetamorphosisLoopSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject))
	{
		return GameInstance->GetSubsystem<UMetamorphosisLoopSubsystem>();
	}
	return nullptr;
}

FString UMetamorphosisLoopSubsystem::GetPhaseText() const
{
	switch (Phase)
	{
	case ELoopPhase::Freeze:     return TEXT("冻结输入");
	case ELoopPhase::Feedback:   return TEXT("显示反馈");
	case ELoopPhase::ApplyReset: return TEXT("应用重置");
	case ELoopPhase::AwaitChoice: return TEXT("等待选择");
	case ELoopPhase::Resume:     return TEXT("恢复");
	default:                     return TEXT("不在结算中");
	}
}

FString UMetamorphosisLoopSubsystem::GetReasonText(ELoopEndReason Reason) const
{
	return Reason == ELoopEndReason::NightEnds ? TEXT("入夜") : TEXT("被捉");
}

AMetamorphosisPlayerController* UMetamorphosisLoopSubsystem::FindLocalController() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UWorld* World = GameInstance->GetWorld())
		{
			return Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController());
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

bool UMetamorphosisLoopSubsystem::RequestLoopEnd(ELoopEndReason Reason)
{
	// 读档保护窗口（2026-10-19）✓：
	// 读档瞬间如果还有残留状态想结算（门超时 / 妻子抓人 / 剧情 Death）✗ -> ApplyFullLoopReset 会
	// **清空背包 + 把玩家传回出生点** ✗✗ —— 玩家看到的就是"读档后位置不对、背包消失"✗。
	// 所以读档后几秒内一律忽略结算请求 ✓。
	if (SuppressResetRemaining > 0.f)
	{
		if (bLogLoop)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Loop] 忽略结算请求（%s）：读档保护窗口还剩 %.1f 秒（读档后不结算，避免清背包/传回出生点）"),
				*GetReasonText(Reason), SuppressResetRemaining);
		}
		return false;
	}

	// 已经通关：不再允许被打断（否则通关界面上还会冒出"你被抓住了"）
	if (bWon)
	{
		if (bLogLoop)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Loop] 忽略结算请求（%s）：本局已通关（来源=%s），不再结算"), *GetReasonText(Reason), *WinSource);
		}
		return false;
	}

	if (bActive || Phase != ELoopPhase::Idle)
	{
		if (bLogLoop)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Loop] 忽略结算请求（%s）：已经在结算中（阶段=%s）—— 防重入"),
				*GetReasonText(Reason), *GetPhaseText());
		}
		return false;
	}

	if (CooldownRemaining > 0.f)
	{
		if (bLogLoop)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Loop] 忽略结算请求（%s）：冷却中（还剩 %.2fs）"),
				*GetReasonText(Reason), CooldownRemaining);
		}
		return false;
	}

	return BeginSettlement(Reason);
}

bool UMetamorphosisLoopSubsystem::BeginSettlement(ELoopEndReason Reason)
{
	bActive = true;
	LastReason = Reason;
	PhaseTimer = 0.f;

	// 记录触发时的怀疑值（副标题用）
	SuspicionAtRequest = 0.f;
	if (const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		SuspicionAtRequest = Stats->GetSuspicion();
	}

	if (bLogLoop)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("=== [Loop] 周目结算开始（第 %d 次）：原因=%s｜怀疑=%.1f｜重生点=%s ==="),
			LoopCount + 1, *GetReasonText(Reason), SuspicionAtRequest,
			bResetPlayerLocation ? *LoopStartLocation.ToCompactString() : TEXT("不改位置"));
	}

	StartTicker();
	EnterPhase(ELoopPhase::Freeze);
	return true;
}

void UMetamorphosisLoopSubsystem::StartTicker()
{
	// 上一轮的计时器对象可能还留着（它自己已经 return false 摘掉了），这里安全地换新
	delete Ticker;
	Ticker = new FLoopSettlementTicker(this);
}

void UMetamorphosisLoopSubsystem::EnterPhase(ELoopPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTimer = 0.f;

	if (bLogLoop)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 阶段 -> %s"), *GetPhaseText());
	}

	// ------------------------------------------------------------------
	// 各阶段"进入时"要做的事
	// ------------------------------------------------------------------
	if (NewPhase == ELoopPhase::Freeze)
	{
		// ① 控制器进入"结算中"：移动/视角/升降/交互/使用/切快捷栏一律早退
		if (AMetamorphosisPlayerController* PC = FindLocalController())
		{
			PC->SetSettlementActive(true);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 已冻结输入（控制器进入结算中）"));
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 找不到玩家控制器：这次结算只重置数值，输入不会被冻结"));
		}

		// ② 停住主角（速度清零；升降输入在 SetSettlementActive 里已经清掉）
		if (AMetamorphosisPlayerController* PC = FindLocalController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				if (UCharacterMovementComponent* Move = Pawn->FindComponentByClass<UCharacterMovementComponent>())
				{
					Move->StopMovementImmediately();
				}
			}
		}

		// ③ 通知妻子 AI 收手（停追逐、清目标、回巡视）
		if (UMetamorphosisWifeSubsystem* Wife = UMetamorphosisWifeSubsystem::Get(this))
		{
			Wife->NotifyLoopReset();
		}
	}
	else if (NewPhase == ELoopPhase::Feedback)
	{
		if (AMetamorphosisPlayerController* PC = FindLocalController())
		{
			const FText Title = (LastReason == ELoopEndReason::NightEnds) ? NightEndTitle : CaughtTitle;
			const FText Subtitle = FText::FromString(FString::Printf(
				TEXT("第 %d 次结算 · 怀疑值 %.0f"), LoopCount + 1, SuspicionAtRequest));

			PC->ShowSettlementFeedback(Title, Subtitle, OverlayMaxOpacity, FadeInSeconds);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 已显示结算反馈：%s（停留 %.2fs）"),
				*Title.ToString(), FeedbackSeconds);
		}
	}
	else if (NewPhase == ELoopPhase::AwaitChoice)
	{
		// 反馈播完了，但**不自动重置** ✗ —— 停在这里等玩家点「重新开始 / 返回主菜单」。
		// 界面已经挂着（Feedback 阶段挂的），这里只把输入交给 UI（显示鼠标 + GameAndUI）。
		if (AMetamorphosisPlayerController* PC = FindLocalController())
		{
			PC->SetSettlementChoiceInput(true);
		}

		UE_LOG(LogMetamorphosis, Warning,
			TEXT("=== [Loop] 结算界面：等待玩家选择（重新开始 / 返回主菜单）｜超时 %.1f 秒后自动重新开始 ==="),
			AwaitChoiceTimeoutSeconds);
	}
	else if (NewPhase == ELoopPhase::ApplyReset)
	{
		BeginNextDay(TEXT("被捉结算后重新开始"));
	}
	else if (NewPhase == ELoopPhase::Resume)
	{
		if (AMetamorphosisPlayerController* PC = FindLocalController())
		{
			PC->HideSettlementFeedback();
			PC->SetSettlementActive(false);
		}

		LoopCount++;
		CooldownRemaining = CooldownSeconds;
		bActive = false;
		Phase = ELoopPhase::Idle;

		UE_LOG(LogMetamorphosis, Warning,
			TEXT("=== [Loop] 周目结算结束（第 %d 次）：输入已恢复，冷却 %.2fs ==="),
			LoopCount, CooldownSeconds);
	}
}

// ---------------------------------------------------------------------------
// 完整周目重置（结算的 ApplyReset 阶段 + 通关界面的「重新开始」共用）
// ---------------------------------------------------------------------------

#include "Config/MetamorphosisConfigSubsystem.h"

void UMetamorphosisLoopSubsystem::ApplyFullLoopReset(const TCHAR* Context)
{
	// ===== 周目完整重置（2026-10-16 用户要求："每次新周目应该全部从头开始"）=====
	// 逐项打日志，方便以后排查"哪一步没重置"。

	// 【诊断（2026-10-19）】这一步会 **清空背包 + 把玩家传回出生点** ✗ ——
	// 读档后如果看到这两个症状，就是这里被意外调了 ✗。所以无条件打一条带调用栈的日志 ✓，
	// 以后"读档位置不对/背包消失"能一秒钟定位到是谁的锅 ✓。
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] === 周目完整重置开始（%s）=== 本步会清空背包并传回出生点"), Context);
	{
		const SIZE_T StackSize = 64 * 1024;
		ANSICHAR* StackBuffer = static_cast<ANSICHAR*>(FMemory::Malloc(StackSize));
		if (StackBuffer != nullptr)
		{
			FPlatformStackWalk::StackWalkAndDump(StackBuffer, StackSize, 0);
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 重置调用栈：\n%s"), ANSI_TO_TCHAR(StackBuffer));
			FMemory::Free(StackBuffer);
		}
	}


	// ① 数值：怀疑/躁郁按配表 LoopResetDelta 处理（保留周目记忆），
	//    时间回到一天开始（Global.DayStartMinutes），速度加成清零（见 Stats::ResetForNewLoop）。
	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		const int32 DayBefore = FMath::RoundToInt(Stats->GetDayMinutes());
		Stats->ResetForNewLoop();
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Loop] 重置①数值/时间（%s）：当天 %d -> %d 分钟（配表 DayStartMinutes），怀疑 %.1f / 躁郁 %.1f，速度加成已清零"),
			Context, DayBefore, FMath::RoundToInt(Stats->GetDayMinutes()), Stats->GetSuspicion(), Stats->GetMood());
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 重置①数值（%s）：拿不到 StatsSubsystem，数值没有重置"), Context);
	}

	// ② 任务回到第一条
	if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
	{
		Tasks->ResetToFirst();
		const FTaskConfigEntry* First = Tasks->GetCurrentTask();
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 重置②任务（%s）：回到第 1 条（%s）"),
			Context, First != nullptr ? *First->TaskId : TEXT("配表为空"));
	}

	// ③ 背包清空 + ④ 把主角放回安全起点（背包挂在 Pawn 上，所以一起从 Pawn 取）
	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		if (AMetamorphosisBeetleCharacter* Beetle = Cast<AMetamorphosisBeetleCharacter>(PC->GetPawn()))
		{
			if (UMetamorphosisInventoryComponent* Inventory = Beetle->FindComponentByClass<UMetamorphosisInventoryComponent>())
			{
				Inventory->ClearAllSlots();
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 重置③背包（%s）：已清空快捷栏与背包"), Context);
			}

			if (bResetPlayerLocation)
			{
				Beetle->TeleportToLoopStart(LoopStartLocation);
				// 重生时把视角也复位到"出生朝向"（ini 的 CameraYaw），否则上一局玩家转过的视角会带过来
				Beetle->ResetCameraYawToSpawn();
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 重置④位置（%s）：放回起点 %s"),
					Context, *LoopStartLocation.ToCompactString());
			}
		}
	}

	// ⑤ 剧情从头开始：重播开场（先清"已播过"标志，否则会被防重播挡住）
	if (UMetamorphosisStorySubsystem* Story = UGameInstance::GetSubsystem<UMetamorphosisStorySubsystem>(GetGameInstance()))
	{
		Story->ReplayOpeningForNewLoop();
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 重置⑤剧情（%s）：拿不到 StorySubsystem，开场没有重播"), Context);
	}
}

void UMetamorphosisLoopSubsystem::BeginNextDay(const FString& Reason)
{
	// ===== 进入"第二天"（2026-10-19 策划语义：周目 = 家中一天）=====
	// 三条入口共用：被捉结算后重新开始 / 23:00 夜晚跳过 / 暂停菜单「下一周目」。
	// 与 ApplyFullLoopReset 的区别：**保留剧情道具（记忆碎片）**、**不播开场**、改播日开端剧情。
	// 日开端节点：ini 可配（DayStartStoryNode，留空 = 不播剧情）
	const FString DayStartNode = DayStartStoryNode;
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] === 进入第二天（%s）=== 保留记忆碎片、不播开场"), *Reason);

	// ① 周目计数由调用方负责 +1（被捉：Resume 阶段；手动：PC 入口；夜晚跳过：IncrementLoopCountForNewDay）。
	//    ⚠️ 这里绝对不能再 +1 ✗ —— 2026-10-19 实测发现"每跳一次周目 +2"就是这里和调用方各加了一次。
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 进第二天①周目计数（由调用方 +1）：当前第 %d 周目"), LoopCount + 1);

	// ② 数值/时间：沿用 ResetForNewLoop（时间回 DayStartMinutes、怀疑/躁郁按配表 LoopResetDelta、速度加成清零）
	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		Stats->ResetForNewLoop();
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 进第二天②数值：当天 %d 分钟，怀疑 %.1f / 躁郁 %.1f"),
			FMath::RoundToInt(Stats->GetDayMinutes()), Stats->GetSuspicion(), Stats->GetMood());
	}

	// ③ 任务：**继承上一天**（2026-10-19 用户确认语义："上一天做到哪里，第二天就是哪里"）
	//   ⚠️ 这里**绝不能** ResetToFirst / 跳到某条 ✗ —— 任务索引与进度跨天保留；
	//   已获得道具的记账同样保留（任务回溯判定要用）。这里只打一条诊断日志。
	if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[DIAG] 第二天任务（继承上一天，不重置）：第 %d 条 标题=%s 详情=%s｜已获得记账 %d 件"),
			Tasks->GetCurrentIndex() + 1, *Tasks->GetCurrentTitle().ToString(),
			*Tasks->GetCurrentDetail().ToString(), Tasks->GetObtainedItemIds().Num());
	}

	// ④ 背包：只保留剧情道具（配表 Category == Story），其余清空；格位保持不动
	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		if (AMetamorphosisBeetleCharacter* Beetle = Cast<AMetamorphosisBeetleCharacter>(PC->GetPawn()))
		{
			if (UMetamorphosisInventoryComponent* Inventory = Beetle->FindComponentByClass<UMetamorphosisInventoryComponent>())
			{
				UMetamorphosisConfigSubsystem* Cfg = (GetGameInstance() != nullptr)
					? GetGameInstance()->GetSubsystem<UMetamorphosisConfigSubsystem>()
					: nullptr;
				int32 KeptCount = 0;
				int32 ClearedCount = 0;
				for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
				{
					const FMetamorphosisItemSlot Slot = Inventory->GetSlot(Index);
					if (!Slot.bOccupied)
					{
						continue;
					}
					const FItemConfigEntry* Entry = (Cfg != nullptr) ? Cfg->GetItem(Slot.ItemId) : nullptr;
					const bool bStoryItem = (Entry != nullptr) && Entry->Category.Equals(TEXT("Story"), ESearchCase::IgnoreCase);
					if (bStoryItem)
					{
						++KeptCount;
						continue;
					}
					Inventory->SetSlot(Index, FMetamorphosisItemSlot());
					++ClearedCount;
				}
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 进第二天④背包：保留剧情道具 %d 件，清除其它 %d 件"), KeptCount, ClearedCount);
			}

			if (bResetPlayerLocation)
			{
				Beetle->TeleportToLoopStart(LoopStartLocation);
				Beetle->ResetCameraYawToSpawn();
			}
		}
	}

	// ⑤-0【关键】播剧情之前必须先恢复"游戏中"状态 ✗✗
	// 2026-10-19 实测：从【暂停菜单】点「下一周目」时游戏是暂停的（SetGameplayPaused(true) + GameAndUI
	// + 鼠标可见）-> 剧情虽然显示出来，但 ①剧情子系统"暂停时忽略推进"会把 Advance 挡掉
	// ②鼠标可见 + GameAndUI 会把点击先吃掉 -> 玩家点屏幕不换句。
	// 三条入口（被捉 / 夜晚跳过 / 手动）都走这里，所以统一在播剧情之前恢复。
	if (AMetamorphosisPlayerController* DayPC = FindLocalController())
	{
		DayPC->HideSettings();      // 先收设置，否则关闭暂停菜单会被"设置必暂停"的三道锁拒绝
		DayPC->ClosePauseMenu();
		DayPC->SetGameplayPaused(false);
		DayPC->bShowMouseCursor = false;
		FInputModeGameOnly GameInput;
		DayPC->SetInputMode(GameInput);
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 进第二天⑤前：已解除暂停并恢复游戏输入（Reason=%s）"), *Reason);
	}

	// ⑤ 剧情：**先把"当前生效的玩法门"记下来**，重置后要把它重新布防 ✓
	//   2026-10-19 用户报的 bug：第二天任务还显示「离婴儿床远一点」（门任务）✓，但门被 ResetForNewRun 清掉了 ✗
	//   -> 只有任务文字、没有门 -> 倒计时永远不完成 ✗。
	//   语义上"任务继承"成立的前提是"它依赖的门也必须恢复"✓，否则就是半截状态 ✗。
	if (UMetamorphosisStorySubsystem* Story = UGameInstance::GetSubsystem<UMetamorphosisStorySubsystem>(GetGameInstance()))
	{
		FName CarryGateNode = NAME_None;

		// ① 门正在激活（玩家暂停在门里）-> 直接用它自己的节点 Id ✓ 最准
		if (Story->IsGateActive())
		{
			CarryGateNode = Story->GetGateNodeId();
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Loop] 进第二天⑤：重置前检测到玩法门正在激活（节点 %s）-> 待会重新布防"), *CarryGateNode.ToString());
		}

		// ② 兜底：门没激活（例如玩家还没走到 / 已被判失败）但"当前任务"本身是门任务 ✓
		//    -> 用配表里 ObjectiveText 与当前任务标题相同的门节点反查（当前配表的 Open20 ↔ Task02 就是这样对上的）
		if (CarryGateNode.IsNone())
		{
			if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
			{
				const FString TitleStr = Tasks->GetCurrentTitle().ToString().TrimStartAndEnd();
				UMetamorphosisConfigSubsystem* Cfg = (GetGameInstance() != nullptr)
					? GetGameInstance()->GetSubsystem<UMetamorphosisConfigSubsystem>()
					: nullptr;
				if (Cfg != nullptr && !TitleStr.IsEmpty())
				{
					for (const FStoryNodeConfig& Node : Cfg->GetStory())
					{
						if (!Node.HasGate())
						{
							continue;
						}
						const FString Objective = Node.ObjectiveText.TrimStartAndEnd();
						if (!Objective.IsEmpty() && Objective.Equals(TitleStr, ESearchCase::IgnoreCase))
						{
							CarryGateNode = FName(Node.NodeId);
							UE_LOG(LogMetamorphosis, Warning,
								TEXT("[Loop] 进第二天⑤：当前任务「%s」与玩法门节点 %s 的目标文字一致 -> 待会重新布防"),
								*TitleStr, *Node.NodeId);
							break;
						}
					}
				}
			}
		}

		Story->ResetForNewRun();

		// 有门要恢复 -> **先播日开端、播完再重新布防门**
		//   2026-10-19 用户反馈：带门任务时"少了一段周目切换的剧情" ✗（之前为防两条剧情打架，直接跳过了 DayStart01）。
		//   串联用 Story->PlayStoryThen(NodeId, OnDone)：EndStory 会先把 bPlaying 置 false 再执行回调，
		//   所以回调里再 StartStoryRestored 是安全的（不会撞"剧情正在播"）。
		if (!CarryGateNode.IsNone())
		{
			if (DayStartNode.IsEmpty())
			{
				if (Story->StartStoryRestored(CarryGateNode))
				{
					UE_LOG(LogMetamorphosis, Warning,
						TEXT("[Loop] 进第二天⑤：当前任务是玩法门（节点 %s）-> 未配日开端，直接重新布防该门（倒计时从 0）"),
						*CarryGateNode.ToString());
				}
			}
			else
			{
				const FName GateToRestore = CarryGateNode;
				UMetamorphosisStorySubsystem* StoryPtr = Story;
				const bool bChained = Story->PlayStoryThen(FName(DayStartNode), [StoryPtr, GateToRestore]()
				{
					if (StoryPtr == nullptr)
					{
						return;
					}
					if (StoryPtr->StartStoryRestored(GateToRestore))
					{
						UE_LOG(LogMetamorphosis, Warning,
							TEXT("[Loop] 进第二天⑤：日开端播完 -> 重新布防玩法门 %s（倒计时从 0）"),
							*GateToRestore.ToString());
					}
					else
					{
						UE_LOG(LogMetamorphosis, Warning,
							TEXT("[Loop] 进第二天⑤：日开端播完，但重新布防玩法门 %s 失败（配表里该节点可能已不再是门）"),
							*GateToRestore.ToString());
					}
				});

				if (bChained)
				{
					UE_LOG(LogMetamorphosis, Warning,
						TEXT("[Loop] 进第二天⑤：先播日开端 %s（带门任务：%s -> 播完再重新布防该门）"),
						*DayStartNode, *CarryGateNode.ToString());
				}
				else
				{
					// 日开端播不了（配表没配 / 配表未就绪）-> 退化成直接恢复门，绝不能把门丢了 ✗
					if (Story->StartStoryRestored(CarryGateNode))
					{
						UE_LOG(LogMetamorphosis, Warning,
							TEXT("[Loop] 进第二天⑤：日开端 %s 播不了 -> 退化为直接重新布防玩法门 %s"),
							*DayStartNode, *CarryGateNode.ToString());
					}
				}
			}
		}
		else if (DayStartNode.IsEmpty())
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 进第二天⑤剧情：DayStartStoryNode 未配置，不播日开端剧情"));
		}
		else if (Story->StartStory(FName(DayStartNode)))
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 进第二天⑤剧情：播日开端 %s（不播开场 Open01）"), *DayStartNode);
		}
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] === 进入第二天完成（%s）：第 %d 周目 ==="), *Reason, LoopCount + 1);
}

// ---------------------------------------------------------------------------
// 通关（2026-10-18）
// ---------------------------------------------------------------------------

FString UMetamorphosisLoopSubsystem::GetWinInfoText() const
{
	return FString::Printf(
		TEXT("已通关=%d 通关界面=%d 来源=%s 剧情节点=%s 必需道具=%s 冻结游戏=%d 本周目=%d"),
		bWon ? 1 : 0,
		bWinEndingShown ? 1 : 0,
		WinSource.IsEmpty() ? TEXT("(无)") : *WinSource,
		WinStoryNode.IsNone() ? TEXT("(不播剧情)") : *WinStoryNode.ToString(),
		WinRequiredItems.Num() > 0 ? *FString::Join(WinRequiredItems, TEXT(",")) : TEXT("(无)"),
		bFreezeGameOnWin ? 1 : 0,
		LoopCount + 1);
}

bool UMetamorphosisLoopSubsystem::RequestWin(const FString& Source)
{
	if (bWon)
	{
		if (bLogLoop)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 忽略通关请求（%s）：已经通关过了"), *Source);
		}
		return false;
	}

	if (bActive || Phase != ELoopPhase::Idle)
	{
		if (bLogLoop)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 忽略通关请求（%s）：正在周目结算（阶段=%s）"),
				*Source, *GetPhaseText());
		}
		return false;
	}

	// 通关必需道具（默认空 = 不需要任何道具；原剧情的蜘蛛/大雪/强风分支已按用户要求删除）
	if (WinRequiredItems.Num() > 0)
	{
		UMetamorphosisInventoryComponent* Inventory = nullptr;
		if (AMetamorphosisPlayerController* PC = FindLocalController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Inventory = Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>();
			}
		}

		TArray<FString> Missing;
		for (const FString& Raw : WinRequiredItems)
		{
			const FName ItemId(*Raw.TrimStartAndEnd());
			if (ItemId.IsNone())
			{
				continue;
			}

			// 背包是固定 8 格，直接逐格找（Inventory 没有 HasItem，别为此加一个新 API）
			bool bFound = false;
			if (Inventory != nullptr)
			{
				for (int32 SlotIndex = 0; SlotIndex < UMetamorphosisInventoryComponent::NumSlots; ++SlotIndex)
				{
					const FMetamorphosisItemSlot Slot = Inventory->GetSlot(SlotIndex);
					if (Slot.bOccupied && Slot.ItemId == ItemId && Slot.Count > 0)
					{
						bFound = true;
						break;
					}
				}
			}

			if (!bFound)
			{
				Missing.Add(ItemId.ToString());
			}
		}

		if (Missing.Num() > 0)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 拒绝通关（%s）：缺少必需道具 %s"),
				*Source, *FString::Join(Missing, TEXT(",")));
			return false;
		}
	}

	bWon = true;
	WinSource = Source;

	UE_LOG(LogMetamorphosis, Warning, TEXT("=== [Loop] 通关（第 %d 周目）：来源=%s ==="),
		LoopCount + 1, *Source);

	// 妻子收手（停追逐/清目标/回巡视），避免通关瞬间还被判定"被捉"
	if (UMetamorphosisWifeSubsystem* Wife = UMetamorphosisWifeSubsystem::Get(this))
	{
		Wife->NotifyLoopReset();
	}

	// 通关剧情：文本阶段本身就会屏蔽输入（bStoryPlaying），所以这里**先不暂停**，
	// 否则剧情点不动（Advance 在暂停时会被忽略 —— 上一轮刚加的规则）。
	// 剧情节点的 Effect=Win 只会标记待通关，真正显示界面是在剧情结束（EndStory）时。
	if (!WinStoryNode.IsNone())
	{
		if (UMetamorphosisStorySubsystem* Story = UGameInstance::GetSubsystem<UMetamorphosisStorySubsystem>(GetGameInstance()))
		{
			// 通关时若还有别的剧情在播（例如开场还没读完就去钻门缝），先收掉再播通关剧情 ——
			// 否则 StartStory 会被"已有剧情在播"挡掉，最后一段通关文本就播不出来。
			if (Story->IsPlaying())
			{
				UE_LOG(LogMetamorphosis, Log,
					TEXT("[Loop] 通关时先收掉进行中的剧情，再播通关剧情（%s）"), *WinStoryNode.ToString());
				Story->StopStory();
			}

			if (Story->StartStory(WinStoryNode))
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 通关剧情已开始：%s（读完最后一句就会显示通关界面）"),
					*WinStoryNode.ToString());
				return true;
			}
		}

		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Loop] 通关剧情节点 %s 在配表里找不到，直接显示通关界面"), *WinStoryNode.ToString());
	}

	return ShowWinEnding(Source);
}

bool UMetamorphosisLoopSubsystem::ShowWinEnding(const FString& Source)
{
	if (bWinEndingShown)
	{
		return false;
	}

	// 从"剧情 Effect=Win"这条路进来时 bWon 还没置位
	if (!bWon)
	{
		bWon = true;
		WinSource = Source;
		UE_LOG(LogMetamorphosis, Warning, TEXT("=== [Loop] 通关（第 %d 周目）：来源=%s ==="),
			LoopCount + 1, *Source);

		if (UMetamorphosisWifeSubsystem* Wife = UMetamorphosisWifeSubsystem::Get(this))
		{
			Wife->NotifyLoopReset();
		}
	}

	bWinEndingShown = true;

	// 结算数据行：第 N 周目 · 怀疑值 X · 当天 H:MM（拿不到就省略）
	FString StatsLine = FString::Printf(TEXT("第 %d 周目"), LoopCount + 1);
	if (const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		const int32 TotalMinutes = FMath::RoundToInt(Stats->GetDayMinutes());
		StatsLine += FString::Printf(TEXT(" · 怀疑值 %.0f · 当天 %02d:%02d"),
			Stats->GetSuspicion(), TotalMinutes / 60, TotalMinutes % 60);
	}

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("=== [Loop] 通关界面（%s）：%s｜%s｜%s｜冻结游戏=%d ==="),
		*Source, *WinTitle.ToString(), *WinSubtitle.ToString(), *StatsLine, bFreezeGameOnWin ? 1 : 0);

	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		PC->ShowWinEnding(WinTitle, WinSubtitle, FText::FromString(StatsLine), bFreezeGameOnWin);
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 找不到玩家控制器：通关界面没有显示出来"));
	}

	return true;
}

void UMetamorphosisLoopSubsystem::RestartAfterWin()
{
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 通关界面：玩家选择「重新开始」"));

	// 先收起界面、解除冻结，再走完整重置（和结算的 ApplyReset 是同一份逻辑）
	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		PC->HideWinEnding();
	}

	bWon = false;
	bWinEndingShown = false;
	WinSource.Reset();
	Phase = ELoopPhase::Idle;
	bActive = false;
	LoopCount++;
	CooldownRemaining = CooldownSeconds;

	ApplyFullLoopReset(TEXT("通关后重新开始"));

	UE_LOG(LogMetamorphosis, Warning, TEXT("=== [Loop] 重新开始：现在是第 %d 周目，冷却 %.2fs ==="),
		LoopCount + 1, CooldownSeconds);
}

void UMetamorphosisLoopSubsystem::ReturnToTitleAfterWin()
{
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 通关界面：玩家选择「返回标题」"));

	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		PC->ReturnToTitleFromEnding();
	}
}

// ---------------------------------------------------------------------------
// 结算界面：玩家选择（2026-10-19）
// ---------------------------------------------------------------------------

bool UMetamorphosisLoopSubsystem::ChooseSettlementRestart()
{
	if (!bActive || Phase != ELoopPhase::AwaitChoice)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Loop] 忽略「重新开始」：当前不在等待选择阶段（阶段=%s）"), *GetPhaseText());
		return false;
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("=== [Loop] 结算界面：玩家选择「重新开始」 ==="));

	// 收掉"等待选择"时的 UI 输入态，进入原有 ApplyReset -> Resume 流程（重置 + 恢复 + 周目 +1 + 冷却）
	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		PC->SetSettlementChoiceInput(false);
	}

	EnterPhase(ELoopPhase::ApplyReset);
	return true;
}

// ---------------------------------------------------------------------------
// 手动跳周目 / 新的一天（2026-10-19 用户要求）
// ---------------------------------------------------------------------------

bool UMetamorphosisLoopSubsystem::RequestManualNextLoop()
{
	// 结算中 / 通关界面显示中时不允许手动跳周目（会被这两套流程覆盖，状态会乱）
	if (IsSettling() || IsWinEndingVisible())
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Loop] 忽略「下一周目」：正在结算/通关界面（阶段=%s，结算中=%d）"),
			*GetPhaseText(), IsSettling() ? 1 : 0);
		return false;
	}

	LoopCount++;

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("=== [Loop] 玩家从设置界面手动跳到下一周目（现在是第 %d 周目）==="), LoopCount + 1);

	// 不播被捉反馈，直接走完整周目重置（数值/任务/背包/剧情/位置与"被捉结算后"完全一致）
	BeginNextDay(TEXT("暂停菜单：下一周目"));

	return true;
}

void UMetamorphosisLoopSubsystem::IncrementLoopCountForNewDay(const FString& Reason)
{
	LoopCount++;

	// 只加计数：时间已经跳到 06:00、玩家还在原地 —— 绝不能在这里做重置（会把玩家传回出生点 + 清背包）
	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Loop] 夜晚跳过结束 -> 新的一天（%s），现在是第 %d 周目（只加计数，不重置）"),
		*Reason, LoopCount + 1);
}

bool UMetamorphosisLoopSubsystem::ChooseSettlementReturnToTitle()
{
	if (!bActive || Phase != ELoopPhase::AwaitChoice)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Loop] 忽略「返回主菜单」：当前不在等待选择阶段（阶段=%s）"), *GetPhaseText());
		return false;
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("=== [Loop] 结算界面：玩家选择「返回主菜单」 ==="));

	// 先把结算状态收干净：**不做** Resume 那套"恢复游戏"，而是直接结束本次结算。
	// 注意：这里**不存档**（"被抓住"的现场不该写进存档 ✗；存档子系统本来也会因为结算中而拒绝 ✓）。
	bActive = false;
	Phase = ELoopPhase::Idle;
	LoopCount++;
	CooldownRemaining = CooldownSeconds;

	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		PC->SetSettlementChoiceInput(false);
		PC->HideSettlementFeedback();       // 收起结算界面 + 恢复 HUD
		PC->SetSettlementActive(false);     // 解除输入冻结
	}

	// 走一次完整重置：回到主界面后玩家再开新游戏时，数值/任务/背包/剧情都是干净的
	ApplyFullLoopReset(TEXT("结算后返回主菜单"));

	if (AMetamorphosisPlayerController* PC = FindLocalController())
	{
		// ReturnToTitleFromEnding 不存档、会清 Preload 的 bEntered、挂开始界面并保持暂停 ✓
		PC->ReturnToTitleFromEnding();
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 已返回主界面（第 %d 次结算完成，冷却 %.2fs）"), LoopCount, CooldownSeconds);
	return true;
}



// ---------------------------------------------------------------------------
// 阶段推进（真实时间）
// ---------------------------------------------------------------------------

bool UMetamorphosisLoopSubsystem::TickSettlement(float DeltaTime)
{
	if (bActive)
	{
		PhaseTimer += DeltaTime;

		switch (Phase)
		{
		case ELoopPhase::Freeze:
			if (PhaseTimer >= FreezeSeconds)
			{
				EnterPhase(ELoopPhase::Feedback);
			}
			break;

		case ELoopPhase::Feedback:
			if (PhaseTimer >= FeedbackSeconds)
			{
				EnterPhase(ELoopPhase::AwaitChoice);
			}
			break;

		case ELoopPhase::AwaitChoice:
			// 停在这里等玩家点按钮；只有兜底超时才自动走「重新开始」
			if (AwaitChoiceTimeoutSeconds > 0.f && PhaseTimer >= AwaitChoiceTimeoutSeconds)
			{
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Loop] 等待选择超时（%.1f 秒）-> 自动重新开始"), AwaitChoiceTimeoutSeconds);
				ChooseSettlementRestart();
			}
			break;

		case ELoopPhase::ApplyReset:
			if (PhaseTimer >= ResetSeconds)
			{
				EnterPhase(ELoopPhase::Resume);
			}
			break;

		default:
			// Resume 在 EnterPhase 里已经一步做完并回到 Idle
			break;
		}
	}
	else if (CooldownRemaining > 0.f)
	{
		CooldownRemaining = FMath::Max(0.f, CooldownRemaining - DeltaTime);
	}

	// 读档保护窗口倒计时（2026-10-19）✓：
	// 注意它不能走上面那条 else-if（结算/冷却时也要递减 ✓，否则读档正好撞上冷却就永远不解除 ✗）。
	if (SuppressResetRemaining > 0.f)
	{
		const float Before = SuppressResetRemaining;
		SuppressResetRemaining = FMath::Max(0.f, SuppressResetRemaining - DeltaTime);
		if (Before > 0.f && SuppressResetRemaining <= 0.f)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 读档保护窗口结束，结算请求恢复正常"));
		}
	}

	// 结算与冷却都结束 -> 摘掉计时器
	return bActive || CooldownRemaining > 0.f || SuppressResetRemaining > 0.f;
}

void UMetamorphosisLoopSubsystem::SuppressLoopReset(float Seconds, const FString& Reason)
{
	if (Seconds <= 0.f)
	{
		return;
	}
	// 取较大值：连续读档时不会把窗口缩短 ✓
	SuppressResetRemaining = FMath::Max(SuppressResetRemaining, Seconds);
	if (bLogLoop)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Loop] 开启读档保护窗口：%.1f 秒内忽略所有结算请求（原因=%s）"), Seconds, *Reason);
	}
}

// ---------------------------------------------------------------------------
// 调试命令（无头验证用）
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
namespace MetamorphosisLoopConsole
{
	static UMetamorphosisLoopSubsystem* Sub(UWorld* World)
	{
		return UMetamorphosisLoopSubsystem::Get(World);
	}

	static void Caught(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			Sys->RequestLoopEnd(ELoopEndReason::Caught);
		}
	}

	static void NightEnd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			Sys->RequestLoopEnd(ELoopEndReason::NightEnds);
		}
	}

	static void Info(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisLoopSubsystem* Sys = Sub(World);
		if (Sys == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 拿不到周目结算子系统"));
			return;
		}

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Loop] 结算中=%d 阶段=%s 已结算=%d 次（当前第 %d 周目）上次原因=%s 冷却=%.2fs 重生点=%s"),
			Sys->IsSettling() ? 1 : 0, *Sys->GetPhaseText(), Sys->GetLoopCount(),
			Sys->GetLoopCount() + 1,
			*Sys->GetReasonText(Sys->GetLastReason()), Sys->GetCooldownRemaining(),
			Sys->bResetPlayerLocation ? *Sys->LoopStartLocation.ToCompactString() : TEXT("不改位置"));

		if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(World != nullptr ? World->GetFirstPlayerController() : nullptr))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 控制器：结算中=%d（输入应被冻结）"),
				PC->IsInSettlement() ? 1 : 0);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopCaught(
		TEXT("Meta.Loop.Caught"),
		TEXT("强制走一次「被捉」周目结算（冻结 -> 反馈 -> 重置 -> 恢复）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Caught));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopNightEnd(
		TEXT("Meta.Loop.NightEnd"),
		TEXT("强制走一次「入夜」周目结算（自动触发还没接，见文档 TODO）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NightEnd));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopInfo(
		TEXT("Meta.Loop.Info"),
		TEXT("打印周目结算状态（是否结算中/阶段/次数/上次原因/冷却/重生点）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Info));

	/** Meta.Loop.Win：直接通关（跳过"走到缝隙"那一步，供无头验证与调试） */
	static void Win(const TArray<FString>& Args, UWorld* World)	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			const FString Source = Args.Num() > 0 ? Args[0] : TEXT("调试命令 Meta.Loop.Win");
			Sys->RequestWin(Source);
		}
	}

	/** Meta.Loop.WinEnding：跳过剧情，直接显示通关界面（验证界面本身） */
	static void WinEnding(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			Sys->ShowWinEnding(TEXT("调试命令 Meta.Loop.WinEnding"));
		}
	}

	/** Meta.Loop.WinInfo：打印通关状态 */
	static void WinInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisLoopSubsystem* Sys = Sub(World);
		if (Sys == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 拿不到周目结算子系统"));
			return;
		}

		UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 通关状态：%s"), *Sys->GetWinInfoText());

		if (const APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr)
		{
			if (const AMetamorphosisPlayerController* Mpc = Cast<AMetamorphosisPlayerController>(PC))
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Loop] 控制器：结算中=%d 输入被屏蔽=%d 游戏暂停=%d"),
					Mpc->IsInSettlement() ? 1 : 0, Mpc->IsInputBlocked() ? 1 : 0,
					UGameplayStatics::IsGamePaused(World) ? 1 : 0);
			}
		}
	}

	/** Meta.Loop.Restart：模拟点"重新开始" */
	static void Restart(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			Sys->RestartAfterWin();
		}
	}

	/** Meta.Loop.Title：模拟点"返回标题" */
	static void Title(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			Sys->ReturnToTitleAfterWin();
		}
	}

	/** Meta.Loop.Next：手动跳到下一个周目（等同设置面板里的「下一周目」按钮） */
	static void Next(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisLoopSubsystem* Sys = Sub(World))
		{
			Sys->RequestManualNextLoop();
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopWin(
		TEXT("Meta.Loop.Win"),
		TEXT("直接通关（优先播通关剧情 WinStoryNode，播不了就直接显示通关界面）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Win));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopNext(
		TEXT("Meta.Loop.Next"),
		TEXT("手动跳到下一个周目（完整重置，不播被捉反馈；等同设置面板「下一周目」按钮）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Next));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopWinEnding(
		TEXT("Meta.Loop.WinEnding"),
		TEXT("跳过剧情，直接显示通关界面（验证界面本身）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WinEnding));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopWinInfo(
		TEXT("Meta.Loop.WinInfo"),
		TEXT("打印通关状态（是否通关/界面/来源/剧情节点/必需道具）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WinInfo));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopRestart(
		TEXT("Meta.Loop.Restart"),
		TEXT("模拟点通关界面的「重新开始」"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Restart));

	static FAutoConsoleCommandWithWorldAndArgs GMetaLoopTitle(
		TEXT("Meta.Loop.Title"),
		TEXT("模拟点通关界面的「返回标题」"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Title));

	// -----------------------------------------------------------------------
	// Meta.SelfTest.NextDay <caught|night|manual>
	//   进关卡 3 秒后自动跑一遍"进第二天"：发道具 -> 前快照 -> 触发 -> 后快照 + 逐项断言。
	//   用途：解决"命令跑得比关卡初始化早"的问题（用 SetTimer 而不是 Meta.Delay）。
	// -----------------------------------------------------------------------
	namespace NextDayTest
	{
		struct FSnapshot
		{
			int32 LoopCount = 0;
			float DayMinutes = 0.f;
			float Suspicion = 0.f;
			float Mood = 0.f;
			FString Position;
			TArray<FString> Items;   // "ItemId x3"
			bool bHasMedicationList = false;
			bool bHasNoteOnFoil = false;
			bool bHasCereal = false;
			int32 TaskIndex = -1;       // 第二天要"继承上一天"，所以快照里必须记任务索引
			FString TaskTitle;
		};

		static UMetamorphosisLoopSubsystem* Loop(UWorld* World)
		{
			return UMetamorphosisLoopSubsystem::Get(World);
		}

		static UMetamorphosisInventoryComponent* Inv(UWorld* World)
		{
			if (World == nullptr)
			{
				return nullptr;
			}
			if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController()))
			{
				return PC->GetInventoryComponent();
			}
			return nullptr;
		}

		static FSnapshot Take(UWorld* World)
		{
			FSnapshot S;
			if (UMetamorphosisLoopSubsystem* L = Loop(World))
			{
				S.LoopCount = L->GetLoopCount();
			}
			if (UMetamorphosisStatsSubsystem* St = UMetamorphosisStatsSubsystem::Get(World))
			{
				S.DayMinutes = St->GetDayMinutes();
				S.Suspicion = St->GetSuspicion();
				S.Mood = St->GetMood();
			}
			if (UMetamorphosisTaskSubsystem* T = UMetamorphosisTaskSubsystem::Get(World))
			{
				S.TaskIndex = T->GetCurrentIndex();
				S.TaskTitle = T->GetCurrentTitle().ToString();
			}
			if (World != nullptr)
			{
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					if (const APawn* Pawn = PC->GetPawn())
					{
						const FVector P = Pawn->GetActorLocation();
						S.Position = FString::Printf(TEXT("V(%.0f,%.0f,%.0f)"), P.X, P.Y, P.Z);
					}
				}
			}
			if (UMetamorphosisInventoryComponent* I = Inv(World))
			{
				for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
				{
					const FMetamorphosisItemSlot Slot = I->GetSlot(Index);
					if (!Slot.bOccupied)
					{
						continue;
					}
					S.Items.Add(FString::Printf(TEXT("%s x%d"), *Slot.ItemId.ToString(), Slot.Count));
					if (Slot.ItemId == TEXT("MedicationList")) { S.bHasMedicationList = true; }
					if (Slot.ItemId == TEXT("NoteOnFoil")) { S.bHasNoteOnFoil = true; }
					if (Slot.ItemId == TEXT("Cereal")) { S.bHasCereal = true; }
				}
			}
			return S;
		}

		static void Print(UWorld* World, const TCHAR* Label)
		{
			const FSnapshot S = Take(World);
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[SelfTest] %s：周目=%d 时间=%.0f 怀疑=%.1f 躁郁=%.1f 位置=%s 背包=[%s]"),
				Label, S.LoopCount, S.DayMinutes, S.Suspicion, S.Mood, *S.Position,
				S.Items.Num() > 0 ? *FString::Join(S.Items, TEXT(", ")) : TEXT("空"));
		}

		static void Step(UWorld* World, const FString& Mode, int32 StepIndex, const FSnapshot& Before);

		static void Schedule(UWorld* /*World*/, const FString& Mode, int32 StepIndex, const FSnapshot& Before, float Delay)
		{
			// ⚠️ 不能用 World->GetTimerManager()：-ExecCmds 通常在主菜单世界执行，
			//    换关卡（OpenLevel）时世界摧毁 -> 定时器一起没了 ✗。
			//    改用 FTSTicker：跨关卡存活，触发时用 GWorld 取"当前"世界 ✓。
			TSharedPtr<float> Elapsed = MakeShared<float>(0.f);
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([Mode, StepIndex, Before, Delay, Elapsed](float DeltaSeconds) -> bool
				{
					*Elapsed += DeltaSeconds;
					if (*Elapsed < Delay)
					{
						return true;   // 继续等
					}
					if (UWorld* W = GWorld)
					{
						UE_LOG(LogMetamorphosis, Warning,
							TEXT("[SelfTest] 定时器触发（步骤 %d，等了 %.1f 秒，当前世界 %s）"),
							StepIndex, *Elapsed, *W->GetName());
						Step(W, Mode, StepIndex, Before);
					}
					return false;      // 完成，移除 ticker
				}));
		}

		/** Step 0：发道具 + 前快照 + 触发 */
		static void Step(UWorld* World, const FString& Mode, int32 StepIndex, const FSnapshot& BeforeIn)
		{
			if (World == nullptr || GEngine == nullptr)
			{
				return;
			}

			if (StepIndex == 0)
			{
				GEngine->Exec(World, TEXT("Meta.Inv.Give Cereal 2"));
				GEngine->Exec(World, TEXT("Meta.Inv.Give MedicationList 1"));
				GEngine->Exec(World, TEXT("Meta.Inv.Give NoteOnFoil 1"));
				// 数值抬高：原本 0 的话"各 -20"会被钳到 0，看不出效果 ✗
				GEngine->Exec(World, TEXT("Meta.Suspicion.Set 80"));
				GEngine->Exec(World, TEXT("Meta.Mood.Set 80"));
				// 用户确认的语义："上一天做到哪里，第二天就是哪里" -> 先把任务推到第 4 条，用来验证跨天继承
				GEngine->Exec(World, TEXT("Meta.Task.Set Task04"));
				Print(World, TEXT("发道具后"));
				const FSnapshot Before = Take(World);

				if (Mode == TEXT("caught"))
				{
					GEngine->Exec(World, TEXT("Meta.Loop.Caught"));
					Schedule(World, Mode, 1, Before, 4.f);   // 等结算进入"等待选择"
				}
				else if (Mode == TEXT("night"))
				{
					// ⚠️ 必须先停掉开场剧情：夜晚跳过按设计要等"别的剧情播完"才启动（时间会停在 23:00 等 ✗）
					GEngine->Exec(World, TEXT("Meta.Story.Stop"));
					GEngine->Exec(World, TEXT("Meta.Time.Set 1379"));
					Schedule(World, Mode, 1, Before, 4.f);   // 等走到 23:00 触发夜晚跳过
				}
				else
				{
					GEngine->Exec(World, TEXT("Meta.Loop.Next"));
					Schedule(World, Mode, 2, Before, 3.f);   // 直接收尾断言
				}
				return;
			}

			if (StepIndex == 1)
			{
				if (Mode == TEXT("caught"))
				{
					GEngine->Exec(World, TEXT("Meta.Settlement.Press Restart"));
					Schedule(World, Mode, 2, BeforeIn, 3.f);
				}
				else // night：把夜晚跳过剧情推完（多推几次，打字机/分页都能过）
				{
					for (int32 i = 0; i < 8; ++i)
					{
						GEngine->Exec(World, TEXT("Meta.Story.Advance"));
					}
					Schedule(World, Mode, 2, BeforeIn, 3.f);
				}
				return;
			}

			// Step 2：后快照 + 断言
			Print(World, TEXT("进第二天后"));
			const FSnapshot After = Take(World);

			auto Check = [](const TCHAR* Name, bool bOk, const FString& Expect, const FString& Actual)
			{
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[SelfTest] %s：%s（期望 %s / 实际 %s）"),
					bOk ? TEXT("✓ 通过") : TEXT("✗ 不符"), Name, *Expect, *Actual);
			};

			Check(TEXT("周目 +1"), After.LoopCount == BeforeIn.LoopCount + 1,
				FString::Printf(TEXT("%d"), BeforeIn.LoopCount + 1), FString::Printf(TEXT("%d"), After.LoopCount));
			// ⚠️ 断言要允许"重置后又过了几秒"：时钟会继续走（2.4 游戏分钟/现实秒），所以时间给 ±30 分钟容差；
			//    怀疑/躁郁会被钳在 0~100，所以期望值取 Clamp(Before-20, 0, 100)，并给 ±3 容差（躁郁会随腿伤微涨）。
			// ⚠️ 容差说明（2026-10-19 实测）：
			//   * 时间：重置后时钟继续走（2.4 游戏分钟/现实秒）-> ±30 分钟；
			//   * 怀疑：被捉时会被拉到 100，且整个流程（结算 2.2s + 等选择 4s + 剧情 3s）期间按 3/秒衰减，
			//     所以"80-20=60"的期望值只对"手动/夜晚"成立，被捉路径会明显更低 -> 用 ±25 的宽容差，
			//     真正要保证的是"**减了 20 而不是清零**"，这一点看 Baseline-20 与 0 的差距即可。
			const float ExpectSuspicion = FMath::Clamp(BeforeIn.Suspicion - 20.f, 0.f, 100.f);
			const float ExpectMood = FMath::Clamp(BeforeIn.Mood - 20.f, 0.f, 100.f);
			Check(TEXT("时间回 06:00"), FMath::Abs(After.DayMinutes - 360.f) <= 30.f,
				TEXT("约 360（±30）"), FString::Printf(TEXT("%.0f"), After.DayMinutes));
			Check(TEXT("怀疑 -20"), After.Suspicion < BeforeIn.Suspicion && FMath::Abs(After.Suspicion - ExpectSuspicion) <= 25.f,
				FString::Printf(TEXT("约 %.1f（且必须 < 前一天）"), ExpectSuspicion), FString::Printf(TEXT("%.1f"), After.Suspicion));
			Check(TEXT("躁郁 -20"), FMath::Abs(After.Mood - ExpectMood) <= 3.f,
				FString::Printf(TEXT("约 %.1f"), ExpectMood), FString::Printf(TEXT("%.1f"), After.Mood));
			Check(TEXT("碎片 MedicationList 保留"), After.bHasMedicationList, TEXT("在"), After.bHasMedicationList ? TEXT("在") : TEXT("丢了"));
			Check(TEXT("碎片 NoteOnFoil 保留"), After.bHasNoteOnFoil, TEXT("在"), After.bHasNoteOnFoil ? TEXT("在") : TEXT("丢了"));
			Check(TEXT("非碎片 Cereal 被清"), !After.bHasCereal, TEXT("已清"), After.bHasCereal ? TEXT("还在") : TEXT("已清"));
			Check(TEXT("位置回出生点"), After.Position.Contains(TEXT("-320")) && After.Position.Contains(TEXT("-630")),
				TEXT("V(-320,-630,...)"), After.Position);
			// 用户确认："第二天的任务应该继承上一天"（不重置、不跳跃）
			Check(TEXT("任务继承上一天"), After.TaskIndex == BeforeIn.TaskIndex,
				FString::Printf(TEXT("第 %d 条 %s"), BeforeIn.TaskIndex + 1, *BeforeIn.TaskTitle),
				FString::Printf(TEXT("第 %d 条 %s"), After.TaskIndex + 1, *After.TaskTitle));

			UE_LOG(LogMetamorphosis, Warning, TEXT("=== [SelfTest] 进第二天自检结束（模式=%s）==="), *Mode);
		}
	}

	static void SelfTestNextDay(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		const FString Mode = (Args.Num() > 0) ? Args[0].ToLower() : TEXT("manual");
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("=== [SelfTest] 进第二天自检：模式=%s，等待进入游戏关卡后开始（在菜单会自动点「新游戏」）==="), *Mode);

		// 2026-10-19：无头跑时命令往往在主菜单就执行了（那时没有 Pawn/背包，断言全是假的 ✗）。
		// 这里先等"有 Pawn 的游戏世界"：在菜单就自动点「新游戏」，最多等 180 秒。
		struct FLocal
		{
			/** 返回 true = 继续轮询；false = 结束（已开始自检或超时） */
			static bool Poll(const FString& InMode, TSharedPtr<float> Elapsed)
			{
				UWorld* W = GWorld;
				if (W == nullptr || GEngine == nullptr)
				{
					return *Elapsed <= 180.f;
				}

				if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(W->GetFirstPlayerController()))
				{
					// ⚠️ 主菜单世界也有一台 Pawn（相机）✗ -> 不能用"有没有 Pawn"判断，必须看是不是游戏关卡。
					const bool bInGameMap = !W->GetMapName().Contains(TEXT("MainMenu"), ESearchCase::IgnoreCase);
					if (bInGameMap && PC->GetPawn() != nullptr)
					{
						UE_LOG(LogMetamorphosis, Warning,
							TEXT("[SelfTest] 已进入游戏关卡（%s，等了 %.1f 秒）-> 3 秒后开始自检"),
							*W->GetName(), *Elapsed);
						NextDayTest::Schedule(W, InMode, 0, NextDayTest::FSnapshot(), 3.f);
						return false;
					}
				}

				// 还在菜单：每 5 秒点一次「新游戏」（预加载期间重复点会被忽略）
				static float LastStartTry = -10.f;
				if (*Elapsed - LastStartTry > 5.f)
				{
					LastStartTry = *Elapsed;
					GEngine->Exec(W, TEXT("Meta.Menu.Start"));
				}

				if (*Elapsed > 180.f)
				{
					UE_LOG(LogMetamorphosis, Error, TEXT("[SelfTest] 等了 180 秒还没进入游戏关卡，放弃 ✗"));
					return false;
				}
				return true;
			}
		};

		TSharedPtr<float> Elapsed = MakeShared<float>(0.f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Mode, Elapsed](float DeltaSeconds) -> bool
			{
				*Elapsed += DeltaSeconds;
				return FLocal::Poll(Mode, Elapsed);   // false = 结束轮询
			}), 0.5f);
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaSelfTestNextDay(
		TEXT("Meta.SelfTest.NextDay"),
		TEXT("进关卡 3 秒后自动跑一遍「进第二天」并逐项断言。参数：caught | night | manual（默认 manual）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SelfTestNextDay));
}
#endif
