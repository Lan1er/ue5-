// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Interaction/MetamorphosisInteractionComponent.h"
#include "Config/GameConfigTypes.h"
#include "Config/MetamorphosisConfigSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "MetamorphosisPlayerController.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Story/MetamorphosisStorySubsystem.h"

namespace
{
	/** 标签前缀：Meta.Item:BrownSugar / Meta.Item:BrownSugar:3 */
	const TCHAR* const ItemTagPrefix = TEXT("Meta.Item:");

	/** 配表里"剧情道具"的分类名（Items.Category）；大小写不敏感比较 */
	const TCHAR* const StoryItemCategory = TEXT("Story");
}

UMetamorphosisInteractionComponent::UMetamorphosisInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMetamorphosisInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	// 这些值来自 ini（config）：UE 先跑构造函数再套用配置，所以只在这里读才拿到玩家/地编真正填的值。
	// （组件实例是从 CDO 拷的属性，BeginPlay 时已经是 ini 的值；这里只做一次合法性夹取 + 日志）
	InteractRangeCm = FMath::Max(1.f, InteractRangeCm);
	InteractScanInterval = FMath::Clamp(InteractScanInterval, 0.02f, 5.f);

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Interact] 交互组件就绪：范围 %.0f cm，扫描间隔 %.2f 秒，待机表现 %s"),
		InteractRangeCm, InteractScanInterval, bInteractBob ? TEXT("开") : TEXT("关"));

	ForceScan();
}

void UMetamorphosisInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 定时扫描：不是每帧遍历 Actor（地图上几百个 Actor，0.15 秒一次足够跟手）
	TimeSinceScan += DeltaTime;
	if (TimeSinceScan < InteractScanInterval)
	{
		return;
	}
	TimeSinceScan = 0.f;

	ScanForTargets();
}

void UMetamorphosisInteractionComponent::ForceScan()
{
	TimeSinceScan = 0.f;
	ScanForTargets();
}

// ---------------------------------------------------------------------------
// 扫描
// ---------------------------------------------------------------------------

bool UMetamorphosisInteractionComponent::ParseItemTag(const FString& Tag, FString& OutItemId, int32& OutCount)
{
	if (!Tag.StartsWith(ItemTagPrefix, ESearchCase::IgnoreCase))
	{
		return false;
	}

	FString Rest = Tag.RightChop(FCString::Strlen(ItemTagPrefix));
	Rest.TrimStartAndEndInline();
	if (Rest.IsEmpty())
	{
		return false;
	}

	// 可选的数量：Meta.Item:Cereal:3
	FString IdPart = Rest;
	FString CountPart;
	if (Rest.Split(TEXT(":"), &IdPart, &CountPart))
	{
		IdPart.TrimStartAndEndInline();
		CountPart.TrimStartAndEndInline();
	}

	if (IdPart.IsEmpty())
	{
		return false;
	}

	OutItemId = IdPart;
	OutCount = CountPart.IsEmpty() ? 1 : FMath::Max(1, FCString::Atoi(*CountPart));
	return true;
}

EMetamorphosisInteractMode UMetamorphosisInteractionComponent::ResolveMode(
	EMetamorphosisInteractMode Requested, const FItemConfigEntry& Entry)
{
	// ★ 全项目唯一一处「不可携带 -> 就地吃」规则
	if (Requested != EMetamorphosisInteractMode::Auto)
	{
		return Requested;
	}

	return Entry.bCarryable ? EMetamorphosisInteractMode::PickUp : EMetamorphosisInteractMode::EatInPlace;
}

const TCHAR* UMetamorphosisInteractionComponent::ModeToString(EMetamorphosisInteractMode Mode)
{
	switch (Mode)
	{
	case EMetamorphosisInteractMode::PickUp:      return TEXT("拾取");
	case EMetamorphosisInteractMode::EatInPlace:  return TEXT("就地吃");
	default:                                      return TEXT("自动");
	}
}

FString UMetamorphosisInteractionComponent::MakePromptText(EMetamorphosisInteractMode Mode,
	const FString& DisplayName, const FText& Override)
{
	if (!Override.IsEmpty())
	{
		return Override.ToString();
	}

	return Mode == EMetamorphosisInteractMode::PickUp
		? FString::Printf(TEXT("拾取 %s"), *DisplayName)
		: FString::Printf(TEXT("吃掉 %s"), *DisplayName);
}

