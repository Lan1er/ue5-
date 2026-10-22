// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Story/MetamorphosisStorySubsystem.h"

#include "Config/MetamorphosisConfigSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Interaction/MetamorphosisInteractionComponent.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "MetamorphosisPlayerController.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Tasks/MetamorphosisTaskSubsystem.h"

void UMetamorphosisStorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 配置系统要先就绪（和 Stats/Task 一致的做法）
	Collection.InitializeDependency<UMetamorphosisConfigSubsystem>();

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 剧情系统就绪：自动播开场=%d，周目重播=%d"),
			bAutoPlayOpening ? 1 : 0, bReplayOpeningOnLoop ? 1 : 0);
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Story] 玩法门参数：轮询=%.2f 秒 标签前缀='%s' 网格名 %d 条 坐标兜底 %d 条 外扩=%.0f 低于顶面=%.0f"),
			GatePollIntervalSeconds, *SpotTagPrefix, SpotMeshNames.Num(), SpotFallbacks.Num(),
			SpotEnterInflateCm, SpotBelowTopMarginCm);
		for (const FString& Entry : SpotMeshNames)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story]   网格名条目：%s"), *Entry);
		}
		for (const FString& Entry : SpotFallbacks)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story]   坐标兜底条目：%s"), *Entry);
		}
	}
}

UMetamorphosisStorySubsystem* UMetamorphosisStorySubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}
	if (const UWorld* World = WorldContextObject->GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UMetamorphosisStorySubsystem>();
		}
	}
	return nullptr;
}

UMetamorphosisConfigSubsystem* UMetamorphosisStorySubsystem::GetConfigSubsystem() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UMetamorphosisConfigSubsystem>();
	}
	return nullptr;
}

const FStoryNodeConfig* UMetamorphosisStorySubsystem::GetCurrentNode() const
{
	if (!bPlaying || CurrentNodeId.IsNone())
	{
		return nullptr;
	}
	// 临时文本（剧情道具的回忆文本，2026-10-18）：不查配表，直接用临时节点。
	// 这样打字机/分页/推进/暂停冻结全都沿用同一套代码，一行都不用分叉。
	if (bInlineText)
	{
		return &InlineNode;
	}
	const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem();
	return Config != nullptr ? Config->GetStoryNode(CurrentNodeId) : nullptr;
}

bool UMetamorphosisStorySubsystem::StartStory(FName NodeId)
{
	const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem();
	if (Config == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 配置还没就绪，无法播放 %s"), *NodeId.ToString());
		return false;
	}

	const FStoryNodeConfig* Node = Config->GetStoryNode(NodeId);
	if (Node == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 配表里没有节点 '%s'"), *NodeId.ToString());
		return false;
	}

	// 临时文本（剧情道具回忆）还在播时，先把它收掉：EndStory 会把回调（读完拾取）执行掉，
	// 这样"正读着回忆文本，剧情又来了"不会丢道具、也不会两层过场叠在一起。
	if (bInlineText)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Story] 临时文本还在播，先结束它再开始 %s（读完的回调会照常执行）"), *NodeId.ToString());
		StopStory();
	}

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("=== [Story] 开始播放：%s（章节 %s，配表共 %d 个节点）==="),
			*Node->NodeId, *Node->Scene, Config->GetStory().Num());
	}

	ClearGate();
	bPendingWin = false;   // 新剧情开始：清掉上一段留下的"待通关"标记
	bPlaying = true;
	Phase = EMetamorphosisStoryPhase::Text;
	EnterNode(Node);
	return true;
}

// ---------------------------------------------------------------------------
// 存档恢复（2026-10-19）
// ---------------------------------------------------------------------------
bool UMetamorphosisStorySubsystem::StartStoryRestored(FName NodeId)
{
	const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem();
	if (Config == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 读档恢复剧情失败：配置未就绪（%s）"), *NodeId.ToString());
		return false;
	}

	const FStoryNodeConfig* Node = Config->GetStoryNode(NodeId);
	if (Node == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 读档恢复剧情失败：配表里没有节点 '%s'"), *NodeId.ToString());
		return false;
	}

	// 带玩法门的节点：**恢复**（用户要求"重新进入时倒计时从 0 重新开始、任务不能被跳过"）✓
	// 恢复走的是 StartStoryRestored -> EnterNode -> 重新布防：GateElapsed/GateHoldElapsed 全归零 ✓
	// 之前的"直接不恢复"会让读档后门丢失 -> 倒计时失效 + 任务被跳过 ✗（实测 bug）。为防止"读档瞬间就超时判死"，
	// SaveSubsystem 侧还会开一个 3 秒保护窗口（SuppressLoopReset），且门本身要重新走布防延迟 ✓。
	if (Node->HasGate())
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Story] 读档恢复剧情：节点 %s 是玩法门节点（%s）-> 重新布防，倒计时从 0 开始"),
			*Node->NodeId, *Node->GateKind);
	}

	// 在恢复模式下，进入节点**不结算 Effect** ✗（核心修复）：
	// 否则 Death / Give / Task / Mood 都会再执行一次 ✗（Death 会让读档瞬间触发周目结算 -> 清背包 + 回出生点）。
	if (bInlineText)
	{
		StopStory();
	}

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("=== [Story] 从存档恢复节点：%s（不重跑 Effect）==="), *Node->NodeId);
	}

	ClearGate();
	bPendingWin = false;
	bPlaying = true;
	Phase = EMetamorphosisStoryPhase::Text;

	bSuppressEnterEffectsOnce = true;
	EnterNode(Node);
	bSuppressEnterEffectsOnce = false;   // 只压这一次（后续 Advance 进别的节点照常结算 ✓）
	return true;
}

bool UMetamorphosisStorySubsystem::PlayInlineText(const FText& Speaker, const FText& Text, TFunction<void()> OnDone)
{
	const FString Body = Text.ToString().TrimStartAndEnd();
	if (Body.IsEmpty())
	{
		// 没文本就别弹空对话框（调用方直接走原流程：拾取 / 就地吃）
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 临时文本内容为空，不播放（%s）"), *Speaker.ToString());
		}
		return false;
	}

	if (bPlaying)
	{
		// 不嵌套：已经在播剧情（开场 / 别的临时文本）时拒绝，调用方按"没播成"处理
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Story] 想播临时文本（%s），但已有剧情在播（节点 %s）—— 不嵌套，忽略"),
			*Speaker.ToString(), *CurrentNodeId.ToString());
		return false;
	}

	// 构造临时节点：不进配表，Next 为空 -> 玩家读完最后一段时 Advance 走到 EndStory -> 执行回调
	InlineNode = FStoryNodeConfig();
	InlineNode.NodeId = TEXT("<临时文本>");
	InlineNode.Scene = TEXT("Inline");
	InlineNode.Speaker = Speaker.ToString();
	InlineNode.Text = Body;
	InlineNode.Next.Reset();

	bInlineText = true;
	InlineOnDone = MoveTemp(OnDone);
	bPlaying = true;
	Phase = EMetamorphosisStoryPhase::Text;
	CurrentNodeId = FName(*InlineNode.NodeId);

	// 分页照常（长回忆文本也会自动切页）
	RebuildPages(InlineNode.Text);

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 临时文本（%s）：%s"),
			InlineNode.Speaker.IsEmpty() ? TEXT("无说话人") : *InlineNode.Speaker,
			*InlineNode.Text.Replace(TEXT("\n"), TEXT(" / ")));
	}

	BroadcastCurrent();
	return true;
}

bool UMetamorphosisStorySubsystem::PlayStoryThen(FName NodeId, TFunction<void()> OnDone)
{
	// 先把回调挂上：StartStory 万一内部又走了 EndStory（例如先收掉临时文本），
	// 那次结束不该触发本回调，所以先开始剧情、成功后再挂。
	if (!StartStory(NodeId))
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Story] PlayStoryThen: 节点 '%s' 播不了，跳过后续剧情（调用方立即继续）"), *NodeId.ToString());
		return false;
	}

	// 覆盖旧回调（正常情况下不会有）：保证"一个剧情链只挂一个回调"
	StoryChainOnDone = MoveTemp(OnDone);
	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Story] 剧情链：%s（播完后执行回调=%d）"), *NodeId.ToString(), StoryChainOnDone ? 1 : 0);
	}
	return true;
}

bool UMetamorphosisStorySubsystem::ReplayOpeningForNewLoop()
{
	// 用户要求"每次新周目都全部从头开始"：先清掉"已播过"标志，否则会被 StartOpening 的防重播挡住。
	// 另外先把正在播的剧情停掉（结算时可能还挂在某个门上），避免两道流程叠加。
	const bool bWasPlaying = IsPlaying();
	if (bWasPlaying)
	{
		StopStory();
	}

	bPlayedOpening = false;
	const bool bOk = StartOpening();
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Loop] 新周目：剧情从头开始（重播开场）%s（重播前是否在播=%d）"),
		bOk ? TEXT("已开始") : TEXT("失败：配表里找不到开场节点"), bWasPlaying ? 1 : 0);
	return bOk;
}

bool UMetamorphosisStorySubsystem::StartOpening()
{
	if (bPlayedOpening && !bReplayOpeningOnLoop)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 本局已经播过开场，跳过（周目重播开关=%d）"), bReplayOpeningOnLoop ? 1 : 0);
		return false;
	}

	FName StartNode = OpeningStartNode;
	if (StartNode.IsNone())
	{
		// 没显式指定 -> 取配表里第一条 Scene=Opening 的节点
		if (const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem())
		{
			for (const FStoryNodeConfig& N : Config->GetStory())
			{
				if (N.Scene == TEXT("Opening"))
				{
					StartNode = FName(*N.NodeId);
					break;
				}
			}
		}
	}

	if (StartNode.IsNone())
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 配表里找不到 Scene=Opening 的节点，无法自动播开场"));
		return false;
	}

	bPlayedOpening = true;
	return StartStory(StartNode);
}

