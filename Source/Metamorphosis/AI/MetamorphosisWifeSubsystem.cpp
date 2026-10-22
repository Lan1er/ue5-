// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "AI/MetamorphosisWifeSubsystem.h"

#include "AI/Navigation/NavigationTypes.h"
#include "Character/MetamorphosisWifeSprite.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Config/MetamorphosisConfigSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Stats/MetamorphosisStatsSubsystem.h"

namespace
{
	/** 找主控（第一个玩家 Pawn），拿不到返回 nullptr */
	APawn* FindPlayerPawn(UWorld* World)
	{
		return World != nullptr ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	}

	/** 她 Mesh 的包围盒（拿不到就返回 false） */
	bool GetWifeBounds(AActor* Actor, FBox& OutBox)
	{
		if (Actor == nullptr)
		{
			return false;
		}
		OutBox = Actor->GetComponentsBoundingBox(true);
		return OutBox.IsValid != 0;
	}
}

// ---------------------------------------------------------------- 生命周期

void UMetamorphosisWifeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

bool UMetamorphosisWifeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 只在真正的游戏世界跑（编辑器预览/缩略图世界不要启动 AI）
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMetamorphosisWifeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bInitialized = FindAndPrepareWife();
}

void UMetamorphosisWifeSubsystem::Deinitialize()
{
	WifeActor = nullptr;
	WifeMesh = nullptr;
	Super::Deinitialize();
}

TStatId UMetamorphosisWifeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetamorphosisWifeSubsystem, STATGROUP_Tickables);
}

UMetamorphosisWifeSubsystem* UMetamorphosisWifeSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World != nullptr ? World->GetSubsystem<UMetamorphosisWifeSubsystem>() : nullptr;
}

// ---------------------------------------------------------------- 找她 + 初始化

bool UMetamorphosisWifeSubsystem::FindAndPrepareWife()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	AActor* Found = nullptr;

	// 1) 标签优先（关卡里已给 Wife 圆柱体打了 Meta.Wife）
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor != nullptr && Actor->ActorHasTag(WifeTag))
		{
			Found = Actor;
			break;
		}
	}

	// 2) label 兜底（**仅编辑器/PIE 有 label** —— GetActorLabel() 是编辑器专用 API，
	//    打包后的 Game/Android 目标里不存在（clang 会直接报 no member named 'GetActorLabel' ✗），
	//    所以整段用 WITH_EDITOR 包起来；真机上一律走标签匹配（关卡里的 Actor 都打了 Meta.Wife ✓）。
#if WITH_EDITOR
	if (Found == nullptr && !WifeLabelFallback.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor != nullptr && Actor->GetActorLabel().Equals(WifeLabelFallback, ESearchCase::IgnoreCase))
			{
				Found = Actor;
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Wife] 没有找到标签 '%s'，用 label '%s' 兜底找到了 %s（建议在关卡里打上标签）"),
					*WifeTag.ToString(), *WifeLabelFallback, *Actor->GetName());
				break;
			}
		}
	}
#endif // WITH_EDITOR

	if (Found == nullptr)
	{
		// 只在"从有变无"时打一次：本图里妻子不存在时每帧都打会把日志刷爆（实测 70 秒 7000+ 条）
		if (bWifeActorFound)
		{
			bWifeActorFound = false;
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Wife] 妻子 Actor 不见了（标签 '%s' 或 label '%s'）——巡视 AI 停止"), *WifeTag.ToString(), *WifeLabelFallback);
		}
		else if (!bLoggedMissingWife)
		{
			bLoggedMissingWife = true;
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Wife] 没找到妻子 Actor（标签 '%s' 或 label '%s'）——巡视 AI 不启动（本局只提示这一次）"),
				*WifeTag.ToString(), *WifeLabelFallback);
		}
		return false;
	}

	WifeActor = Found;
	if (!bWifeActorFound)
	{
		bWifeActorFound = true;
		bLoggedMissingWife = false;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 已找到妻子 Actor：%s"), *Found->GetName());
	}

	// 移动性：StaticMeshActor 默认 Static，不改成 Movable 的话运行时挪不动
	if (UPrimitiveComponent* Prim = Found->GetComponentByClass<UPrimitiveComponent>())
	{
		Prim->SetMobility(EComponentMobility::Movable);
		WifeMesh = Prim;

		// 大圆柱会把 16cm 的虫子挤住/卡死 —— 默认不阻挡 Pawn
		if (!bWifeBlocksPlayer)
		{
			Prim->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			Prim->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
		}

		// 她自己不能参与导航：否则会在 NavMesh 上挖个洞，而且她一动就让 NavMesh 重建（白烧性能）
		Prim->SetCanEverAffectNavigation(false);
	}

	// 按 agent 半径缩放 + 量尺寸（半径/半高/贴地偏移）
	ApplyAgentRadiusScale();

	// 2026-10-18：用户要求"尺寸符合现实"（1 uu = 1 cm）—— 半径已按 NavMesh agent 对齐，
	// 这里单独把**身高**拉到现实值：先用场景里的标尺（双人床）反推"这个场景的人体尺度"
	// （本图家具 4 倍缩放 -> 她应该 ~660cm，比床高，才像正常人而不是玩偶），再按现实尺寸
	// 重算半高 / 贴地偏移 / 眼睛高度。
	DeriveWifeHeightFromScene();
	ApplyRealBodyHeight();

	// 视觉换成 2D 三方向精灵：本体网格**只隐藏渲染、保留碰撞**（挡玩家 + NavMesh 都靠它）
	if (bWifeSpriteEnabled)
	{
		if (UPrimitiveComponent* MeshComp = WifeMesh.Get())
		{
			MeshComp->SetVisibility(false, /*bPropagateToChildren=*/false);
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Wife] 本体网格已隐藏渲染（保留碰撞）：%s"), *GetNameSafe(MeshComp));
		}
	}
	EnsureWifeSprite();

	HomeLocation = Found->GetActorLocation();
	TargetLocation = HomeLocation;
	BaseYaw = Found->GetActorRotation().Yaw;
	State = EWifeState::Pause;          // 先原地停一下再出发
	StateTimer = 0.f;
	PauseDuration = 1.f;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Wife] 巡视 AI 就绪：%s @ %s（家=%s 半径=%.1f 半高=%.1f 贴地偏移=%.1f）阻挡玩家=%d 参与导航=0"),
		*Found->GetName(), *HomeLocation.ToCompactString(), *HomeLocation.ToCompactString(),
		WifeRadiusCm, HalfHeightCm, BottomOffsetCm, bWifeBlocksPlayer ? 1 : 0);
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Wife] 参数：巡视半径=%.0fcm(可达 NavMesh 内取点) 基础速度=%.0fcm/s 停留=%.1f~%.1fs 视线=%.0fcm 视锥=%.0f度 限频=%.1fs"),
		PatrolRadiusCm, PatrolSpeedCmPerSec, PauseSecondsMin, PauseSecondsMax, BaseSightRangeCm, SightConeDeg,
		GlanceIntervalSeconds);

	return true;
}

// ---------------------------------------------------------------- 尺寸对齐 NavMesh agent

void UMetamorphosisWifeSubsystem::ApplyAgentRadiusScale()
{
	AActor* Wife = WifeActor.Get();
	UPrimitiveComponent* Mesh = WifeMesh.Get();
	if (Wife == nullptr || Mesh == nullptr)
	{
		return;
	}

	// 先按当前尺寸量一次（缩放前的半径，用来算目标缩放）
	FBox BoxBefore(ForceInit);
	const bool bHasBoxBefore = GetWifeBounds(Wife, BoxBefore);

	// 圆柱的 BoxExtent.X 就是半径（世界空间，已含缩放）
	const double RadiusBefore = bHasBoxBefore ? BoxBefore.GetExtent().X : 50.0;
	const FVector ScaleBefore = Mesh->GetComponentScale();

	if (bMatchAgentRadius && RadiusBefore > KINDA_SMALL_NUMBER)
	{
		// 目标半径 = agent 半径 × 安全系数；只缩小、不放大（她原本 50cm > agent 34cm）
		const double DesiredRadius = FMath::Max(1.0, static_cast<double>(NavAgentRadiusCm) * FMath::Clamp(static_cast<double>(WifeAgentRadiusSafetyFactor), 0.1, 1.0));
		if (RadiusBefore > DesiredRadius)
		{
			const double Factor = DesiredRadius / RadiusBefore;
			Mesh->SetWorldScale3D(FVector(ScaleBefore.X * Factor, ScaleBefore.Y * Factor, ScaleBefore.Z));
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Wife] 体型对齐 NavMesh agent：半径 %.1f -> %.1f cm（agent 半径 %.1f，缩放 %.3f -> %.3f/%.3f/%.3f）"),
				RadiusBefore, DesiredRadius, NavAgentRadiusCm, ScaleBefore.X, ScaleBefore.X * Factor,
				ScaleBefore.Y * Factor, ScaleBefore.Z);
		}
	}

	// 缩放后再量最终尺寸与贴地偏移（圆柱 pivot 常在底部，用包围盒推）
	FBox Box(ForceInit);
	if (GetWifeBounds(Wife, Box))
	{
		WifeRadiusCm = static_cast<float>(Box.GetExtent().X);
		HalfHeightCm = static_cast<float>(Box.GetExtent().Z);
		BottomOffsetCm = static_cast<float>(Wife->GetActorLocation().Z - Box.Min.Z);
	}
	else
	{
		WifeRadiusCm = 34.f;
		HalfHeightCm = 100.f;
		BottomOffsetCm = 100.f;
	}
}

// ---------------------------------------------------------------- 现实尺寸（1 uu = 1 cm）