const FItemConfigEntry* UMetamorphosisInteractionComponent::FindItemConfig(const FString& ItemId) const
{
	if (ItemId.IsEmpty())
	{
		return nullptr;
	}

	const AActor* Owner = GetOwner();
	const UGameInstance* GameInstance = Owner != nullptr ? Owner->GetGameInstance() : nullptr;
	const UMetamorphosisConfigSubsystem* ConfigSubsystem = GameInstance != nullptr
		? GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>()
		: nullptr;

	return ConfigSubsystem != nullptr ? ConfigSubsystem->GetItem(FName(*ItemId)) : nullptr;
}

UMetamorphosisInventoryComponent* UMetamorphosisInteractionComponent::GetInventory() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->FindComponentByClass<UMetamorphosisInventoryComponent>() : nullptr;
}

void UMetamorphosisInteractionComponent::WarnMissingItemIdOnce(const FString& ItemId, const FString& SourceName)
{
	if (WarnedMissingItemIds.Contains(ItemId))
	{
		return;
	}
	WarnedMissingItemIds.Add(ItemId);

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Interact] %s 写的道具 ID「%s」在配表里没有（大小写不敏感）；可用 Meta.Inv.List 看有哪些道具"),
		*SourceName, *ItemId);
}

void UMetamorphosisInteractionComponent::ScanForTargets()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (World == nullptr || OwnerActor == nullptr)
	{
		CurrentTarget.Reset();
		return;
	}

	const FVector Origin = OwnerActor->GetActorLocation();

	FMetamorphosisInteractTarget Best;
	float BestDistance = TNumericLimits<float>::Max();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor == OwnerActor || Actor->IsActorBeingDestroyed() || !Actor->IsActorInitialized())
		{
			continue;
		}

		// ---- 解析候选：正式 Actor 路 / 标签路 ----
		FString ItemId;
		int32 Count = 1;
		EMetamorphosisInteractMode RequestedMode = EMetamorphosisInteractMode::Auto;
		FText PromptOverride;
		float RangeOverride = 0.f;
		FVector Location = Actor->GetActorLocation();
		bool bIsCandidate = false;
		bool bIsEscapeCandidate = false;

		if (const AMetamorphosisInteractable* Interactable = Cast<AMetamorphosisInteractable>(Actor))
		{
			if (!Interactable->ItemId.IsNone())
			{
				ItemId = Interactable->ItemId.ToString();
				Count = FMath::Max(1, Interactable->Count);
				RequestedMode = Interactable->Mode;
				PromptOverride = Interactable->PromptTextOverride;
				RangeOverride = Interactable->InteractRangeOverrideCm;
				Location = Interactable->GetInteractLocation();
				bIsCandidate = true;
			}
		}
		else
		{
			// 标签路 A：通关点（门缝）——没有道具配表，交互后直接通关（2026-10-18）
			if (!EscapeTag.IsEmpty() && Actor->ActorHasTag(FName(*EscapeTag)))
			{
				// 通关点优先于同距离的道具：清掉道具相关字段，只留"这是个通关点"
				ItemId.Reset();
				Count = 1;
				RequestedMode = EMetamorphosisInteractMode::Auto;
				PromptOverride = FText::GetEmpty();
				RangeOverride = 0.f;
				bIsEscapeCandidate = true;
				bIsCandidate = true;
			}

			// 标签路 B：地编/美术不用换类，给任意 Actor 打 "Meta.Item:<Id>[:N]" 即可
			if (!bIsCandidate)
			{
				for (const FName& Tag : Actor->Tags)
				{
					if (ParseItemTag(Tag.ToString(), ItemId, Count))
					{
						bIsCandidate = true;
						break;
					}
				}
			}
		}

		if (!bIsCandidate)
		{
			continue;
		}

		// 通关点不需要道具配表（它就是"出口"，不是一件道具）
		const FItemConfigEntry* Entry = nullptr;
		if (!bIsEscapeCandidate)
		{
			Entry = FindItemConfig(ItemId);
			if (Entry == nullptr)
			{
				WarnMissingItemIdOnce(ItemId, Actor->GetName());
				continue;
			}
		}

		// ---- 距离筛选：取最近的、在交互范围内的一个 ----
		// 通关点用自己的距离（默认 200cm），它的 Actor 往往只是为了"框个位置"而摆的。
		// 通关点用 **2D（水平）距离** + 高度容差：缝隙的 Actor 常常摆在门框/墙脚，Z 与玩家差很多，
		// 用 3D 距离会出现"人贴在缝隙下面却判定太远"的假失败（2026-10-18 实测踩到）。
		const float Range = bIsEscapeCandidate
			? (EscapeRangeCm > 0.f ? EscapeRangeCm : FMath::Max(1.f, InteractRangeCm))
			: (RangeOverride > 0.f ? RangeOverride : FMath::Max(1.f, InteractRangeCm));
		const float Distance = bIsEscapeCandidate
			? FVector::Dist2D(Origin, Location)
			: FVector::Dist(Origin, Location);
		const bool bHeightTooFar = bIsEscapeCandidate
			&& FMath::Abs(Location.Z - Origin.Z) > FMath::Max(1.f, EscapeHeightToleranceCm);
		if (Distance > Range || Distance >= BestDistance || bHeightTooFar)
		{
			continue;
		}

		Best.Actor = Actor;
		Best.ItemId = Entry != nullptr ? Entry->ItemId : FString();
		Best.Count = Count;
		Best.Mode = Entry != nullptr ? ResolveMode(RequestedMode, *Entry) : EMetamorphosisInteractMode::Auto;
		Best.DistanceCm = Distance;
		Best.Config = Entry;
		Best.PromptOverride = PromptOverride;
		Best.bIsEscape = bIsEscapeCandidate;
		Best.bIsStoryItem = Entry != nullptr
			&& Entry->Category.Equals(StoryItemCategory, ESearchCase::IgnoreCase);
		BestDistance = Distance;
	}

	CurrentTarget = Best;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