void UMetamorphosisStorySubsystem::Advance()
{
	// 暂停时忽略推进：暂停菜单盖在过场之上时，玩家点"继续游戏"以外的位置不应该翻页/推进
	if (IsGameplayPaused(this))
	{
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 游戏暂停中，忽略推进（Advance）"));
		}
		return;
	}

	const FStoryNodeConfig* Node = GetCurrentNode();
	if (Node == nullptr)
	{
		return;
	}

	// 玩法门阶段：正在等玩家在游戏里做事，点屏幕不推进
	if (Phase == EMetamorphosisStoryPhase::Gate)
	{
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 玩法门进行中（%s），请完成目标后再继续"), *GateNodeId.ToString());
		}
		return;
	}

	// 分页：本节点还没读完（还有下一页）就只翻页，**不推进节点**。
	// 放在门/分支判定之前：门要"读完最后一页再点一下"才激活，分支也要读到最后才能选。
	if (Pages.Num() > 1 && PageIndex + 1 < Pages.Num())
	{
		++PageIndex;
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 翻页 -> 第 %d/%d 页（节点 %s）"),
				PageIndex + 1, Pages.Num(), *Node->NodeId);
		}
		BroadcastCurrent();   // 让对话框控件立刻刷新到下一页
		return;
	}

	// 门节点：读完文本点一下 -> 进入玩法门（黑幕收起、回到游戏画面）
	if (Node->HasGate())
	{
		ActivateGate(*Node);
		return;
	}

	if (Node->HasChoice())
	{
		// 分支节点必须选，不允许直接推进（避免玩家误跳过选择）
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] %s 是分支节点，请用 Meta.Story.Choose <A|B> / 点按钮"), *Node->NodeId);
		}
		return;
	}

	const FString NextId = Node->Next;
	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 推进：%s -> %s"), *Node->NodeId,
			NextId.IsEmpty() ? TEXT("(结束)") : *NextId);
	}

	if (NextId.IsEmpty())
	{
		EndStory(TEXT("走完最后一个节点"));
		return;
	}

	const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem();
	const FStoryNodeConfig* Next = Config != nullptr ? Config->GetStoryNode(FName(*NextId)) : nullptr;
	if (Next == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 节点 '%s' 的 Next 指向了不存在的 '%s'，剧情中止"),
			*Node->NodeId, *NextId);
		EndStory(TEXT("Next 指向不存在的节点"));
		return;
	}

	EnterNode(Next);
}

bool UMetamorphosisStorySubsystem::ChooseOption(int32 Index)
{
	// 暂停时忽略：暂停菜单盖在过场之上时，玩家误点在选项上不应该推进剧情
	if (IsGameplayPaused(this))
	{
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 游戏暂停中，忽略选项点击（%d）"), Index);
		}
		return false;
	}

	const FStoryNodeConfig* Node = GetCurrentNode();
	if (Node == nullptr || !Node->HasChoice())
	{
		return false;
	}
	if (Index != 0 && Index != 1)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] ChooseOption 只接受 0(A) / 1(B)，收到 %d"), Index);
		return false;
	}

	const FString ChoiceText = Index == 0 ? Node->ChoiceA_Text : Node->ChoiceB_Text;
	const FString NextId = Index == 0 ? Node->ChoiceA_Next : Node->ChoiceB_Next;

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 选择 %s：%s -> %s"),
			Index == 0 ? TEXT("A") : TEXT("B"), *ChoiceText,
			NextId.IsEmpty() ? TEXT("(结束)") : *NextId);
	}

	if (NextId.IsEmpty())
	{
		EndStory(TEXT("分支没有后继节点"));
		return true;
	}

	const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem();
	const FStoryNodeConfig* Next = Config != nullptr ? Config->GetStoryNode(FName(*NextId)) : nullptr;
	if (Next == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 分支指向了不存在的节点 '%s'，剧情中止"), *NextId);
		EndStory(TEXT("分支指向不存在的节点"));
		return false;
	}

	EnterNode(Next);
	return true;
}

void UMetamorphosisStorySubsystem::StopStory()
{
	if (bPlaying)
	{
		EndStory(TEXT("被显式停止"));
	}
}

void UMetamorphosisStorySubsystem::ResetForNewRun()
{
	// 1) 结束可能还在播的剧情（EndStory 会清门、清临时文本、把 bPlaying 归零）
	StopStory();

	// 2) 兜底再清一次（剧情本来就没在播时，门/临时文本/阶段仍可能有残留）
	ClearGate();
	Phase = EMetamorphosisStoryPhase::Idle;
	bPlaying = false;
	CurrentNodeId = NAME_None;
	StoryChainOnDone = nullptr;
	if (bInlineText)
	{
		bInlineText = false;
		InlineOnDone = nullptr;
		InlineNode = FStoryNodeConfig();
	}

	// 3) 允许新一局重新播开场（PC 的 BeginPlay 里会调 StartOpening）
	bPlayedOpening = false;

	UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 已重置剧情状态（准备开始新一局：清除残留播放/门/临时文本，允许重播开场）"));
}

void UMetamorphosisStorySubsystem::EnterNode(const FStoryNodeConfig* Node)
{
	if (Node == nullptr)
	{
		EndStory(TEXT("空节点"));
		return;
	}

	// Condition 不满足 -> 跳过该节点（继续沿 Next 走），保证策划能按形态/道具裁剪节点
	if (!EvaluateCondition(Node->Condition))
	{
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 节点 %s 的条件 '%s' 不满足，跳过"),
				*Node->NodeId, *Node->Condition);
		}
		if (Node->Next.IsEmpty())
		{
			EndStory(TEXT("条件不满足且没有后继"));
			return;
		}
		if (const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem())
		{
			if (const FStoryNodeConfig* Next = Config->GetStoryNode(FName(*Node->Next)))
			{
				EnterNode(Next);
				return;
			}
		}
		EndStory(TEXT("条件跳过后找不到后继"));
		return;
	}

	CurrentNodeId = FName(*Node->NodeId);
	Phase = EMetamorphosisStoryPhase::Text;   // 一律先按文本播；门节点等玩家点一下才进入玩法门

	// 进节点就重新分页（长文本切页；短文本只有 1 页，等于没分页）
	RebuildPages(Node->Text);

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 节点 %s %s｜%s｜%s%s"),
			*Node->NodeId,
			Node->Speaker.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("[%s]"), *Node->Speaker),
			*Node->Text.Replace(TEXT("\n"), TEXT(" / ")),
			Node->HasChoice() ? *FString::Printf(TEXT("｜选项 A=%s B=%s"), *Node->ChoiceA_Text, *Node->ChoiceB_Text) : TEXT(""),
			Node->Effect.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("｜Effect=%s"), *Node->Effect));
	}

	// 结算 Effect（进入节点时执行；Death 会让剧情在内层结束，这里做一次保护）
	// 存档恢复模式（bSuppressEnterEffectsOnce）下**跳过** ✗：
	//   否则 Death/Give/Task/Mood 都会再执行一次 ✗ -> 读档瞬间触发周目结算 -> 清空背包 + 传回出生点 ✗✗
	const FString Summary = bSuppressEnterEffectsOnce ? FString() : ApplyEffects(Node->Effect);
	if (bSuppressEnterEffectsOnce && bLogStory && !Node->Effect.IsEmpty())
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 存档恢复：节点 %s 的 Effect '(%s)' 不再重跑 ✓"),
			*Node->NodeId, *Node->Effect);
	}
	if (!Summary.IsEmpty() && bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 节点 %s 结算：%s"), *Node->NodeId, *Summary);
	}

	if (!bPlaying)
	{
		return; // 例如 Death 已经把剧情收掉了
	}

	BroadcastCurrent();
}

FString UMetamorphosisStorySubsystem::ApplyEffects(const FString& EffectText)
{
	TArray<FString> Summary;

	TArray<FString> Parts;
	EffectText.ParseIntoArray(Parts, TEXT(";"), true);

	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}

		// Death
		if (Part.Equals(TEXT("Death"), ESearchCase::IgnoreCase))
		{
			if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
			{
				Summary.Add(TEXT("Death -> 周目结算"));
				UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 触发死亡：交给周目结算（复用 LoopSubsystem）"));
				StopStory(); // 先收掉过场（HUD 由 PlayerController 恢复），再走结算流程
				Loop->RequestLoopEnd(ELoopEndReason::Caught);
			}
			else
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 想触发 Death，但拿不到周目结算子系统"));
			}
			continue;
		}

		// Win（2026-10-18）：标记"本局通关"。真正显示通关界面是在剧情结束（EndStory）时，
		// 这样最后一句文本还能读完（原剧情的蜘蛛/大雪/强风分支已按用户要求删除）。
		if (Part.Equals(TEXT("Win"), ESearchCase::IgnoreCase))
		{
			if (UMetamorphosisLoopSubsystem::Get(this) != nullptr)
			{
				bPendingWin = true;
				Summary.Add(TEXT("Win -> 剧情结束后显示通关界面"));
				UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 标记通关：剧情结束后显示通关界面"));
			}
			else
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 想标记通关，但拿不到周目结算子系统"));
			}
			continue;
		}

		// Give:ItemId[:N]
		if (Part.StartsWith(TEXT("Give:"), ESearchCase::IgnoreCase))
		{
			TArray<FString> Seg;
			Part.ParseIntoArray(Seg, TEXT(":"), true);
			if (Seg.Num() >= 2)
			{
				const FName ItemId(*Seg[1].TrimStartAndEnd());
				const int32 Count = Seg.Num() >= 3 ? FCString::Atoi(*Seg[2]) : 1;
				bool bOk = false;
				if (const UWorld* World = GetWorld())
				{
					if (const APlayerController* PC = World->GetFirstPlayerController())
					{
						if (const APawn* Pawn = PC->GetPawn())
						{
							if (UMetamorphosisInventoryComponent* Inv = Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>())
							{
								bOk = Inv->AddItem(ItemId, FMath::Max(1, Count));
							}
						}
					}
				}
				Summary.Add(FString::Printf(TEXT("Give %s x%d -> %s"), *ItemId.ToString(), Count, bOk ? TEXT("成功") : TEXT("失败")));
			}
			continue;
		}

		// Task:TaskId
		if (Part.StartsWith(TEXT("Task:"), ESearchCase::IgnoreCase))
		{
			const FString TaskId = Part.RightChop(5).TrimStartAndEnd();
			if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
			{
				Tasks->SetTask(FName(*TaskId));
				Summary.Add(FString::Printf(TEXT("Task -> %s"), *TaskId));
			}
			continue;
		}

		// Mood±N / Sus±N（前缀匹配，符号在中间）
		auto ApplyStat = [this, &Summary](const TCHAR* Prefix, const FString& Text, bool bMood) -> bool
		{
			if (!Text.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				return false;
			}
			const FString Rest = Text.RightChop(FCString::Strlen(Prefix));
			const float Delta = FCString::Atof(*Rest);
			if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
			{
				if (bMood)
				{
					Stats->AddMood(Delta);
				}
				else
				{
					Stats->AddSuspicion(Delta);
				}
				Summary.Add(FString::Printf(TEXT("%s %+.0f"), bMood ? TEXT("躁郁") : TEXT("怀疑"), Delta));
			}
			else
			{
				Summary.Add(FString::Printf(TEXT("%s %+.0f（拿不到数值系统）"), bMood ? TEXT("躁郁") : TEXT("怀疑"), Delta));
			}
			return true;
		};

		const bool bHandled = ApplyStat(TEXT("Mood"), Part, true) || ApplyStat(TEXT("Sus"), Part, false);
		if (!bHandled)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 无法识别的 Effect 片段：'%s'"), *Part);
		}
	}

	return FString::Join(Summary, TEXT("；"));
}

