// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Inventory/MetamorphosisInventoryComponent.h"

#include "Config/GameConfigTypes.h"
#include "Config/MetamorphosisConfigSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/CameraComponent.h"
#include "Interaction/MetamorphosisInteractable.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Metamorphosis.h"
#include "Misc/Parse.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Tasks/MetamorphosisTaskSubsystem.h"

namespace
{
	/** 解析失败时的中性灰（美术图没到之前，颜色只是"看得出是不同道具"，不该崩也不该全黑） */
	const FLinearColor FallbackPlaceholderColor(0.75f, 0.75f, 0.78f, 1.f);

	/**
	 *  从世界里的 Actor 抓一份"可视外观"：**包围盒体积最大的那个可见 StaticMeshComponent**。
	 *
	 *  为什么不是"根组件的网格"：关卡里的道具大多是 Blueprint Actor（BP_SalarySlip / BP_MedicineBottle …），
	 *  网格挂在根下面的 StaticMeshComponent 上，有的 BP 还带小装饰件 —— 取最大的那个才拿到主体。
	 *  隐藏的组件（碰撞盒、示意体）要跳过，否则可能记下一个看不见的方块。
	 *
	 *  OutXf 记的是**世界变换**：丢弃时只用它的旋转/缩放，位置按落点重算。
	 *  OutMats 里的动态材质实例会被跳过（临时对象，软引用载不回来），但**保留空位**以对齐材质槽下标。
	 */
	bool CaptureWorldLook(AActor* Actor, TSoftObjectPtr<UStaticMesh>& OutMesh, FTransform& OutXf,
		TArray<TSoftObjectPtr<UMaterialInterface>>& OutMats)
	{
		OutMesh = nullptr;
		OutXf = FTransform::Identity;
		OutMats.Reset();

		if (Actor == nullptr || !IsValid(Actor))
		{
			return false;
		}

		TInlineComponentArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		UStaticMeshComponent* BestComp = nullptr;
		float BestVolume = -1.f;
		for (UStaticMeshComponent* Comp : MeshComps)
		{
			if (Comp == nullptr || Comp->GetStaticMesh() == nullptr)
			{
				continue;
			}
			if (!Comp->IsVisible())
			{
				continue;
			}

			const FBoxSphereBounds Bounds = Comp->CalcBounds(Comp->GetComponentTransform());
			const FVector Extent = Bounds.BoxExtent;
			const float Volume = Extent.X * Extent.Y * Extent.Z;
			if (Volume > BestVolume)
			{
				BestVolume = Volume;
				BestComp = Comp;
			}
		}

		if (BestComp == nullptr)
		{
			return false;
		}

		OutMesh = BestComp->GetStaticMesh();
		OutXf = BestComp->GetComponentTransform();

		const int32 NumMats = BestComp->GetNumMaterials();
		OutMats.Reserve(NumMats);
		int32 LoadableMats = 0;
		for (int32 Index = 0; Index < NumMats; ++Index)
		{
			UMaterialInterface* Mat = BestComp->GetMaterial(Index);
			// 动态材质实例是运行时对象，软引用存不下来（存了也 Load 不回），用空位占着槽号
			if (Mat != nullptr && !Mat->IsA<UMaterialInstanceDynamic>())
			{
				OutMats.Add(TSoftObjectPtr<UMaterialInterface>(Mat));
				++LoadableMats;
			}
			else
			{
				OutMats.Add(TSoftObjectPtr<UMaterialInterface>());
			}
		}

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Inventory] 记下世界模型：%s -> %s（组件 %s，材质 %d/%d 可还原，世界缩放 %s）"),
			*GetNameSafe(Actor), *GetNameSafe(OutMesh.Get()), *BestComp->GetName(), LoadableMats, NumMats,
			*OutXf.GetScale3D().ToCompactString());
		return true;
	}
}

UMetamorphosisInventoryComponent::UMetamorphosisInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	Slots.SetNum(NumSlots);
}

void UMetamorphosisInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	Slots.SetNum(NumSlots);
	SelectedIndex = FMath::Clamp(SelectedIndex, 0, NumSlots - 1);

	OnSelectionChanged.Broadcast(SelectedIndex);
}

// ---------------------------------------------------------------------------
// 选中 / 切换
// ---------------------------------------------------------------------------

void UMetamorphosisInventoryComponent::SelectSlot(int32 Index)
{
	const int32 Clamped = FMath::Clamp(Index, 0, NumSlots - 1);
	if (Clamped == SelectedIndex)
	{
		return;
	}

	SelectedIndex = Clamped;
	OnSelectionChanged.Broadcast(SelectedIndex);
}