FString UMetamorphosisInteractionComponent::GetPromptText() const
{
	if (!CurrentTarget.IsValid())
	{
		return FString();
	}

	// 通关点：提示文字来自 ini（EscapePromptText），不走道具文案
	if (CurrentTarget.bIsEscape)
	{
		return EscapePromptText.ToString();
	}

	// 剧情道具：交互后先播回忆文本（2026-10-18 用户要求），提示用「查看 <名>」
	if (CurrentTarget.bIsStoryItem && CurrentTarget.PromptOverride.IsEmpty())
	{
		// ⚠️ 这里不能用 FString::Printf(*Format, ...)：UE 的格式串检查要求格式是字面量，
		//    运行期拼出来的 FString 过不了检查，所以直接做占位符替换。
		FString Result = StoryPromptFormat.IsEmpty() ? FString(TEXT("查看 %s")) : StoryPromptFormat;
		Result.ReplaceInline(TEXT("%s"), *CurrentTarget.Config->DisplayName);
		return Result;
	}

	// 兜底：历史上有"没有道具配表的目标"（通关点走的是上面两条早退），这里再加一层判空
	return MakePromptText(CurrentTarget.Mode,
		CurrentTarget.Config != nullptr ? CurrentTarget.Config->DisplayName : FString(),
		CurrentTarget.PromptOverride);
}

FText UMetamorphosisInteractionComponent::GetPromptTextText() const
{
	return FText::FromString(GetPromptText());
}

FString UMetamorphosisInteractionComponent::GetTargetDisplayName() const
{
	return CurrentTarget.Config != nullptr ? CurrentTarget.Config->DisplayName : FString();
}

bool UMetamorphosisInteractionComponent::IsInteractionBlocked() const
{
	if (UGameplayStatics::IsGamePaused(this))
	{
		return true;
	}

	// 弹窗暂停也是走 SetGamePaused，但这里再确认一次控制器状态，双保险
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(OwnerPawn->GetController()))
		{
			return Controller->IsGameplayPaused();
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// 执行交互
// ---------------------------------------------------------------------------

bool UMetamorphosisInteractionComponent::IsStoryTextPlaying() const
{
	// 读回忆文本期间 / 读完回忆文本后在播"后续台词"期间，都算"流程进行中" -> 交互全锁
	if (bAwaitingStoryItemPickup)
	{
		return true;
	}

	const UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this);
	return Story != nullptr && Story->IsInlineTextPlaying();
}

