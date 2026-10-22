// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Save/MetamorphosisSaveSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "UObject/ConstructorHelpers.h"

#include "Save/MetamorphosisSaveGame.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Tasks/MetamorphosisTaskSubsystem.h"
#include "Story/MetamorphosisStorySubsystem.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Character/MetamorphosisBeetleCharacter.h"
#include "MetamorphosisPlayerController.h"

// 读档后清理场景里"已捡走"的道具要遍历 Actor
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaSave, Log, All);

namespace
{
	const TCHAR* kSlotName = TEXT("MetamorphosisSave");

	/** 把背包各格拼成一行（"1:Cereal x3 / 3:Payslip x1"），用于日志与摘要；跳过空格子 */
	FString DescribeSlots(const TArray<FString>& Ids, const TArray<int32>& Counts)
	{
		TArray<FString> Parts;
		for (int32 i = 0; i < Ids.Num(); ++i)
		{
			if (Ids[i].IsEmpty())
			{
				continue;
			}
			const int32 C = Counts.IsValidIndex(i) ? Counts[i] : 0;
			Parts.Add(FString::Printf(TEXT("%d:%s x%d"), i + 1, *Ids[i], C));
		}
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" / ")) : FString(TEXT("(空)"));
	}
}

void UMetamorphosisSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogMetaSave, Log, TEXT("[Save] 存档子系统就绪：槽=%s 版本=%d 路径=%s 自动存=%.0fs"),
		kSlotName, CurrentVersion, *GetSavePath(), AutoSaveIntervalSeconds);

	// 心跳自动存（0=关）。进关卡后由 PlayerController 再次 SaveNow("进关卡") 兜一次。
	if (AutoSaveIntervalSeconds > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(AutoSaveTimer,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					SaveNow(TEXT("心跳自动存"));
				}), AutoSaveIntervalSeconds, true);
		}
	}
}

void UMetamorphosisSaveSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoSaveTimer);
	}
	Super::Deinitialize();
}

FString UMetamorphosisSaveSubsystem::GetSavePath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (FString(kSlotName) + TEXT(".sav")));
}

bool UMetamorphosisSaveSubsystem::HasSave() const
{
	return UGameplayStatics::DoesSaveGameExist(kSlotName, 0);
}