void UMetamorphosisInventoryComponent::CycleSelection(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}

	// 环绕：-1 从第 1 格绕到第 8 格
	const int32 Next = ((SelectedIndex + Delta) % NumSlots + NumSlots) % NumSlots;
	SelectSlot(Next);
}

// ---------------------------------------------------------------------------
// 格子读写
// ---------------------------------------------------------------------------

FMetamorphosisItemSlot UMetamorphosisInventoryComponent::GetSlot(int32 Index) const
{
	if (Slots.IsValidIndex(Index))
	{
		return Slots[Index];
	}

	return FMetamorphosisItemSlot();
}

void UMetamorphosisInventoryComponent::SetSlot(int32 Index, FMetamorphosisItemSlot NewSlot)
{
	if (!Slots.IsValidIndex(Index))
	{
		return;
	}

	Slots[Index] = NewSlot;
	OnInventoryChanged.Broadcast();
}

void UMetamorphosisInventoryComponent::ClearAllSlots()
{
	Slots.SetNum(NumSlots);
	for (FMetamorphosisItemSlot& Slot : Slots)
	{
		Slot.Clear();
	}

	OnInventoryChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// 道具：配表查询 / 获得 / 使用（2026-10-13 接入道具配表）
// ---------------------------------------------------------------------------

const FItemConfigEntry* UMetamorphosisInventoryComponent::FindItemConfig(FName ItemId) const
{
	if (ItemId.IsNone())
	{
		return nullptr;
	}

	// 按任务规格走 GetOwner()->GetGameInstance()->GetSubsystem<...>()（组件不持有配置子系统）
	const AActor* Owner = GetOwner();
	const UGameInstance* GameInstance = Owner != nullptr ? Owner->GetGameInstance() : nullptr;
	const UMetamorphosisConfigSubsystem* ConfigSubsystem = GameInstance != nullptr
		? GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>()
		: nullptr;

	return ConfigSubsystem != nullptr ? ConfigSubsystem->GetItem(ItemId) : nullptr;
}

TSoftObjectPtr<UTexture2D> UMetamorphosisInventoryComponent::MakeIconPath(const FString& ItemIdString) const
{
	// 约定：<ItemIconFolder>/<ItemIconPrefix><ItemId>，例如 /Game/UI/Items/T_Item_BrownSugar
	// 美术图还没到 -> LoadSynchronous() 返回空 -> UI 用 PlaceholderColor 染色（见 HotbarWidget）
	const FString ObjectPath = FString::Printf(TEXT("%s/%s%s.%s%s"),
		*ItemIconFolder, *ItemIconPrefix, *ItemIdString, *ItemIconPrefix, *ItemIdString);
	return TSoftObjectPtr<UTexture2D>(FSoftObjectPath(ObjectPath));
}

bool UMetamorphosisInventoryComponent::AddItem(FName ItemId, int32 Count)
{
	if (Count < 1)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] AddItem(%s) 的数量 %d 不合法，忽略"), *ItemId.ToString(), Count);
		return false;
	}

	const FItemConfigEntry* Entry = FindItemConfig(ItemId);
	if (Entry == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Inventory] 配表里没有道具 %s（大小写不敏感，可用 Meta.Inv.List 看有哪些）"), *ItemId.ToString());
		return false;
	}

	if (!Entry->bCarryable)
	{
		// 策划表原文：不可储存/不可携带 -> 就地食用。这类道具不该进快捷栏，
		// 以后接"世界里的交互物件"时直接调 Stats 结算，别走 AddItem。
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Inventory] %s（%s，%s）不可携带（bCarryable=false，储存类型 %s），没有放进快捷栏"),
			*Entry->DisplayName, *Entry->ItemId, *Entry->Category, *Entry->StorageType);
		return false;
	}

	Slots.SetNum(NumSlots);

	const int32 MaxStack = FMath::Max(1, Entry->MaxStack);
	int32 Remaining = Count;
	int32 TouchedSlots = 0;

	// 1) 先堆到"已经有同种道具且没满"的格子里（顺序靠前的优先，符合 MC 直觉）
	for (FMetamorphosisItemSlot& Slot : Slots)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (!Slot.bOccupied || Slot.ItemId != ItemId)
		{
			continue;
		}

		const int32 Space = MaxStack - Slot.Count;
		if (Space <= 0)
		{
			continue;
		}

		const int32 PutIn = FMath::Min(Space, Remaining);
		Slot.Count += PutIn;
		Remaining -= PutIn;
		++TouchedSlots;
	}

	// 2) 再找空格子放（新格子要填满 ID/名字/图标/占位色）
	for (FMetamorphosisItemSlot& Slot : Slots)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Slot.bOccupied)
		{
			continue;
		}

		const int32 PutIn = FMath::Min(MaxStack, Remaining);
		Slot.bOccupied = true;
		Slot.ItemId = ItemId;
		Slot.DisplayName = FText::FromString(Entry->DisplayName);
		Slot.Icon = MakeIconPath(Entry->ItemId);
		Slot.PlaceholderColor = ParsePlaceholderColor(Entry->PlaceholderColor);
		Slot.Count = PutIn;
		Remaining -= PutIn;
		++TouchedSlots;
	}

	if (TouchedSlots > 0)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Inventory] 获得 %s x%d（%s，堆叠上限 %d）%s"),
			*Entry->DisplayName, Count - Remaining, *Entry->ItemId, MaxStack,
			Remaining > 0 ? *FString::Printf(TEXT("；快捷栏放不下，剩 %d 个丢了"), Remaining) : TEXT(""));
		OnInventoryChanged.Broadcast();

		// 事件推进：任务系统订阅"拿到某件道具"
		NotifyItemObtained(ItemId);
	}

	if (Remaining > 0 && TouchedSlots == 0)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] 快捷栏已满，%s 一个都没放进去"), *Entry->DisplayName);
		return false;
	}

	// 放了一部分也算成功（拿到的比要的少，上面日志里写清了）
	return Remaining < Count;
}