void UMetamorphosisWifeSubsystem::DeriveWifeHeightFromScene()
{
	if (!bDeriveHeightFromScene)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 找标尺物件（默认：一张双人床）。用"网格名包含"匹配，避开 Actor label 的命名差异。
	AActor* Reference = nullptr;
	float ReferenceLengthCm = 0.f;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor == WifeActor.Get())
		{
			continue;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			const UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Comp);
			if (MeshComp == nullptr || MeshComp->GetStaticMesh() == nullptr)
			{
				continue;
			}
			if (!MeshComp->GetStaticMesh()->GetName().Contains(HeightReferenceMeshName))
			{
				continue;
			}
			// 取最长的水平边当"床长"（双人床的长边 = 人躺下的方向）
			FBox Box(ForceInit);
			if (!GetWifeBounds(Actor, Box))
			{
				continue;
			}
			const FVector Size = Box.GetSize();
			const float Longest = static_cast<float>(FMath::Max(Size.X, Size.Y));
			if (Longest > ReferenceLengthCm)
			{
				ReferenceLengthCm = Longest;
				Reference = Actor;
			}
		}
	}

	if (Reference == nullptr || ReferenceLengthCm <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Wife] 场景标尺（网格名含 '%s'）没找到 -> 身高用兜底值 %.0fcm"),
			*HeightReferenceMeshName, WifeHeightCm);
		return;
	}

	const float RealLength = FMath::Max(10.f, HeightReferenceRealLengthCm);
	const float RawFactor = ReferenceLengthCm / RealLength;
	const float AutoFactor = FMath::Clamp(RawFactor, DerivedHeightFactorMin, FMath::Max(DerivedHeightFactorMin, DerivedHeightFactorMax));

	// 用户口径（2026-10-18）：自动反推是 4 倍（660cm）但"太大" -> 用 HeightSceneScaleOverride 直接指定 2 倍（330cm）。
	// 仍保留"按标尺反推"的日志，方便以后想改回去时一眼看到真实倍数。
	const bool bUseOverride = HeightSceneScaleOverride > 0.001f;
	const float Factor = bUseOverride ? HeightSceneScaleOverride : AutoFactor;

	WifeHeightCm = 165.f * Factor;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Wife] 场景尺度：标尺 %s 长 %.0fcm / 真实 %.0fcm = 倍数 %.2f（自动夹到 %.2f）｜%s -> 倍数 %.2f｜身高 = 165 × %.2f = %.0fcm"),
		*GetNameSafe(Reference), ReferenceLengthCm, RealLength, RawFactor, AutoFactor,
		bUseOverride ? TEXT("用 HeightSceneScaleOverride 指定") : TEXT("用自动反推"),
		Factor, Factor, WifeHeightCm);
}

void UMetamorphosisWifeSubsystem::ApplyRealBodyHeight()
{
	AActor* Wife = WifeActor.Get();
	UPrimitiveComponent* Mesh = WifeMesh.Get();
	if (Wife == nullptr || Mesh == nullptr)
	{
		return;
	}

	const float TargetHeight = FMath::Clamp(WifeHeightCm, 60.f, 2000.f);

	FBox Box(ForceInit);
	if (!GetWifeBounds(Wife, Box))
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 量不到包围盒，跳过身高缩放"));
		return;
	}

	const double HeightBefore = Box.GetSize().Z;
	if (HeightBefore <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double Factor = static_cast<double>(TargetHeight) / HeightBefore;
	if (FMath::Abs(Factor - 1.0) > 1.e-3)
	{
		const FVector Scale = Mesh->GetComponentScale();
		// 只改 Z：X/Y 已经按 NavMesh agent 半径对齐过了，不能再动（会把她的通行宽度改坏）
		Mesh->SetWorldScale3D(FVector(Scale.X, Scale.Y, Scale.Z * Factor));
	}

	// 重新量一次，得到最终的半径/半高/贴地偏移
	if (GetWifeBounds(Wife, Box))
	{
		WifeRadiusCm = static_cast<float>(Box.GetExtent().X);
		HalfHeightCm = static_cast<float>(Box.GetExtent().Z);
		BottomOffsetCm = static_cast<float>(Wife->GetActorLocation().Z - Box.Min.Z);
	}

	// 眼睛高度 = 身高 × 比例（成人约 0.92）。用比例而不是绝对值，身高随场景缩放时眼睛一起跟着走。
	const float FeetZ = static_cast<float>(Wife->GetActorLocation().Z) - BottomOffsetCm;
	const float EyeOffset = TargetHeight * FMath::Clamp(WifeEyeHeightRatio, 0.2f, 0.99f);
	EyeWorldZ = FeetZ + EyeOffset;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Wife] 现实尺寸：身高 %.1f -> %.1f cm（Z 缩放 ×%.3f）｜半径 %.1f 半高 %.1f 贴地偏移 %.1f｜眼睛高 %.1f（脚底 Z=%.1f）"),
		HeightBefore, HeightBefore * Factor, Factor,
		WifeRadiusCm, HalfHeightCm, BottomOffsetCm, EyeWorldZ - FeetZ, FeetZ);
}

// ---------------------------------------------------------------- 2D 精灵

AMetamorphosisWifeSprite* UMetamorphosisWifeSubsystem::GetWifeSprite() const
{
	return WifeSpriteActor.Get();
}

void UMetamorphosisWifeSubsystem::EnsureWifeSprite()
{
	if (!bWifeSpriteEnabled)
	{
		return;
	}

	AActor* Wife = WifeActor.Get();
	UWorld* World = GetWorld();
	if (Wife == nullptr || World == nullptr)
	{
		return;
	}

	if (AMetamorphosisWifeSprite* Existing = WifeSpriteActor.Get())
	{
		Existing->SetChaseMode(State == EWifeState::Chase);
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = nullptr;

	AMetamorphosisWifeSprite* Sprite = World->SpawnActor<AMetamorphosisWifeSprite>(
		AMetamorphosisWifeSprite::StaticClass(), Wife->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (Sprite == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 精灵生成失败"));
		return;
	}

	// 参数跟着子系统走（ini 只维护一份）
	Sprite->WifeHeightCm = WifeHeightCm;
	Sprite->SpriteFps = WifeSpriteFps;
	Sprite->SpriteIdleFps = WifeSpriteIdleFps;
	Sprite->SpriteChaseFps = WifeSpriteChaseFps;
	Sprite->bCastFakeShadow = bWifeSpriteShadow;
	Sprite->bMirrorSideInvert = bWifeSpriteMirrorSideInvert;
	Sprite->bLogWifeSprite = bLogWife;

	Sprite->SetFollowTarget(Wife);
	Sprite->SetBodyBottomOffsetCm(BottomOffsetCm);
	Sprite->SetChaseMode(State == EWifeState::Chase);

	WifeSpriteActor = Sprite;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Wife] 2D 精灵已生成：%s（跟随 %s，身高 %.0fcm，底边贴地偏移 %.1f）"),
		*Sprite->GetName(), *Wife->GetName(), WifeHeightCm, BottomOffsetCm);
}

// ---------------------------------------------------------------- 躁郁值影响

float UMetamorphosisWifeSubsystem::GetMoodRatio() const
{
	const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(GetWorld());
	return Stats != nullptr ? Stats->GetMoodRatio() : 0.f;
}

int32 UMetamorphosisWifeSubsystem::GetPlayerStage() const
{
	const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(GetWorld());
	return Stats != nullptr ? Stats->GetBodyStage() : 1;
}

float UMetamorphosisWifeSubsystem::GetEffectiveSightRangeCm() const
{
	float Range = BaseSightRangeCm;

	// 阶段 2+ 且没有风衣 -> 视线范围 × 配表倍率
	// ⚠️ TODO（风衣）：工程里还没有"是否穿风衣"的状态，现在按"阶段2+ 即无风衣"占位；
	//    风衣做出来后把这里的条件换成"未穿风衣"即可。
	const int32 Stage = GetPlayerStage();
	if (Stage >= 2)
	{
		if (const UMetamorphosisConfigSubsystem* Cfg = GetWorld() != nullptr && GetWorld()->GetGameInstance() != nullptr
			? GetWorld()->GetGameInstance()->GetSubsystem<UMetamorphosisConfigSubsystem>() : nullptr)
		{
			Range *= Cfg->GetMood().Stage2PlusNoCoatSightRangeMult;
		}
	}
	return Range;
}

float UMetamorphosisWifeSubsystem::GetEffectiveSpeedCmPerSec() const
{
	const int32 Stage = FMath::Max(1, GetPlayerStage());
	return PatrolSpeedCmPerSec * (1.f + SpeedBonusPerStage * (Stage - 1));
}

// ---------------------------------------------------------------- Tick / 状态机

void UMetamorphosisWifeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 暂停时她也必须停：否则玩家在暂停页里，她还会继续巡视/追逐/抓人（实测 bug）。
	if (UGameplayStatics::IsGamePaused(this) || (GetWorld() != nullptr && GetWorld()->IsPaused()))
	{
		return;
	}

	if (!bInitialized)
	{
		// 世界刚起时她可能还没就绪，重试几次
		bInitialized = FindAndPrepareWife();
		if (!bInitialized)
		{
			return;
		}
	}

	// 公寓范围（只量一次，2026-10-14）：她与以后的追兵都只在这个盒内活动
	if (!bApartmentMeasured)
	{
		MeasureApartmentBounds();
	}

	// 确保有 NavMesh（没铺就运行时生成；就绪前她**不移动**，避免退回到"手写航点"那版的穿模）
	if (!bNavReady)
	{
		EnsureNavReady();
	}

	AActor* Wife = WifeActor.Get();
	UWorld* World = GetWorld();
	if (Wife == nullptr || World == nullptr)
	{
		return;
	}

	// 2D 精灵：确保存在（关卡重载/被销毁时会没），并把"贴地偏移 / 追逐态"同步给它。
	// 注意精灵自己算位置与朝向，这里只推状态，避免两处各算一套。
	EyeWorldZ = static_cast<float>(Wife->GetActorLocation().Z) - BottomOffsetCm
		+ WifeHeightCm * FMath::Clamp(WifeEyeHeightRatio, 0.2f, 0.99f);
	EnsureWifeSprite();
	if (AMetamorphosisWifeSprite* Sprite = WifeSpriteActor.Get())
	{
		Sprite->SetBodyBottomOffsetCm(BottomOffsetCm);
		Sprite->SetChaseMode(State == EWifeState::Chase);
	}

	// 阶段变化时打一次日志（方便观察躁郁值影响）
	const int32 Stage = GetPlayerStage();
	if (Stage != LoggedStage)
	{
		LoggedStage = Stage;
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 受躁郁值影响更新：阶段=%d 躁郁比例=%.2f -> 视线=%.0fcm 速度=%.0fcm/s 停留缩放=%.2f"),
			Stage, GetMoodRatio(), GetEffectiveSightRangeCm(), GetEffectiveSpeedCmPerSec(),
			FMath::Lerp(1.f, PauseScaleAtMaxMood, FMath::Clamp(GetMoodRatio(), 0.f, 1.f)));
	}

	// 调试暂停 / NavMesh 未就绪：不移动，但视线判定照常跑（数值逻辑不回退）
	if (bPausedByDebug || !bNavReady)
	{
		UpdateSight(DeltaTime);
		return;
	}

	// ---- 追逐战：冷却计时 + 状态切换（阶段3；进入/退出用迟滞，避免在阈值上抖）----
	ChaseCooldownTimer = FMath::Max(0.f, ChaseCooldownTimer - DeltaTime);
	ChaseCatchCooldownTimer = FMath::Max(0.f, ChaseCatchCooldownTimer - DeltaTime);

	const float PlayerMoodNow = GetPlayerMood();

	// 怀疑值同样能触发追逐：默认自动取配表 Suspicion.ThresholdExposed（"暴露"档）。
	// ChaseEnterSuspicion：<0 = 自动读配表；0 = 禁用该条件；>0 = 手动覆盖。
	float EnterSuspicionNow = ChaseEnterSuspicion;
	if (EnterSuspicionNow < 0.f)
	{
		if (const UWorld* CfgWorld = GetWorld())
		{
			if (const UMetamorphosisConfigSubsystem* Cfg = CfgWorld->GetGameInstance() != nullptr
				? CfgWorld->GetGameInstance()->GetSubsystem<UMetamorphosisConfigSubsystem>() : nullptr)
			{
				EnterSuspicionNow = Cfg->GetSuspicion().ThresholdExposed;
			}
		}
	}
	const float PlayerSuspicionNow = [this]()
	{
		const UMetamorphosisStatsSubsystem* S = UMetamorphosisStatsSubsystem::Get(GetWorld());
		return S != nullptr ? S->GetSuspicion() : 0.f;
	}();

	if (State != EWifeState::Chase)
	{
		const bool bByMood = PlayerMoodNow >= ChaseEnterMood;
		const bool bBySuspicion = EnterSuspicionNow > 0.f && PlayerSuspicionNow >= EnterSuspicionNow;
		// 怀疑值【满】-> 持续追逐（只看怀疑值，不要求躁郁；进入后不因追丢而放弃）
		const bool bByPersistent = PersistentChaseAtSuspicion > 0.f && PlayerSuspicionNow >= PersistentChaseAtSuspicion;
		if (ChaseCooldownTimer <= 0.f && (bByMood || bBySuspicion || bByPersistent))
		{
			bPersistentChase = bByPersistent;
			EnterChase(bByPersistent
				? TEXT("怀疑值满 -> 持续追逐（追丢也不放弃，直到抓住或怀疑值降下来）")
				: (bByMood && bBySuspicion
					? TEXT("躁郁与怀疑值同时到进入线")
					: (bByMood ? TEXT("主控进入阶段3（躁郁值到进入线）")
						: TEXT("主控怀疑值到进入线（暴露档 ThresholdExposed）"))));
			if (bPersistentChase)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 怀疑值满（%.1f，进入线 %.0f）-> 持续追逐开始｜解除线 %.0f"),
					PlayerSuspicionNow, PersistentChaseAtSuspicion, PersistentChaseReleaseSuspicion);
			}
		}
	}
	// 持续追逐：只有怀疑值降到解除线以下才退出（不看躁郁、也不看是否追丢）
	else if (bPersistentChase)
	{
		if (!bDebugForceChase && PlayerSuspicionNow < PersistentChaseReleaseSuspicion)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 持续追逐结束：怀疑值降到 %.1f（解除线 %.0f）"),
				PlayerSuspicionNow, PersistentChaseReleaseSuspicion);
			bPersistentChase = false;
			ExitChase(TEXT("持续追逐解除：怀疑值降到解除线以下"));
		}
	}
	// 退出：两个条件都低于各自退出线才退（迟滞）
	else if (!bDebugForceChase && PlayerMoodNow < ChaseExitMoodBelow && PlayerSuspicionNow < ChaseExitSuspicionBelow)
	{
		ExitChase(TEXT("躁郁与怀疑值都回落到退出线以下"));
	}

	StateTimer += DeltaTime;

	/** 进入停留（越躁停得越短） */
	auto EnterPause = [this, Wife](const TCHAR* Reason)
	{
		State = EWifeState::Pause;
		StateTimer = 0.f;
		const float Scale = FMath::Lerp(1.f, PauseScaleAtMaxMood, FMath::Clamp(GetMoodRatio(), 0.f, 1.f));
		PauseDuration = FMath::FRandRange(PauseSecondsMin, PauseSecondsMax) * Scale;
		BaseYaw = Wife->GetActorRotation().Yaw;
		PathPoints.Reset();
		PathIndex = 0;
		if (bLogWife)
		{
			UE_LOG(LogMetamorphosis, Verbose, TEXT("[Wife] %s -> 停留 %.2f 秒（躁郁缩放 %.2f）"), Reason, PauseDuration, Scale);
		}
	};

	switch (State)
	{
	case EWifeState::Patrol:
	{
		const FVector Loc = Wife->GetActorLocation();
		MoveElapsed += DeltaTime;

		// 跳过已经走到的路径点
		while (PathIndex < PathPoints.Num())
		{
			if (FVector::Dist2D(PathPoints[PathIndex], Loc) <= PathPointToleranceCm)
			{
				++PathIndex;
				continue;
			}
			break;
		}

		// 路径走完（或压根没有路径）-> 到点，进入停留
		if (PathPoints.Num() == 0 || PathIndex >= PathPoints.Num())
		{
			EnterPause(TEXT("到达巡视点"));
			break;
		}

		// 走太久（被卡住/路径失效）-> 重新规划，失败就先停下来
		if (MoveElapsed > MoveTimeoutSeconds)
		{
			FVector NewPoint;
			if (PickPatrolPoint(NewPoint) && BuildPathTo(NewPoint))
			{
				TargetLocation = NewPoint;
				MoveElapsed = 0.f;
				if (bLogWife)
				{
					UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 走了 %.0f 秒还没到 -> 重新规划去 %s"),
						MoveTimeoutSeconds, *NewPoint.ToCompactString());
				}
			}
			else
			{
				EnterPause(TEXT("走太久且重新规划失败"));
			}
			break;
		}

		const FVector Aim = PathPoints[PathIndex];
		FVector Dir = Aim - Loc;
		Dir.Z = 0.f;
		const float Dist = static_cast<float>(Dir.Size());
		const float Step = GetEffectiveSpeedCmPerSec() * DeltaTime;

		if (Dist > KINDA_SMALL_NUMBER)
		{
			const FVector DirN = Dir / Dist;
			// 贴到主控身上就停止推进：她现在是实体（bWifeBlocksPlayer=true），继续推会把 16cm 的虫子顶进墙里卡死
			const APawn* NearPlayer = FindPlayerPawn(GetWorld());
			const bool bHoldNearPlayer = StopApproachDistanceCm > 0.f && NearPlayer != nullptr
				&& FVector::Dist2D(Loc, NearPlayer->GetActorLocation()) < StopApproachDistanceCm;
			if (!bHoldNearPlayer)
			{
				if (!TryMoveWife(Loc + DirN * FMath::Min(Step, Dist)))
				{
					// 前方会撞家具：放弃当前路径，换一个点再走（不硬挤，避免穿模）
					PathPoints.Reset();
					PathIndex = 0;
					EnterPause(TEXT("前方被家具挡住"));
					break;
				}
			}
			SnapToGround();

			// 朝向移动方向
			const float TargetYaw = DirN.Rotation().Yaw;
			const float NewYaw = FMath::FInterpTo(Wife->GetActorRotation().Yaw, TargetYaw, DeltaTime, TurnRateDegPerSec / 90.f);
			Wife->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
		}
		break;
	}

	case EWifeState::Chase:
	{
		TickChase(DeltaTime);
		break;
	}

	case EWifeState::Pause:
	{
		// 停留时缓慢左右看
		if (PauseLookAroundDeg > 0.f && PauseDuration > KINDA_SMALL_NUMBER)
		{
			const float Phase = (StateTimer / PauseDuration) * 2.f * PI;
			Wife->SetActorRotation(FRotator(0.f, BaseYaw + FMath::Sin(Phase) * PauseLookAroundDeg, 0.f));
		}

		if (StateTimer >= PauseDuration)
		{
			FVector NewPoint;
			if (PickPatrolPoint(NewPoint) && BuildPathTo(NewPoint))
			{
				TargetLocation = NewPoint;
				State = EWifeState::Patrol;
				MoveElapsed = 0.f;
				StateTimer = 0.f;
				if (bLogWife)
				{
					UE_LOG(LogMetamorphosis, Verbose, TEXT("[Wife] 出发去下一个巡视点 %s（路径 %d 个点）"),
						*NewPoint.ToCompactString(), PathPoints.Num());
				}
			}
			else
			{
				// 取不到点/规划失败：原地再停一会儿
				StateTimer = 0.f;
				PauseDuration = FMath::FRandRange(PauseSecondsMin, PauseSecondsMax);
				BaseYaw = Wife->GetActorRotation().Yaw;
			}
		}
		break;
	}

	default:
		break;
	}

	// 穿模自检（客观判据）：每 0.25 秒看她有没有和关卡静态几何体重叠
	OverlapCheckTimer += DeltaTime;
	if (OverlapCheckTimer >= 0.25f)
	{
		OverlapCheckTimer = 0.f;
		FString HitName;
		if (IsOverlappingStaticGeometry(HitName))
		{
			++OverlapFrameCount;   // 验证用累计计数：必须为 0
			if (bWarnOnOverlap && !bOverlapWarned)
			{
				bOverlapWarned = true;
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Wife] ⚠ 与静态几何体重叠（穿模）：%s @ %s [状态=%d 路径点=%d/%d]"),
					*HitName, *Wife->GetActorLocation().ToCompactString(), static_cast<int32>(State), PathIndex, PathPoints.Num());
			}
		}
		else
		{
			bOverlapWarned = false;   // 恢复正常，允许再次告警
		}

		// 严格口径复核（2026-10-17 用户要求）：**用她的视觉半径、不做 shrink、不做台阶忽略**，
		// 只看"她的视觉圆柱有没有蹭进静态几何体"（唯一例外是把胶囊底抬 3cm 避开地板本身）。
		// 这条和上面的"碰撞胶囊"口径是两件事，分别计数，避免"计数为 0 但其实视觉上蹭进去了"。
		FString StrictHit;
		if (IsPenetratingFurnitureStrict(StrictHit))
		{
			++StrictPenetrationCount;
			StrictPenetrationHits.FindOrAdd(StrictHit)++;
			if (bLogWife && StrictPenetrationCount <= 5)
			{
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Wife] ⚠ 视觉圆柱蹭进静态几何体（严格口径，第 %d 次）：%s @ %s"),
					StrictPenetrationCount, *StrictHit, *Wife->GetActorLocation().ToCompactString());
			}
		}
	}

	UpdateSight(DeltaTime);
}