void UMetamorphosisSaveSubsystem::FillFromWorld(UMetamorphosisSaveGame& Out) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	Out.Version = CurrentVersion;
	Out.SavedAtIso = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));

	if (const UMetamorphosisStatsSubsystem* Stats = World->GetGameInstance()->GetSubsystem<UMetamorphosisStatsSubsystem>())
	{
		Out.Suspicion = Stats->GetSuspicion();
		Out.Mood = Stats->GetMood();
		Out.DayMinutes = Stats->GetDayMinutes();

		// 时段 + 本时段剩余探索次数（2026-10-19 时间/次数系统）：
		// 读档要"照抄"而不是"按时间重算"，否则玩家存档时的剩余次数会被刷满/清空。
		Out.CurrentPeriodId = Stats->GetCurrentPeriodId().ToString();
		Out.ExploreChancesRemaining = Stats->GetExploreChancesRemaining();
	}
	if (const UMetamorphosisLoopSubsystem* Loop = World->GetGameInstance()->GetSubsystem<UMetamorphosisLoopSubsystem>())
	{
		Out.LoopCount = Loop->GetLoopCount();
	}
	if (const UMetamorphosisTaskSubsystem* Tasks = World->GetGameInstance()->GetSubsystem<UMetamorphosisTaskSubsystem>())
	{
		Out.CurrentTaskIndex = Tasks->GetCurrentIndex();
		Out.CurrentTaskId = Tasks->GetCurrentTaskId().ToString();
		for (const FString& Id : Tasks->GetObtainedItemIds())
		{
			Out.ObtainedItemIds.Add(Id);
		}
	}
	if (const UMetamorphosisStorySubsystem* Story = World->GetGameInstance()->GetSubsystem<UMetamorphosisStorySubsystem>())
	{
		if (Story->IsPlaying())
		{
			Out.StoryNodeId = Story->GetCurrentNodeId().ToString();

			// 门激活中存档：额外记下"在门里" + 门节点 Id ✓
			// 读档时用 StartStoryRestored(门节点) 重新布防 -> 门的计时/保持全部从 0 开始 ✓，
			// 任务也不会因为"门丢了"而被跳过 ✓。
			Out.bWasInGate = Story->IsGateActive();
			if (Out.bWasInGate)
			{
				const FName GateNode = Story->GetGateNodeId();
				Out.GateNodeId = GateNode.IsNone() ? Out.StoryNodeId : GateNode.ToString();
				Out.StoryNodeId = Out.GateNodeId;
			}
		}
	}

	// 背包 + 玩家
	// 注意：背包组件挂在【Beetle 角色】上（BeetleCharacter 的 CreateDefaultSubobject<UMetamorphosisInventoryComponent>），
	// 不在 PlayerController 上 -> 必须通过 PC->GetInventoryComponent()（内部是 ControlledPawn->FindComponentByClass）取。
	const AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
	if (PC != nullptr)
	{
		if (const UMetamorphosisInventoryComponent* Inv = PC->GetInventoryComponent())
		{
			Out.SlotItemIds.Reset();
			Out.SlotCounts.Reset();
			Out.SlotWorldMeshes.Reset();
			Out.SlotWorldTransforms.Reset();
			Out.SlotWorldMaterials.Reset();
			for (int32 i = 0; i < UMetamorphosisInventoryComponent::NumSlots; ++i)
			{
				const FMetamorphosisItemSlot Slot = Inv->GetSlot(i);
				Out.SlotItemIds.Add(Slot.bOccupied ? Slot.ItemId.ToString() : FString());
				Out.SlotCounts.Add(Slot.bOccupied ? Slot.Count : 0);
				Out.SlotWorldMeshes.Add(Slot.WorldMesh.IsNull() ? FString() : Slot.WorldMesh.ToSoftObjectPath().ToString());
				Out.SlotWorldTransforms.Add(Slot.WorldMeshTransform);
				if (Slot.bOccupied && Slot.WorldMeshMaterials.Num() > 0)
				{
					TArray<FString> Paths;
					for (const TSoftObjectPtr<UMaterialInterface>& M : Slot.WorldMeshMaterials)
					{
						Paths.Add(M.IsNull() ? FString() : M.ToSoftObjectPath().ToString());
					}
					Out.SlotWorldMaterials.Add(FString::Join(Paths, TEXT(";")));
				}
				else
				{
					Out.SlotWorldMaterials.Add(FString());
				}
			}
		}
	}
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		Out.PlayerLocation = Pawn->GetActorLocation();
		Out.CameraYaw = Pawn->GetActorRotation().Yaw;
		if (const AMetamorphosisBeetleCharacter* Beetle = Cast<AMetamorphosisBeetleCharacter>(Pawn))
		{
			Out.CameraYaw = Beetle->GetSpawnYawDegrees();
		}
	}
}