bool UMetamorphosisInventoryComponent::AddItemFromWorld(FName ItemId, int32 Count, AActor* SourceActor)
{
	// 先记外观（必须在 AddItem 之前也没关系 —— 记的是软引用，原 Actor 之后被 Destroy 不影响）
	TSoftObjectPtr<UStaticMesh> CapturedMesh;
	FTransform CapturedTransform = FTransform::Identity;
	TArray<TSoftObjectPtr<UMaterialInterface>> CapturedMaterials;
	const bool bHasLook = CaptureWorldLook(SourceActor, CapturedMesh, CapturedTransform, CapturedMaterials);

	if (!AddItem(ItemId, Count))
	{
		return false;
	}

	if (!bHasLook)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Inventory] %s（%s）来自 %s，但那个 Actor 上没有可记录的可视网格 -> 丢出来会用占位球"),
			*ItemId.ToString(), *ItemId.ToString(), *GetNameSafe(SourceActor));
		return true;
	}

	// 盖章到"这件道具、且还没有世界模型"的格子。
	// 只盖没有的：同种道具叠到已有格子上时，保留最早那次记录（模型一致性更好）。
	int32 StampedSlots = 0;
	for (FMetamorphosisItemSlot& Slot : Slots)
	{
		if (Slot.bOccupied && Slot.ItemId == ItemId && Slot.WorldMesh.IsNull())
		{
			Slot.WorldMesh = CapturedMesh;
			Slot.WorldMeshTransform = CapturedTransform;
			Slot.WorldMeshMaterials = CapturedMaterials;
			++StampedSlots;
		}
	}

	if (StampedSlots > 0)
	{
		OnInventoryChanged.Broadcast();
	}

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Inventory] %s 拾取自 %s：已记下世界模型 %s（盖章 %d 个格子；之后丢弃会还原这个模型）"),
		*ItemId.ToString(), *GetNameSafe(SourceActor), *GetNameSafe(CapturedMesh.Get()), StampedSlots);
	return true;
}

FMetamorphosisItemSlot UMetamorphosisInventoryComponent::GetSelectedSlot() const
{
	return Slots.IsValidIndex(SelectedIndex) ? Slots[SelectedIndex] : FMetamorphosisItemSlot();
}

void UMetamorphosisInventoryComponent::NotifyItemObtained(FName ItemId)
{
	if (ItemId.IsNone())
	{
		return;
	}

	// 1) 广播给 UI / 特效
	OnItemObtained.Broadcast(ItemId);

	// 2) 事件推进：任务系统是 GameInstance 级的（跨关卡保留），这里直接把事件送过去。
	//    不在任务子系统 Initialize 里绑本组件的委托——那时还没有 Pawn（每关重新生成），绑定必然落空。
	if (UMetamorphosisTaskSubsystem* TaskSystem = UMetamorphosisTaskSubsystem::Get(this))
	{
		TaskSystem->NotifyItemObtained(ItemId);
	}
}

// ---------------------------------------------------------------------------
// 道具效果 / 外观（交互系统的"就地吃掉"与快捷栏"使用"共用同一份）
// ---------------------------------------------------------------------------