bool UMetamorphosisInteractionComponent::PlayFollowUpThenPickup(
	const FString& NodeId, const FString& ItemId, int32 Count, TWeakObjectPtr<AActor> TargetActor)
{
	UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this);
	if (Story == nullptr || NodeId.IsEmpty())
	{
		return false;
	}

	TWeakObjectPtr<UMetamorphosisInteractionComponent> SelfWeak(this);
	const bool bStarted = Story->PlayStoryThen(
		FName(*NodeId),
		[SelfWeak, ItemId, Count, TargetActor]()
		{
			if (UMetamorphosisInteractionComponent* Self = SelfWeak.Get())
			{
				// 台词播完（读完 / 跳过 / 被打断）-> 解除锁 -> 才真正拾取
				Self->bAwaitingStoryItemPickup = false;
				Self->CompleteStoryItemPickup(ItemId, Count, TargetActor);
			}
		});

	if (bStarted)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Interact] 剧情道具 %s：回忆读完 -> 播后续台词节点 %s -> 台词结束才拾取"), *ItemId, *NodeId);
	}
	return bStarted;
}

void UMetamorphosisInteractionComponent::CompleteStoryItemPickup(
	const FString& ItemId, int32 Count, TWeakObjectPtr<AActor> TargetActor)
{
	// 读回忆文本期间玩家不能动，但 Actor 仍可能被别的东西销毁 -> 用弱指针
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Interact] 读完回忆文本，但主角身上没有物品栏组件，拾取 %s 失败"), *ItemId);
		return;
	}

	if (!Inventory->AddItemFromWorld(FName(*ItemId), FMath::Max(1, Count), TargetActor.Get()))
	{
		// 背包放不下：物件**保留**，玩家可以再走近读一次（不会白读）
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Interact] 读完回忆文本，但拾取 %s x%d 失败（背包放不下），物件保留在原地"), *ItemId, Count);
		return;
	}

	AActor* Actor = TargetActor.Get();
	const FString ActorName = GetNameSafe(Actor);
	if (Actor != nullptr)
	{
		Actor->Destroy();
	}

	CurrentTarget.Reset();
	ForceScan();

	// 成功交互才扣次数（配表 Periods 的探索机会；剧情道具"读完才拾取"也算 1 次）
	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		Stats->ConsumeExploreChance();
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 读完回忆文本 -> 拾取 %s x%d（%s）-> 已进快捷栏"),
		*ItemId, Count, *ActorName);
}