bool UMetamorphosisStorySubsystem::EvaluateCondition(const FString& ConditionText) const
{
	FString Cond = ConditionText;
	Cond.TrimStartAndEndInline();
	if (Cond.IsEmpty())
	{
		return true;
	}

	const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this);
	const int32 Stage = Stats != nullptr ? Stats->GetBodyStage() : 1;

	if (Cond.StartsWith(TEXT("Stage>="), ESearchCase::IgnoreCase))
	{
		return Stage >= FCString::Atoi(*Cond.RightChop(7));
	}
	if (Cond.StartsWith(TEXT("Stage<="), ESearchCase::IgnoreCase))
	{
		return Stage <= FCString::Atoi(*Cond.RightChop(7));
	}
	if (Cond.StartsWith(TEXT("HasItem:"), ESearchCase::IgnoreCase))
	{
		return HasItemInInventory(*Cond.RightChop(8).TrimStartAndEnd());
	}
	// NoItem:X = **没有**该道具时才满足（2026-10-18）
	// 用途：引导节点"去找工资条"在玩家已经拿着工资条时应该被跳过，而不是提示去捡已有的东西。
	if (Cond.StartsWith(TEXT("NoItem:"), ESearchCase::IgnoreCase))
	{
		return !HasItemInInventory(*Cond.RightChop(7).TrimStartAndEnd());
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 无法识别的 Condition：'%s'（当作满足处理）"), *ConditionText);
	return true;
}

bool UMetamorphosisStorySubsystem::HasItemInInventory(const FString& ItemIdText) const
{
	const FName ItemId(*ItemIdText);
	if (ItemId.IsNone())
	{
		return false;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				if (const UMetamorphosisInventoryComponent* Inv = Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>())
				{
					for (int32 i = 0; i < UMetamorphosisInventoryComponent::NumSlots; ++i)
					{
						const FMetamorphosisItemSlot Slot = Inv->GetSlot(i);
						if (Slot.bOccupied && Slot.ItemId == ItemId && Slot.Count > 0)
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

void UMetamorphosisStorySubsystem::BroadcastCurrent()
{
	const FStoryNodeConfig* Node = GetCurrentNode();
	OnStoryChanged.Broadcast(bPlaying,
		CurrentNodeId,
		FText::FromString(Node != nullptr ? Node->Speaker : FString()),
		// 用"当前页"文本广播，翻页时 UI 才会跟着变
		FText::FromString(GetCurrentPageString()),
		Node != nullptr && Node->HasChoice());
}

void UMetamorphosisStorySubsystem::EndStory(const TCHAR* Reason)
{
	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("=== [Story] 剧情结束（%s）：回到正常游玩 ==="), Reason);
	}

	// 剧情链回调（PlayStoryThen）：**必须在开头就摘出来**。
	// ⚠️ 踩过的坑：之前放在函数末尾才摘，结果"读完回忆文本 -> 回调里启动后续台词链"这个顺序会出问题 ——
	//    回调里刚挂上的 StoryChainOnDone 会被**同一次** EndStory 的尾巴立刻执行掉，
	//    表现为"台词刚弹出就被拾取、玩家根本没读到"。
	//    开头摘走 -> 本次只剩"早就挂好的"回调；回调中挂的留给下一次 EndStory。
	TFunction<void()> PendingChainDone = MoveTemp(StoryChainOnDone);
	StoryChainOnDone = nullptr;

	// 临时文本（剧情道具回忆）：先把回调摘出来、清掉临时节点，广播/通关处理完再执行回调
	// （免得回调里弹界面与"收过场"撞在一起；MoveTemp 保证只触发一次）
	TFunction<void()> PendingInlineDone;
	if (bInlineText)
	{
		PendingInlineDone = MoveTemp(InlineOnDone);
		InlineOnDone = nullptr;
		bInlineText = false;
		InlineNode = FStoryNodeConfig();
	}

	ClearGate();
	Phase = EMetamorphosisStoryPhase::Idle;
	bPlaying = false;
	CurrentNodeId = NAME_None;
	OnStoryChanged.Broadcast(false, NAME_None, FText::GetEmpty(), FText::GetEmpty(), false);

	// 通关（Effect=Win）：剧情收完（黑幕已收起、HUD 已恢复）再显示通关界面，
	// 否则会先冒出通关界面、黑幕还压在下面。bPendingWin 只在本局生效，这里立刻清掉。
	if (bPendingWin)
	{
		bPendingWin = false;
		if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("=== [Story] 通关剧情结束 -> 显示通关界面 ==="));
			Loop->ShowWinEnding(TEXT("剧情 Effect=Win"));
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 想显示通关界面，但拿不到周目结算子系统"));
		}
	}

	// 临时文本读完 -> 执行回调（剧情道具：此时才真正拾取进背包）
	if (PendingInlineDone)
	{
		PendingInlineDone();
	}

	// 剧情链回调（PlayStoryThen）：上面各项收尾都做完再执行。
	// 典型用途：剧情道具"读完回忆文本 -> 播人物台词 -> 此时才拾取道具"。
	// PendingChainDone 在函数开头就摘走了，所以这里只会执行"进入本次 EndStory 之前"挂好的回调；
	// 玩家点跳过/被打断也会执行 —— 道具因此不会丢。
	if (PendingChainDone)
	{
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 剧情链结束 -> 执行后续回调"));
		}
		PendingChainDone();
	}
}

// ------------------------------------------------------------------
// 玩法门（2026-10-16）：分支抉择 = 回到游戏画面 + 任务 UI 指引 + 玩家真实操作 + 系统检测
// ------------------------------------------------------------------

void UMetamorphosisStorySubsystem::ActivateGate(const FStoryNodeConfig& Node)
{
	ClearGate();   // 保险：理论上不会连开两道门

	GateNodeId = FName(*Node.NodeId);
	GateKind = Node.GateKind;
	GateTarget = Node.GateTarget;
	GateParam = Node.GateParam;
	GateTimeLimit = Node.GateTimeLimit;
	GateStartTime = FPlatformTime::Seconds();   // 仅用于"本次门激活后是否按过交互键"
	GateElapsedSeconds = 0.f;                   // 计时改用累计未暂停时间（暂停时冻结）
	GateLastTickRealTime = FPlatformTime::Seconds();
	GateHoldElapsedSeconds = 0.f;
	bGatePausedLogged = false;

	// Region 门：解析区域盒。优先级（2026-10-17 用户要求"可调 + 标签优先"）：
	//   ① ini RegionBoxOverrides（格式 "节点Id:XMin,XMax,YMin,YMax,ZMin,ZMax"，每条带 + 前缀）
	//      —— 实机微调时改 ini 立刻生效，不用重跑导出脚本
	//   ② GateTarget 指向的标签 Actor（Meta.Spot:<Id>）的运行时包围盒
	//      —— 想在编辑器里精确画区域：放一个空 Actor/Box 打上标签摆到目标范围即可，不用改代码
	//   ③ 配表里的 GateRegionBox（策划改 Excel 的正规途径）
	bGateRegionValid = false;
	GateRegionSource = TEXT("(未配置)");
	if (GateKind.Equals(TEXT("Region"), ESearchCase::IgnoreCase))
	{
		// ① ini 覆盖
		for (const FString& Entry : RegionBoxOverrides)
		{
			FString NodePart, BoxPart;
			if (Entry.Split(TEXT(":"), &NodePart, &BoxPart)
				&& NodePart.TrimStartAndEnd().Equals(Node.NodeId, ESearchCase::IgnoreCase)
				&& ParseRegionBox(BoxPart, GateRegionMin, GateRegionMax))
			{
				bGateRegionValid = true;
				GateRegionSource = TEXT("ini RegionBoxOverrides");
				break;
			}
		}

		// ② 标签 Actor 的包围盒
		if (!bGateRegionValid && !GateTarget.IsEmpty())
		{
			if (AActor* Tagged = FindTaggedActor(GateTarget))
			{
				const FBox Box = Tagged->GetComponentsBoundingBox(true);
				if (Box.IsValid)
				{
					GateRegionMin = Box.Min;
					GateRegionMax = Box.Max;
					bGateRegionValid = true;
					GateRegionSource = FString::Printf(TEXT("标签 %s%s 的包围盒"), *SpotTagPrefix, *GateTarget);
				}
			}
			else if (bLogStory)
			{
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Story] Region 门的 GateTarget='%s' 找不到对应标签（%s%s），回落到配表 GateRegionBox"),
					*GateTarget, *SpotTagPrefix, *GateTarget);
			}
		}

		// ③ 配表 AABB
		if (!bGateRegionValid && ParseRegionBox(Node.GateRegionBox, GateRegionMin, GateRegionMax))
		{
			bGateRegionValid = true;
			GateRegionSource = TEXT("配表 GateRegionBox");
		}

		if (bGateRegionValid)
		{
			if (bLogStory)
			{
				UE_LOG(LogMetamorphosis, Log,
					TEXT("[Story] Region 门区域（来源=%s）：X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]"),
					*GateRegionSource,
					GateRegionMin.X, GateRegionMax.X, GateRegionMin.Y, GateRegionMax.Y,
					GateRegionMin.Z, GateRegionMax.Z);
			}
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Story] ⚠ Region 门没有可用区域（ini 覆盖/标签/配表都没给对），本门永远不满足 -> 只能等超时。"
					 "格式：\"XMin,XMax,YMin,YMax,ZMin,ZMax\"；ini 写法：+RegionBoxOverrides=%s:XMin,XMax,..."),
				*Node.NodeId);
		}
	}

	// 保持阶段（2026-10-16 用户要求）：条件满足后还要"连续坚持 N 秒"才算完成
	GateHoldSeconds = FMath::Max(0.f, Node.GateHoldSeconds);
	GateObjectiveBaseText = Node.ObjectiveText;
	GateObjectiveHoldText = Node.ObjectiveHoldText;
	GateHoldStartTime = -1.0;
	GateHoldLastShownSeconds = -1;
	bGateHolding = false;

	// 门一律从"未满足"起步（修 2026-10-16 bug：激活瞬间条件已成立就直接跳下一段剧情）
	bGateSawUnsatisfied = false;
	bGateWarnedInstant = false;
	GateLastDetailLogTime = 0.0;

	GetPlayerLocation(GateAnchorLocation);

	Phase = EMetamorphosisStoryPhase::Gate;

	// 目标文字交给任务 HUD（覆盖显示；门结束时清掉）
	RefreshGateObjective(-1);

	if (bLogStory)
	{
		// 顺便把"激活瞬间玩家就已经满足"这件事打出来（之前就是它导致门瞬间完成、下一段剧情立刻弹出）
		FString InitialDetail;
		const bool bInitiallySatisfied = IsGateSatisfied(InitialDetail);
		// 目标标识的占位文案要跟着门类型走（Region 用区域盒，不看 GateTarget；Distance 才用"激活位置"）
		FString TargetDesc;
		if (!GateTarget.IsEmpty())
		{
			TargetDesc = GateTarget;
		}
		else if (GateKind.Equals(TEXT("Region"), ESearchCase::IgnoreCase))
		{
			TargetDesc = FString::Printf(TEXT("(区域盒 X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]，来源=%s)"),
				GateRegionMin.X, GateRegionMax.X, GateRegionMin.Y, GateRegionMax.Y,
				GateRegionMin.Z, GateRegionMax.Z, *GateRegionSource);
		}
		else
		{
			TargetDesc = TEXT("(以激活位置为参照)");
		}
		UE_LOG(LogMetamorphosis, Log,
			TEXT("=== [Story] 玩法门激活（%s）：目标「%s」类型=%s 目标标识=%s 参数=%.0f 限时=%.0f 秒 布防延迟=%.2f 秒 ==="),
			*Node.NodeId, *Node.ObjectiveText, *GateKind, *TargetDesc, GateParam, GateTimeLimit,
			GateArmDelaySeconds);
		if (bInitiallySatisfied)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Story] ⚠ 门激活瞬间条件就已成立（%s）——按「事件」规则不算完成：需要先离开/先不满足，再重新满足才继续"),
				*InitialDetail);
		}
	}

	// 开始轮询（FTSTicker = 真实时间，暂停也能走；弱引用捕获，系统销毁自动注销）
	TWeakObjectPtr<UMetamorphosisStorySubsystem> WeakThis(this);
	GateTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis](float DeltaTime) -> bool
		{
			UMetamorphosisStorySubsystem* Self = WeakThis.Get();
			return Self != nullptr && Self->TickGate(DeltaTime);
		}), FMath::Max(0.01f, GatePollIntervalSeconds));

	// 广播：UI 据此收起黑幕、恢复 HUD（IsOverlayVisible() == false）
	BroadcastCurrent();
}