FLinearColor UMetamorphosisInventoryComponent::ParsePlaceholderColor(const FString& Hex)
{
	// 配表里的占位色是 6 位十六进制（"9AA5B1"，无 #）。
	// 先按 sRGB 理解成 FColor 再转线性（UI 上颜色才不会被 Gamma 拉亮）。
	FString Clean = Hex;
	Clean.RemoveFromStart(TEXT("#"));
	Clean.TrimStartAndEndInline();

	if (Clean.Len() == 6 || Clean.Len() == 8)
	{
		const int32 R = static_cast<int32>(FParse::HexNumber(*Clean.Mid(0, 2)));
		const int32 G = static_cast<int32>(FParse::HexNumber(*Clean.Mid(2, 2)));
		const int32 B = static_cast<int32>(FParse::HexNumber(*Clean.Mid(4, 2)));
		const int32 A = Clean.Len() == 8 ? static_cast<int32>(FParse::HexNumber(*Clean.Mid(6, 2))) : 255;
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>(FMath::Clamp(R, 0, 255)),
			static_cast<uint8>(FMath::Clamp(G, 0, 255)),
			static_cast<uint8>(FMath::Clamp(B, 0, 255)),
			static_cast<uint8>(FMath::Clamp(A, 0, 255))));
	}

	return FallbackPlaceholderColor;
}

void UMetamorphosisInventoryComponent::ApplyItemEffects(const UObject* WorldContextObject,
	const FItemConfigEntry& Entry, FString& OutSummary)
{
	float AppliedMood = 0.f;
	float AppliedSuspicion = 0.f;
	float AppliedSpeed = 0.f;

	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(WorldContextObject))
	{
		if (!FMath::IsNearlyZero(Entry.MoodDelta))
		{
			Stats->AddMood(Entry.MoodDelta);
			AppliedMood = Entry.MoodDelta;
		}
		if (!FMath::IsNearlyZero(Entry.SuspicionDelta))
		{
			Stats->AddSuspicion(Entry.SuspicionDelta);
			AppliedSuspicion = Entry.SuspicionDelta;
		}
		if (!FMath::IsNearlyZero(Entry.SpeedBonusPercent))
		{
			Stats->AddSpeedBonusPercent(Entry.SpeedBonusPercent);
			AppliedSpeed = Entry.SpeedBonusPercent;
		}
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Inventory] 取不到数值系统，%s 的效果没生效"), *Entry.DisplayName);
	}

	OutSummary = FString::Printf(TEXT("躁郁 %+.0f / 怀疑 %+.0f / 速度 %+.0f%%"),
		AppliedMood, AppliedSuspicion, AppliedSpeed);
}

UTexture2D* UMetamorphosisInventoryComponent::GetItemIconResolved(FName ItemId)
{
	if (ItemId.IsNone())
	{
		return nullptr;
	}

	if (const TObjectPtr<UTexture2D>* Found = ItemIconCache.Find(ItemId))
	{
		if (IsValid(*Found))
		{
			return *Found;
		}
	}

	// 没缓存（或缓存被 GC）→ 重新加载并持有硬引用
	UTexture2D* Loaded = MakeIconPath(ItemId.ToString()).LoadSynchronous();
	ItemIconCache.Add(ItemId, Loaded);

	UE_LOG(LogMetamorphosis, Log, TEXT("[Icon] %s：缓存%s，资源有效=%d（%s）"),
		*ItemId.ToString(), ItemIconCache.Contains(ItemId) ? TEXT("旧缓存失效，重新加载") : TEXT("首次加载"),
		Loaded != nullptr ? 1 : 0, Loaded != nullptr ? *Loaded->GetPathName() : TEXT("(空)"));

	return Loaded;
}
TSoftObjectPtr<UTexture2D> UMetamorphosisInventoryComponent::GetItemIcon(FName ItemId) const
{
	return ItemId.IsNone() ? TSoftObjectPtr<UTexture2D>() : MakeIconPath(ItemId.ToString());
}

bool UMetamorphosisInventoryComponent::UseSelectedSlot()
{
	Slots.SetNum(NumSlots);
	return UseSlot(SelectedIndex);
}

bool UMetamorphosisInventoryComponent::UseSlot(int32 Index)
{
	Slots.SetNum(NumSlots);
	if (!Slots.IsValidIndex(Index))
	{
		return false;
	}

	const FMetamorphosisItemSlot Selected = Slots[Index];
	if (!Selected.bOccupied)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] 第 %d 格是空的，没有可用的道具"), Index + 1);
		return false;
	}

	const FItemConfigEntry* Entry = FindItemConfig(Selected.ItemId);
	if (Entry == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] 第 %d 格的 %s 不在配表里，无法使用"), Index + 1, *Selected.ItemId.ToString());
		return false;
	}

	// 剧情/工具类（bConsumable=false）：当作"查看"，不施加数值效果、不消耗。
	// （当前配表里这类道具的 Mood/Suspicion/Speed 全是 0，所以行为上没有区别；
	//   万一以后给工具配了被动效果，需要在这里或"装备"系统里单独处理 —— 见 Docs/道具配表.md TODO）
	if (!Entry->bConsumable)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Inventory] 查看了 %s（%s，%s，不可消耗）%s%s"),
			*Entry->DisplayName, *Entry->ItemId, *Entry->Category,
			Entry->Description.IsEmpty() ? TEXT("") : TEXT("："),
			*Entry->Description);
		return false;
	}

	// 1) 施加配表效果（与交互系统的"就地吃掉"共用同一份实现）
	FString EffectSummary;
	ApplyItemEffects(this, *Entry, EffectSummary);

	// 2) 消耗：扣数量，归零就清格
	FMetamorphosisItemSlot& Target = Slots[Index];
	Target.Count -= 1;
	if (Target.Count <= 0)
	{
		Target.Clear();
	}

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Inventory] 使用了 %s（%s，%s）：%s -> 第 %d 格剩余 %d"),
		*Entry->DisplayName, *Entry->ItemId, *Entry->Category, *EffectSummary, Index + 1,
		Target.bOccupied ? Target.Count : 0);

	OnInventoryChanged.Broadcast();
	return true;
}