bool UMetamorphosisInteractionComponent::TryInteract()
{
	if (IsInteractionBlocked())
	{
		if (bLogInteract)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 游戏暂停中，不响应交互"));
		}
		return false;
	}

	// 剧情道具的回忆文本正在播：一切交互都拒绝（防重复拾取 / 防连点）
	if (IsStoryTextPlaying())
	{
		if (bLogInteract)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 正在读剧情道具的回忆文本，忽略本次交互"));
		}
		return false;
	}

	// 目标可能已经过期（走开了 / 被销毁了）：先重扫一次，保证用的是最新状态
	if (!CurrentTarget.IsValid())
	{
		ForceScan();
	}

	if (!CurrentTarget.IsValid())
	{
		if (bLogInteract)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 附近没有可交互的东西"));
		}
		return false;
	}

	// 拷贝一份：下面会把 Actor 销毁，销毁后不能再读 CurrentTarget
	const FMetamorphosisInteractTarget Target = CurrentTarget;

	// ---- 通关点（门缝）：不扣道具、不销毁物件，直接交给周目系统判定通关 ----
	if (Target.bIsEscape)
	{
		if (bLogEscape)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 从缝隙出去（%s）：请求通关"),
				*GetNameSafe(Target.Actor.Get()));
		}

		if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
		{
			const bool bAccepted = Loop->RequestWin(TEXT("交互：从缝隙出去"));
			if (!bAccepted)
			{
				if (bLogEscape)
				{
					UE_LOG(LogMetamorphosis, Warning,
						TEXT("[Interact] 通关请求被拒绝（已经通关 / 缺少必需道具 / 结算中）"));
				}

				// 拒绝时给玩家长一句提示（否则点了没反应，玩家会以为坏了）。
				// 用剧情对话框播一句话：打字机/分页/暂停冻结全部复用；已有剧情在播时 PlayInlineText 会自己返回 false。
				if (!EscapeLockedText.IsEmpty())
				{
					if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
					{
						Story->PlayInlineText(FText::FromString(TEXT("主控")), EscapeLockedText, nullptr);
					}
				}
			}
			return bAccepted;
		}

		UE_LOG(LogMetamorphosis, Warning, TEXT("[Interact] 拿不到周目结算子系统，通关请求发不出去"));
		return false;
	}

	AActor* TargetActor = Target.Actor.Get();
	const FItemConfigEntry& Entry = *Target.Config;

	// ---- 探索机会（2026-10-19，严格按配表 Periods 的 ExploreChances）----
	// 每个时段只能成功交互 N 次；用完就拒绝并给玩家一句提示（否则点了没反应，像坏了）。
	// 注意：上面的"通关点"不消耗次数（它不是翻找东西，而且必须永远能走）。
	UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this);
	if (Stats != nullptr && !Stats->HasExploreChanceLeft())
	{
		if (bLogInteract)
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Interact] 本时段（%s）探索机会已用完（0/%d），拒绝交互"),
				*Stats->GetCurrentPeriodDisplayName().ToString(), Stats->GetExploreChancesMax());
		}

		if (!NoExploreChanceText.IsEmpty())
		{
			if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
			{
				Story->PlayInlineText(FText::FromString(TEXT("主控")), NoExploreChanceText, nullptr);
			}
		}

		return false;
	}

	// ---- 剧情道具（2026-10-18）：交互 -> 先播它的回忆文本（Description）-> 读完/跳过后才拾取 ----
	if (Target.bIsStoryItem && !Entry.Description.TrimStartAndEnd().IsEmpty())
	{
		UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this);
		if (Story != nullptr)
		{
			const FString ItemIdCopy = Entry.ItemId;
			const int32 CountCopy = FMath::Max(1, Target.Count);
			const TWeakObjectPtr<AActor> ActorWeak = Target.Actor;
			TWeakObjectPtr<UMetamorphosisInteractionComponent> SelfWeak(this);

			// 后续台词节点（策划对话表 B 组，Items.FollowUpStoryNode）：读完回忆文本后再播一句人物台词，
			// 这句播完才拾取。留空 = 读完直接拾取（老行为）。
			const FString FollowUpCopy = Entry.FollowUpStoryNode;

			// 说话人：ini 配了就用配的，否则用道具显示名（不写死文案）
			const FString SpeakerStr = StoryItemSpeaker.IsEmpty() ? Entry.DisplayName : StoryItemSpeaker;
			const FString ActorName = GetNameSafe(TargetActor);

			// 从这一刻起锁住交互（回忆文本 + 后续台词 + 拾取 整条流程），防连点重复拾取
			bAwaitingStoryItemPickup = true;

			const bool bStarted = Story->PlayInlineText(
				FText::FromString(SpeakerStr),
				FText::FromString(Entry.Description),
				[SelfWeak, ItemIdCopy, CountCopy, ActorWeak, FollowUpCopy]()
				{
					UMetamorphosisInteractionComponent* Self = SelfWeak.Get();
					if (Self == nullptr)
					{
						return;
					}

					// 有后续台词：先播它，播完再拾取（PlayFollowUpThenPickup 内部会清锁）
					if (!FollowUpCopy.IsEmpty()
						&& Self->PlayFollowUpThenPickup(FollowUpCopy, ItemIdCopy, CountCopy, ActorWeak))
					{
						return;
					}

					// 没有后续台词（或播不了）：解除锁 -> 直接拾取
					Self->bAwaitingStoryItemPickup = false;
					Self->CompleteStoryItemPickup(ItemIdCopy, CountCopy, ActorWeak);
				});

			if (bStarted)
			{
				UE_LOG(LogMetamorphosis, Log,
					TEXT("[Interact] 剧情道具 %s（%s）：播放回忆文本（%d 字），读完后再拾取（后续台词=%s）"),
					*Entry.DisplayName, *Entry.ItemId, Entry.Description.Len(),
					FollowUpCopy.IsEmpty() ? TEXT("无") : *FollowUpCopy);
				return true;
			}

			// 播不了（已有剧情在播等）：解锁、不拾取，让玩家重来一次；日志已经把原因打出来了
			bAwaitingStoryItemPickup = false;
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Interact] 剧情道具 %s 的回忆文本播放失败（可能已有剧情在播），本次不拾取"), *ActorName);
			return false;
		}
	}

	if (Target.Mode == EMetamorphosisInteractMode::EatInPlace)
	{
		// 就地吃：不进背包，直接结算配表效果（和"使用道具"共用同一份 ApplyItemEffects）
		FString Summary;
		UMetamorphosisInventoryComponent::ApplyItemEffects(this, Entry, Summary);

		// 事件推进：就地吃掉也算"获得了这件道具"，任务系统要靠它推进（走 AddItem 的那条路已经在组件里广播了）
		if (UMetamorphosisInventoryComponent* Inventory = GetInventory())
		{
			Inventory->NotifyItemObtained(FName(*Entry.ItemId));
		}

		if (TargetActor != nullptr)
		{
			TargetActor->Destroy();
		}
		CurrentTarget.Reset();
		ForceScan();

		// 成功交互才扣次数（配表 Periods 的探索机会）
		if (Stats != nullptr)
		{
			Stats->ConsumeExploreChance();
		}

		UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 就地吃掉 %s（%s）x%d：%s；物件已移除"),
			*Entry.DisplayName, *Entry.ItemId, Target.Count, *Summary);
		return true;
	}

	// 拾取进背包
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Interact] 主角身上没有物品栏组件，拾取 %s 失败"), *Entry.DisplayName);
		return false;
	}

	const FName ItemIdName(*Target.ItemId);
	// ⚠️ 用 AddItemFromWorld 而不是 AddItem：它会把场景里这个 Actor 的**世界模型**记进槽位，
	//    之后在背包里丢弃时就能把原本的模型还原出来（2026-10-18 用户要求）。
	if (!Inventory->AddItemFromWorld(ItemIdName, Target.Count, TargetActor))
	{
		// 拾取失败（背包满 / 配表 bCarryable=false）：**不销毁**物件，提示也保留，玩家可以再来一次
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Interact] 拾取 %s（%s）失败：背包放不下或不可携带，物件保留在原地"),
			*Entry.DisplayName, *Entry.ItemId);
		return false;
	}

	if (TargetActor != nullptr)
	{
		TargetActor->Destroy();
	}
	CurrentTarget.Reset();
	ForceScan();

	// 成功交互才扣次数（配表 Periods 的探索机会）
	if (Stats != nullptr)
	{
		Stats->ConsumeExploreChance();
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Interact] 拾取 %s（%s）x%d -> 已进快捷栏"),
		*Entry.DisplayName, *Entry.ItemId, Target.Count);
	return true;
}