bool UMetamorphosisStorySubsystem::IsGameplayPaused(const UObject* WorldContext)
{
	// 门的轮询走 FTSTicker（真实时间），不受 SetGamePaused 影响 —— 所以必须自己判断暂停并冻结计时。
	// 这里同时看 UGameplayStatics（全局暂停标志）与 World->IsPaused()，覆盖两种暂停来源。
	if (WorldContext == nullptr)
	{
		return false;
	}
	if (const UWorld* W = WorldContext->GetWorld())
	{
		return UGameplayStatics::IsGamePaused(W) || W->IsPaused();
	}
	return false;
}

bool UMetamorphosisStorySubsystem::TickGate(float DeltaTime)
{
	if (!bPlaying || Phase != EMetamorphosisStoryPhase::Gate)
	{
		return false;   // 剧情已结束/门已结束 -> 注销
	}

	// ---- 暂停时冻结所有门的计时（2026-10-17 修的严重 bug）----
	// 原来用 `FPlatformTime::Seconds() - GateStartTime` 的绝对时间戳，FTSTicker 在暂停时照跑，
	// 于是玩家一开设置，躲藏门的 15 秒限时继续走 -> 15 秒后直接"被看见->死亡"。
	const bool bPaused = IsGameplayPaused(this);
	if (bPaused != bGatePausedLogged)
	{
		bGatePausedLogged = bPaused;
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Story] 玩法门（%s）%s：已过 %.1f/%.0f 秒，保持 %.1f/%.0f 秒（暂停期间计时冻结）"),
				*GateNodeId.ToString(), bPaused ? TEXT("游戏暂停") : TEXT("游戏恢复"),
				GateElapsedSeconds, GateTimeLimit, GateHoldElapsedSeconds, GateHoldSeconds);
		}
	}

	// 计时用"两次轮询之间的真实时间差"，暂停期间不计入；恢复后从冻结处继续。
	// ⚠️ 不能用 FTSTicker 给的 DeltaTime：实测它和实际调用频率不匹配（每 1 秒只累计 0.2 秒，计时慢 5 倍）。
	const double Now = FPlatformTime::Seconds();
	if (!bPaused && GateLastTickRealTime > 0.0)
	{
		const double RealDt = FMath::Clamp(Now - GateLastTickRealTime, 0.0, 0.5);   // 夹住卡顿尖峰
		GateElapsedSeconds += static_cast<float>(RealDt);
		if (bGateHolding)
		{
			GateHoldElapsedSeconds += static_cast<float>(RealDt);
		}
	}
	GateLastTickRealTime = Now;

	const double Elapsed = GateElapsedSeconds;

	// 布防延迟：门刚激活的这一小段时间只观察、不判定（避免黑幕刚收起就完成）
	const bool bArmed = Elapsed >= FMath::Max(0.f, GateArmDelaySeconds);

	FString Detail;
	const bool bRaw = IsGateSatisfied(Detail);

	// 限频打印判定明细：出问题时（"我明明躲进去了却不触发"）看日志就知道差在哪
	if (bLogStory && GateDetailLogIntervalSeconds > 0.f && (Now - GateLastDetailLogTime) >= GateDetailLogIntervalSeconds)
	{
		GateLastDetailLogTime = Now;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 门明细（%s，已过 %.1f 秒）：%s -> %s"),
			*GateNodeId.ToString(), Elapsed, bRaw ? TEXT("满足") : TEXT("未满足"), *Detail);
	}

	// 门是「事件」不是「状态」：瞬时门（无保持时间）必须至少观察到一次"未满足"，之后的"满足"才承认。
	// 否则玩家一出生/一激活就在判定区内时，会立刻跳到下一段剧情（用户 2026-10-16 报的 bug）。
	// ⚠ 保持型门不受这条约束（见下面保持阶段的说明）：它的"事件"是那 N 秒连续保持。
	if (!bRaw)
	{
		bGateSawUnsatisfied = true;
	}
	else if (!bGateSawUnsatisfied && bArmed && GateHoldSeconds <= 0.f && !bGateWarnedInstant)
	{
		bGateWarnedInstant = true;
		if (bLogStory)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Story] 玩法门（%s）条件已成立但还没观察到「先不满足」——按规则要等玩家离开/停止满足后重新满足才继续（%s）"),
				*GateNodeId.ToString(), *Detail);
		}
	}

	// Timeout 门：时间到就算满足（"等一会儿"本身就是目标）
	if (!bRaw && GateKind == TEXT("Timeout") && GateTimeLimit > 0.f && Elapsed >= GateTimeLimit)
	{
		FinishGate(true, FString::Printf(TEXT("等待 %.0f 秒完成"), GateTimeLimit));
		return false;
	}

	// ---- 保持阶段（GateHoldSeconds > 0）----
	// 语义：条件满足后开始计时，必须**连续保持** N 秒才完成；中途条件失效 -> 计时清零、回到"未满足"。
	// 注意 GateTimeLimit 是"从门激活起算的总时限"，所以保持期间超时仍然会走 GateTimeoutNext。
	if (GateHoldSeconds > 0.f)
	{
		// 保持型门（GateHoldSeconds > 0）**不要求**"先观察到未满足"：
		//   * 秒级的"连续保持"本身就是事件 —— 玩家必须真的待在区域内满 N 秒，不会重演"一激活就完成"那个老 bug ✓
		//   * 若强制要求"先离开过"，那么"门激活时玩家已经在区域内"（比如区域覆盖了出生点/玩家提前站好了）
		//     就会永远等不到保持开始 -> 门一直显示"满足"却永远完不成（2026-10-17 用户报的回归）✗
		// 没有保持时间的门（瞬时门）仍然保留事件式判定（下面 else-if 分支），避免瞬间跳过一段剧情。
		const bool bPreludeOk = bArmed;

		if (bPreludeOk && bRaw)
		{
			if (GateHoldStartTime < 0.0)
			{
				// 刚开始保持
				GateHoldStartTime = Now;
				GateHoldElapsedSeconds = 0.f;
				bGateHolding = true;
				GateHoldLastShownSeconds = -1;
				if (bLogStory)
				{
					UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 门开始保持（%s）：需要连续 %.1f 秒（%s）"),
						*GateNodeId.ToString(), GateHoldSeconds, *Detail);
				}
			}

			const double Held = GateHoldElapsedSeconds;
			const int32 Remaining = FMath::Max(0, FMath::CeilToInt(GateHoldSeconds - Held));

			// 每秒一条日志 + 每秒刷一次 HUD（不要每帧刷文本/重排布局）
			if (Remaining != GateHoldLastShownSeconds)
			{
				GateHoldLastShownSeconds = Remaining;
				RefreshGateObjective(Remaining);
				if (bLogStory)
				{
					UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 门保持（%s）：已保持 %.1f/%.0f 秒（剩余 %d 秒）"),
						*GateNodeId.ToString(), Held, GateHoldSeconds, Remaining);
				}
			}

			if (Held >= GateHoldSeconds)
			{
				FinishGate(true, FString::Printf(TEXT("连续保持 %.1f 秒完成"), GateHoldSeconds));
				return false;
			}
		}
		else if (bGateHolding)
		{
			// 条件失效（离开了判定区/还没满足过）-> 计时清零，回到未满足
			const double Held = GateHoldElapsedSeconds;
			GateHoldStartTime = -1.0;
			GateHoldElapsedSeconds = 0.f;
			bGateHolding = false;
			GateHoldLastShownSeconds = -1;
			RefreshGateObjective(-1);
			if (bLogStory)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 门保持中断（%s）：只坚持了 %.1f/%.0f 秒就失效 -> 计时清零，需重新累计（%s）"),
					*GateNodeId.ToString(), Held, GateHoldSeconds, *Detail);
			}
		}
	}
	else if (bArmed && bRaw && bGateSawUnsatisfied)
	{
		FinishGate(true, Detail);
		return false;
	}

	if (GateTimeLimit > 0.f && Elapsed >= GateTimeLimit)
	{
		FinishGate(false, FString::Printf(TEXT("超时（%.0f 秒）"), GateTimeLimit));
		return false;
	}

	return true;
}