bool UMetamorphosisInventoryComponent::DropSlot(int32 Index)
{
	Slots.SetNum(NumSlots);
	if (!Slots.IsValidIndex(Index))
	{
		return false;
	}

	const FMetamorphosisItemSlot Slot = Slots[Index];
	if (!Slot.bOccupied)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] 第 %d 格是空的，没东西可丢"), Index + 1);
		return false;
	}

	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (OwnerActor == nullptr || World == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] 丢弃失败：没有世界或拥有者"));
		return false;
	}

	// 「前方」用相机朝向投影到水平面 —— 玩家看到的方向才是直觉上的"前面"（虫子精灵是朝向相机的）
	FVector Forward = OwnerActor->GetActorForwardVector();
	if (const UCameraComponent* Camera = OwnerActor->FindComponentByClass<UCameraComponent>())
	{
		Forward = Camera->GetForwardVector();
	}
	Forward.Z = 0.f;
	if (!Forward.Normalize())
	{
		Forward = FVector::ForwardVector;
	}

	const FVector PawnLoc = OwnerActor->GetActorLocation();
	FVector DropLoc = PawnLoc + Forward * FMath::Max(10.f, DropDistanceCm);

	// 可见替身的尺寸（直径，厘米）。用于算"球心要抬多高球底才贴地"。
	// ⚠️ 之前这里只把**球心**放在地面 + DropHeightCm(12cm)，而球半径是 27.5cm
	//    -> 半个球埋进地里，配合待机浮动看起来像"在地里一直弹"（2026-10-17 修）。
	const float VisualSizeCm = FMath::Clamp(DropVisualSizeCm, 8.f, 300.f);
	const float VisualRadiusCm = VisualSizeCm * 0.5f;

	// 往下找地面：找不到就用角色当前高度（至少不会丢到天上/地下）
	float GroundZCm = PawnLoc.Z;
	bool bFoundGround = false;
	{
		const FVector TraceStart = FVector(DropLoc.X, DropLoc.Y, PawnLoc.Z + 60.f);
		const FVector TraceEnd = FVector(DropLoc.X, DropLoc.Y, PawnLoc.Z - 300.f);
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DropItemGround), false, OwnerActor);
		if (World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			GroundZCm = GroundHit.ImpactPoint.Z;
			bFoundGround = true;
		}
	}

	// 球心高度 = 地面 + 半径 + 额外抬高（DropHeightCm 默认 0 = 正好贴地，不要再默认抬 12cm）
	DropLoc.Z = GroundZCm + VisualRadiusCm + FMath::Max(0.f, DropHeightCm);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = OwnerActor;

	AMetamorphosisInteractable* Spawned = World->SpawnActor<AMetamorphosisInteractable>(
		AMetamorphosisInteractable::StaticClass(), DropLoc, FRotator(0.f, OwnerActor->GetActorRotation().Yaw, 0.f), SpawnParams);
	if (Spawned == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Inventory] 丢弃失败：生成可交互物失败（%s）"), *Slot.ItemId.ToString());
		return false;
	}

	Spawned->ItemId = Slot.ItemId;
	Spawned->Count = FMath::Max(1, Slot.Count);
	Spawned->Mode = EMetamorphosisInteractMode::Auto;
	// 丢在地上的道具**不上下浮动**（2026-10-17：可交互物已整体移除垂直 bob）。
	// 默认开自转（只有占位球需要它来"显眼"）；还原真模型时会关掉，见下面。
	Spawned->bIdleSpin = true;

	// ---------------------------------------------------------------
	// 可见替身（2026-10-18 用户要求：**丢出来要还原道具原本的模型**）
	//
	// 拾取时由 AddItemFromWorld() 把原物件的网格/朝向/材质记进了槽位，这里照抄回世界。
	// 只有"没记到模型"（例如 Meta.Inv.Give 直接塞进背包的）才退回旧的占位球，并打一条 Warning。
	// ---------------------------------------------------------------
	bool bRestoredWorldMesh = false;
	if (Spawned->Mesh != nullptr)
	{
		if (UStaticMesh* WorldMesh = Slot.WorldMesh.LoadSynchronous())
		{
			Spawned->Mesh->SetStaticMesh(WorldMesh);

			// 朝向/缩放照抄原物件（位置由下面的"按包围盒贴地"统一修正，这里先给个大致位置）
			const FRotator WorldRotation = Slot.WorldMeshTransform.Rotator();
			FVector WorldScale = Slot.WorldMeshTransform.GetScale3D();
			if (WorldScale.IsNearlyZero())
			{
				WorldScale = FVector::OneVector;
			}
			Spawned->Mesh->SetWorldLocationAndRotation(DropLoc, WorldRotation);
			Spawned->Mesh->SetWorldScale3D(WorldScale);

			// 材质：有些 BP 道具的材质挂在组件上而不是网格资产上，不还原会丢出来变灰
			int32 AppliedMaterials = 0;
			const int32 NumMats = FMath::Min(Slot.WorldMeshMaterials.Num(), Spawned->Mesh->GetNumMaterials());
			for (int32 MatIndex = 0; MatIndex < NumMats; ++MatIndex)
			{
				if (UMaterialInterface* Mat = Slot.WorldMeshMaterials[MatIndex].LoadSynchronous())
				{
					Spawned->Mesh->SetMaterial(MatIndex, Mat);
					++AppliedMaterials;
				}
			}

			Spawned->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			// 还原的模型是"真物件"：不要再绕自身转 —— 否则一张工资条在地上转圈很怪。
			Spawned->bIdleSpin = false;
			Spawned->SetActorTickEnabled(false);

			bRestoredWorldMesh = true;
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Inventory] 丢弃 : 还原原本模型 %s（网格缩放 %s，还原材质 %d/%d）"),
				*WorldMesh->GetName(), *WorldScale.ToCompactString(), AppliedMaterials, Slot.WorldMeshMaterials.Num());
		}
		else
		{
			// 没记到模型 -> 旧的占位球兜底（球直径 100cm -> 缩放到 DropVisualSizeCm）
			if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Spawned->Mesh->SetStaticMesh(SphereMesh);
				Spawned->Mesh->SetWorldScale3D(FVector(VisualSizeCm / 100.f));

				const FLinearColor ItemColor = ParsePlaceholderColor(Slot.PlaceholderColor.ToString());
				if (UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, Spawned))
					{
						Mid->SetVectorParameterValue(TEXT("Color"), ItemColor);
						Spawned->Mesh->SetMaterial(0, Mid);
					}
				}
				Spawned->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Inventory] %s（%s）没有记录世界模型，丢出来用占位球（%.0f cm）—— 拾取时若经过 AddItemFromWorld 就不会这样"),
				*Slot.DisplayName.ToString(), *Slot.ItemId.ToString(), VisualSizeCm);
		}
		// 改了网格/缩放，先刷新一下包围盒，下面的贴地计算才准
		Spawned->Mesh->UpdateBounds();
	}

	// 贴地：按**实际包围盒**对齐（占位球/任意网格、任意枢轴都成立；比"球心 + 半径"通用）
	{
		const FBox BoxBefore = Spawned->GetComponentsBoundingBox(true);
		if (BoxBefore.IsValid)
		{
			const float DesiredMinZ = GroundZCm + FMath::Max(0.f, DropHeightCm);
			const float DeltaZ = DesiredMinZ - BoxBefore.Min.Z;
			if (!FMath::IsNearlyZero(DeltaZ, 0.1f))
			{
				Spawned->AddActorWorldOffset(FVector(0.f, 0.f, DeltaZ), false);
			}
		}
	}

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Inventory] 丢弃了 %s（%s）x%d -> 世界物件 %s @ %s（走过去按交互可捡回）"),
		*Slot.DisplayName.ToString(), *Slot.ItemId.ToString(), Spawned->Count,
		*Spawned->GetName(), *DropLoc.ToCompactString());

	// 客观数据：方便验证"模型还原 / 贴地 / 可再交互"（无头环境也能看日志判断）
	{
		const FBox Box = Spawned->GetComponentsBoundingBox(true);
		const FVector Size = Box.GetSize();
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Inventory] 丢出物实测：视觉=%s｜世界尺寸=%.1fx%.1fx%.1f cm｜底面 Z=%.1f｜地面 Z=%.1f（找到地面=%d，额外抬高=%.1f）"),
			bRestoredWorldMesh ? TEXT("还原原本模型") : TEXT("占位球"),
			Size.X, Size.Y, Size.Z, Box.Min.Z, GroundZCm, bFoundGround ? 1 : 0,
			FMath::Max(0.f, DropHeightCm));
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Inventory] 丢出物状态：自转=%d（还原真模型时关闭）｜碰撞=%d｜ItemId=%s x%d"),
			Spawned->bIdleSpin ? 1 : 0, static_cast<int32>(Spawned->Mesh != nullptr ? Spawned->Mesh->GetCollisionEnabled() : -1),
			*Spawned->ItemId.ToString(), Spawned->Count);
	}

	Slots[Index].Clear();
	OnInventoryChanged.Broadcast();
	return true;
}