// ---------------------------------------------------------------------------
// 调试命令（Meta.Interact.*）
//
// 放在交互组件自己这里（而不是塞进 MetamorphosisInventoryComponent.cpp）：全项目 Meta.* 命令
// 都按"谁的系统谁提供"来放，风格与那边的 MetamorphosisInventoryConsole 完全一致。
// ---------------------------------------------------------------------------

#if !UE_BUILD_SHIPPING
namespace MetamorphosisInteractionConsole
{
	static UMetamorphosisInteractionComponent* InteractionOf(UWorld* World)
	{
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
		return Pawn != nullptr ? Pawn->FindComponentByClass<UMetamorphosisInteractionComponent>() : nullptr;
	}

	/** 列出世界里所有候选（不论是否在范围内），带距离与解析出的模式 */
	static void Scan(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisInteractionComponent* Interaction = InteractionOf(World);
		if (Interaction == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到交互组件（当前 Pawn 上没有）"));
			return;
		}

		const AActor* Owner = Interaction->GetOwner();
		const FVector Origin = Owner != nullptr ? Owner->GetActorLocation() : FVector::ZeroVector;
		const float Range = Interaction->InteractRangeCm;

		int32 CandidateCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor == nullptr || Actor == Owner || Actor->IsActorBeingDestroyed())
			{
				continue;
			}