void UMetamorphosisStorySubsystem::RefreshGateObjective(int32 RemainingSeconds)
{
	UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this);
	if (Tasks == nullptr || !IsGateActive())
	{
		return;
	}

	// 保持阶段：优先用 ObjectiveHoldText（{N} 换成剩余秒数）；否则沿用 ObjectiveText
	FString Text = GateObjectiveBaseText;
	const bool bInHold = RemainingSeconds >= 0 && !GateObjectiveHoldText.IsEmpty();
	if (bInHold)
	{
		Text = GateObjectiveHoldText.Replace(TEXT("{N}"), *FString::FromInt(RemainingSeconds));
	}

	// 剩余总时限 = 门总限时 - 已过时间（不要每次都传原始限时，否则 HUD 上的倒计时会被一直"续命"）
	double LeftLimit = 0.0;
	if (GateTimeLimit > 0.f)
	{
		LeftLimit = FMath::Max(0.0, GateTimeLimit - GateElapsedSeconds);
	}

	// 保持文案里已经带了「（N 秒）」，此时不要再追加总限时倒计时 —— 否则会显示成
	// 「躲好，别出声（3 秒）（12 秒）」两个数字叠在一起（2026-10-16 截图发现）。
	if (bInHold)
	{
		LeftLimit = 0.0;
	}

	Tasks->SetObjectiveOverride(FText::FromString(Text), static_cast<float>(LeftLimit));
}

void UMetamorphosisStorySubsystem::ClearGate()
{
	if (GateTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GateTickerHandle);
		GateTickerHandle.Reset();
	}

	GateNodeId = NAME_None;
	GateKind.Reset();
	GateTarget.Reset();
	GateParam = 0.f;
	GateTimeLimit = 0.f;
	bGateSawUnsatisfied = false;
	bGateWarnedInstant = false;
	GateHoldSeconds = 0.f;
	GateObjectiveHoldText.Reset();
	GateObjectiveBaseText.Reset();
	GateHoldStartTime = -1.0;
	GateElapsedSeconds = 0.f;
	GateHoldElapsedSeconds = 0.f;
	bGatePausedLogged = false;
	GateHoldLastShownSeconds = -1;
	bGateHolding = false;

	if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
	{
		Tasks->ClearObjectiveOverride();
	}
}

void UMetamorphosisStorySubsystem::FinishGate(bool bSatisfied, const FString& Reason)
{
	const FString NextId = bSatisfied
		? (GetCurrentNode() != nullptr ? GetCurrentNode()->GateNext : FString())
		: (GetCurrentNode() != nullptr && !GetCurrentNode()->GateTimeoutNext.IsEmpty()
			? GetCurrentNode()->GateTimeoutNext
			: (GetCurrentNode() != nullptr ? GetCurrentNode()->Next : FString()));

	const FName FinishedGate = GateNodeId;
	ClearGate();
	Phase = EMetamorphosisStoryPhase::Text;

	if (bLogStory)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 玩法门%s（%s）：%s -> %s"),
			bSatisfied ? TEXT("满足") : TEXT("超时"),
			*FinishedGate.ToString(), *Reason,
			NextId.IsEmpty() ? TEXT("(结束)") : *NextId);
	}

	if (NextId.IsEmpty())
	{
		EndStory(bSatisfied ? TEXT("门之后没有后继节点") : TEXT("门超时且没有后备节点"));
		return;
	}

	const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem();
	const FStoryNodeConfig* Next = Config != nullptr ? Config->GetStoryNode(FName(*NextId)) : nullptr;
	if (Next == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Story] 玩法门指向了不存在的节点 '%s'，剧情中止"), *NextId);
		EndStory(TEXT("门指向不存在的节点"));
		return;
	}

	EnterNode(Next);
}

bool UMetamorphosisStorySubsystem::IsGateSatisfied(FString& OutDetail) const
{
	FVector PlayerLoc;
	if (!GetPlayerLocation(PlayerLoc))
	{
		OutDetail = TEXT("拿不到玩家位置");
		return false;
	}

	// ---- UnderMesh：玩家真的藏在目标物件"下面"（向上打一条竖直线，命中目标才算） ----
	// 与 Spot 的区别：Spot 用包围盒（会把床周围一大片都算进去，实测连重生点都在盒边），
	// UnderMesh 用几何：站在床边/床前/床上都不会命中床底，只有钻进床板下方才算 ✓
	if (GateKind == TEXT("UnderMesh"))
	{
		if (GateTarget.IsEmpty())
		{
			OutDetail = TEXT("UnderMesh 门没填 GateTarget（需要区域 Id，如 HideUnderBed）");
			return false;
		}

		UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
		if (World == nullptr)
		{
			OutDetail = TEXT("拿不到 World");
			return false;
		}

		const float MaxDist = GateParam > 0.f ? GateParam : UnderMeshMaxCm;
		const FVector Start = PlayerLoc + FVector(0.f, 0.f, 2.f);
		const FVector End = PlayerLoc + FVector(0.f, 0.f, MaxDist);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(StoryGateUnderMesh), false);
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const APlayerController* PC = GI->GetFirstLocalPlayerController())
			{
				if (const APawn* Pawn = PC->GetPawn())
				{
					Params.AddIgnoredActor(Pawn);
				}
			}
		}

		// 先 Visibility（灯光/装饰件通常也在这一通道），打不到再试 WorldStatic
		// （有的家具不挡 Visibility 但挡 WorldStatic —— 两次都试，避免"明明在床底却判定不满足"）
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
			|| World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);

		if (!bHit)
		{
			OutDetail = FString::Printf(TEXT("向上 %.0f cm 没有命中任何东西（玩家头顶是空的 -> 不在 '%s' 下面）"),
				MaxDist, *GateTarget);
			return false;
		}

		const FString HitActor = GetNameSafe(Hit.GetComponent() != nullptr ? Hit.GetComponent()->GetOwner() : nullptr);
		const FString HitComp = GetNameSafe(Hit.GetComponent());
		const float HitDist = static_cast<float>(Hit.Distance);

		FString MatchDesc;
		const bool bIsTarget = DoesComponentMatchSpotTarget(Hit.GetComponent(), GateTarget, MatchDesc);
		const bool bFarEnough = HitDist >= FMath::Max(0.f, UnderMeshMinCm);

		OutDetail = FString::Printf(
			TEXT("向上命中 %s.%s（距离 %.0f cm，需在 %.0f~%.0f 之间）=%d；是否目标 '%s'=%d（%s）"),
			*HitActor, *HitComp, HitDist, UnderMeshMinCm, MaxDist, bFarEnough ? 1 : 0,
			*GateTarget, bIsTarget ? 1 : 0, *MatchDesc);

		return bIsTarget && bFarEnough;
	}

	// ---- Spot：进入某区域（XY 在盒内 + 低于盒顶一定高度）----
	if (GateKind == TEXT("Spot"))
	{
		FVector Center, Extent;
		FString Source;
		if (!ResolveSpotBox(GateTarget, Center, Extent, Source))
		{
			OutDetail = FString::Printf(TEXT("找不到区域 '%s'（标签 %s%s / 网格名 / ini 坐标都没有）"),
				*GateTarget, *SpotTagPrefix, *GateTarget);
			return false;
		}

		const float Inflate = FMath::Max(0.f, GateParam) + SpotEnterInflateCm;
		const bool bInsideXY = FMath::Abs(PlayerLoc.X - Center.X) <= Extent.X + Inflate
			&& FMath::Abs(PlayerLoc.Y - Center.Y) <= Extent.Y + Inflate;
		const float BoxTop = Center.Z + Extent.Z;
		const bool bLowEnough = PlayerLoc.Z <= BoxTop - SpotBelowTopMarginCm;

		OutDetail = FString::Printf(
			TEXT("区域 '%s'（来源=%s 中心=(%.0f,%.0f,%.0f) 半径=(%.0f,%.0f,%.0f)）；玩家=(%.0f,%.0f,%.0f) XY在内=%d 低于顶面=%.0f=%d"),
			*GateTarget, *Source, Center.X, Center.Y, Center.Z, Extent.X, Extent.Y, Extent.Z,
			PlayerLoc.X, PlayerLoc.Y, PlayerLoc.Z, bInsideXY ? 1 : 0, BoxTop - SpotBelowTopMarginCm, bLowEnough ? 1 : 0);

		return bInsideXY && bLowEnough;
	}

	// ---- Region：玩家在配表写死的 AABB 内（GateParam > 0 时按该值向内收缩，可用来"别贴边就算"）----
	if (GateKind == TEXT("Region"))
	{
		if (!bGateRegionValid)
		{
			OutDetail = FString::Printf(TEXT("区域盒没有配置/解析失败（来源=%s）"), *GateRegionSource);
			return false;
		}

		const float Inset = FMath::Max(0.f, GateParam);
		const FVector Min = GateRegionMin + FVector(Inset);
		const FVector Max = GateRegionMax - FVector(Inset);

		const bool bInside = PlayerLoc.X >= Min.X && PlayerLoc.X <= Max.X
			&& PlayerLoc.Y >= Min.Y && PlayerLoc.Y <= Max.Y
			&& PlayerLoc.Z >= Min.Z && PlayerLoc.Z <= Max.Z;

		if (bInside)
		{
			OutDetail = FString::Printf(
				TEXT("在区域 X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f] 内；玩家=(%.0f,%.0f,%.0f)"),
				Min.X, Max.X, Min.Y, Max.Y, Min.Z, Max.Z, PlayerLoc.X, PlayerLoc.Y, PlayerLoc.Z);
		}
		else
		{
			// 逐轴算"差多少、偏哪个方向"，现场一眼就能看出该往哪走
			FString Worst;
			float WorstGap = 0.f;
			auto ReportAxis = [&Worst, &WorstGap](const TCHAR* AxisName, float Value, float Lo, float Hi,
				const TCHAR* NegName, const TCHAR* PosName)
			{
				float Gap = 0.f;
				const TCHAR* Side = TEXT("");
				if (Value < Lo) { Gap = Lo - Value; Side = NegName; }
				else if (Value > Hi) { Gap = Value - Hi; Side = PosName; }
				if (Gap > WorstGap)
				{
					WorstGap = Gap;
					Worst = FString::Printf(TEXT("%s 偏%s %.0f cm"), AxisName, Side, Gap);
				}
			};
			ReportAxis(TEXT("X"), PlayerLoc.X, Min.X, Max.X, TEXT("西(-X)"), TEXT("东(+X)"));
			ReportAxis(TEXT("Y"), PlayerLoc.Y, Min.Y, Max.Y, TEXT("北(-Y)"), TEXT("南(+Y)"));
			ReportAxis(TEXT("Z"), PlayerLoc.Z, Min.Z, Max.Z, TEXT("低"), TEXT("高"));

			OutDetail = FString::Printf(
				TEXT("不在区域 X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f] 内；玩家=(%.0f,%.0f,%.0f)；主要偏差：%s"),
				Min.X, Max.X, Min.Y, Max.Y, Min.Z, Max.Z, PlayerLoc.X, PlayerLoc.Y, PlayerLoc.Z,
				Worst.IsEmpty() ? TEXT("(无)") : *Worst);
		}

		return bInside;
	}

	// ---- Distance：离参照点足够远（GateParam>0）或足够近（GateParam<0）----
	if (GateKind == TEXT("Distance"))
	{
		FVector Ref = GateAnchorLocation;
		FString RefName = TEXT("门激活位置");
		if (!GateTarget.IsEmpty())
		{
			if (AActor* Target = FindTaggedActor(GateTarget))
			{
				Ref = Target->GetActorLocation();
				RefName = FString::Printf(TEXT("区域 '%s'"), *GateTarget);
			}
			else if (FVector Center, Extent; ResolveSpotBox(GateTarget, Center, Extent, RefName))
			{
				Ref = Center;
			}
		}

		const float Dist = FVector::Dist2D(PlayerLoc, Ref);
		const float Need = FMath::Abs(GateParam);
		const bool bFar = GateParam >= 0.f;
		const bool bOk = bFar ? (Dist >= Need) : (Dist <= Need);

		OutDetail = FString::Printf(TEXT("距%s %.0f cm（%s %.0f）=%s"),
			*RefName, Dist, bFar ? TEXT("需要 >=") : TEXT("需要 <="), Need, bOk ? TEXT("满足") : TEXT("还不够"));

		return bOk;
	}

	// ---- Interact：走到目标旁并按交互（交互输入时间记在 PlayerController 上，不动交互系统）----
	if (GateKind == TEXT("Interact"))
	{
		AActor* Target = FindTaggedActor(GateTarget);
		if (Target == nullptr)
		{
			OutDetail = FString::Printf(TEXT("找不到可交互区域 '%s'（标签 %s%s）"), *GateTarget, *SpotTagPrefix, *GateTarget);
			return false;
		}

		const float Dist = FVector::Dist2D(PlayerLoc, Target->GetActorLocation());
		const float Need = GateParam > 0.f ? GateParam : 150.f;

		bool bPressed = false;
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const AMetamorphosisPlayerController* PC =
				Cast<AMetamorphosisPlayerController>(GI->GetFirstLocalPlayerController()))
			{
				bPressed = PC->GetLastInteractInputTime() >= GateStartTime;
			}
		}

		OutDetail = FString::Printf(TEXT("距目标 %.0f cm（需要 <= %.0f）且本次门激活后按过交互=%d"), Dist, Need, bPressed ? 1 : 0);
		return Dist <= Need && bPressed;
	}

	// ---- Timeout：由 TickGate 的时间判断处理 ----
	if (GateKind == TEXT("Timeout"))
	{
		OutDetail = FString::Printf(TEXT("等待中（已 %.1f 秒 / 目标 %.0f 秒）"),
			GateElapsedSeconds, GateTimeLimit);
		return false;
	}

	OutDetail = FString::Printf(TEXT("未知的门类型 '%s'"), *GateKind);
	return false;
}