// ---------------------------------------------------------------- NavMesh（就绪 / 取点 / 规划）

FVector UMetamorphosisWifeSubsystem::GetNavVolumeCenter() const
{
	if (!NavVolumeCenterOverride.IsNearlyZero())
	{
		return NavVolumeCenterOverride;
	}
	// 锁在公寓且量到了 -> 用公寓中心（这样导航包围体正好盖住公寓）
	if (bLockToApartment && ApartmentBox.IsValid)
	{
		return ApartmentBox.GetCenter();
	}
	// 默认以她的家为中心
	return HomeLocation;
}

bool UMetamorphosisWifeSubsystem::EnsureNavReady()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (NavSys == nullptr)
	{
		if (!bNavFailureLogged)
		{
			bNavFailureLogged = true;
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Wife] 拿不到 NavigationSystem —— 她不会移动（请确认工程依赖包含 NavigationSystem 模块）"));
		}
		return false;
	}

	// ① 已经有导航数据（地图里正规铺过，或刚才运行时生成完毕）-> 用它
	//    注意：导航数据对象存在 != 网格已经建好 —— 必须等构建结束、且可达区域非空，才算真就绪，
	//    否则会提前宣布"就绪"，她一动就按空 NavMesh 规划（表现和"穿模"一样难看）。
	if (const ANavigationData* Existing = NavSys->GetDefaultNavDataInstance())
	{
		if (NavSys->IsNavigationBuildInProgress() || Existing->GetNavigableBounds().Num() == 0)
		{
			return false;
		}

		const bool bWasReady = bNavReady;
		bNavReady = true;
		if (!bWasReady)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] NavMesh 就绪（%s，用了 %.1f 秒）"),
				NavBoundsVolume.IsValid() ? TEXT("运行时生成") : TEXT("地图里已有的导航数据"), NavWaitElapsed);
			DebugPrintNav();
		}
		return true;
	}

	// ② 没有导航数据：运行时自己生成包围体
	if (!bRuntimeNavMesh)
	{
		if (!bNavFailureLogged)
		{
			bNavFailureLogged = true;
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Wife] 地图里没有 NavMesh 且 bRuntimeNavMesh=false —— 她不会移动。"
					 "请在地图里铺 NavMeshBoundsVolume（编辑器步骤见 Docs/妻子AI（临时圆柱体）.md）"));
		}
		return false;
	}

	if (!bNavSetupTried)
	{
		bNavSetupTried = true;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;   // 运行时临时的，不进存档/关卡
		const FVector Center = GetNavVolumeCenter();
		ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(
			ANavMeshBoundsVolume::StaticClass(), FTransform(Center), SpawnParams);

		if (Volume == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 运行时生成 NavMeshBoundsVolume 失败 —— 她不会移动"));
			return false;
		}

		// ⚠️ 关键：运行时 Spawn 的 Volume 没有 brush（brush 是编辑器里生成的），
		//    于是 GetComponentsBoundingBox() 是 0 体积，导航系统会当成"无效包围体"直接忽略 ->
		//    挂一个**不碰撞、不影响导航**的 BoxComponent，只为提供有效包围盒。
		UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Volume, TEXT("MetaNavBoundsBox"));
		if (BoundsBox != nullptr)
		{
			// 锁在公寓时：包围体按**公寓盒**来（比默认 40×40m 小得多 -> 重建更快，也不会把户外地形铺进来）
			// ⚠️ Z 也必须覆盖公寓盒：盒子中心在楼层中部（Z≈419），若还用固定的 300 半高，
			//    体积会变成 Z∈[119,719]，把地板（Z≈0~110）排除在外 -> NavMesh 铺不出来、她原地不动（实测踩到过）
			FVector Extent(NavVolumeRadiusCm, NavVolumeRadiusCm, NavVolumeHalfHeightCm);
			if (bLockToApartment && ApartmentBox.IsValid)
			{
				const FVector AExtent = ApartmentBox.GetExtent();
				Extent = FVector(AExtent.X + ApartmentNavPaddingCm, AExtent.Y + ApartmentNavPaddingCm,
					AExtent.Z + ApartmentNavPaddingCm);
			}

			BoundsBox->SetBoxExtent(Extent);
			BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BoundsBox->SetCanEverAffectNavigation(false);
			BoundsBox->SetMobility(EComponentMobility::Movable);
			BoundsBox->SetHiddenInGame(true);
			BoundsBox->SetupAttachment(Volume->GetRootComponent());
			BoundsBox->RegisterComponent();
		}

		NavBoundsVolume = Volume;
		NavSys->OnNavigationBoundsUpdated(Volume);
		NavSys->Build();

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 运行时导航包围体已生成：中心=%s XY半径=%s 半高=%.0fcm（动态 NavMesh 开始重建，等待就绪…）"),
			*Center.ToCompactString(),
			*(bLockToApartment && ApartmentBox.IsValid
				? FString::Printf(TEXT("公寓盒 %.0f×%.0f"), ApartmentBox.GetExtent().X + ApartmentNavPaddingCm, ApartmentBox.GetExtent().Y + ApartmentNavPaddingCm)
				: FString::Printf(TEXT("%.0f×%.0f"), NavVolumeRadiusCm, NavVolumeRadiusCm)),
			NavVolumeHalfHeightCm);
		return false;
	}

	// ③ 等重建完成
	NavWaitElapsed += GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.f;
	if (NavSys->IsNavigationBuildInProgress())
	{
		return false;
	}

	if (ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		bNavReady = true;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] NavMesh 就绪（运行时生成，用了 %.1f 秒）"), NavWaitElapsed);
		DebugPrintNav();
		return true;
	}

	if (NavWaitElapsed > NavRebuildWaitSeconds && !bNavFailureLogged)
	{
		bNavFailureLogged = true;
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Wife] 等了 %.0f 秒 NavMesh 还是没建出来 —— 她不会移动（不再假装能走）。"
				 "排查：①地图里地板/墙是否有碰撞（导航靠碰撞生成）②导航包围体是否覆盖到房间 "
				 "③[/Script/NavigationSystem.RecastNavMesh] RuntimeGeneration 是否为 Dynamic"),
			NavRebuildWaitSeconds);
	}
	return false;
}

bool UMetamorphosisWifeSubsystem::PickPatrolPoint(FVector& OutPoint)
{
	UWorld* World = GetWorld();
	AActor* Wife = WifeActor.Get();
	UNavigationSystemV1* NavSys = World != nullptr ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (World == nullptr || Wife == nullptr || NavSys == nullptr || !bNavReady)
	{
		return false;
	}

	const FVector Cur = Wife->GetActorLocation();
	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);

	for (int32 Try = 0; Try < FMath::Max(1, PatrolPointRetryCount); ++Try)
	{
		// 在整个可达 NavMesh 上随机取点（半径给大 = 全公寓游走；NavMesh 按 agent 半径生成，
		// 取到的点天然离家具/墙有间隙，不会再像"手写航点"那样穿床穿墙）
		FNavLocation Result;
		if (!NavSys->GetRandomReachablePointInRadius(HomeLocation, PatrolRadiusCm, Result, NavData))
		{
			continue;
		}

		// 太近的点没意义（会在原地打转）
		if (FVector::Dist2D(Result.Location, Cur) < MinPatrolPointDistanceCm)
		{
			continue;
		}

		// 锁在公寓内（2026-10-14）：超出公寓盒的点直接丢弃，避免从开口走到户外
		if (!IsInsideApartment(Result.Location))
		{
			continue;
		}

		OutPoint = Result.Location;   // Z 就是 NavMesh 表面高度，贴地由 SnapToGround 负责
		return true;
	}

	return false;
}

bool UMetamorphosisWifeSubsystem::BuildPathTo(const FVector& Dest)
{
	UWorld* World = GetWorld();
	AActor* Wife = WifeActor.Get();
	if (World == nullptr || Wife == nullptr)
	{
		return false;
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Wife->GetActorLocation(), Dest, Wife);
	if (Path == nullptr || !Path->IsValid() || Path->PathPoints.Num() < 2)
	{
		if (bLogWife)
		{
			UE_LOG(LogMetamorphosis, Verbose, TEXT("[Wife] 规划到 %s 的路径失败（点是可达的，但可能被临时挡住的体积隔开）"),
				*Dest.ToCompactString());
		}
		return false;
	}

	PathPoints = Path->PathPoints;
	PathIndex = 1;   // 第 0 个是起点
	return true;
}

// ---------------------------------------------------------------- 取点 / 贴地

void UMetamorphosisWifeSubsystem::SnapToGround()
{
	UWorld* World = GetWorld();
	AActor* Wife = WifeActor.Get();
	if (World == nullptr || Wife == nullptr)
	{
		return;
	}

	const FVector Loc = Wife->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WifeSnapGround), false, Wife);
	FHitResult Hit;
	const FVector Start = Loc + FVector(0.f, 0.f, GroundProbeUpCm);
	const FVector End = Loc - FVector(0.f, 0.f, GroundProbeDownCm);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params))
	{
		const float DesiredZ = Hit.ImpactPoint.Z + BottomOffsetCm;
		if (FMath::Abs(DesiredZ - Loc.Z) > 0.2f)
		{
			Wife->SetActorLocation(FVector(Loc.X, Loc.Y, DesiredZ), false, nullptr, ETeleportType::None);
		}
	}
}

// ---------------------------------------------------------------- 视线 → 怀疑值