void UMetamorphosisInventoryComponent::FillWithDebugItems()
{
	Slots.SetNum(NumSlots);

	// 8 个明显不同的颜色，方便一眼看出滚轮/数字键到底切没切
	static const FLinearColor DebugColors[NumSlots] =
	{
		FLinearColor(0.85f, 0.30f, 0.25f, 1.f),   // 红
		FLinearColor(0.90f, 0.60f, 0.20f, 1.f),   // 橙
		FLinearColor(0.90f, 0.85f, 0.25f, 1.f),   // 黄
		FLinearColor(0.40f, 0.75f, 0.35f, 1.f),   // 绿
		FLinearColor(0.25f, 0.70f, 0.70f, 1.f),   // 青
		FLinearColor(0.30f, 0.50f, 0.85f, 1.f),   // 蓝
		FLinearColor(0.55f, 0.40f, 0.80f, 1.f),   // 紫
		FLinearColor(0.80f, 0.45f, 0.65f, 1.f),   // 粉
	};

	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		FMetamorphosisItemSlot& Slot = Slots[Index];
		Slot.Clear();
		Slot.bOccupied = true;
		Slot.ItemId = FName(*FString::Printf(TEXT("DebugItem_%d"), Index + 1));
		Slot.DisplayName = FText::FromString(FString::Printf(TEXT("占位道具 %d"), Index + 1));
		Slot.PlaceholderColor = DebugColors[Index];
		Slot.Count = Index + 1;   // 数量 1..8，顺便验证角标显示
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Inventory] 已填入 %d 个占位道具（调试用）"), NumSlots);
	OnInventoryChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// 调试命令（Meta.Inv.*）