bool UMetamorphosisStorySubsystem::ParseRegionBox(const FString& InText, FVector& OutMin, FVector& OutMax)
{
	// 期望格式："XMin,XMax,YMin,YMax,ZMin,ZMax"（世界坐标 cm）；容错空格，但数字个数/大小顺序必须对
	if (InText.IsEmpty())
	{
		return false;
	}

	TArray<FString> Parts;
	InText.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != 6)
	{
		return false;
	}

	TArray<float> Values;
	for (const FString& Part : Parts)
	{
		const FString Trimmed = Part.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		// 只接受纯数字（strtod 式解析失败会返回 0，用 ToFloat 后还得排除 "abc" 这种）
		for (const TCHAR Ch : Trimmed)
		{
			if (!FChar::IsDigit(Ch) && Ch != TEXT('-') && Ch != TEXT('+') && Ch != TEXT('.'))
			{
				return false;
			}
		}
		Values.Add(FCString::Atof(*Trimmed));
	}

	const FVector Min(Values[0], Values[2], Values[4]);
	const FVector Max(Values[1], Values[3], Values[5]);
	if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z)
	{
		return false;   // min/max 写反了
	}

	OutMin = Min;
	OutMax = Max;
	return true;
}

bool UMetamorphosisStorySubsystem::ResolveSpotBox(const FString& Id, FVector& OutCenter, FVector& OutExtent, FString& OutSource) const
{
	if (Id.IsEmpty())
	{
		return false;
	}

	// ① 场景标签：Meta.Spot:<Id>（最权威，用 Actor 自己的运行时包围盒）
	if (const AActor* Tagged = FindTaggedActor(Id))
	{
		FBox Box = Tagged->GetComponentsBoundingBox(true);
		if (Box.IsValid)
		{
			OutCenter = Box.GetCenter();
			OutExtent = Box.GetExtent();
			OutSource = TEXT("场景标签");
			return true;
		}
		OutCenter = Tagged->GetActorLocation();
		OutExtent = FVector(60.f, 60.f, 60.f);
		OutSource = TEXT("场景标签(无包围盒,用点)");
		return true;
	}

	// ② 网格资产名（Id:MeshName）：新图/没打标签时靠这个自动找到床、婴儿床
	//    容错：允许一条配置里用 ";" 写多条（策划可能这么写）
	TArray<FString> MeshEntries;
	for (const FString& Raw : SpotMeshNames)
	{
		TArray<FString> Split;
		Raw.ParseIntoArray(Split, TEXT(";"), true);
		MeshEntries.Append(Split);
	}
	for (const FString& Entry : MeshEntries)
	{
		FString Key, MeshName;
		if (!Entry.Split(TEXT(":"), &Key, &MeshName))
		{
			continue;
		}
		if (!Key.TrimStartAndEnd().Equals(Id, ESearchCase::IgnoreCase))
		{
			continue;
		}
		MeshName.TrimStartAndEndInline();

		const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
		if (World == nullptr)
		{
			continue;
		}

		// 场景里同名网格可能有好几个（本图就有两张床！）——取"离玩家最近的那个"，
		// 否则 Actor 遍历顺序会让判定区随机落在另一张床上（实测会出现"站对了地方却判定不满足"）。
		FVector PlayerLoc = FVector::ZeroVector;
		const bool bHasPlayer = GetPlayerLocation(PlayerLoc);

		bool bFound = false;
		float BestDistSq = TNumericLimits<float>::Max();
		FVector BestCenter = FVector::ZeroVector;
		FVector BestExtent = FVector::ZeroVector;
		FString BestSource;

		for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AActor* Actor = *It;
			TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
			// 用"匹配到的那个网格组件"的包围盒，而不是整个 Actor 的聚合包围盒：
			// 实测床 Actor 的聚合盒比床本身大很多 -> 判定区过大，站着不动都能算"已躲好"。
			const UStaticMeshComponent* Match = nullptr;
			for (const UStaticMeshComponent* Comp : Meshes)
			{
				if (Comp != nullptr && Comp->GetStaticMesh() != nullptr
					&& Comp->GetStaticMesh()->GetName().Equals(MeshName, ESearchCase::IgnoreCase))
				{
					Match = Comp;
					break;
				}
			}
			if (Match == nullptr)
			{
				continue;
			}

			const FBox Box = Match->Bounds.GetBox();
			if (!Box.IsValid)
			{
				continue;
			}

			const FVector Center = Box.GetCenter();
			const float DistSq = bHasPlayer ? FVector::DistSquared2D(PlayerLoc, Center) : 0.f;
			if (!bFound || DistSq < BestDistSq)
			{
				bFound = true;
				BestDistSq = DistSq;
				BestCenter = Center;
				BestExtent = Box.GetExtent();
				BestSource = FString::Printf(TEXT("网格名(%s):%s"), *MeshName, *Actor->GetActorNameOrLabel());
			}
		}

		if (bFound)
		{
			OutCenter = BestCenter;
			OutExtent = BestExtent;
			OutSource = BestSource;
			return true;
		}
	}

	// ③ ini 坐标盒兜底：Id,X,Y,Z,ExtentX,ExtentY,ExtentZ
	for (const FString& Entry : SpotFallbacks)
	{
		TArray<FString> Parts;
		Entry.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() < 7 || !Parts[0].TrimStartAndEnd().Equals(Id, ESearchCase::IgnoreCase))
		{
			continue;
		}
		OutCenter = FVector(FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
		OutExtent = FVector(FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5]), FCString::Atof(*Parts[6]));
		OutSource = TEXT("ini 坐标盒");
		return true;
	}

	return false;
}