void UMetamorphosisSaveSubsystem::ApplyToSubsystems(const UMetamorphosisSaveGame& In)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (UMetamorphosisStatsSubsystem* Stats = World->GetGameInstance()->GetSubsystem<UMetamorphosisStatsSubsystem>())
	{
		Stats->SetSuspicion(In.Suspicion);
		Stats->SetMood(In.Mood);
		Stats->SetDayMinutes(In.DayMinutes);

		// 时段 + 剩余探索次数（2026-10-19）：**照抄存档值**，不按时间重算。
		// 注意顺序：先 SetDayMinutes（它可能会因跨时段重置配额），再用存档值覆盖。
		if (In.ExploreChancesRemaining >= 0)
		{
			Stats->SetExploreChancesRemaining(In.ExploreChancesRemaining);
		}
	}
	if (UMetamorphosisTaskSubsystem* Tasks = World->GetGameInstance()->GetSubsystem<UMetamorphosisTaskSubsystem>())
	{
		Tasks->SetObtainedItemIds(In.ObtainedItemIds);
		if (!In.CurrentTaskId.IsEmpty())
		{
			Tasks->SetTask(FName(*In.CurrentTaskId));
		}
	}
	// 背包：**一律延迟恢复** ✓（不做"立即"分支 ✗）
	// 原因：LoadNow() 可能在主界面（MainMenu 关卡）被调用 ✓ 那时 PC/Pawn 状态不可靠 ✗
	//       即使 GetInventoryComponent() 返回了非空（上一个关卡残留 ✗），也恢复不到玩家真正用的那个背包 ✗
	//       -> 统一等 ApplyPendingPlayerTransform()（游戏关卡加载完 2 秒后）再恢复 ✓✓
	// 另外：存档对象指针要**自己持有** ✓（调用方传进来的是临时对象 ✓ 不能长期引用 ✗）
	PendingInventorySource = DuplicateObject<UMetamorphosisSaveGame>(&In, this);
	bHasPendingInventory = true;
	UE_LOG(LogMetaSave, Log, TEXT("[Save] 背包延迟到进关卡后再恢复（存档里 %d 格 [%s]）"),
		In.SlotItemIds.FilterByPredicate([](const FString& S) { return !S.IsEmpty(); }).Num(),
		*DescribeSlots(In.SlotItemIds, In.SlotCounts));

	bHasPendingTransform = true;
	PendingLocation = In.PlayerLocation;
	PendingYaw = In.CameraYaw;

	// 剧情：**一律延迟恢复** ✓（2026-10-19 修）
	// 原来在这里直接 Story->StartStory(In.StoryNodeId) ✗，有两个致命后果：
	//   ① LoadNow() 可能是在主界面调的 ✗ -> 过场控件挂在主界面世界上 ✗，换关卡就没了 ✗；
	//   ② 更要命：StartStory() 会**重新结算该节点的 Effect** ✗ -> 若存档节点是 Death（或被门超时带到 Death）
	//      就在读档瞬间触发周目结算 ✗✗ -> ApplyFullLoopReset 会 **清空背包 + 把玩家传回出生点** ✗
	//      = 用户报的"读档后位置不对、背包被清空" ✓✓
	// 现在：只记节点 Id，等进关卡后（ApplyPendingPlayerTransform）用 StartStoryRestored 恢复 ✓
	//      —— 只摆文本、不重跑 Effect、不重新布防玩法门 ✓
	PendingStoryNodeId = In.StoryNodeId;
	bHasPendingStory = !In.StoryNodeId.IsEmpty();
	if (bHasPendingStory)
	{
		UE_LOG(LogMetaSave, Log, TEXT("[Save] 剧情延迟到进关卡后再恢复（节点 %s，不重跑 Effect）"), *PendingStoryNodeId);
	}

	// 读档窗口内**禁止周目结算** ✓（防御性兜底 ✗）：
	// 即便还有别的路径（门的超时、妻子抓人、剧情 Death）想结算 ✗，也先压制几秒，
	// 避免"读档瞬间就被清空背包 + 传回出生点"✓。
	if (UMetamorphosisLoopSubsystem* Loop = World->GetGameInstance()->GetSubsystem<UMetamorphosisLoopSubsystem>())
	{
		Loop->SuppressLoopReset(3.0f, TEXT("读档后保护窗口"));
	}
}

void UMetamorphosisSaveSubsystem::ApplyPendingInventory()
{
	if (!bHasPendingInventory || PendingInventorySource == nullptr)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
	UMetamorphosisInventoryComponent* Inv = (PC != nullptr) ? PC->GetInventoryComponent() : nullptr;
	if (Inv == nullptr)
	{
		return;
	}
	const UMetamorphosisSaveGame& In = *PendingInventorySource;
	Inv->ClearAllSlots();
	int32 Restored = 0;
	for (int32 i = 0; i < In.SlotItemIds.Num() && i < UMetamorphosisInventoryComponent::NumSlots; ++i)
	{
		if (In.SlotItemIds[i].IsEmpty() || In.SlotCounts.IsValidIndex(i) == false || In.SlotCounts[i] <= 0)
		{
			continue;
		}
		FMetamorphosisItemSlot Slot;
		Slot.bOccupied = true;
		Slot.ItemId = FName(*In.SlotItemIds[i]);
		Slot.Count = In.SlotCounts[i];
		if (In.SlotWorldMeshes.IsValidIndex(i) && !In.SlotWorldMeshes[i].IsEmpty())
		{
			Slot.WorldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(In.SlotWorldMeshes[i]));
		}
		if (In.SlotWorldTransforms.IsValidIndex(i))
		{
			Slot.WorldMeshTransform = In.SlotWorldTransforms[i];
		}
		if (In.SlotWorldMaterials.IsValidIndex(i) && !In.SlotWorldMaterials[i].IsEmpty())
		{
			TArray<FString> Paths;
			In.SlotWorldMaterials[i].ParseIntoArray(Paths, TEXT(";"), false);
			for (const FString& P : Paths)
			{
				Slot.WorldMeshMaterials.Add(P.IsEmpty() ? TSoftObjectPtr<UMaterialInterface>() : TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(P)));
			}
		}
		Inv->SetSlot(i, Slot);
		++Restored;
	}
	bHasPendingInventory = false;
	PendingInventorySource = nullptr;
	UE_LOG(LogMetaSave, Log, TEXT("[Save] 背包已恢复（延迟到关卡加载后）：%d 格"), Restored);
}