bool UMetamorphosisWifeSubsystem::ComputeSeeingPlayer(float& OutDistance) const
{
	OutDistance = -1.f;

	UWorld* World = GetWorld();
	const AActor* Wife = WifeActor.Get();
	APawn* Player = FindPlayerPawn(World);
	if (World == nullptr || Wife == nullptr || Player == nullptr)
	{
		return false;
	}

	const FVector WifeLoc = Wife->GetActorLocation();
	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector ToPlayer = PlayerLoc - WifeLoc;
	const float Dist = static_cast<float>(ToPlayer.Size());
	OutDistance = Dist;

	// ① 距离
	const float Range = GetEffectiveSightRangeCm();
	if (Dist > Range)
	{
		return false;
	}

	// ② 视锥（以她朝向为中心，水平面判定）
	const FVector Flat = FVector(ToPlayer.X, ToPlayer.Y, 0.f).GetSafeNormal();
	if (!Flat.IsNearlyZero())
	{
		const FVector Facing = Wife->GetActorForwardVector().GetSafeNormal2D();
		const float CosAngle = FVector::DotProduct(Facing, Flat);
		const float HalfConeCos = FMath::Cos(FMath::DegreesToRadians(SightConeDeg * 0.5f));
		if (CosAngle < HalfConeCos)
		{
			return false;
		}
	}

	// ③ 视线不被挡
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WifeSight), false, Wife);
	Params.AddIgnoredActor(Player);
	FHitResult Hit;
	// 2026-10-18：她现在是"现实身高"的人（165cm），眼睛要按**脚底 + 眼睛高**算，
	// 不能再用 HalfHeight*0.5（那是 100cm 高矮圆柱时代的算法，眼睛会跑到腰以下）。
	const float FeetZ = static_cast<float>(WifeLoc.Z) - BottomOffsetCm;
	const float EyeZ = FeetZ + WifeHeightCm * FMath::Clamp(WifeEyeHeightRatio, 0.2f, 0.99f);
	const FVector Eye(WifeLoc.X, WifeLoc.Y, EyeZ);
	const FVector Target = PlayerLoc + FVector(0.f, 0.f, 8.f);
	if (World->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Params))
	{
		return false;
	}

	return true;
}

void UMetamorphosisWifeSubsystem::UpdateSight(float DeltaTime)
{
	GlanceTimer += DeltaTime;
	if (GlanceTimer < GlanceIntervalSeconds)
	{
		return;
	}
	GlanceTimer = 0.f;

	float Dist = -1.f;
	const bool bSeeing = ComputeSeeingPlayer(Dist);
	const bool bChanged = (bSeeing != bSeeingPlayer);
	bSeeingPlayer = bSeeing;

	if (bSeeing)
	{
		// 配表 AddSuspicionByTrigger 没有自带冷却，限频由 GlanceIntervalSeconds 负责
		if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(GetWorld()))
		{
			Stats->AddSuspicionByTrigger(TEXT("SightGlance"));
		}
		if (bLogWife)
		{
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Wife] 看见主控：距离=%.0fcm 视线范围=%.0fcm 主控阶段=%d 躁郁比例=%.2f -> 加怀疑(SightGlance)"),
				Dist, GetEffectiveSightRangeCm(), GetPlayerStage(), GetMoodRatio());
		}
	}
	else if (bChanged && bLogWife)
	{
		UE_LOG(LogMetamorphosis, Verbose, TEXT("[Wife] 主控离开视线（距离=%.0fcm 视线范围=%.0fcm）"), Dist, GetEffectiveSightRangeCm());
	}
}

// ---------------------------------------------------------------- 调试控制

void UMetamorphosisWifeSubsystem::DebugPause()
{
	bPausedByDebug = true;
	State = EWifeState::Idle;
	UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 调试暂停：AI 已停止"));
}

void UMetamorphosisWifeSubsystem::DebugResume()
{
	bPausedByDebug = false;
	State = EWifeState::Pause;
	StateTimer = 0.f;
	PauseDuration = 1.f;
	UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 调试恢复：AI 继续"));
}

bool UMetamorphosisWifeSubsystem::DebugGoto(float X, float Y)
{
	UWorld* World = GetWorld();
	AActor* Wife = WifeActor.Get();
	if (World == nullptr || Wife == nullptr)
	{
		return false;
	}

	FVector Point(X, Y, Wife->GetActorLocation().Z);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WifeGoto), false, Wife);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, GroundProbeUpCm),
		Point - FVector(0.f, 0.f, GroundProbeDownCm), ECC_Pawn, Params))
	{
		Point.Z = Hit.ImpactPoint.Z + BottomOffsetCm;
	}

	Wife->SetActorLocation(Point, false, nullptr, ETeleportType::TeleportPhysics);
	TargetLocation = Point;
	State = EWifeState::Pause;
	StateTimer = 0.f;
	PauseDuration = 0.5f;
	UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 调试传送 -> %s"), *Point.ToCompactString());
	return true;
}

bool UMetamorphosisWifeSubsystem::DebugGlanceNow()
{
	const bool bWas = bSeeingPlayer;
	float Dist = -1.f;
	bSeeingPlayer = ComputeSeeingPlayer(Dist);
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Wife] 强制视线判定：看见=%d 距离=%.0fcm 视线范围=%.0fcm 视锥=%.0f度 阶段=%d 躁郁比例=%.2f（之前=%d）"),
		bSeeingPlayer ? 1 : 0, Dist, GetEffectiveSightRangeCm(), SightConeDeg, GetPlayerStage(), GetMoodRatio(),
		bWas ? 1 : 0);
	return bSeeingPlayer;
}

// ---------------------------------------------------------------- NavMesh 查询 / 自检

FVector UMetamorphosisWifeSubsystem::GetCurrentPathPoint() const
{
	if (PathPoints.IsValidIndex(PathIndex))
	{
		return PathPoints[PathIndex];
	}
	return TargetLocation;
}

bool UMetamorphosisWifeSubsystem::IsOverlappingStaticGeometry(FString& OutHitName) const
{
	const AActor* Wife = WifeActor.Get();
	if (Wife == nullptr)
	{
		OutHitName.Reset();
		return false;
	}
	return IsOverlappingStaticGeometryAt(Wife->GetActorLocation(), OutHitName);
}

bool UMetamorphosisWifeSubsystem::IsOverlappingStaticGeometryAt(const FVector& TestLocation, FString& OutHitName) const
{
	OutHitName.Reset();

	UWorld* World = GetWorld();
	AActor* Wife = WifeActor.Get();
	if (World == nullptr || Wife == nullptr)
	{
		return false;
	}

	// 用她的体积（半径/半高）做一个略缩的胶囊去重叠检测。
	// 说明：UE 5.6 的 FCollisionShape 只有 Box/Sphere/Capsule（没有 Cylinder），
	// 胶囊底是圆的，站立时最低点刚好贴地会被误判成"重叠"，所以按 OverlapCheckShrink 缩一点。
	const float Radius = FMath::Max(1.f, WifeRadiusCm * FMath::Clamp(OverlapCheckShrink, 0.1f, 1.f));
	const float HalfHeight = FMath::Max(Radius + 1.f, HalfHeightCm * FMath::Clamp(OverlapCheckShrink, 0.1f, 1.f));
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WifeOverlapCheck), false, Wife);
	if (const APawn* Player = FindPlayerPawn(World))
	{
		Params.AddIgnoredActor(Player);   // 虫子贴着她不算她穿模
	}

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, TestLocation, FQuat::Identity, ECC_WorldStatic, Shape, Params);

	// 她的脚底高度：低于"脚 + 台阶高度"的矮物件（鞋、地毯、小杂物）会被 NavMesh 当成可行走地面跨过去，
	// 那不算穿模 —— 用户报的是"穿床/穿桌/穿墙"。所以只把够高的静态几何体当判据。
	const float FeetZ = static_cast<float>(TestLocation.Z) - BottomOffsetCm;
	const float IgnoreBelowZ = FeetZ + FMath::Max(0.f, OverlapIgnoreBelowStepCm);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Comp = Overlap.GetComponent();
		if (Comp == nullptr || !Overlap.bBlockingHit)
		{
			continue;
		}
		// 只把"关卡里的静态几何体"当成穿模判据（她自己是 Movable 且已忽略）
		if (Comp->Mobility != EComponentMobility::Static)
		{
			continue;
		}
		const float CompTopZ = static_cast<float>(Comp->Bounds.Origin.Z + Comp->Bounds.BoxExtent.Z);
		if (CompTopZ <= IgnoreBelowZ)
		{
			continue;   // 矮物件：她本来就该从上面走过去
		}
		OutHitName = FString::Printf(TEXT("%s.%s"), *GetNameSafe(Comp->GetOwner()), *Comp->GetName());
		return true;
	}

	return false;
}

FString UMetamorphosisWifeSubsystem::GetStrictPenetrationSummary() const
{
	if (StrictPenetrationHits.Num() == 0)
	{
		return TEXT("（没蹭到任何东西）");
	}

	// 按次数排序，最多列 5 个，避免一行日志太长
	TArray<TPair<FString, int32>> Sorted;
	for (const TPair<FString, int32>& Pair : StrictPenetrationHits)
	{
		Sorted.Add(Pair);
	}
	Sorted.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
	{
		return A.Value > B.Value;
	});

	FString Result;
	for (int32 i = 0; i < Sorted.Num() && i < 5; ++i)
	{
		Result += FString::Printf(TEXT("%s%s x%d"), i == 0 ? TEXT("") : TEXT(", "), *Sorted[i].Key, Sorted[i].Value);
	}
	if (Sorted.Num() > 5)
	{
		Result += FString::Printf(TEXT(" …共 %d 种"), Sorted.Num());
	}
	return Result;
}

bool UMetamorphosisWifeSubsystem::IsPenetratingFurnitureStrict(FString& OutHitName) const
{
	const AActor* Wife = WifeActor.Get();
	if (Wife == nullptr)
	{
		OutHitName.Reset();
		return false;
	}
	return IsPenetratingFurnitureStrictAt(Wife->GetActorLocation(), OutHitName);
}