bool UMetamorphosisStorySubsystem::DoesComponentMatchSpotTarget(const UPrimitiveComponent* Comp, const FString& Id, FString& OutDesc) const
{
	OutDesc.Reset();
	if (Comp == nullptr || Id.IsEmpty())
	{
		return false;
	}

	// ① 场景标签：Meta.Spot:<Id> 的 Actor 的任意组件都算
	if (const AActor* Tagged = FindTaggedActor(Id))
	{
		const bool bSameActor = Comp->GetOwner() == Tagged;
		if (bSameActor)
		{
			OutDesc = FString::Printf(TEXT("标签 %s%s（命中它的组件）"), *SpotTagPrefix, *Id);
			return true;
		}
		OutDesc = FString::Printf(TEXT("标签 %s%s 是别的 Actor"), *SpotTagPrefix, *Id);
	}

	// ② 网格资产名：命中的组件是网格组件且网格名匹配
	//    同名多张床时不用再"取离玩家最近的那张"：向上射线打到的就是玩家头顶那张，天然选对 ✓
	const UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Comp);
	if (MeshComp != nullptr && MeshComp->GetStaticMesh() != nullptr)
	{
		const FString HitMesh = MeshComp->GetStaticMesh()->GetName();
		for (const FString& Raw : SpotMeshNames)
		{
			TArray<FString> Split;
			Raw.ParseIntoArray(Split, TEXT(";"), true);
			for (const FString& Entry : Split)
			{
				FString Key, MeshName;
				if (!Entry.Split(TEXT(":"), &Key, &MeshName))
				{
					continue;
				}
				if (!Key.TrimStartAndEnd().Equals(Id, ESearchCase::IgnoreCase))
				{
					continue;
				}
				MeshName.TrimStartAndEndInline();
				if (HitMesh.Equals(MeshName, ESearchCase::IgnoreCase))
				{
					OutDesc = FString::Printf(TEXT("网格名匹配 %s"), *MeshName);
					return true;
				}
			}
		}
		OutDesc = FString::Printf(TEXT("命中网格 %s 不在 '%s' 的网格名配置里"), *HitMesh, *Id);
		return false;
	}

	if (OutDesc.IsEmpty())
	{
		OutDesc = TEXT("命中的组件既不是该区域的网格，也没有匹配的标签");
	}
	return false;
}

AActor* UMetamorphosisStorySubsystem::FindTaggedActor(const FString& Id) const
{
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || Id.IsEmpty())
	{
		return nullptr;
	}

	const FName Want(*FString::Printf(TEXT("%s%s"), *SpotTagPrefix, *Id));
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->Tags.Contains(Want))
		{
			return *It;
		}
	}
	return nullptr;
}

bool UMetamorphosisStorySubsystem::GetPlayerLocation(FVector& OutLocation) const
{
	const UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI != nullptr ? GI->GetFirstLocalPlayerController() : nullptr;
	const APawn* Pawn = PC != nullptr ? PC->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return false;
	}
	OutLocation = Pawn->GetActorLocation();
	return true;
}

FString UMetamorphosisStorySubsystem::DescribeGate() const
{
	if (!IsGateActive())
	{
		return TEXT("(没有进行中的玩法门)");
	}

	FString Detail;
	const bool bOk = IsGateSatisfied(Detail);
	const double Elapsed = GateElapsedSeconds;

	// Region 门：把区域盒与被缩进后的实际判定范围也打出来（现场核对区域对不对时最有用）
	FString RegionInfo;
	if (GateKind == TEXT("Region"))
	{
		if (bGateRegionValid)
		{
			const float Inset = FMath::Max(0.f, GateParam);
			RegionInfo = FString::Printf(TEXT(" 区域盒[%s] X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f] 内缩=%.0f cm"),
				GateRegionSource.IsEmpty() ? TEXT("?") : *GateRegionSource,
				GateRegionMin.X + Inset, GateRegionMax.X - Inset,
				GateRegionMin.Y + Inset, GateRegionMax.Y - Inset,
				GateRegionMin.Z + Inset, GateRegionMax.Z - Inset, Inset);
		}
		else
		{
			RegionInfo = TEXT(" 区域盒=无效（GateRegionBox 没配或格式错）");
		}
	}

	return FString::Printf(TEXT("节点=%s 类型=%s 目标=%s 参数=%.0f 限时=%.0f 秒 已过=%.1f 秒 保持=%.0f 秒(%s)%s 当前=%s｜%s"),
		*GateNodeId.ToString(), *GateKind,
		GateTarget.IsEmpty() ? TEXT("(激活位置)") : *GateTarget,
		GateParam, GateTimeLimit, Elapsed,
		GateHoldSeconds,
		bGateHolding ? *FString::Printf(TEXT("保持中 %.1f/%.0f"), GateHoldElapsedSeconds, GateHoldSeconds)
			: (GateHoldSeconds > 0.f ? TEXT("未开始保持") : TEXT("无保持")),
		*RegionInfo,
		bOk ? TEXT("已满足") : TEXT("未满足"), *Detail);
}

void UMetamorphosisStorySubsystem::LogRegionDiagnostics() const
{
	// Distance 门也走这条诊断（2026-10-17：Open20 改成"离婴儿床多远"之后，现场最想知道的就是当前距离）
	if (IsGateActive() && GateKind.Equals(TEXT("Distance"), ESearchCase::IgnoreCase))
	{
		FVector PlayerLoc = FVector::ZeroVector;
		if (!GetPlayerLocation(PlayerLoc))
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] Distance 门：拿不到玩家位置"));
			return;
		}

		FVector Ref = GateAnchorLocation;
		FString RefName = TEXT("门激活位置");
		if (!GateTarget.IsEmpty())
		{
			if (FVector Center, Extent; ResolveSpotBox(GateTarget, Center, Extent, RefName))
			{
				Ref = Center;
			}
			else if (const AActor* Tagged = FindTaggedActor(GateTarget))
			{
				Ref = Tagged->GetActorLocation();
				RefName = FString::Printf(TEXT("区域 '%s'"), *GateTarget);
			}
		}

		const float Dist = FVector::Dist2D(PlayerLoc, Ref);
		const float Need = FMath::Abs(GateParam);
		const bool bFar = GateParam >= 0.f;
		const float Delta = bFar ? (Dist - Need) : (Need - Dist);
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Debug] Distance 门：到 %s（%.0f,%.0f）的水平距离 = %.0f cm；判定=需要%s%.0f cm -> %s（%s%.0f cm）"),
			*RefName, Ref.X, Ref.Y, Dist, bFar ? TEXT("≥") : TEXT("≤"), Need,
			Delta >= 0.f ? TEXT("满足") : TEXT("还不够"),
			Delta >= 0.f ? TEXT("余量 ") : TEXT("还差 "), FMath::Abs(Delta));
		if (!GateTarget.IsEmpty())
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 参照物标识=%s（若 GateTarget 是 Crib，这行就是\"离婴儿床多远\"）"),
				*GateTarget);
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 玩家=(%.0f,%.0f,%.0f) ｜ %s"),
			PlayerLoc.X, PlayerLoc.Y, PlayerLoc.Z, *DescribeGate());
		return;
	}

	if (!IsGateActive() || !GateKind.Equals(TEXT("Region"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 当前没有 Region/Distance 门（类型=%s）"), *GateKind);
		return;
	}

	if (!bGateRegionValid)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 区域盒无效：GateRegionBox 没配或格式错"));
		return;
	}

	const FVector Min = GateRegionMin;
	const FVector Max = GateRegionMax;
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Debug] 区域盒 X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]（中心 %.0f,%.0f,%.0f；尺寸 %.0f x %.0f x %.0f）"),
		Min.X, Max.X, Min.Y, Max.Y, Min.Z, Max.Z,
		(Min.X + Max.X) * 0.5f, (Min.Y + Max.Y) * 0.5f, (Min.Z + Max.Z) * 0.5f,
		Max.X - Min.X, Max.Y - Min.Y, Max.Z - Min.Z);
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Debug] 8 个角：(%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f) / (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f) (%.0f,%.0f,%.0f)"),
		Min.X, Min.Y, Min.Z, Max.X, Min.Y, Min.Z, Min.X, Max.Y, Min.Z, Max.X, Max.Y, Min.Z,
		Min.X, Min.Y, Max.Z, Max.X, Min.Y, Max.Z, Min.X, Max.Y, Max.Z, Max.X, Max.Y, Max.Z);
	UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] %s"), *DescribeGate());

	FVector PlayerLoc = FVector::ZeroVector;
	if (GetPlayerLocation(PlayerLoc))
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Debug] 玩家=(%.0f,%.0f,%.0f)；到各边界差：X 左 %.0f / 右 %.0f，Y 下 %.0f / 上 %.0f，Z 下 %.0f / 上 %.0f（负数=已在界外）"),
			PlayerLoc.X, PlayerLoc.Y, PlayerLoc.Z,
			PlayerLoc.X - Min.X, Max.X - PlayerLoc.X,
			PlayerLoc.Y - Min.Y, Max.Y - PlayerLoc.Y,
			PlayerLoc.Z - Min.Z, Max.Z - PlayerLoc.Z);
	}
}

FText UMetamorphosisStorySubsystem::GetObjectiveText() const
{
	const FStoryNodeConfig* Node = GetCurrentNode();
	if (Node == nullptr || !IsGateActive())
	{
		return FText::GetEmpty();
	}

	// 保持阶段：和任务 HUD 保持一致（{N} 换成剩余秒数）
	if (bGateHolding && GateHoldStartTime >= 0.0 && !GateObjectiveHoldText.IsEmpty())
	{
		const double Held = GateHoldElapsedSeconds;
		const int32 Remaining = FMath::Max(0, FMath::CeilToInt(GateHoldSeconds - Held));
		return FText::FromString(GateObjectiveHoldText.Replace(TEXT("{N}"), *FString::FromInt(Remaining)));
	}
	return FText::FromString(Node->ObjectiveText);
}

FText UMetamorphosisStorySubsystem::GetSpeakerText() const
{
	const FStoryNodeConfig* Node = GetCurrentNode();
	return FText::FromString(Node != nullptr ? Node->Speaker : FString());
}

FText UMetamorphosisStorySubsystem::GetBodyText() const
{
	const FStoryNodeConfig* Node = GetCurrentNode();
	if (Node == nullptr)
	{
		return FText::FromString(FString());
	}
	// 分页：返回"当前页"；没超过阈值时 Pages 只有 1 页，等于原文
	return FText::FromString(GetCurrentPageString());
}