			FString ItemId;
			int32 Count = 1;
			EMetamorphosisInteractMode Requested = EMetamorphosisInteractMode::Auto;
			float RangeOverride = 0.f;
			FVector Location = Actor->GetActorLocation();

			if (const AMetamorphosisInteractable* Interactable = Cast<AMetamorphosisInteractable>(Actor))
			{
				if (Interactable->ItemId.IsNone())
				{
					continue;
				}
				ItemId = Interactable->ItemId.ToString();
				Count = FMath::Max(1, Interactable->Count);
				Requested = Interactable->Mode;
				RangeOverride = Interactable->InteractRangeOverrideCm;
				Location = Interactable->GetInteractLocation();
			}
			else
			{
				// 通关点（门缝）：也列出来，方便验证"摆对没摆对"
				const bool bEscape = !Interaction->EscapeTag.IsEmpty()
					&& Actor->ActorHasTag(FName(*Interaction->EscapeTag));
				if (bEscape)
				{
					++CandidateCount;
					// 跟扫描逻辑保持一致：通关点只比水平距离，外加 Z 容差
					const float EscapeDist = FVector::Dist2D(Origin, Location);
					const float EscapeRange = Interaction->EscapeRangeCm > 0.f
						? Interaction->EscapeRangeCm : Range;
					const bool bHeightOk = FMath::Abs(Location.Z - Origin.Z)
						<= FMath::Max(1.f, Interaction->EscapeHeightToleranceCm);
					UE_LOG(LogMetamorphosis, Log,
						TEXT("[Debug]   %-22s %-14s 模式=%-10s 距离=%.0f cm / 范围 %.0f cm %s"),
						*Actor->GetName(), TEXT("<通关点>"), TEXT("通关"), EscapeDist, EscapeRange,
						(!bHeightOk) ? TEXT("(高度差太大)")
							: (EscapeDist <= EscapeRange ? TEXT("★ 在范围内") : TEXT("(太远)")));
					continue;
				}

				bool bTagged = false;
				for (const FName& Tag : Actor->Tags)
				{
					if (UMetamorphosisInteractionComponent::ParseItemTag(Tag.ToString(), ItemId, Count))
					{
						bTagged = true;
						break;
					}
				}
				if (!bTagged)
				{
					continue;
				}
			}

			++CandidateCount;
			const float Distance = FVector::Dist(Origin, Location);
			const float UseRange = RangeOverride > 0.f ? RangeOverride : Range;

			// 配表条目拿不到时只打警告（扫描本身会跳过它）
			const UGameInstance* GameInstance = World->GetGameInstance();
			const UMetamorphosisConfigSubsystem* ConfigSubsystem = GameInstance != nullptr
				? GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>() : nullptr;
			const FItemConfigEntry* Entry = ConfigSubsystem != nullptr
				? ConfigSubsystem->GetItem(FName(*ItemId)) : nullptr;