bool UMetamorphosisWifeSubsystem::IsPenetratingFurnitureStrictAt(const FVector& TestLocation, FString& OutHitName) const
{
	// 严格口径（2026-10-17）：**视觉半径、不 shrink、不按台阶高度忽略**，
	// 只把胶囊底抬 StrictProbeFloorLiftCm 避开"站立时本来就贴着的地面"。
	// 用途：① 复核"她的视觉圆柱到底有没有蹭进家具" ② **移动预检也用它**
	//       （用户报的"走进家具"实测就发生在这些被旧口径忽略的情形：shrink 0.9 让半径小了 3cm、
	//        35cm 以下的家具被整条忽略 -> 视觉上明显蹭进去）。
	OutHitName.Reset();

	UWorld* World = GetWorld();
	AActor* Wife = WifeActor.Get();
	if (World == nullptr || Wife == nullptr)
	{
		return false;
	}

	const float Radius = FMath::Max(1.f, WifeRadiusCm);
	const float HalfHeight = FMath::Max(Radius + 1.f, HalfHeightCm);
	const float Lift = FMath::Clamp(StrictProbeFloorLiftCm, 0.f, HalfHeight * 0.5f);

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight - Lift);
	const FVector TestCenter = TestLocation + FVector(0.f, 0.f, Lift);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WifeStrictPenetration), false, Wife);
	if (const APawn* Player = FindPlayerPawn(World))
	{
		Params.AddIgnoredActor(Player);
	}

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, TestCenter, FQuat::Identity, ECC_WorldStatic, Shape, Params);

	const float FeetZ = static_cast<float>(TestLocation.Z) - BottomOffsetCm;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Comp = Overlap.GetComponent();
		if (Comp == nullptr || !Overlap.bBlockingHit)
		{
			continue;
		}
		if (Comp->Mobility != EComponentMobility::Static)
		{
			continue;
		}
		// 唯一例外：顶面在她脚底以下（地板/地毯/贴地的矮台）—— 这些本来就该被走过
		const float CompTopZ = static_cast<float>(Comp->Bounds.Origin.Z + Comp->Bounds.BoxExtent.Z);
		if (CompTopZ <= FeetZ + StrictProbeFloorLiftCm)
		{
			continue;
		}
		OutHitName = FString::Printf(TEXT("%s.%s"), *GetNameSafe(Comp->GetOwner()), *Comp->GetName());
		return true;
	}

	return false;
}

bool UMetamorphosisWifeSubsystem::TryMoveWife(const FVector& NewLocation)
{
	AActor* Wife = WifeActor.Get();
	UWorld* World = GetWorld();
	if (Wife == nullptr || World == nullptr)
	{
		return false;
	}

	// 移动前预检（2026-10-16 修穿模）：目标位置会与静态几何体重叠就**不移动**。
	// 之前只做"每 0.25 秒报告一次"的自检，没有任何阻止 —— 路径跟随切角/动态 NavMesh 通道偏窄时
	// 她会直接切进床、桌、墙里（用户报的"走进家具"）。现在改成"宁可停下重规划，也不走进几何体"。
	FString BlockedBy;

	// 例外：她**此刻已经**在几何体里（例如被 Meta.Wife.Goto 瞬移进去、或极端情况下被挤进去）时，
	// 必须允许移动，否则预检会把她永久困在里面（逃出过程中自检计数会涨，属已知情况）。
	// 2026-10-17：预检改用**严格口径**（视觉半径 / 不 shrink / 不忽略矮家具）。
	// 原因：旧口径（shrink 0.9 + 忽略 35cm 以下）实测漏掉了真正的穿模
	// （严格口径统计到 SM_NetWorkFilter_01_D x130、StaticMeshActor_15 x46、SM_RadioComponents_01_F x11 次蹭入）。
	FString CurrentHit;
	const bool bCurrentlyStuck = IsPenetratingFurnitureStrictAt(Wife->GetActorLocation(), CurrentHit);

	if (!bCurrentlyStuck && IsPenetratingFurnitureStrictAt(NewLocation, BlockedBy))
	{
		++MoveBlockedCount;
		MoveBlockedLogTimer -= 1.f;   // 由调用方按帧推进；这里只做简化限频（每 ~2 秒一条）
		if (bLogWife && MoveBlockedLogTimer <= 0.f)
		{
			MoveBlockedLogTimer = 120.f;   // 60fps 下约 2 秒
			UE_LOG(LogMetamorphosis, Verbose,
				TEXT("[Wife] 移动预检拦下（第 %d 次）：目标 %s 会撞上 %s -> 停在原地并重规划"),
				MoveBlockedCount, *NewLocation.ToCompactString(), *BlockedBy);
		}
		return false;
	}

	Wife->SetActorLocation(NewLocation, false, nullptr, ETeleportType::None);
	return true;
}

FString UMetamorphosisWifeSubsystem::GetNavSummary() const
{
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys = World != nullptr ? UNavigationSystemV1::GetCurrent(const_cast<UWorld*>(World)) : nullptr;
	const ANavigationData* NavData = NavSys != nullptr ? NavSys->GetDefaultNavDataInstance() : nullptr;

	FString BoundsText = TEXT("无");
	if (NavData != nullptr)
	{
		const TArray<FBox> Bounds = NavData->GetNavigableBounds();
		if (Bounds.Num() > 0)
		{
			FBox Union(ForceInit);
			for (const FBox& B : Bounds)
			{
				Union += B;
			}
			BoundsText = FString::Printf(TEXT("%d 块 并集中心=%s 半径=%s"),
				Bounds.Num(), *Union.GetCenter().ToCompactString(), *Union.GetExtent().ToCompactString());
		}
		else
		{
			BoundsText = TEXT("0 块（还没建出来）");
		}
	}

	return FString::Printf(
		TEXT("就绪=%d 生成方式=%s 包围体=%s 可达区域=%s 等待=%.1fs"),
		bNavReady ? 1 : 0,
		NavBoundsVolume.IsValid() ? TEXT("运行时生成") : TEXT("地图自带"),
		NavBoundsVolume.IsValid() ? *FString::Printf(TEXT("中心=%s 半径=%.0f"), *GetNavVolumeCenter().ToCompactString(), NavVolumeRadiusCm) : TEXT("-"),
		*BoundsText, NavWaitElapsed);
}

void UMetamorphosisWifeSubsystem::DebugPrintNav()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] NavMesh：%s"), *GetNavSummary());
	UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 公寓盒：%s"), *GetApartmentSummary());
	if (const AActor* Wife = WifeActor.Get())
	{
		const FVector Loc = Wife->GetActorLocation();
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 她在公寓盒内=%d（位置=%s）"),
			IsInsideApartment(Loc) ? 1 : 0, *Loc.ToCompactString());
		if (ApartmentBox.IsValid)
		{
			const FVector C = ApartmentBox.GetCenter();
			const FVector E = ApartmentBox.GetExtent();
			UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 公寓盒范围：X[%.0f, %.0f] Y[%.0f, %.0f] Z[%.0f, %.0f]"),
				C.X - E.X, C.X + E.X, C.Y - E.Y, C.Y + E.Y, C.Z - E.Z, C.Z + E.Z);
		}
	}

	const UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys = World != nullptr ? UNavigationSystemV1::GetCurrent(const_cast<UWorld*>(World)) : nullptr;
	const ANavigationData* NavData = NavSys != nullptr ? NavSys->GetDefaultNavDataInstance() : nullptr;
	if (NavData != nullptr)
	{
		const TArray<FBox> Bounds = NavData->GetNavigableBounds();
		for (int32 i = 0; i < Bounds.Num() && i < 8; ++i)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Wife]   可达块 %d：中心=%s 半径=%s"),
				i, *Bounds[i].GetCenter().ToCompactString(), *Bounds[i].GetExtent().ToCompactString());
		}
	}

	// 顺带报一次她此刻有没有和静态几何体重叠（穿模的客观判据）
	FString HitName;
	const bool bOverlapping = IsOverlappingStaticGeometry(HitName);
	UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 与静态几何体重叠=%d %s（半径=%.1f 半高=%.1f 检测缩比=%.2f）"),
		bOverlapping ? 1 : 0, bOverlapping ? *FString::Printf(TEXT("命中 %s"), *HitName) : TEXT(""),
		WifeRadiusCm, HalfHeightCm, OverlapCheckShrink);
}

// ---------------------------------------------------------------- 公寓范围（2026-10-14）

void UMetamorphosisWifeSubsystem::MeasureApartmentBounds()
{
	bApartmentMeasured = true;

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// ① ini 给了覆盖值就直接用（自动量失败、或想手填时）
	if (!ApartmentCenterOverride.IsNearlyZero() && !ApartmentExtentOverride.IsNearlyZero())
	{
		ApartmentBox = FBox::BuildAABB(ApartmentCenterOverride, ApartmentExtentOverride + FVector(ApartmentPaddingCm));
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 公寓范围（ini 覆盖）：%s"), *GetApartmentSummary());
		return;
	}

	// ② 自动量：把"房间构件"的包围盒并起来。
	//    只认墙/地板/天花板/窗台/勒脚这类**建筑构件**（前缀可配），避免把户外地形、树算进来。
	TArray<FString> Prefixes;
	ApartmentMeshPrefixes.ParseIntoArray(Prefixes, TEXT(","), true);
	for (FString& P : Prefixes)
	{
		P.TrimStartAndEndInline();
	}

	FBox Union(ForceInit);
	int32 Count = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (Actor == nullptr)
		{
			continue;
		}

		// 名字匹配（角色名或网格资产名任一命中即可）
		FString MeshName;
		if (const UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent())
		{
			if (const UStaticMesh* Mesh = SMC->GetStaticMesh())
			{
				MeshName = Mesh->GetName();
			}
		}
		const FString ActorName = Actor->GetName();

		bool bMatched = false;
		for (const FString& P : Prefixes)
		{
			if (!P.IsEmpty() && (MeshName.StartsWith(P) || ActorName.StartsWith(P)))
			{
				bMatched = true;
				break;
			}
		}
		if (!bMatched)
		{
			continue;
		}

		const FBox Box = Actor->GetComponentsBoundingBox(true);
		if (Box.IsValid != 0)
		{
			Union += Box;
			++Count;
		}
	}

	ApartmentMeshCount = Count;
	if (Union.IsValid != 0)
	{
		ApartmentBox = Union.ExpandBy(FVector(ApartmentPaddingCm));
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 公寓范围（自动量得）：%s"), *GetApartmentSummary());
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Wife] 公寓范围自动量失败（没找到名字以「%s」开头的房间构件）—— 她此时不受公寓盒约束"),
			*ApartmentMeshPrefixes);
	}
}

bool UMetamorphosisWifeSubsystem::IsInsideApartment(const FVector& Location) const
{
	if (!bLockToApartment || !ApartmentBox.IsValid)
	{
		return true;   // 没锁/没量到 -> 不限制
	}
	return ApartmentBox.IsInsideOrOn(Location);
}