void UMetamorphosisSaveSubsystem::ApplyPendingPlayerTransform()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// ① 先恢复背包（必须在清点"已拾取"之前：清理逻辑要看当前背包里有什么）
	ApplyPendingInventory();

	// ② 再应用玩家位置
	if (bHasPendingTransform)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = Cast<AMetamorphosisBeetleCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)))
		{
			Beetle->SetActorLocation(PendingLocation, false, nullptr, ETeleportType::TeleportPhysics);
			Beetle->SetSpawnYawDegrees(PendingYaw);
			bHasPendingTransform = false;
			UE_LOG(LogMetaSave, Log, TEXT("[Save] 玩家位置已恢复：%s yaw=%.1f"), *PendingLocation.ToCompactString(), PendingYaw);
		}
	}

	// ③ 位置与背包都就位之后，再清理"已捡走"的场景道具：
	// 读档会重开关卡 -> 之前 Destroy 掉的道具 Actor 又回来了 -> 玩家能再捡一次（物品重复）✗
	PurgeAlreadyObtainedWorldItems();

	// ④ 最后才恢复剧情 ✓（2026-10-19 修）：
	// 这时关卡已经加载完、玩家位置也已经落在存档位置 ✓，再摆剧情过场 ✓。
	// StartStoryRestored = 只摆文本、**不重跑该节点的 Effect**（否则 Death 会立刻触发周目结算 ->
	// 清空背包 + 传回出生点 ✗）、**不重新布防玩法门**（否则读档几秒后超时 -> 同样是结算 ✗）。
	if (bHasPendingStory && !PendingStoryNodeId.IsEmpty())
	{
		bHasPendingStory = false;
		const FString NodeId = PendingStoryNodeId;
		PendingStoryNodeId.Reset();
		if (UMetamorphosisStorySubsystem* Story = World->GetGameInstance()->GetSubsystem<UMetamorphosisStorySubsystem>())
		{
			if (Story->StartStoryRestored(FName(*NodeId)))
			{
				UE_LOG(LogMetaSave, Log, TEXT("[Save] 剧情已从存档恢复：%s"), *NodeId);
			}
			else
			{
				UE_LOG(LogMetaSave, Log, TEXT("[Save] 存档节点 %s 不适合恢复（门节点/找不到），按无剧情继续"), *NodeId);
			}
		}
	}
}

void UMetamorphosisSaveSubsystem::PurgeAlreadyObtainedWorldItems()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 收集"这一局已经拿到过"的 ItemId 集合：
	// ①任务子系统的 ObtainedItemIds（本局获得/吃掉过的，含不可携带的）②当前背包里的 ③存档里记的（已由 ApplyToSubsystems 灌进任务子系统）
	TSet<FString> Obtained;
	if (const UMetamorphosisTaskSubsystem* Tasks = World->GetGameInstance()->GetSubsystem<UMetamorphosisTaskSubsystem>())
	{
		for (const FString& Id : Tasks->GetObtainedItemIds())
		{
			Obtained.Add(Id);
		}
	}
	const AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
	if (PC != nullptr)
	{
		if (const UMetamorphosisInventoryComponent* Inv = PC->GetInventoryComponent())
		{
			for (int32 i = 0; i < UMetamorphosisInventoryComponent::NumSlots; ++i)
			{
				const FMetamorphosisItemSlot Slot = Inv->GetSlot(i);
				if (Slot.bOccupied && !Slot.ItemId.IsNone())
				{
					Obtained.Add(Slot.ItemId.ToString());
				}
			}
		}
	}
	if (Obtained.Num() == 0)
	{
		return;
	}

	int32 PurgedCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		// 只处理带 Meta.Item:<ItemId> 标签的场景道具（Meta.Spot: / Meta.Wife 之类绝对不要动 ✗）
		for (const FName& Tag : Actor->Tags)
		{
			const FString TagStr = Tag.ToString();
			if (!TagStr.StartsWith(TEXT("Meta.Item:"), ESearchCase::CaseSensitive))
			{
				continue;
			}
			// 形如 Meta.Item:Cereal[:3] -> 取中间的 ItemId
			FString Rest = TagStr.Mid(FCString::Strlen(TEXT("Meta.Item:")));
			FString ItemId;
			FString CountPart;
			if (Rest.Split(TEXT(":"), &ItemId, &CountPart))
			{
				// 有数量后缀，ItemId 已取到
			}
			else
			{
				ItemId = Rest;
			}
			ItemId.TrimStartAndEndInline();
			if (ItemId.IsEmpty() || !Obtained.Contains(ItemId))
			{
				continue;
			}

			UE_LOG(LogMetaSave, Log, TEXT("[Save] 清理场景中已拾取的道具：%s（%s）"), *Actor->GetName(), *ItemId);
			Actor->Destroy();
			++PurgedCount;
			break;
		}
	}
	if (bLogSave)
	{
		UE_LOG(LogMetaSave, Log, TEXT("[Save] 已拾取道具清理完成：清掉 %d 个场景物件（获得过的道具 %d 种）"), PurgedCount, Obtained.Num());
	}
}