// ---------------------------------------------------------------------------
// 文本自动分页（2026-10-17）
//
// 长文本一次性塞进纸面板的书写区会溢出，所以按 DialogueMaxCharsPerPage 切页：
//   * 优先在中文标点（。！？…；，、：）或换行后断，不切断词；
//   * 每页收下半页以上才找断点，避免切出只有几个字的页；
//   * 只做**排版切分**，不改配表文案；
//   * 翻页由 Advance() 负责：还有下一页就只翻页，最后一页点完才推进节点。
// ---------------------------------------------------------------------------

namespace
{
	/** 适合断页的字符（中文标点 + 换行；英文标点顺手带上） */
	bool IsStoryPageBreakCharSub(const TCHAR Ch)
	{
		switch (Ch)
		{
		case TEXT('。'): case TEXT('！'): case TEXT('？'): case TEXT('…'):
		case TEXT('；'): case TEXT('，'): case TEXT('、'): case TEXT('：'):
		case TEXT('.'): case TEXT('!'): case TEXT('?'): case TEXT(';'): case TEXT(','):
		case TEXT('\n'): case TEXT('~'):
			return true;
		default:
			return false;
		}
	}
}

void UMetamorphosisStorySubsystem::RebuildPages(const FString& RawText)
{
	Pages.Reset();
	PageIndex = 0;

	const int32 MaxChars = FMath::Max(20, DialogueMaxCharsPerPage);
	const int32 Len = RawText.Len();

	if (Len <= MaxChars)
	{
		FString Only = RawText;
		Only.TrimStartAndEndInline();
		Pages.Add(Only);
		return;
	}

	int32 Pos = 0;
	while (Pos < Len)
	{
		int32 CutEnd = Pos + FMath::Min(MaxChars, Len - Pos);

		if (CutEnd < Len)
		{
			const int32 SearchFloor = Pos + FMath::Max(1, MaxChars / 2);
			for (int32 i = CutEnd - 1; i >= SearchFloor; --i)
			{
				if (IsStoryPageBreakCharSub(RawText[i]))
				{
					CutEnd = i + 1;
					break;
				}
			}
		}

		FString Page = RawText.Mid(Pos, CutEnd - Pos);
		Page.TrimStartAndEndInline();
		if (!Page.IsEmpty())
		{
			Pages.Add(Page);
		}

		Pos = CutEnd;
		while (Pos < Len && FChar::IsWhitespace(RawText[Pos]))
		{
			++Pos;
		}
	}

	if (Pages.Num() == 0)
	{
		Pages.Add(FString());
	}

	if (bLogStory && Pages.Num() > 1)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 文本分页：%s 原文 %d 字 -> %d 页（每页上限 %d 字）"),
			*GetCurrentNodeId().ToString(), Len, Pages.Num(), MaxChars);
	}
}

FString UMetamorphosisStorySubsystem::GetCurrentPageString() const
{
	if (Pages.Num() > 1 && Pages.IsValidIndex(PageIndex))
	{
		return Pages[PageIndex];
	}
	const FStoryNodeConfig* Node = GetCurrentNode();
	return Node != nullptr ? Node->Text : FString();
}

bool UMetamorphosisStorySubsystem::HasChoice() const
{
	const FStoryNodeConfig* Node = GetCurrentNode();
	return Node != nullptr && Node->HasChoice();
}

FText UMetamorphosisStorySubsystem::GetChoiceText(int32 Index) const
{
	const FStoryNodeConfig* Node = GetCurrentNode();
	if (Node == nullptr)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(Index == 0 ? Node->ChoiceA_Text : Node->ChoiceB_Text);
}

// ------------------------------------------------------------------
// 调试命令（无头验证就靠这几条跑完整条开场）
// ------------------------------------------------------------------
namespace MetamorphosisStoryConsole
{
	UMetamorphosisStorySubsystem* StoryOf(UWorld* World)
	{
		return World != nullptr ? UMetamorphosisStorySubsystem::Get(World) : nullptr;
	}

	void Play(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisStorySubsystem* Story = StoryOf(World))
		{
			if (Args.Num() >= 1)
			{
				Story->StartStory(FName(*Args[0]));
			}
			else
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Story.Play <NodeId>"));
			}
		}
	}

	void Restart(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisStorySubsystem* Story = StoryOf(World))
		{
			Story->StartOpening();
		}
	}

	/** 直接结束当前过场（调试/截图用：不然进关卡就会一直黑屏演开场） */
	void Stop(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisStorySubsystem* Story = StoryOf(World))
		{
			Story->StopStory();
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 剧情已强制结束（Meta.Story.Stop）"));
		}
	}

	void Info(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisStorySubsystem* Story = StoryOf(World);
		if (Story == nullptr)
		{
			return;
		}
		const FStoryNodeConfig* Node = Story->GetCurrentNode();
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] ===== Meta.Story.Info ====="));
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 播放中=%d 当前节点=%s 已播过开场=%d 黑幕显示=%d 玩法门=%d"),
			Story->IsPlaying() ? 1 : 0, *Story->GetCurrentNodeId().ToString(), Story->HasPlayedOpening() ? 1 : 0,
			Story->IsOverlayVisible() ? 1 : 0, Story->IsGateActive() ? 1 : 0);
		if (Story->IsGateActive())
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 玩法门：%s"), *Story->DescribeGate());
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 任务HUD目标文字：%s"), *Story->GetObjectiveText().ToString());
		}
		if (Node != nullptr)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 章节=%s 说话人=%s 分支=%d"),
				*Node->Scene, *Node->Speaker, Node->HasChoice() ? 1 : 0);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 文本=%s"), *Node->Text.Replace(TEXT("\n"), TEXT(" / ")));
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] Next=%s Effect=%s Condition=%s"),
				*Node->Next, *Node->Effect, *Node->Condition);
			if (Node->HasChoice())
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 选项 A：%s -> %s ｜ 选项 B：%s -> %s"),
					*Node->ChoiceA_Text, *Node->ChoiceA_Next, *Node->ChoiceB_Text, *Node->ChoiceB_Next);
			}
		}
	}

	void Advance(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisStorySubsystem* Story = StoryOf(World))
		{
			Story->Advance();
		}
	}

	void Choose(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisStorySubsystem* Story = StoryOf(World);
		if (Story == nullptr || Args.Num() < 1)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Story.Choose <A|B|1|2>"));
			return;
		}
		const FString S = Args[0].TrimStartAndEnd().ToUpper();
		const int32 Index = (S == TEXT("A") || S == TEXT("1")) ? 0 : 1;
		Story->ChooseOption(Index);
	}

	/** 临时文本（剧情道具的回忆文本）：Meta.Story.Inline "<说话人>" "<正文>" */
	void Inline(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisStorySubsystem* Story = StoryOf(World);
		if (Story == nullptr)
		{
			return;
		}
		if (Args.Num() < 2)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Story.Inline \"<说话人>\" \"<正文>\""));
			return;
		}
		const bool bOk = Story->PlayInlineText(
			FText::FromString(Args[0]), FText::FromString(Args[1]),
			[]() { UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 临时文本读完（回调已执行）")); });
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] Meta.Story.Inline -> %s"), bOk ? TEXT("开始播放") : TEXT("被拒绝（见上面日志）"));
	}

	static FAutoConsoleCommandWithWorldAndArgs GStoryPlay(
		TEXT("Meta.Story.Play"), TEXT("从指定节点开始播剧情：Meta.Story.Play <NodeId>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Play));
	static FAutoConsoleCommandWithWorldAndArgs GStoryRestart(
		TEXT("Meta.Story.Restart"), TEXT("重放开场（忽略本局已播过标记）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Restart));
	static FAutoConsoleCommandWithWorldAndArgs GStoryStop(
		TEXT("Meta.Story.Stop"), TEXT("强制结束当前剧情过场（调试/截图用）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stop));
	static FAutoConsoleCommandWithWorldAndArgs GStoryInline(
		TEXT("Meta.Story.Inline"), TEXT("播放一段临时文本（不走配表）：Meta.Story.Inline \"说话人\" \"正文\""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Inline));
	void GateInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisStorySubsystem* Story = StoryOf(World);
		if (Story == nullptr)
		{
			return;
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] ===== Meta.Story.Gate ====="));
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] %s"), *Story->DescribeGate());
		if (Story->IsGateActive())
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 任务HUD目标文字：%s"), *Story->GetObjectiveText().ToString());
		}
	}

	/** Region 门的专用诊断：打印区域边界（含 8 个角）、玩家坐标与逐轴差距，现场核对区域对不对用 */
	void Region(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		if (UMetamorphosisStorySubsystem* Story = StoryOf(World))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] ===== Meta.Story.Region ====="));
			Story->LogRegionDiagnostics();
		}
	}

	/** 模拟"按下交互键"（无头验证 Interact 门用；真机上由 PC 的交互输入记录） */
	void Press(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController()))
		{
			PC->NotifyInteractInput();
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 已模拟按下交互键（Meta.Story.Press）"));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GStoryRegion(
		TEXT("Meta.Story.Region"), TEXT("打印 Region 门的区域盒/8 个角/玩家坐标与逐轴差距"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Region));
	static FAutoConsoleCommandWithWorldAndArgs GStoryGate(
		TEXT("Meta.Story.Gate"), TEXT("打印当前玩法门状态（类型/目标/限时/是否满足）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GateInfo));
	static FAutoConsoleCommandWithWorldAndArgs GStoryPress(
		TEXT("Meta.Story.Press"), TEXT("模拟按下交互键（验证 Interact 类型的玩法门）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Press));
	static FAutoConsoleCommandWithWorldAndArgs GStoryInfo(
		TEXT("Meta.Story.Info"), TEXT("打印当前剧情节点/文本/选项/播放状态"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Info));
	static FAutoConsoleCommandWithWorldAndArgs GStoryAdvance(
		TEXT("Meta.Story.Advance"), TEXT("推进到下一节点（分支节点无效）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Advance));
	static FAutoConsoleCommandWithWorldAndArgs GStoryChoose(
		TEXT("Meta.Story.Choose"), TEXT("选择分支：Meta.Story.Choose <A|B|1|2>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Choose));
}