FString UMetamorphosisWifeSubsystem::GetApartmentSummary() const
{
	if (!ApartmentBox.IsValid)
	{
		return FString::Printf(TEXT("未量到（锁定=%d 前缀=%s）"), bLockToApartment ? 1 : 0, *ApartmentMeshPrefixes);
	}
	const bool bOverride = !ApartmentCenterOverride.IsNearlyZero() && !ApartmentExtentOverride.IsNearlyZero();
	return FString::Printf(TEXT("中心=%s 半径=%s（来源=%s 构件=%d 外扩=%.0fcm 锁定=%d）"),
		*ApartmentBox.GetCenter().ToCompactString(), *ApartmentBox.GetExtent().ToCompactString(),
		bOverride ? TEXT("ini覆盖") : TEXT("自动量"), ApartmentMeshCount, ApartmentPaddingCm, bLockToApartment ? 1 : 0);
}

// ---------------------------------------------------------------- 追逐战（阶段3，2026-10-14）

float UMetamorphosisWifeSubsystem::GetPlayerMood() const
{
	if (const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		return Stats->GetMood();
	}
	return 0.f;
}

void UMetamorphosisWifeSubsystem::EnterChase(const TCHAR* Reason)
{
	if (State == EWifeState::Chase)
	{
		return;
	}
	State = EWifeState::Chase;
	StateTimer = 0.f;
	ChaseRepathTimer = 0.f;
	TimeSinceSeenPlayer = 0.f;
	PathPoints.Reset();
	PathIndex = 0;
	++ChaseCount;

	// 一进来就记住「主控此刻在哪」：追兵是「知道大概位置」的（原神式被锁）。
	//   否则没看见主控时目标会是 (0,0,0)，路径规划必然失败 -> 她原地站着不动（实测踩到过）。
	if (const APawn* ChasePlayer = FindPlayerPawn(GetWorld()))
	{
		LastKnownPlayerLocation = ChasePlayer->GetActorLocation();
		bHasLastKnown = true;
	}

	if (bLogChase || bLogWife)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Wife] ★ 进入追逐战（第 %d 次）：%s｜躁郁=%.1f 阶段=%d 追逐速度=%.0fcm/s 放弃=%.0fs 抓取半径=%.0fcm"),
			ChaseCount, Reason, GetPlayerMood(), GetPlayerStage(), ChaseSpeedCmPerSec, ChaseGiveUpSeconds, CatchRadiusCm);
	}
}

void UMetamorphosisWifeSubsystem::ExitChase(const TCHAR* Reason)
{
	if (State != EWifeState::Chase)
	{
		return;
	}
	State = EWifeState::Patrol;
	StateTimer = 0.f;
	MoveElapsed = 0.f;
	ChaseRepathTimer = 0.f;
	bHasLastKnown = false;
	bDebugForceChase = false;   // 任何一次退出都清掉调试强制
	bPersistentChase = false;   // 任何一次退出都清掉"持续追逐"（下次按进入线重新判定）
	PathPoints.Reset();
	PathIndex = 0;
	ChaseCooldownTimer = ChaseCooldownSeconds;

	if (bLogChase || bLogWife)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 退出追逐战：%s｜躁郁=%.1f 冷却=%.0fs -> 回到巡视"),
			Reason, GetPlayerMood(), ChaseCooldownSeconds);
	}
}

void UMetamorphosisWifeSubsystem::TickChase(float DeltaTime)
{
	AActor* Wife = WifeActor.Get();
	UWorld* World = GetWorld();
	APawn* Player = FindPlayerPawn(World);
	if (Wife == nullptr || World == nullptr || Player == nullptr)
	{
		ExitChase(TEXT("拿不到主控"));
		return;
	}

	// 抓到之后短暂停手（避免同一帧/连续帧反复触发）
	if (ChaseCatchCooldownTimer > 0.f)
	{
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector WifeLoc = Wife->GetActorLocation();
	const bool bCanSee = bSeeingPlayer;   // 由 UpdateSight 限频更新

	if (bCanSee)
	{
		LastKnownPlayerLocation = PlayerLoc;
		bHasLastKnown = true;
		TimeSinceSeenPlayer = 0.f;
	}
	else
	{
		TimeSinceSeenPlayer += DeltaTime;
	}

	// ① 抓到判定：够近就算被捉（不看视锥 —— 追兵"知道大概位置"，符合原神式被锁的感觉）
	//
	//    ⚠️ 必须用**水平距离**（Dist2D）+ 高度容差，不能用 3D 距离：
	//    她是 200cm 高的圆柱、角色位置在**胶囊中心**（约 +100cm），而虫子只有 16cm 高（中心 ~+10cm），
	//    两者高度差本来就有 ~90cm —— 用 3D 距离的话永远进不了 60cm 的抓取半径，
	//    实测她会在 90cm 处"贴脸却抓不到"（这个坑还被"用 Meta.Wife.Goto 把她摆到虫子身上"的验证方式掩盖过）。
	const float DistToPlayer2D = FVector::Dist2D(WifeLoc, PlayerLoc);
	const float HeightDiff = FMath::Abs(WifeLoc.Z - PlayerLoc.Z);
	if (DistToPlayer2D <= CatchRadiusCm && HeightDiff <= CatchHeightToleranceCm)
	{
		HandleCaught();
		return;
	}

	// ② 主控跑出公寓 -> 放弃（她守在公寓里，符合"锁在公寓"的设定）
	//    持续追逐（怀疑值满）时默认也不越界（bPersistentChaseIgnoreApartment 可开）
	if (bOnlyChaseInsideApartment && !(bPersistentChase && bPersistentChaseIgnoreApartment) && !IsInsideApartment(PlayerLoc))
	{
		ExitChase(TEXT("主控跑出了公寓范围"));
		return;
	}

	// ③ 追丢：连续看不见超过放弃时长
	//    ⚠ 持续追逐（怀疑值满）时【不放弃】—— 她知道你在哪，必须追到抓住你或怀疑值降下来
	if (!bPersistentChase && TimeSinceSeenPlayer > ChaseGiveUpSeconds)
	{
		ExitChase(TEXT("追丢（连续看不见超时）"));
		return;
	}

	// 持续追逐时目标始终是玩家当前位置（不看"最后已知位置"——她不会再被甩掉）
	const FVector Goal = bPersistentChase
		? PlayerLoc
		: (bCanSee ? PlayerLoc : (bHasLastKnown ? LastKnownPlayerLocation : PlayerLoc));

	// ④ 周期性重规划（看得见就追当前位置，看不见就奔最后已知位置）
	ChaseRepathTimer -= DeltaTime;
	if (ChaseRepathTimer <= 0.f || PathPoints.Num() == 0)
	{
		ChaseRepathTimer = ChaseRepathIntervalSeconds;
		if (IsInsideApartment(Goal))
		{
			TargetLocation = Goal;
			if (!BuildPathTo(Goal) && bLogChase)
			{
				UE_LOG(LogMetamorphosis, Verbose, TEXT("[Wife] 追逐：到 %s 的路径暂时规划不出来，下个周期再试"),
					*Goal.ToCompactString());
			}
		}
		else
		{
			ExitChase(TEXT("追赶目标落在公寓范围外"));
			return;
		}
	}

	// ⑤ 沿路径走（速度用 ChaseSpeedCmPerSec，不吃巡视那套阶段加成 —— 追逐速度本身就是设计值）
	while (PathIndex < PathPoints.Num() && FVector::Dist2D(PathPoints[PathIndex], WifeLoc) <= PathPointToleranceCm)
	{
		++PathIndex;
	}

	if (PathIndex < PathPoints.Num())
	{
		const FVector Aim = PathPoints[PathIndex];
		FVector Dir = Aim - WifeLoc;
		Dir.Z = 0.f;
		const float Dist = static_cast<float>(Dir.Size());
		if (Dist > KINDA_SMALL_NUMBER)
		{
			const FVector DirN = Dir / Dist;
			const float Step = ChaseSpeedCmPerSec * DeltaTime;
			// 同上：追上人身边就停止推进（抓取判定用 Dist2D，别用"继续挤"去凑距离）
			const APawn* NearPlayer = FindPlayerPawn(GetWorld());
			const bool bHoldNearPlayer = StopApproachDistanceCm > 0.f && NearPlayer != nullptr
				&& FVector::Dist2D(WifeLoc, NearPlayer->GetActorLocation()) < StopApproachDistanceCm;
			if (!bHoldNearPlayer)
			{
				if (!TryMoveWife(WifeLoc + DirN * FMath::Min(Step, Dist)))
				{
					// 追逐时也不许切进家具：原地停一帧，下个周期会重新规划路径
					PathPoints.Reset();
					PathIndex = 0;
					ChaseRepathTimer = 0.f;
					return;
				}
			}
			SnapToGround();
			const float NewYaw = FMath::FInterpTo(Wife->GetActorRotation().Yaw, DirN.Rotation().Yaw, DeltaTime, TurnRateDegPerSec / 90.f);
			Wife->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
		}
	}
	// 注：不在"到达最后已知位置"时收手 —— 那样追逐会结束得太快。
	//     收手条件保持三条：连续看不见超时 / 主控跑出公寓 / 躁郁值回落到退出线以下。
}

void UMetamorphosisWifeSubsystem::HandleCaught()
{
	UWorld* World = GetWorld();
	APawn* Player = FindPlayerPawn(World);
	const FVector WifeLoc = WifeActor.IsValid() ? WifeActor->GetActorLocation() : FVector::ZeroVector;
	const float Dist = Player != nullptr ? FVector::Dist2D(WifeLoc, Player->GetActorLocation()) : -1.f;

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Wife] ★★ 被捉：妻子在 %.0fcm 内（水平距离）抓到主控（第 %d 次追逐）—— 走**现有**的被捉/周目重置入口，不另造一套"),
		Dist, ChaseCount);

	UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this);
	if (Stats == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 拿不到 StatsSubsystem -> 这次被捉没有结算（只收手）"));
	}
	else
	{
		// ① 拉到"被捉线"：Stats 内部会打「怀疑值到达被捉线」告警 —— 保留这个既有入口语义
		Stats->SetSuspicion(Stats->GetMaxSuspicion());
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 已把怀疑值拉到被捉线（%.0f），Stats 会判定 IsCaught"), Stats->GetMaxSuspicion());

		// ② 交给周目结算系统：它会走「冻结 -> 反馈 -> 重置 -> 恢复」四阶段，
		//    并在重置阶段按配表 Global.CaughtResetsLoop 调 Stats->ResetForNewLoop()。
		//    这里**不再自己重置**，避免"两套周目流程"（玩家看不到反馈就是因为以前只有这一句日志）。
		if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
		{
			Loop->RequestLoopEnd(ELoopEndReason::Caught);
		}
		else
		{
			// 兜底：拿不到结算子系统时，退回原来的直接重置（至少数值是对的）
			bool bResetLoop = true;
			if (World != nullptr && World->GetGameInstance() != nullptr)
			{
				if (const UMetamorphosisConfigSubsystem* Cfg = World->GetGameInstance()->GetSubsystem<UMetamorphosisConfigSubsystem>())
				{
					bResetLoop = Cfg->GetGlobal().bCaughtResetsLoop;
				}
			}
			if (bResetLoop)
			{
				Stats->ResetForNewLoop();
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 拿不到周目结算子系统 -> 退回直接 Stats->ResetForNewLoop()"));
			}
		}
	}

	// 收手 + 冷却（避免刚抓完立刻又追）
	ExitChase(TEXT("抓到人之后收手"));
	ChaseCatchCooldownTimer = ChaseCooldownSeconds;
	ChaseCooldownTimer = ChaseCooldownSeconds;
}