bool UMetamorphosisSaveSubsystem::SaveNow(const FString& Reason, bool bForce)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	// 剧情播放中 / 门激活中 不存（状态不完整）—— 但"返回主界面/退出"这种主动离场必须存（bForce），
	// 否则玩家在剧情里退出时背包和位置会丢在最后一次自动存上 ✗。结算中永远跳过。
	// ⚠️ 夜晚跳过（23:00 -> 06:00）期间**任何情况都不存**（连 bForce 也不行 ✗）：
	//    那时光间停在 23:00、剧情却已播，存下来会变成"读档 -> 又播一遍夜晚跳过"的怪档。
	if (const UMetamorphosisStatsSubsystem* NightStats = World->GetGameInstance()->GetSubsystem<UMetamorphosisStatsSubsystem>())
	{
		if (NightStats->IsNightSkipActive())
		{
			UE_LOG(LogMetaSave, Log, TEXT("[Save] 跳过存档（夜晚跳过剧情进行中）：%s"), *Reason);
			return false;
		}
	}
	if (!bForce)
	{
		if (const UMetamorphosisStorySubsystem* Story = World->GetGameInstance()->GetSubsystem<UMetamorphosisStorySubsystem>())
		{
			if (Story->IsPlaying())
			{
				UE_LOG(LogMetaSave, Verbose, TEXT("[Save] 跳过存档（剧情/门进行中）：%s"), *Reason);
				return false;
			}
		}
	}
	if (const UMetamorphosisLoopSubsystem* Loop = World->GetGameInstance()->GetSubsystem<UMetamorphosisLoopSubsystem>())
	{
		if (Loop->IsSettling())
		{
			UE_LOG(LogMetaSave, Verbose, TEXT("[Save] 跳过存档（结算中）：%s"), *Reason);
			return false;
		}
	}

	// ★★ 关键保护：玩家不在游戏关卡里（主界面 MainMenu / 关卡切换中）时绝对不能存 ✗✗
	//   此时没有 Pawn -> FillFromWorld 采集不到背包（并行数组留空）和位置（0,0,0）✗
	//   -> 一存就把玩家真实的背包与位置覆盖成"空" ✓✓
	//   用户反馈"背包里有道具，重新进入游戏道具直接消失"，根因就是心跳自动存 / 某个主动存
	//   在主界面（或切图过程中）跑了一次，把好档覆盖成了空档。
	{
		const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
		AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(PlayerPawn != nullptr ? PlayerPawn->GetController() : nullptr);
		if (PlayerPawn == nullptr || PC == nullptr || PC->GetInventoryComponent() == nullptr)
		{
			UE_LOG(LogMetaSave, Log,
				TEXT("[Save] 跳过存档（%s）：当前没有玩家角色/背包组件（在主界面或关卡切换中），不覆盖已有存档"), *Reason);
			return false;
		}
	}

	UMetamorphosisSaveGame* Save = Cast<UMetamorphosisSaveGame>(UGameplayStatics::CreateSaveGameObject(UMetamorphosisSaveGame::StaticClass()));
	if (Save == nullptr)
	{
		return false;
	}
	FillFromWorld(*Save);
	Save->SavedReason = Reason;
	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, kSlotName, 0);
	if (bLogSave)
	{
		UE_LOG(LogMetaSave, Log, TEXT("[Save] 已存档（%s）：周目 %d · 怀疑 %.0f · 躁郁 %.0f · 当天 %.0f 分钟 · 时段 %s 探索机会 %d · 任务[%d] %s · 背包 %d 格 [%s] · 剧情节点 %s · 结果=%s"),
			*Reason, Save->LoopCount, Save->Suspicion, Save->Mood, Save->DayMinutes,
			Save->CurrentPeriodId.IsEmpty() ? TEXT("(无)") : *Save->CurrentPeriodId, Save->ExploreChancesRemaining,
			Save->CurrentTaskIndex, *Save->CurrentTaskId,
			Save->SlotItemIds.FilterByPredicate([](const FString& S) { return !S.IsEmpty(); }).Num(),
			*DescribeSlots(Save->SlotItemIds, Save->SlotCounts),
			Save->StoryNodeId.IsEmpty() ? TEXT("(无)") : *Save->StoryNodeId,
			bOk ? TEXT("成功") : TEXT("失败"));
	}
	return bOk;
}