// ---------------------------------------------------------------------------

#if !UE_BUILD_SHIPPING
namespace MetamorphosisInventoryConsole
{
	static UMetamorphosisInventoryComponent* InventoryOf(UWorld* World)
	{
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
		return Pawn != nullptr ? Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>() : nullptr;
	}

	static void Print(UMetamorphosisInventoryComponent* Inventory)
	{
		if (Inventory == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到物品栏组件（当前 Pawn 上没有）"));
			return;
		}

		FString Line;
		for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
		{
			const FMetamorphosisItemSlot Slot = Inventory->GetSlot(Index);
			const TCHAR* Mark = (Index == Inventory->GetSelectedIndex()) ? TEXT("[*]") : TEXT("[ ]");
			// 数量也打出来（>1 才带 xN）：验证 MaxStack 堆叠就靠它
			const FString Content = Slot.bOccupied
				? (Slot.Count > 1 ? FString::Printf(TEXT("%s x%d"), *Slot.ItemId.ToString(), Slot.Count) : Slot.ItemId.ToString())
				: TEXT("空");
			Line += FString::Printf(TEXT("%s%d:%s "), Mark, Index + 1, *Content);
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 快捷栏：%s"), *Line);
	}

	static void PrintCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		Print(InventoryOf(World));
	}

	static void Select(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisInventoryComponent* Inventory = InventoryOf(World);
		if (Inventory == nullptr)
		{
			return;
		}

		// 参数按"格子号 1..10"给，跟数字键一致
		const int32 SlotNumber = Args.Num() > 0 ? FMath::RoundToInt(FCString::Atof(*Args[0])) : 1;
		Inventory->SelectSlot(SlotNumber - 1);
		Print(Inventory);
	}

	static void Cycle(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisInventoryComponent* Inventory = InventoryOf(World);
		if (Inventory == nullptr)
		{
			return;
		}

		const int32 Delta = Args.Num() > 0 ? FMath::RoundToInt(FCString::Atof(*Args[0])) : 1;
		Inventory->CycleSelection(Delta);
		Print(Inventory);
	}