void UMetamorphosisWifeSubsystem::NotifyLoopReset()
{
	const bool bWasChasing = (State == EWifeState::Chase);

	// 停追逐（ExitChase 对非 Chase 状态会直接 return，所以下面还要自己清一遍）
	ExitChase(TEXT("周目结算：收手"));

	// 清掉"调试强制追逐"，否则结算后她会立刻又追上来
	bDebugForceChase = false;

	// 回巡视待命：清目标与路径，并给一段追逐冷却（结算完不该马上又被追）
	State = EWifeState::Patrol;
	StateTimer = 0.f;
	MoveElapsed = 0.f;
	TargetLocation = HomeLocation;
	PathPoints.Reset();
	PathIndex = 0;
	ChaseRepathTimer = 0.f;
	TimeSinceSeenPlayer = 0.f;
	bHasLastKnown = false;
	ChaseCatchCooldownTimer = ChaseCooldownSeconds;
	ChaseCooldownTimer = ChaseCooldownSeconds;

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Wife] 收到周目结算通知（%s）：回巡视待命，追逐冷却 %.0fs"),
		bWasChasing ? TEXT("已停止追逐") : TEXT("本来就不在追逐"), ChaseCooldownSeconds);
}

void UMetamorphosisWifeSubsystem::DebugChase()
{
	bDebugForceChase = !bDebugForceChase;
	if (bDebugForceChase)
	{
		EnterChase(TEXT("调试命令强制进入（Meta.Wife.Chase；再敲一次取消）"));
	}
	else
	{
		ExitChase(TEXT("调试命令取消强制追逐（Meta.Wife.Chase）"));
	}
}

// ---------------------------------------------------------------- 控制台命令

namespace MetamorphosisWifeConsole
{
	static UMetamorphosisWifeSubsystem* Sub(UWorld* World)
	{
		return World != nullptr ? World->GetSubsystem<UMetamorphosisWifeSubsystem>() : nullptr;
	}

	static void Info(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisWifeSubsystem* Sys = Sub(World);
		if (Sys == nullptr || Sys->GetWifeActor() == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 没有妻子 Actor（或子系统未启动）"));
			return;
		}
		AActor* Wife = Sys->GetWifeActor();
		const TCHAR* StateText = Sys->GetState() == EWifeState::Patrol ? TEXT("巡视")
			: Sys->GetState() == EWifeState::Pause ? TEXT("停留")
			: Sys->GetState() == EWifeState::Chase ? TEXT("★追逐战") : TEXT("空闲");

		FString OverlapHit;
		const bool bOverlapping = Sys->IsOverlappingStaticGeometry(OverlapHit);

		const FVector WifeLoc = Wife->GetActorLocation();
		const APawn* Player = World != nullptr ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		const float DistToPlayer = Player != nullptr ? FVector::Dist(WifeLoc, Player->GetActorLocation()) : -1.f;

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 位置=%s 家=%s 状态=%s 目标=%s 视线范围=%.0fcm 速度=%.0fcm/s 主控阶段=%d 躁郁=%.1f(比例%.2f) 看见主控=%d"),
			*WifeLoc.ToCompactString(), *Sys->GetHomeLocation().ToCompactString(), StateText,
			*Sys->GetTargetLocation().ToCompactString(), Sys->GetEffectiveSightRangeCm(), Sys->GetEffectiveSpeedCmPerSec(),
			Sys->GetPlayerStage(), Sys->GetPlayerMood(), Sys->GetMoodRatio(), Sys->IsSeeingPlayer() ? 1 : 0);
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 速度构成：巡视=%.0fcm/s（含阶段加成） 追逐=%.0fcm/s（本状态实际用 %s）"),
			Sys->GetEffectiveSpeedCmPerSec(), Sys->ChaseSpeedCmPerSec,
			Sys->GetState() == EWifeState::Chase ? TEXT("追逐速度") : TEXT("巡视速度"));
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 公寓：%s | 她在盒内=%d 主控在盒内=%d | 追逐次数=%d 上次已知位置=%s 距主控=%.0fcm"),
			*Sys->GetApartmentSummary(), Sys->IsInsideApartment(WifeLoc) ? 1 : 0,
			Player != nullptr ? (Sys->IsInsideApartment(Player->GetActorLocation()) ? 1 : 0) : -1,
			Sys->GetChaseCount(), *Sys->GetLastKnownPlayerLocation().ToCompactString(), DistToPlayer);
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] Nav：%s | 路径点=%d 当前第=%d 正走向=%s | 半径=%.1fcm | **与静态几何体重叠=%d** %s"),
			*Sys->GetNavSummary(), Sys->GetPathPointCount(), Sys->GetPathIndex(),
			*Sys->GetCurrentPathPoint().ToCompactString(), Sys->GetWifeRadiusCm(), bOverlapping ? 1 : 0,
			bOverlapping ? *FString::Printf(TEXT("(命中 %s)"), *OverlapHit) : TEXT(""));
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 穿模自检累计：重叠次数=%d（验证必须为 0） 移动预检拦下=%d 次"),
			Sys->GetOverlapFrameCount(), Sys->GetMoveBlockedCount());
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 严格口径（视觉半径 %.1fcm / 不 shrink / 不按台阶忽略）：蹭进家具次数=%d（目标 0） %s"),
			Sys->GetWifeRadiusCm(), Sys->GetStrictPenetrationCount(), *Sys->GetStrictPenetrationSummary());

		// 现实尺寸 + 2D 精灵
		if (const AMetamorphosisWifeSprite* Sprite = Sys->GetWifeSprite())
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 2D 精灵：%s"), *Sprite->GetDebugString());
		}
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Wife] 现实尺寸：身高=%.1fcm 半径=%.1fcm 半高=%.1fcm 贴地偏移=%.1fcm 眼睛Z=%.1f（脚底=%.1f）"),
			Sys->GetWifeHeightCm(), Sys->GetWifeRadiusCm(), Sys->GetWifeHalfHeightCm(), Sys->GetWifeBottomOffsetCm(),
			Sys->GetEyeWorldZ(), WifeLoc.Z - Sys->GetWifeBottomOffsetCm());
	}

	/** Meta.Wife.Sprite：打印 2D 精灵状态（朝向/帧/镜像/画布高/底边 Z） */
	static void SpriteInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisWifeSubsystem* Sys = Sub(World);
		if (Sys == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[WifeSprite] 子系统未启动"));
			return;
		}
		const AMetamorphosisWifeSprite* Sprite = Sys->GetWifeSprite();
		if (Sprite == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[WifeSprite] 精灵还没生成（bWifeSpriteEnabled=%d）"),
				Sys->bWifeSpriteEnabled ? 1 : 0);
			return;
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[WifeSprite] %s"), *Sprite->GetDebugString());
		UE_LOG(LogMetamorphosis, Log, TEXT("[WifeSprite] 世界高度=%.1fcm（应=WifeHeightCm %.1f） 跟随目标=%s"),
			Sprite->GetSpriteHeightCm(), Sys->GetWifeHeightCm(),
			*GetNameSafe(Sprite->GetFollowTarget()));
	}

	static void Chase(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisWifeSubsystem* Sys = Sub(World)) { Sys->DebugChase(); }
	}

	static void Nav(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisWifeSubsystem* Sys = Sub(World)) { Sys->DebugPrintNav(); }
	}

	static void Pause(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisWifeSubsystem* Sys = Sub(World)) { Sys->DebugPause(); }
	}

	static void Resume(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisWifeSubsystem* Sys = Sub(World)) { Sys->DebugResume(); }
	}

	static void Goto(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisWifeSubsystem* Sys = Sub(World);
		if (Sys == nullptr) { return; }
		if (Args.Num() < 2)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Wife] 用法：Meta.Wife.Goto <x> <y>"));
			return;
		}
		Sys->DebugGoto(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
	}

	static void Glance(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisWifeSubsystem* Sys = Sub(World)) { Sys->DebugGlanceNow(); }
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeInfo(
		TEXT("Meta.Wife.Info"),
		TEXT("打印妻子巡视 AI 状态（位置/家/状态/目标/视线范围/速度/NavMesh/路径/是否与几何体重叠）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Info));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeNav(
		TEXT("Meta.Wife.Nav"),
		TEXT("打印 NavMesh 状态（是否就绪/生成方式/包围体/可达区域包围盒/是否穿模）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Nav));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifePause(
		TEXT("Meta.Wife.Pause"),
		TEXT("暂停妻子 AI（调试）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Pause));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeResume(
		TEXT("Meta.Wife.Resume"),
		TEXT("恢复妻子 AI（调试）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Resume));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeGoto(
		TEXT("Meta.Wife.Goto"),
		TEXT("把妻子传送到指定 XY（自动贴地）：Meta.Wife.Goto <x> <y>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Goto));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeGlance(
		TEXT("Meta.Wife.Glance"),
		TEXT("立即做一次视线判定（不受限频影响）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Glance));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeChase(
		TEXT("Meta.Wife.Chase"),
		TEXT("强制进入/取消追逐战（调试；等价于把主控躁郁值拉到阶段3）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Chase));

	static FAutoConsoleCommandWithWorldAndArgs GMetaWifeSprite(
		TEXT("Meta.Wife.Sprite"),
		TEXT("打印妻子的 2D 精灵状态（朝向/帧/镜像/画布高/底边 Z）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpriteInfo));
}