			if (Entry == nullptr)
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug]   %-22s %-18s 配表里没有这个 ID，距离 %.0f cm"),
					*Actor->GetName(), *ItemId, Distance);
				continue;
			}

			const EMetamorphosisInteractMode Mode = UMetamorphosisInteractionComponent::ResolveMode(Requested, *Entry);
			// 模式标签先拼好再进日志：UE 的编译期格式串校验对 "多段 %s 三元表达式" 会报错
			const bool bStoryItem = Entry->Category.Equals(StoryItemCategory, ESearchCase::IgnoreCase);
			const FString ModeLabel = bStoryItem
				? FString(TEXT("剧情道具"))
				: FString::Printf(TEXT("%s%s"),
					UMetamorphosisInteractionComponent::ModeToString(Mode),
					Requested == EMetamorphosisInteractMode::Auto ? TEXT("(自动)") : TEXT("(强制)"));
			const TCHAR* RangeNote = Distance <= UseRange ? TEXT("★ 在范围内") : TEXT("(太远)");

			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug]   %-22s %-14s 模式=%-10s 距离=%.0f cm / 范围 %.0f cm %s"),
				*Actor->GetName(), *Entry->DisplayName, *ModeLabel, Distance, UseRange, RangeNote);
		}

		if (CandidateCount == 0)
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug] 世界里没有候选交互物（要么摆 AMetamorphosisInteractable，要么给 Actor 打标签 Meta.Item:<ItemId>[:Count]）"));
		}
		else
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 共 %d 个候选（★ = 当前可交互范围内）"), CandidateCount);
		}
	}

	static void Info(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisInteractionComponent* Interaction = InteractionOf(World);
		if (Interaction == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到交互组件（当前 Pawn 上没有）"));
			return;
		}

		if (!Interaction->HasTarget())
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 当前没有交互目标（提示 UI 应该是隐藏的）"));
			return;
		}

		const FMetamorphosisInteractTarget& Target = Interaction->GetTarget();
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Debug] 当前目标：%s / %s / 模式=%s / x%d / 距离 %.0f cm / 提示「%s」"),
			*GetNameSafe(Target.Actor.Get()), *Target.ItemId,
			UMetamorphosisInteractionComponent::ModeToString(Target.Mode), Target.Count,
			Target.DistanceCm, *Interaction->GetPromptText());
		if (Target.bIsStoryItem)
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug] 剧情道具：交互后先播放回忆文本（%d 字），读完/跳过后才拾取进背包"),
				Target.Config != nullptr ? Target.Config->Description.Len() : 0);
		}
	}

	static void Do(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisInteractionComponent* Interaction = InteractionOf(World);
		if (Interaction == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到交互组件（当前 Pawn 上没有）"));
			return;
		}

		const bool bOk = Interaction->TryInteract();
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] Meta.Interact.Do -> %s"), bOk ? TEXT("成功") : TEXT("没做成（见上面日志）"));
	}

	/** 在运行时临时摆一个"通关点"（只活在本次进程里，不动关卡）。
	 *  用法：Meta.Interact.SpawnEscape [X Y Z] —— 不给坐标就摆在玩家正前方 80cm。
	 *  这是无头验证"从缝隙出去"专用：关卡里真正的出口要由用户摆 Actor 打标签。 */
	static void SpawnEscape(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}

		UMetamorphosisInteractionComponent* Interaction = InteractionOf(World);
		if (Interaction == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到交互组件（当前 Pawn 上没有）"));
			return;
		}

		FVector Location = FVector::ZeroVector;
		if (Args.Num() >= 3)
		{
			Location = FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
		}
		else if (const APawn* Pawn = Cast<APawn>(Interaction->GetOwner()))
		{
			Location = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 80.f;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Spawned = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (Spawned == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 临时通关点生成失败"));
			return;
		}

		Spawned->Tags.Add(FName(*Interaction->EscapeTag));
		Interaction->ForceScan();

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 已临时生成通关点 %s @ %s（标签 %s），本次进程有效"),
			*Spawned->GetName(), *Location.ToCompactString(), *Interaction->EscapeTag);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMetaInteractScan(
	TEXT("Meta.Interact.Scan"),
	TEXT("列出世界里所有候选交互物（名称 / 道具 / 模式 / 距离），★ 表示在当前交互范围内"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInteractionConsole::Scan));

static FAutoConsoleCommandWithWorldAndArgs GMetaInteractInfo(
	TEXT("Meta.Interact.Info"),
	TEXT("打印当前交互目标与提示文字"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInteractionConsole::Info));

static FAutoConsoleCommandWithWorldAndArgs GMetaInteractDo(
	TEXT("Meta.Interact.Do"),
	TEXT("执行一次交互（拾取 / 就地吃掉 / 从缝隙出去）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInteractionConsole::Do));

static FAutoConsoleCommandWithWorldAndArgs GMetaInteractSpawnEscape(
	TEXT("Meta.Interact.SpawnEscape"),
	TEXT("临时生成一个通关点（Meta.Spot:EscapeHole），只活在本次进程：Meta.Interact.SpawnEscape [X Y Z]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInteractionConsole::SpawnEscape));
#endif // !UE_BUILD_SHIPPING