	static void Fill(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisInventoryComponent* Inventory = InventoryOf(World))
		{
			Inventory->FillWithDebugItems();
			Print(Inventory);
		}
	}

	static void Clear(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisInventoryComponent* Inventory = InventoryOf(World))
		{
			Inventory->ClearAllSlots();
			Print(Inventory);
		}
	}

	// ---------------- 道具配表相关（2026-10-13） ----------------

	static const TArray<FItemConfigEntry>* ConfigItems(UWorld* World)
	{
		const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		const UMetamorphosisConfigSubsystem* ConfigSubsystem = GameInstance != nullptr
			? GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>()
			: nullptr;
		return ConfigSubsystem != nullptr ? &ConfigSubsystem->GetItems() : nullptr;
	}

	/** 配表里有哪些 ItemId（写错 ID 时自动列出来，省得去翻 JSON） */
	static void PrintAvailableItemIds(UWorld* World)
	{
		const TArray<FItemConfigEntry>* Items = ConfigItems(World);
		if (Items == nullptr || Items->Num() == 0)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 拿不到道具配表（配置子系统没起来，或 Items 是空的）"));
			return;
		}

		FString Line;
		for (const FItemConfigEntry& Item : *Items)
		{
			Line += FString::Printf(TEXT("%s(%s%s) "), *Item.ItemId, *Item.DisplayName,
				Item.bCarryable ? TEXT("") : TEXT("/不可携带"));
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 可用的 ItemId（共 %d）：%s"), Items->Num(), *Line);
	}

	static void Give(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisInventoryComponent* Inventory = InventoryOf(World);
		if (Inventory == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到物品栏组件（当前 Pawn 上没有）"));
			return;
		}

		if (Args.Num() < 1)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Inv.Give <ItemId> [Count]，例如 Meta.Inv.Give Cereal 2"));
			PrintAvailableItemIds(World);
			return;
		}

		const FName ItemId(*Args[0]);
		const int32 Count = Args.Num() > 1 ? FMath::Max(1, FMath::RoundToInt(FCString::Atof(*Args[1]))) : 1;
		const bool bOk = Inventory->AddItem(ItemId, Count);
		if (!bOk)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 给 %s x%d 失败（见上面一行原因）"), *ItemId.ToString(), Count);
			PrintAvailableItemIds(World);
		}
		Print(Inventory);
	}

	static void Use(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisInventoryComponent* Inventory = InventoryOf(World);
		if (Inventory == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到物品栏组件（当前 Pawn 上没有）"));
			return;
		}

		Inventory->UseSelectedSlot();
		Print(Inventory);
	}

	static void List(const TArray<FString>& /*Args*/, UWorld* World)
	{
		const TArray<FItemConfigEntry>* Items = ConfigItems(World);
		if (Items == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 拿不到道具配表（配置子系统没起来？）"));
			return;
		}

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Debug] 道具配表 %d 件（ItemId | 名称 | 分类 | 堆叠 | 携带/消耗 | 躁郁/怀疑/速度 | 防蛛/卡门 | 储存 | 占位色）"),
			Items->Num());
		for (const FItemConfigEntry& Item : *Items)
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug]   %-18s %-10s %-8s 堆叠%d %s%s 躁郁%+.0f 怀疑%+.0f 速度%+.0f%% 防蛛%.0f 卡门%.0fs %-11s #%s"),
				*Item.ItemId, *Item.DisplayName, *Item.Category, Item.MaxStack,
				Item.bCarryable ? TEXT("可携带") : TEXT("不可携带"),
				Item.bConsumable ? TEXT(" 可消耗") : TEXT(" 不可消耗"),
				Item.MoodDelta, Item.SuspicionDelta, Item.SpeedBonusPercent,
				Item.SpiderDefense, Item.DoorBlockSeconds, *Item.StorageType, *Item.PlaceholderColor);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMetaInvPrint(
	TEXT("Meta.Inv.Print"),
	TEXT("打印快捷栏内容与当前选中格"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::PrintCommand));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvSelect(
	TEXT("Meta.Inv.Select"),
	TEXT("选中第 N 格（1..10），如 Meta.Inv.Select 3"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::Select));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvCycle(
	TEXT("Meta.Inv.Cycle"),
	TEXT("相对切换 N 格（正负均可），如 Meta.Inv.Cycle 1"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::Cycle));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvFill(
	TEXT("Meta.Inv.Fill"),
	TEXT("填入 10 个占位道具（调试 UI 用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::Fill));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvClear(
	TEXT("Meta.Inv.Clear"),
	TEXT("清空快捷栏"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::Clear));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvGive(
	TEXT("Meta.Inv.Give"),
	TEXT("按道具配表获得道具：Meta.Inv.Give <ItemId> [Count]，如 Meta.Inv.Give Cereal 2"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::Give));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvUse(
	TEXT("Meta.Inv.Use"),
	TEXT("使用当前选中格的道具（按道具配表施加效果；可消耗的会扣数量）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::Use));

static FAutoConsoleCommandWithWorldAndArgs GMetaInvList(
	TEXT("Meta.Inv.List"),
	TEXT("列出道具配表里所有道具（ItemId/名称/分类/效果）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisInventoryConsole::List));
#endif // !UE_BUILD_SHIPPING