bool UMetamorphosisSaveSubsystem::LoadNow(bool bApplyImmediately)
{
	if (!HasSave())
	{
		UE_LOG(LogMetaSave, Warning, TEXT("[Save] 读档失败：没有存档（%s）"), *GetSavePath());
		return false;
	}
	UMetamorphosisSaveGame* Save = Cast<UMetamorphosisSaveGame>(UGameplayStatics::LoadGameFromSlot(kSlotName, 0));
	if (Save == nullptr)
	{
		UE_LOG(LogMetaSave, Error, TEXT("[Save] 读档失败：槽存在但反序列化失败"));
		return false;
	}
	if (Save->Version != CurrentVersion)
	{
		UE_LOG(LogMetaSave, Error, TEXT("[Save] 读档失败：存档版本 %d 与当前 %d 不一致，已忽略"), Save->Version, CurrentVersion);
		return false;
	}
	ApplyToSubsystems(*Save);
	UE_LOG(LogMetaSave, Log, TEXT("[DIAG] 读档：%s | 背包=%d 格 [%s] | 位置=%s | 剧情=%s | 任务=%d"),
		*Save->SavedAtIso,
		Save->SlotItemIds.FilterByPredicate([](const FString& S) { return !S.IsEmpty(); }).Num(),
		*DescribeSlots(Save->SlotItemIds, Save->SlotCounts),
		*Save->PlayerLocation.ToCompactString(),
		Save->StoryNodeId.IsEmpty() ? TEXT("(无)") : *Save->StoryNodeId,
		Save->CurrentTaskIndex);

	// ⚠️ 只给"用户主动调 Meta.Save.Load"这种调试场景用 ✓：
	// RequestContinue 必须传 false ✗ —— 因为它随后会 OpenLevel（换新角色 ✗），
	// 若在这里应用，挂起数据会被写进**旧角色**并清空 ✗ -> 新角色永远拿不到背包/位置 ✗✗
	if (bApplyImmediately && UGameplayStatics::GetPlayerPawn(GetWorld(), 0) != nullptr)
	{
		UE_LOG(LogMetaSave, Log, TEXT("[DIAG] 读档：当前已在关卡内（有 Pawn），立即应用挂起数据"));
		ApplyPendingPlayerTransform();
	}
	else
	{
		UE_LOG(LogMetaSave, Log, TEXT("[DIAG] 读档：保持挂起，等新关卡的角色就绪后再应用（bApplyImmediately=%d）"), bApplyImmediately ? 1 : 0);
	}
	return true;
}

bool UMetamorphosisSaveSubsystem::DeleteSave()
{
	const bool bExisted = HasSave();
	if (bExisted)
	{
		UGameplayStatics::DeleteGameInSlot(kSlotName, 0);
	}
	UE_LOG(LogMetaSave, Log, TEXT("[Save] 删除存档：%s（原本%s）"), *GetSavePath(), bExisted ? TEXT("存在") : TEXT("不存在"));
	return true;
}

FString UMetamorphosisSaveSubsystem::Summary() const
{
	if (!HasSave())
	{
		return TEXT("无存档");
	}
	UMetamorphosisSaveGame* Save = Cast<UMetamorphosisSaveGame>(UGameplayStatics::LoadGameFromSlot(kSlotName, 0));
	if (Save == nullptr)
	{
		return TEXT("存档存在但读取失败");
	}
	return FString::Printf(TEXT("第 %d 周目 · 怀疑 %.0f · 躁郁 %.0f · 当天 %.0f 分钟 · 任务[%d] %s · 背包 %d 格 [%s] · 剧情节点 %s · 存档于 %s"),
		Save->LoopCount, Save->Suspicion, Save->Mood, Save->DayMinutes, Save->CurrentTaskIndex, *Save->CurrentTaskId,
		Save->SlotItemIds.FilterByPredicate([](const FString& S) { return !S.IsEmpty(); }).Num(),
		*DescribeSlots(Save->SlotItemIds, Save->SlotCounts),
		Save->StoryNodeId.IsEmpty() ? TEXT("(无)") : *Save->StoryNodeId,
		*Save->SavedAtIso);
}

void UMetamorphosisSaveSubsystem::RequestContinue(UObject* WorldContextObject)
{
	// ⚠️ 关键：**不能**在打开关卡前应用挂起数据 ✗
	// 主界面是挂在游戏关卡上的（不换图）-> 这里"有 Pawn"是上一局残留的 ✗
	// 若应用，背包/位置会写进旧角色并把挂起标记清空 ✗ -> OpenLevel 换新角色后永远拿不到 ✗✗
	// = 用户报的"继续游戏后背包消失、位置不对" ✓✓
	if (!LoadNow(/*bApplyImmediately=*/false))
	{
		return;
	}
	UE_LOG(LogMetaSave, Log, TEXT("[DIAG] 继续游戏：已读档（挂起），即将打开游戏关卡，由新角色的 BeginPlay 延迟应用"));

	// 读档只把数值/背包/任务/剧情恢复到子系统里；关卡由这里重新打开，玩家位置在 PC 的 BeginPlay 里应用。
	// 与 GameMap 配置一致（Config/DefaultGame.ini 的 GameMap）。
	UGameplayStatics::OpenLevel(WorldContextObject, FName(TEXT("/Game/mymap/map/Untitled")));
}

void UMetamorphosisSaveSubsystem::StartNewGame()
{
	DeleteSave();

	// 「新游戏」= 完全从头（用户反馈：不放主界面后重开时躁郁值没清零 ✗）：
	// 之前这里只删存档、其余全交给 GameInstance 子系统 —— 而它们是跨关卡存活的 ✗，
	// 所以"新游戏"会带着上一局的怀疑/躁郁/任务/剧情继续 ✓✓。
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (GI == nullptr)
	{
		UE_LOG(LogMetaSave, Warning, TEXT("[DIAG] 新游戏重置：拿不到 GameInstance，跳过数值/任务/剧情重置"));
		return;
	}

	if (UMetamorphosisStatsSubsystem* Stats = GI->GetSubsystem<UMetamorphosisStatsSubsystem>())
	{
		Stats->SetSuspicion(0.f);
		Stats->SetMood(Stats->InitialMood);
		Stats->SetDayMinutes(360.f);   // 一天开始（与配置 Global.DayStartMinutes 一致）
		// 新游戏：探索次数按第一个时段（早晨）的配额拉满（2026-10-19 时间/次数系统）
		Stats->ResetExploreChances();
		UE_LOG(LogMetaSave, Log, TEXT("[DIAG] 新游戏重置：怀疑=%.1f 躁郁=%.1f 时间=%.0f 分钟"),
			Stats->GetSuspicion(), Stats->GetMood(), Stats->GetDayMinutes());
	}
	if (UMetamorphosisTaskSubsystem* Tasks = GI->GetSubsystem<UMetamorphosisTaskSubsystem>())
	{
		Tasks->ResetToFirst();
	}
	if (UMetamorphosisStorySubsystem* Story = GI->GetSubsystem<UMetamorphosisStorySubsystem>())
	{
		Story->ResetForNewRun();
	}
	UE_LOG(LogMetaSave, Log, TEXT("[DIAG] 新游戏重置完成（数值/任务/剧情）"));
}
