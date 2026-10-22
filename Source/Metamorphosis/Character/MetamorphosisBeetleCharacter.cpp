// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Character/MetamorphosisBeetleCharacter.h"
#include "Character/MetamorphosisBeetleMovementComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "Metamorphosis.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Interaction/MetamorphosisInteractionComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 参考基准网格 Plane 是 100x100 cm */
	constexpr float BasicPlaneSizeCm = 100.f;
}

AMetamorphosisBeetleCharacter::AMetamorphosisBeetleCharacter(const FObjectInitializer& ObjectInitializer)
	// 用我们自己的移动组件：容忍"法线朝下"的地板（地编地图里有这种网格，否则一步都走不了）
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMetamorphosisBeetleMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	// ---- 尺寸：主角是「手掌大小的虫子」，全部按厘米级做 ----
	// 2026-10-09：体型整体放大一倍（精灵 8cm -> 16cm），胶囊与步高跟着调
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadiusCm, CapsuleHalfHeightCm);

	// 不随移动方向转身（精灵自己负责朝向）
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->MaxWalkSpeed = WalkSpeed;         // 虫子尺度：约 3~4 个身长/秒
	Move->MaxAcceleration = 400.f;
	Move->BrakingDecelerationWalking = 400.f;
	Move->JumpZVelocity = 0.f;              // 不做跳跃
	Move->AirControl = 0.05f;
	Move->MaxStepHeight = MaxStepHeightCm;  // 虫子爬不上家具（引擎默认 45cm 在这儿不合理）
	Move->SetWalkableFloorAngle(44.765f);   // 真正的值在 BeginPlay 里按配置覆盖（构造函数读不到 ini）
	Move->bUseFlatBaseForFloorChecks = true;

	// 小胶囊 + 长距离下落会穿透薄地形（地编新图踩到过）：开启连续碰撞检测
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetUseCCD(true);
	}

	// 物品栏（8 格快捷栏）：数据挂在角色身上，UI 在 PlayerController 上
	InventoryComponent = CreateDefaultSubobject<UMetamorphosisInventoryComponent>(TEXT("Inventory"));

	// 交互系统（走近 -> 提示 -> 拾取/就地吃）：扫描范围与间隔见 ini
	InteractionComponent = CreateDefaultSubobject<UMetamorphosisInteractionComponent>(TEXT("Interaction"));

	// 模板自带的骨骼网格用不到（2D 精灵替代），关掉免得报缺资源
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetVisibility(false);
		SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// ---- 2D 精灵：一个正对相机的 Plane ----
	SpriteQuad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpriteQuad"));
	SpriteQuad->SetupAttachment(GetCapsuleComponent());
	SpriteQuad->SetMobility(EComponentMobility::Movable);
	SpriteQuad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpriteQuad->SetGenerateOverlapEvents(false);
	SpriteQuad->SetCanEverAffectNavigation(false);
	SpriteQuad->SetCastShadow(true);
	SpriteQuad->bReceivesDecals = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		SpriteQuad->SetStaticMesh(PlaneMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteMatFinder(TEXT("/Game/Characters/Beetle/M_BeetleSprite.M_BeetleSprite"));
	if (SpriteMatFinder.Succeeded())
	{
		SpriteMaterial = SpriteMatFinder.Object;
		SpriteQuad->SetMaterial(0, SpriteMaterial);
	}

	// 三阶段贴图（由 Tools/make_beetle_assets.py 生成）
	static ConstructorHelpers::FObjectFinder<UTexture2D> Stage1Tex(TEXT("/Game/Characters/Beetle/Textures/T_Beetle_Stage1.T_Beetle_Stage1"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> Stage2Tex(TEXT("/Game/Characters/Beetle/Textures/T_Beetle_Stage2.T_Beetle_Stage2"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> Stage3Tex(TEXT("/Game/Characters/Beetle/Textures/T_Beetle_Stage3.T_Beetle_Stage3"));

	StageTextures.Add(Stage1Tex.Succeeded() ? Stage1Tex.Object : nullptr);
	StageTextures.Add(Stage2Tex.Succeeded() ? Stage2Tex.Object : nullptr);
	StageTextures.Add(Stage3Tex.Succeeded() ? Stage3Tex.Object : nullptr);

	// ---- 锁定视角相机 ----
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetUsingAbsoluteRotation(true);          // 角度锁死在世界上，玩家转不了视角
	CameraBoom->bDoCollisionTest = bCameraCollisionTest;
	CameraBoom->ProbeSize = 2.f;                         // 虫子尺度下探针也要小
	CameraBoom->bEnableCameraLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	ApplyCameraSettings();                               // 用初始机位初始化

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AMetamorphosisBeetleCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 精灵用动态材质实例，方便按阶段换贴图
	if (SpriteMaterial != nullptr)
	{
		SpriteMID = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
		SpriteQuad->SetMaterial(0, SpriteMID);
	}

	// 这些值来自 ini（config）：UE 是先跑构造函数再套用配置，所以只能在 BeginPlay 里应用
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// 道具给的永久速度加成（Stats 是 GameInstance 子系统，跨关卡保留）
		const UMetamorphosisStatsSubsystem* StatsSubsystem = UMetamorphosisStatsSubsystem::Get(this);
		AppliedSpeedMultiplier = StatsSubsystem != nullptr ? StatsSubsystem->GetSpeedMultiplier() : 1.f;

		MoveComp->MaxWalkSpeed = WalkSpeed * AppliedSpeedMultiplier;
		MoveComp->MaxFlySpeed = FlySpeed * AppliedSpeedMultiplier;
		MoveComp->MaxStepHeight = MaxStepHeightCm;
		MoveComp->SetWalkableFloorAngle(WalkableFloorAngleDeg);
		MoveComp->bUseFlatBaseForFloorChecks = bUseFlatBaseForFloorChecks;


		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Character] 移动参数：MaxWalkSpeed=%.1f（基础 %.1f × 速度倍率 %.2f） MaxFlySpeed=%.1f MaxStepHeight=%.1f 可走坡度=%.1f 度 可走Z阈值=%.3f"),
			MoveComp->MaxWalkSpeed, WalkSpeed, AppliedSpeedMultiplier, MoveComp->MaxFlySpeed, MoveComp->MaxStepHeight,
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(MoveComp->GetWalkableFloorZ(), -1.f, 1.f))),
			MoveComp->GetWalkableFloorZ());
	}

	SettleOnGroundAtSpawn();

	LoadSequenceFrames();

	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		Stats->OnBodyStageChanged.AddDynamic(this, &AMetamorphosisBeetleCharacter::HandleBodyStageChanged);
		ApplyMoodStage(Stats->GetBodyStage());
	}
	else
	{
		ApplyMoodStage(1);
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Character] 虫形主角就绪：精灵高 %.1f cm，机位 距离 %.0f cm / 俯角 %.0f / FOV %.0f"),
		GetSpriteHeightCm(), CameraBoom != nullptr ? CameraBoom->TargetArmLength : 0.f,
		CameraBoom != nullptr ? CameraBoom->GetRelativeRotation().Pitch : 0.f,
		Camera != nullptr ? Camera->FieldOfView : 0.f);

	// 精灵是否真的挂上了（排查「看不见人」用）
	if (SpriteQuad != nullptr)
	{
		const UTexture2D* CurrentTexture = CurrentFrameTexture != nullptr ? CurrentFrameTexture : (StageTextures.IsValidIndex(MoodStage - 1) ? StageTextures[MoodStage - 1] : nullptr);
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Character] 精灵检查：Mesh=%s 材质=%s MID=%s 贴图=%s 缩放=%s 可见=%d"),
			*GetNameSafe(SpriteQuad->GetStaticMesh()),
			*GetNameSafe(SpriteQuad->GetMaterial(0)),
			*GetNameSafe(SpriteMID),
			*GetNameSafe(CurrentTexture),
			*SpriteQuad->GetRelativeScale3D().ToString(),
			SpriteQuad->IsVisible() ? 1 : 0);
	}
}

void AMetamorphosisBeetleCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 道具给的永久速度加成：每帧脏检查，变了就同步到移动组件（含飞行速度）
	SyncSpeedBonus();

	// 先推进序列动画，再按当前帧算尺寸/朝向
	UpdateSpriteAnimation(DeltaSeconds);

	UpdateSpriteTransform();

	// 广角机位会跑到墙里/墙外：把挡在相机和注视点之间的构件藏起来（近墙剖切）
	UpdateCameraOccluders();

	// 升空：有升降输入时按输入飞；下降贴地就自动落地
	if (bAirborne)
	{
		if (!FMath::IsNearlyZero(VerticalInput))
		{
			AddMovementInput(FVector::UpVector, VerticalInput);
		}

		// 下降过程中贴到地面 → 直接回到行走（不用双击空格也行）
		if (VerticalInput < 0.f && GetCharacterMovement()->CurrentFloor.IsWalkableFloor())
		{
			bAirborne = false;
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Character] 落地（下降触地）"));
		}
	}

	// 首帧打印一次：确认精灵到底在哪儿、多大、相机离它多远
	if (!bLoggedSpriteDiag && Camera != nullptr && SpriteQuad != nullptr)
	{
		bLoggedSpriteDiag = true;
		const FBox Box = SpriteQuad->Bounds.GetBox();
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Character] 精灵诊断：中心=%s 包围盒 %s ~ %s（%.1f x %.1f x %.1f cm）相机=%s 距离=%.1f cm 相机朝向=%s"),
			*SpriteQuad->GetComponentLocation().ToCompactString(),
			*Box.Min.ToCompactString(), *Box.Max.ToCompactString(),
			Box.GetSize().X, Box.GetSize().Y, Box.GetSize().Z,
			*Camera->GetComponentLocation().ToCompactString(),
			FVector::Dist(Camera->GetComponentLocation(), SpriteQuad->GetComponentLocation()),
			*Camera->GetComponentRotation().ToCompactString());
	}
}

// ---------------------------------------------------------------------------
// 视角水平旋转
// ---------------------------------------------------------------------------

void AMetamorphosisBeetleCharacter::AddCameraYawInput(float AxisValue, float DeltaSeconds)
{
	if (FMath::IsNearlyZero(AxisValue))
	{
		return;
	}

	AddCameraYawDelta(AxisValue * CameraYawSpeed * DeltaSeconds);
}

void AMetamorphosisBeetleCharacter::AddCameraYawDelta(float DeltaDegrees)
{
	// 锁视角（Meta.Cam.Lock）：无头/自动化验证里有"幻影输入"会一直转视角，
	// 锁住之后同一个机位才可复现（否则每次相机位置都不一样，遮挡类问题没法对比）。
	if (bCameraYawLocked)
	{
		return;
	}

	if (FMath::IsNearlyZero(DeltaDegrees))
	{
		return;
	}

	CameraYawOffset = FRotator::NormalizeAxis(CameraYawOffset + DeltaDegrees);
	ApplyCameraSettings();
}

void AMetamorphosisBeetleCharacter::SetCameraYawOffset(float NewOffsetDegrees)
{
	CameraYawOffset = FRotator::NormalizeAxis(NewOffsetDegrees);
	ApplyCameraSettings();
}

// ---------------------------------------------------------------------------
// 出生朝向（用户可配）
// ---------------------------------------------------------------------------

float AMetamorphosisBeetleCharacter::GetSpawnYawDegrees() const
{
	return FRotator::NormalizeAxis(CameraYaw);
}

void AMetamorphosisBeetleCharacter::SetSpawnYawDegrees(float NewYawDegrees)
{
	CameraYaw = FRotator::NormalizeAxis(NewYawDegrees);
	// 改出生朝向时顺便把玩家转过的偏移清掉，否则"改了朝向但画面没变"
	CameraYawOffset = 0.f;
	ApplyCameraSettings();

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Character] 出生朝向设为 %.1f 度（相机世界 yaw 现在 %.1f 度）"),
		CameraYaw, GetCameraWorldYaw());
}

void AMetamorphosisBeetleCharacter::ResetCameraYawToSpawn()
{
	CameraYawOffset = 0.f;
	ApplyCameraSettings();

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Character] 视角复位到出生朝向：%.1f 度（相机世界 yaw %.1f 度）"),
		GetSpawnYawDegrees(), GetCameraWorldYaw());
}

void AMetamorphosisBeetleCharacter::SetCameraDistance(float NewDistanceCm)
{
	CameraDistance = FMath::Max(1.f, NewDistanceCm);
	ApplyCameraSettings();

	UE_LOG(LogMetamorphosis, Log, TEXT("[Camera] 机位距离 -> %.0f cm"), CameraDistance);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 镜头挡墙处理
//
// 规则：**只有镜头本身要跟物体重合时才隐藏** —— 在相机位置放一个球做重叠检测，
// 重叠到谁就临时把谁藏起来（只关渲染开关，不动 Visibility / 碰撞），离开后延迟恢复。
//
// 为什么不用"视线被挡就隐藏"：那个判定要覆盖"相机→角色"整条线段，
// 角色一旦贴近物体（站到桌边、贴墙），物体本身就被判成遮挡而消失（2026-10-10 反馈的问题）。
// 规则 2 负责"挡在镜头和角色之间的东西"：从相机往角色轮廓上的几个点各打一条精确射线，\r\n// 打到谁藏谁 —— 只有真的挡住角色的东西会消失，角色旁边/身后的物体不受影响。
// ---------------------------------------------------------------------------

void AMetamorphosisBeetleCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreHiddenOccluders();
	Super::EndPlay(EndPlayReason);
}

void AMetamorphosisBeetleCharacter::RestoreHiddenOccluders()
{
	for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FHiddenOccluderState>& Pair : HiddenOccluders)
	{
		if (UPrimitiveComponent* Component = Pair.Key.Get())
		{
			// 按"隐藏之前"的状态还原（这些开关可能被别处改过，所以记录原值）
			Component->SetRenderInMainPass(Pair.Value.bWasRenderInMainPass);
			Component->SetRenderInDepthPass(Pair.Value.bWasRenderInDepthPass);
			Component->SetCastShadow(Pair.Value.bWasCastShadow);
		}
	}

	HiddenOccluders.Reset();
}

bool AMetamorphosisBeetleCharacter::ShouldSkipCutaway(UPrimitiveComponent* Component, FString* OutReason)
{
	auto SetReason = [OutReason](const TCHAR* Text)
	{
		if (OutReason != nullptr)
		{
			*OutReason = Text;
		}
	};

	if (!IsValid(Component))
	{
		SetReason(TEXT("组件无效"));
		return true;
	}

	AActor* OwnerActor = Component->GetOwner();
	if (OwnerActor == nullptr)
	{
		SetReason(TEXT("没有 Owner"));
		return true;
	}
	if (OwnerActor == this)
	{
		SetReason(TEXT("是主角自己"));
		return true;
	}

	// 玩家自己（Pawn）以及它身上挂着的东西一律不剖
	if (OwnerActor->IsA(APawn::StaticClass()))
	{
		SetReason(TEXT("是 Pawn（玩家/角色）"));
		return true;
	}

	// 只剖静态几何体（默认）：妻子等角色在运行时是 Movable，天然被排除
	if (bOnlyCutStaticGeometry && Component->Mobility != EComponentMobility::Static)
	{
		SetReason(TEXT("非 Static（只剖静态=开）"));
		return true;
	}

	// 显式排除标签（默认 Meta.Wife / Meta.Item）：角色与可交互道具不参与剖切
	if (!bParsedExcludeTags)
	{
		bParsedExcludeTags = true;
		ParsedExcludeTags.Reset();
		TArray<FString> Parts;
		CutawayExcludeTags.ParseIntoArray(Parts, TEXT(","), true);
		for (const FString& Part : Parts)
		{
			const FString Trimmed = Part.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				ParsedExcludeTags.Add(FName(*Trimmed));
			}
		}
	}
	for (const FName& Tag : ParsedExcludeTags)
	{
		// 精确匹配 + **前缀匹配**：场景里的标签是 `Meta.Item:Cereal` 这种带参数的写法，
		// 而配置里写的是 `Meta.Item` —— 只做 ActorHasTag 的精确比较会漏掉它们（实测漏过 Test2）。
		if (OwnerActor->ActorHasTag(Tag))
		{
			if (OutReason != nullptr)
			{
				*OutReason = FString::Printf(TEXT("带排除标签 %s"), *Tag.ToString());
			}
			return true;
		}

		const FString TagString = Tag.ToString();
		for (const FName& OwnerTag : OwnerActor->Tags)
		{
			if (OwnerTag.ToString().StartsWith(TagString, ESearchCase::CaseSensitive))
			{
				if (OutReason != nullptr)
				{
					*OutReason = FString::Printf(TEXT("带排除标签 %s（前缀匹配）"), *OwnerTag.ToString());
				}
				return true;
			}
		}
	}

	return false;
}

void AMetamorphosisBeetleCharacter::SetCameraYawLocked(bool bLocked, float LockedYawDegrees)
{
	bCameraYawLocked = bLocked;
	if (bLocked)
	{
		SetCameraYawOffset(LockedYawDegrees);
	}
	UE_LOG(LogMetamorphosis, Log, TEXT("[Camera] 视角锁定：%s%s"),
		bLocked ? TEXT("开（忽略转视角输入，机位可复现）") : TEXT("关"),
		bLocked ? *FString::Printf(TEXT("，锁定偏航 %.1f 度"), CameraYawOffset) : TEXT(""));
}

void AMetamorphosisBeetleCharacter::RefreshCameraBoundsCandidates()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	CameraBoundsCandidates.Reset();
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!IsValid(Component) || Component->GetWorld() != World || !Component->IsRegistered())
		{
			continue;
		}
		if (!Component->IsVisible() || Component->Bounds.SphereRadius <= 0.f)
		{
			continue;
		}
		// 玩家自己身上的一律不进候选（SpriteQuad/胶囊等）
		if (Component->GetOwner() == this)
		{
			continue;
		}
		// 只认网格：粒子系统/导航网格渲染件/调试线框这些"可视化辅助件"的包围盒往往巨大，
		// 藏掉它们没有意义（实测这几类都在候选里出现过，靠尺寸闸门挡掉太脆弱）。
		if (!Component->IsA<UMeshComponent>())
		{
			continue;
		}
		// 排除地形（Landscape）：它既不是"包住相机的物体"，又是最危险的误判源 ——
		// 相机压低（Meta.Cam.Distance 调小）时会落进某个地形分块的包围盒里，
		// 一旦藏掉就是地上破一个 10m 的洞。用类名字符串判断，避免给本模块引入 Landscape 依赖。
		if (Component->GetClass()->GetName().Contains(TEXT("Landscape")))
		{
			continue;
		}
		CameraBoundsCandidates.Add(Component);
	}

	// 只在候选数量变化时打一条：这个函数每个 CameraBoundsCacheSeconds 都会重跑，
	// 每帧刷屏会把真正有用的日志淹掉（曾经 294 个构件 × 每帧一条）。
	if (LastLoggedCameraBoundsCount != CameraBoundsCandidates.Num())
	{
		LastLoggedCameraBoundsCount = CameraBoundsCandidates.Num();
		UE_LOG(LogMetamorphosis, Log, TEXT("[Camera] 包围盒候选清单刷新：%d 个可见网格构件"), CameraBoundsCandidates.Num());
	}
}

bool AMetamorphosisBeetleCharacter::IsCameraInsideComponentBounds(
	const UPrimitiveComponent* Component, const FVector& Point) const
{
	if (!IsValid(Component))
	{
		return false;
	}

	const FVector Origin = Component->Bounds.Origin;
	const FVector Extent = Component->Bounds.BoxExtent + FVector(FMath::Max(0.f, CameraBoundsInflateCm));
	if (FMath::Abs(Point.X - Origin.X) > Extent.X
		|| FMath::Abs(Point.Y - Origin.Y) > Extent.Y
		|| FMath::Abs(Point.Z - Origin.Z) > Extent.Z)
	{
		return false;
	}

	// 尺寸闸门：太小（蹭过的小道具）不值得藏；太大（天空球/远景壳）永远包着相机，藏了会天变黑
	const float MaxSize = FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 2.f;
	if (MaxSize < CameraBoundsMinSizeCm)
	{
		return false;
	}
	if (CameraBoundsMaxSizeCm > 0.f && MaxSize > CameraBoundsMaxSizeCm)
	{
		return false;
	}

	return true;
}


void AMetamorphosisBeetleCharacter::MarkCameraOccluder(UPrimitiveComponent* Component, float Now, const TCHAR* Reason, int32 BlockedSamples)
{
	if (!IsValid(Component) || Component->GetOwner() == this)
	{
		return;
	}

	// 排除角色/道具（妻子、测试道具、玩家自己）：剖掉角色会很怪，也会让人误以为模型出问题。
	// 三条规则（镜头重合 / 相机被包住 / 挡住角色）都走这里，口径一致。
	if (ShouldSkipCutaway(Component))
	{
		return;
	}

	if (BlockerComponentTag != NAME_None && !Component->ComponentHasTag(BlockerComponentTag))
	{
		return;
	}

	if (FHiddenOccluderState* Existing = HiddenOccluders.Find(Component))
	{
		Existing->LastBlockedTime = Now;
		return;
	}

	// 进入迟滞：要求**连续**遮挡 OccluderHideDelay 秒才真隐藏。
	// 虫子贴着家具被"挤出/推回"的那一两帧也会命中，单帧就隐藏会让家具突然消失一闪
	// （2026-10-14 实测：贴住床头柜时整件床头柜被隐藏）。
	if (OccluderHideDelay > 0.f)
	{
		FPendingOccluder& Pending = PendingOccluders.FindOrAdd(Component);
		if (Pending.LastSeenTime <= 0.f)
		{
			Pending.FirstBlockedTime = Now;
		}
		Pending.LastSeenTime = Now;

		if ((Now - Pending.FirstBlockedTime) < OccluderHideDelay)
		{
			return;   // 还没连续够久，先不下手
		}
		PendingOccluders.Remove(Component);
	}

	FHiddenOccluderState State;
	State.bWasRenderInMainPass = Component->bRenderInMainPass;
	State.bWasRenderInDepthPass = Component->bRenderInDepthPass;
	State.bWasCastShadow = Component->CastShadow;
	State.LastBlockedTime = Now;
	HiddenOccluders.Add(Component, State);

	// 只关渲染：物体不画出来，但碰撞与射线照旧（虫子不会穿墙、游戏逻辑不受影响）
	Component->SetRenderInMainPass(false);
	Component->SetRenderInDepthPass(false);
	Component->SetCastShadow(false);

	// 打 Log 而不是 Verbose：这条日志是排查"某个物件是不是被这套系统藏了、被哪条规则藏的"的唯一依据
	UE_LOG(LogMetamorphosis, Log, TEXT("[Camera] 隐藏构件：%s.%s（规则=%s%s，离相机 %.0f cm）"),
		*GetNameSafe(Component->GetOwner()), *Component->GetName(), Reason,
		BlockedSamples >= 0 ? *FString::Printf(TEXT("，遮挡采样 %d 条"), BlockedSamples) : TEXT(""),
		Camera != nullptr ? FVector::Dist(Camera->GetComponentLocation(), Component->Bounds.Origin) : -1.f);
}

void AMetamorphosisBeetleCharacter::LogCameraInfo() const
{
	UWorld* World = GetWorld();
	const FVector CameraLocation = Camera != nullptr ? Camera->GetComponentLocation() : FVector::ZeroVector;
	const FVector CharacterLocation = GetActorLocation();

	// 相机是否在几何体内：与判定同口径（按物体类型查询，忽略角色自己）
	bool bInsideGeometry = false;
	if (World != nullptr && Camera != nullptr && CameraInsideProbeRadius > 0.f)
	{
		const FCollisionObjectQueryParams ObjectParams(FCollisionObjectQueryParams::AllObjects);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MetamorphosisCameraInfo), false);
		Params.AddIgnoredActor(this);

		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(
			Overlaps, CameraLocation, FQuat::Identity, ObjectParams,
			FCollisionShape::MakeSphere(FMath::Max(1.f, CameraInsideProbeRadius)), Params);
		bInsideGeometry = Overlaps.Num() > 0;
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("===== Meta.Cam.Info ====="));
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Cam.Info] 相机=%s 角色=%s 距离=%.0f cm；角色可视高度=%.1f cm 判定半径=%.1f cm（随躁郁值体型阶段变化）"),
		*CameraLocation.ToCompactString(), *CharacterLocation.ToCompactString(),
		FVector::Dist(CameraLocation, CharacterLocation), GetSpriteHeightCm(), GetSilhouetteRadiusCm());
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Cam.Info] 相机在几何体内=%d（探测半径 %.1f cm）；被挡方向=%d/%d=%.2f（阈值 %.2f）判定被包住=%d（探测距离 %.0f cm）"),
		bInsideGeometry ? 1 : 0, CameraInsideProbeRadius, LastEnclosedBlockedDirs, LastEnclosedTotalDirs,
		LastEnclosedBlockedRatio, CameraEnclosedBlockedRatio, bLastCameraEnclosed ? 1 : 0, CameraEnclosedProbeDistance);
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Cam.Info] 规则开关：镜头重合=%d（半径 %.1f）/ 相机被包住=%d / 挡住角色=%d；只剖静态=%d；排除标签='%s'；隐藏清单 %d 个"),
		bHideCameraBlockers ? 1 : 0, CameraClearanceRadius,
		(CameraInsideProbeRadius > 0.f || CameraEnclosedProbeDistance > 0.f) ? 1 : 0,
		bHideCharacterBlockers ? 1 : 0, bOnlyCutStaticGeometry ? 1 : 0, *CutawayExcludeTags,
		HiddenOccluders.Num());

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FHiddenOccluderState>& Pair : HiddenOccluders)
	{
		UPrimitiveComponent* Component = Pair.Key.Get();
		UE_LOG(LogMetamorphosis, Log, TEXT("[Cam.Info]     藏：%s.%s（%s，离相机 %.0f cm）"),
			*GetNameSafe(Component != nullptr ? Component->GetOwner() : nullptr), *GetNameSafe(Component),
			(Component != nullptr && Component->Mobility == EComponentMobility::Static) ? TEXT("Static") : TEXT("Movable"),
			(Component != nullptr && Camera != nullptr) ? FVector::Dist(CameraLocation, Component->Bounds.Origin) : -1.f);
	}
}

float AMetamorphosisBeetleCharacter::GetSilhouetteRadiusCm() const
{
	// 用**可视高度**而不是精灵四边形外包尺寸：
	// 四边形是 384 画布（内容只占约 57%），按外包算半径会大近一倍 —— 那正是 2026-10-14
	// "站在家具旁边、家具却被整件隐藏"的原因。可视高度本身已随体型阶段放大（阶段3 = 4x）。
	const float VisibleHeight = GetSpriteHeightCm();
	return FMath::Max(1.f, VisibleHeight * CharacterSilhouetteRadiusScale);
}

void AMetamorphosisBeetleCharacter::BuildSilhouetteSamples(const FVector& CameraLocation,
	TArray<FVector>& OutSamples, FVector& OutCenter, float& OutRadius) const
{
	OutSamples.Reset();

	// 轮廓中心：优先用精灵组件自己的世界位置（比缓存的 Bounds 可靠；Bounds 是渲染线程数据，
	// 体型刚变过的那一两帧可能还是旧值）。
	OutCenter = SpriteQuad != nullptr ? SpriteQuad->GetComponentLocation() : GetActorLocation();

	OutRadius = GetSilhouetteRadiusCm();

	// 环半径刻意小于判定半径（默认 0.8）：贴着身体外侧的家具不会被误判成遮挡，
	// 而"切进身体"的墙仍然会被环上的点打到。
	const float RingRadius = OutRadius * FMath::Clamp(CharacterSilhouetteSpread, 0.f, 1.f);

	// 采样平面垂直于"相机 → 角色"的视线；用相机的右/上向量取环，正好贴合精灵面片朝向。
	FVector Right = FVector::RightVector;
	FVector Up = FVector::UpVector;
	if (Camera != nullptr)
	{
		Right = Camera->GetRightVector();
		Up = Camera->GetUpVector();
	}

	OutSamples.Add(OutCenter);

	const int32 RingCount = FMath::Clamp(SilhouetteRingSamples, 0, 32);
	for (int32 Index = 0; Index < RingCount; ++Index)
	{
		const float Angle = 2.f * PI * static_cast<float>(Index) / static_cast<float>(RingCount);
		OutSamples.Add(OutCenter + Right * (FMath::Cos(Angle) * RingRadius) + Up * (FMath::Sin(Angle) * RingRadius));
	}
}

void AMetamorphosisBeetleCharacter::DebugCameraBoundsAt(const FVector& Point)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	struct FEntry
	{
		UPrimitiveComponent* Component = nullptr;
		float MaxSize = 0.f;
		float Distance = 0.f;
	};
	TArray<FEntry> Inside;
	int32 Scanned = 0;

	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!IsValid(Component) || Component->GetWorld() != World || !Component->IsRegistered())
		{
			continue;
		}
		if (!Component->IsVisible() || Component->Bounds.SphereRadius <= 0.f)
		{
			continue;
		}
		++Scanned;

		const FVector Origin = Component->Bounds.Origin;
		const FVector Extent = Component->Bounds.BoxExtent;
		if (FMath::Abs(Point.X - Origin.X) > Extent.X
			|| FMath::Abs(Point.Y - Origin.Y) > Extent.Y
			|| FMath::Abs(Point.Z - Origin.Z) > Extent.Z)
		{
			continue;
		}

		FEntry Entry;
		Entry.Component = Component;
		Entry.MaxSize = FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 2.f;
		Entry.Distance = FVector::Dist(Point, Origin);
		Inside.Add(Entry);
	}

	// 尺寸从小到大：最可能是"包住相机的管子/道具内部"，排前面最好看
	Inside.Sort([](const FEntry& A, const FEntry& B) { return A.MaxSize < B.MaxSize; });

	UE_LOG(LogMetamorphosis, Log, TEXT("===== Meta.Cam.Bounds @ %s ====="), *Point.ToCompactString());
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Cam.Bounds] 扫描可见构件 %d 个；**世界包围盒包含该点**的 %d 个（尺寸下限 %.0f / 上限 %.0f cm，外扩 %.0f）"),
		Scanned, Inside.Num(), CameraBoundsMinSizeCm, CameraBoundsMaxSizeCm, CameraBoundsInflateCm);

	for (const FEntry& Entry : Inside)
	{
		UPrimitiveComponent* Component = Entry.Component;
		const FVector Extent = Component->Bounds.BoxExtent;

		FString SkipReason;
		const bool bSkip = ShouldSkipCutaway(Component, &SkipReason);

		// 与运行时规则完全同一套判定（IsCameraInsideComponentBounds），所以"会不会藏"就是真结论
		const bool bWouldHideByBounds = bHideCameraBoundsBlockers && IsCameraInsideComponentBounds(Component, Point);
		const bool bWouldHideButExcluded = bWouldHideByBounds && bSkip;

		FString Verdict;
		if (bWouldHideButExcluded)
		{
			Verdict = FString::Printf(TEXT("包围盒规则命中**但被排除**（%s）"), *SkipReason);
		}
		else if (bWouldHideByBounds)
		{
			Verdict = TEXT("包围盒规则会隐藏它");
		}
		else
		{
			Verdict = TEXT("包围盒规则不管它（尺寸闸门/未开启）");
		}

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Cam.Bounds]   %s.%s 类=%s 网格=%s Mobility=%s 碰撞=%d 已隐藏=%d 尺寸=(%.0f,%.0f,%.0f) 最大边=%.0f 离点=%.0f -> %s"),
			*GetNameSafe(Component->GetOwner()), *Component->GetName(), *Component->GetClass()->GetName(),
			*GetNameSafe(Cast<UStaticMeshComponent>(Component) != nullptr
				? Cast<UStaticMeshComponent>(Component)->GetStaticMesh() : nullptr),
			Component->Mobility == EComponentMobility::Static ? TEXT("Static") : TEXT("Movable"),
			static_cast<int32>(Component->GetCollisionEnabled()),
			HiddenOccluders.Contains(Component) ? 1 : 0,
			Extent.X * 2.f, Extent.Y * 2.f, Extent.Z * 2.f, Entry.MaxSize, Entry.Distance, *Verdict);
	}

	if (Inside.Num() == 0)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Cam.Bounds]   该点不在任何可见构件的包围盒内 —— 说明挡住画面的东西不在这里（可能是贴图/贴花/特效），")
			TEXT("或者相机点给错了"));
	}
}

void AMetamorphosisBeetleCharacter::DebugCameraProbe()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Camera == nullptr || CameraBoom == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Camera] 探针失败：相机/世界为空"));
		return;
	}

	const FVector Start = Camera->GetComponentLocation();
	const FVector End = CameraBoom->GetComponentLocation();

	const FCollisionObjectQueryParams ObjectParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetamorphosisCameraProbe), false);
	Params.AddIgnoredActor(this);

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Camera] 探针：相机=%s 注视点=%s 距离=%.0f cm 镜头重合半径=%.0f"),
		*Start.ToCompactString(), *End.ToCompactString(), FVector::Dist(Start, End), CameraClearanceRadius);

	// 1) 正式规则：镜头位置的重合检测
	{
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Start, FQuat::Identity, ObjectParams,
			FCollisionShape::MakeSphere(FMath::Max(1.f, CameraClearanceRadius)), Params);

		UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   镜头重合检测：%d 个"), Overlaps.Num());
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]     - %s（%s）"),
				*GetNameSafe(Overlap.GetComponent()), *GetNameSafe(Overlap.GetActor()));
		}
	}

	// 2) 正式规则：往角色轮廓的采样点打射线，看谁挡住了角色
	{
		TArray<FVector> Samples;
		FVector SilhouetteCenter = FVector::ZeroVector;
		float SilhouetteRadius = 0.f;
		BuildSilhouetteSamples(Start, Samples, SilhouetteCenter, SilhouetteRadius);

		const float RingRadius = SilhouetteRadius * FMath::Clamp(CharacterSilhouetteSpread, 0.f, 1.f);
		// 用角色自己缓存的阶段（不是局部变量，避免遮蔽类成员 MoodStage）
		const int32 CurrentStage = MoodStage;

		// 排查"该隐藏没隐藏/不该隐藏却隐藏"最关键的三组数据：
		// 角色可视尺寸（随体型阶段变化）、判定半径、以及每个采样点各自的命中。
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Camera]   角色=%s  精灵=%s  轮廓中心=%s  可视高度=%.1f cm  体型阶段=%d  判定半径=%.1f cm（环半径 %.1f cm，采样 %d 点，阈值 %d）"),
			*GetActorLocation().ToCompactString(),
			*GetNameSafe(SpriteQuad),
			*SilhouetteCenter.ToCompactString(),
			GetSpriteHeightCm(), CurrentStage, SilhouetteRadius, RingRadius,
			Samples.Num(), FMath::Max(1, OccluderBlockedSampleThreshold));

		UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   角色轮廓采样：%d 条（该规则当前%s）"),
			Samples.Num(), bHideCharacterBlockers ? TEXT("开启") : TEXT("关闭"));

		TMap<UPrimitiveComponent*, int32> BlockedCounts;
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			TArray<FHitResult> Hits;
			World->LineTraceMultiByObjectType(Hits, Start, Samples[Index], ObjectParams, Params);

			UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]     采样%d 坐标=%s（离相机 %.0f cm）"),
				Index, *Samples[Index].ToCompactString(), FVector::Dist(Start, Samples[Index]));

			// 逐条命中都打出来（含命中点与到相机的距离），用来判断"谁真的挡住了角色"。
			// 每条线每个构件只统计一次：同一条线可能命中同一构件的多个凸体/三角面，
			// 不去重的话"挡了几条采样"会超过采样总数（实测见过 18/9），结论就不可信了。
			TSet<UPrimitiveComponent*> HitOnThisLine;
			for (int32 HitIndex = 0; HitIndex < Hits.Num(); ++HitIndex)
			{
				const FHitResult& Hit = Hits[HitIndex];
				UPrimitiveComponent* HitComp = Hit.GetComponent();
				// Mobility 与"会不会被排除"必须跟着命中一起打：只看到"命中了墙"却不知道
				// 它是不是被"只剖静态/排除标签"过滤掉的，就没法判断"该隐藏没隐藏"的根因。
				FString HitSkipReason;
				const bool bHitSkipped = ShouldSkipCutaway(HitComp, &HitSkipReason);
				UE_LOG(LogMetamorphosis, Log,
					TEXT("[Camera]       命中%d：%s（%s）命中点=%s 离相机 %.0f cm | Mobility=%s 碰撞=%d 会被排除=%d%s"),
					HitIndex,
					*GetNameSafe(HitComp),
					*GetNameSafe(HitComp != nullptr ? HitComp->GetOwner() : nullptr),
					*Hit.ImpactPoint.ToCompactString(),
					FVector::Dist(Start, Hit.ImpactPoint),
					(HitComp != nullptr && HitComp->Mobility == EComponentMobility::Static) ? TEXT("Static") : TEXT("Movable"),
					HitComp != nullptr ? static_cast<int32>(HitComp->GetCollisionEnabled()) : -1,
					bHitSkipped ? 1 : 0,
					bHitSkipped ? *FString::Printf(TEXT("（%s）"), *HitSkipReason) : TEXT(""));
				UPrimitiveComponent* HitComponent = HitComp;
				if (HitComponent != nullptr && HitComponent->GetOwner() != this && !HitOnThisLine.Contains(HitComponent))
				{
					HitOnThisLine.Add(HitComponent);
					BlockedCounts.FindOrAdd(HitComponent)++;
				}
			}
			if (Hits.Num() == 0)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]       未命中"));
			}
		}

		if (BlockedCounts.Num() == 0)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   结论：没有任何构件挡住角色（不会隐藏任何东西）"));
		}
		// 采样0 就是轮廓中心：**中心未命中、而环上命中** 正是 2026-10-16 修掉的
		// "该隐藏没隐藏"（旧版只认中心这一条线）。看上面"采样0"的命中列表即可判断。
		UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   提示：采样0 = 轮廓中心（旧版只认这一条）；中心未命中而环上命中 = 旧版会漏"));
		for (const TPair<UPrimitiveComponent*, int32>& Pair : BlockedCounts)
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   结论：%s（%s）挡了 %d/%d 条采样（%s 隐藏）"),
				*GetNameSafe(Pair.Key),
				*GetNameSafe(Pair.Key != nullptr ? Pair.Key->GetOwner() : nullptr),
				Pair.Value, Samples.Num(),
				Pair.Value >= FMath::Max(1, OccluderBlockedSampleThreshold) ? TEXT("会") : TEXT("不会"));
		}
	}

	// 3) 通道版：只用来对比关卡的碰撞通道设置，与正式判定无关
	{
		TArray<FHitResult> ChannelHits;
		World->SweepMultiByChannel(ChannelHits, Start, End, FQuat::Identity, BlockerTraceChannel,
			FCollisionShape::MakeSphere(1.f), Params);

		UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   碰撞通道 %s 查询：命中 %d 个"),
			*UEnum::GetValueAsString(BlockerTraceChannel.GetValue()), ChannelHits.Num());
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Camera]   当前已隐藏 %d 个构件"), HiddenOccluders.Num());
}

void AMetamorphosisBeetleCharacter::UpdateCameraOccluders()
{
	if (Camera == nullptr || CameraBoom == nullptr)
	{
		return;
	}

	if (!bHideCameraBlockers)
	{
		RestoreHiddenOccluders();
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const FVector CameraLocation = Camera->GetComponentLocation();

	// 按"物体类型"查询，不看碰撞通道响应：关卡里只要给了碰撞就能被查到
	const FCollisionObjectQueryParams ObjectParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetamorphosisCameraOccluder), false);
	Params.AddIgnoredActor(this);

	// (1) 镜头本身"要跟物体重合"：相机位置的重叠检测
	if (CameraClearanceRadius > 0.f)
	{
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(
			Overlaps, CameraLocation, FQuat::Identity, ObjectParams,
			FCollisionShape::MakeSphere(CameraClearanceRadius), Params);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			MarkCameraOccluder(Overlap.GetComponent(), Now, TEXT("镜头重合"));
		}
	}

	// (1b) 相机被"空心物体"包住（2026-10-16 用户报：相机钻进大管子内部，画面被管壁糊住却没被剖开）。
	//
	//      为什么规则1/2 会同时失效：
	//      * 规则2 的射线从**相机自身**出发，相机在几何体内部时是"起点即穿透"，拿不到有效命中；
	//      * 规则1 的球（默认 30cm）可能**小于管内径**（内腔 > 60cm），碰不到管壁。
	//      所以这里补两条：①相机处小半径球（直接碰到管壁）②相机周围多方向短距射线，
	//      被挡方向比例够高就判定"相机被包住"，把近距命中物藏掉。
	//      短距离(120cm)+高比例(0.75) 是为了避免"相机站在房间/走廊里"被误判（那里只有少数方向被挡）。
	if (CameraInsideProbeRadius > 0.f)
	{
		TArray<FOverlapResult> InsideOverlaps;
		World->OverlapMultiByObjectType(
			InsideOverlaps, CameraLocation, FQuat::Identity, ObjectParams,
			FCollisionShape::MakeSphere(FMath::Max(1.f, CameraInsideProbeRadius)), Params);

		for (const FOverlapResult& Inside : InsideOverlaps)
		{
			MarkCameraOccluder(Inside.GetComponent(), Now, TEXT("相机在几何体内"));
		}
	}

	if (CameraEnclosedProbeDistance > 0.f)
	{
		static const FVector EnclosedDirs[] = {
			FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
			FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f),
			FVector(0.f, 0.f, 1.f), FVector(0.f, 0.f, -1.f),
			FVector(1.f, 1.f, 1.f).GetSafeNormal(),   FVector(1.f, 1.f, -1.f).GetSafeNormal(),
			FVector(1.f, -1.f, 1.f).GetSafeNormal(),  FVector(1.f, -1.f, -1.f).GetSafeNormal(),
			FVector(-1.f, 1.f, 1.f).GetSafeNormal(),  FVector(-1.f, 1.f, -1.f).GetSafeNormal(),
			FVector(-1.f, -1.f, 1.f).GetSafeNormal(), FVector(-1.f, -1.f, -1.f).GetSafeNormal()
		};

		const int32 TotalDirs = UE_ARRAY_COUNT(EnclosedDirs);
		int32 BlockedDirs = 0;
		TMap<UPrimitiveComponent*, int32> NearHits;   // 近距命中物 -> 被挡方向数

		for (const FVector& Dir : EnclosedDirs)
		{
			TArray<FHitResult> DirHits;
			World->LineTraceMultiByObjectType(
				DirHits, CameraLocation, CameraLocation + Dir * CameraEnclosedProbeDistance, ObjectParams, Params);

			TSet<UPrimitiveComponent*> HitOnThisDir;
			for (const FHitResult& Hit : DirHits)
			{
				UPrimitiveComponent* HitComponent = Hit.GetComponent();
				if (HitComponent != nullptr && !HitOnThisDir.Contains(HitComponent))
				{
					HitOnThisDir.Add(HitComponent);
					NearHits.FindOrAdd(HitComponent)++;
				}
			}
			if (HitOnThisDir.Num() > 0)
			{
				++BlockedDirs;
			}
		}

		LastEnclosedBlockedDirs = BlockedDirs;
		LastEnclosedTotalDirs = TotalDirs;
		LastEnclosedBlockedRatio = TotalDirs > 0 ? static_cast<float>(BlockedDirs) / static_cast<float>(TotalDirs) : 0.f;

		const bool bWasEnclosed = bLastCameraEnclosed;
		bLastCameraEnclosed = LastEnclosedBlockedRatio >= CameraEnclosedBlockedRatio;

		if (bLastCameraEnclosed)
		{
			// 只在"刚进入被包住"时打一次日志：这个状态会连续持续很多帧，
			// 每帧都打会刷屏（实测刷过 572 条），排查时反而看不到别的东西。
			if (!bWasEnclosed)
			{
				UE_LOG(LogMetamorphosis, Log,
					TEXT("[Camera] 相机被包住（被挡方向 %d/%d = %.2f >= %.2f，探测距离 %.0f cm）-> 隐藏近距命中物 %d 个"),
					BlockedDirs, TotalDirs, LastEnclosedBlockedRatio, CameraEnclosedBlockedRatio,
					CameraEnclosedProbeDistance, NearHits.Num());
			}

			for (const TPair<UPrimitiveComponent*, int32>& Pair : NearHits)
			{
				MarkCameraOccluder(Pair.Key, Now, TEXT("相机被包住"), Pair.Value);
			}
		}
	}

	// (1c) 相机落在某构件的**世界包围盒**内（纯几何判定，完全不依赖碰撞）。
	//
	//      为什么还要这条（2026-10-16 用户现场定位出来的根因）：
	//      相机埋在一件**没有碰撞**的空心物体（管子/道具内部）里时，
	//      规则1（30cm 球 overlap）、规则1b（3cm 球 + 14 方向短射线）、规则2（相机->角色射线）
	//      **全都感知不到它** —— 射线和 overlap 都要求碰撞，没碰撞就一条都打不到；
	//      而画面上它的内壁糊满整个屏幕。用户现场数据正是这个特征：
	//      `隐藏清单=0 / 被挡方向=0/14 / 相机在几何体内=0`（三条规则全"没发现东西"）。
	//      Bounds 是**渲染**数据（没有碰撞也有），所以这条能把上述情况兜住。
	//
	//      代价与闸门：候选清单每 CameraBoundsCacheSeconds 秒重建一次（遍历全世界太贵）；
	//      尺寸有上下限（太小不值得藏、太大是天空球/远景壳，藏了天会黑）。
	if (bHideCameraBoundsBlockers)
	{
		if (Now >= NextCameraBoundsRefreshTime || CameraBoundsCandidates.Num() == 0)
		{
			NextCameraBoundsRefreshTime = Now + FMath::Max(0.1f, CameraBoundsCacheSeconds);
			RefreshCameraBoundsCandidates();
		}

		const FVector CharacterLocation = GetActorLocation();
		for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : CameraBoundsCandidates)
		{
			UPrimitiveComponent* Component = Weak.Get();
			if (Component == nullptr)
			{
				continue;
			}

			if (!IsCameraInsideComponentBounds(Component, CameraLocation))
			{
				continue;
			}

			// 可选取舍：相机与角色同处一个巨大构件的包围盒里时不藏（避免把整个房间壳藏掉）
			if (bCameraBoundsRequirePlayerOutside
				&& IsCameraInsideComponentBounds(Component, CharacterLocation))
			{
				continue;
			}

			MarkCameraOccluder(Component, Now, TEXT("相机在包围盒内"));
		}
	}

	// (2) 挡住角色的：按**角色自身可视尺寸**取一圈采样点（中心 + 环），每条从相机打一条精确射线，
	//     被挡到至少 OccluderBlockedSampleThreshold 条就把该构件藏起来。
	//
	//     演进史（两次相反的 bug，都很典型，别改回去）：
	//     * 2026-10-14：以前用"精灵四边形外包尺寸"取中心+上沿+左右 4 点，**任意一条**命中就隐藏。
	//       四边形半宽 14cm 远大于胶囊半径 5cm（含画布透明边距），虫子贴着家具时采样点会插进家具里
	//       -> "明明是正面、没挡镜头，靠近了却被隐藏"。
	//     * 2026-10-16：改成"只认穿过轮廓中心的那一条线"后**矫枉过正** ——
	//       中心线从门缝/家具边缘溜过去、而身体被挡住时，什么都不隐藏（用户报"该隐藏没隐藏"）。
	//       而且判定尺寸是**固定**的，躁郁值阶段3 体型涨到 4 倍（可视高度 16 -> 64cm）时更盖不住。
	//     现在：半径 = GetSpriteHeightCm() * CharacterSilhouetteRadiusScale（**随体型阶段变化**），
	//     环半径再乘 CharacterSilhouetteSpread（<1，避免把紧贴身体的家具算成遮挡）。
	if (bHideCharacterBlockers)
	{
		TArray<FVector> Samples;
		FVector SilhouetteCenter = FVector::ZeroVector;
		float SilhouetteRadius = 0.f;
		BuildSilhouetteSamples(CameraLocation, Samples, SilhouetteCenter, SilhouetteRadius);

		// 统计每个构件被多少条采样线挡住
		TMap<UPrimitiveComponent*, int32> BlockedCounts;
		for (const FVector& Sample : Samples)
		{
			TArray<FHitResult> Hits;
			World->LineTraceMultiByObjectType(Hits, CameraLocation, Sample, ObjectParams, Params);

			// 同一条线上同一构件可能被命中多次（多个凸体/多个三角面），每条线每个构件只记一次：
			// 否则"遮挡采样数"会超过采样总数（实测见过 18/9），阈值参数就失去意义了。
			TSet<UPrimitiveComponent*> HitOnThisLine;
			for (const FHitResult& Hit : Hits)
			{
				UPrimitiveComponent* HitComponent = Hit.GetComponent();
				if (HitComponent != nullptr && HitComponent->GetOwner() != this && !HitOnThisLine.Contains(HitComponent))
				{
					HitOnThisLine.Add(HitComponent);
					BlockedCounts.FindOrAdd(HitComponent)++;
				}
			}
		}

		const int32 Threshold = FMath::Max(1, OccluderBlockedSampleThreshold);
		for (const TPair<UPrimitiveComponent*, int32>& Pair : BlockedCounts)
		{
			if (Pair.Value >= Threshold)
			{
				MarkCameraOccluder(Pair.Key, Now, TEXT("挡住角色"), Pair.Value);
			}
		}
	}

	// 没再被命中的挂起项丢掉（挂起 = 已开始遮挡但还没到隐藏延迟）
	for (auto It = PendingOccluders.CreateIterator(); It; ++It)
	{
		if (It.Key().Get() == nullptr || (Now - It.Value().LastSeenTime) > 0.5f)
		{
			It.RemoveCurrent();
		}
	}

	// 不再重合/遮挡的：延迟一点再恢复（防止在边界上反复闪）
	for (auto It = HiddenOccluders.CreateIterator(); It; ++It)
	{
		UPrimitiveComponent* Component = It.Key().Get();
		if (Component == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}

		if ((Now - It.Value().LastBlockedTime) > OccluderRestoreDelay)
		{
			Component->SetRenderInMainPass(It.Value().bWasRenderInMainPass);
			Component->SetRenderInDepthPass(It.Value().bWasRenderInDepthPass);
			Component->SetCastShadow(It.Value().bWasCastShadow);
			It.RemoveCurrent();
		}
	}

	// 数量变化时打一条日志，方便排查"某个角度是不是藏了东西"
	if (HiddenOccluders.Num() != LastLoggedOccluderCount)
	{
		LastLoggedOccluderCount = HiddenOccluders.Num();
		UE_LOG(LogMetamorphosis, Log, TEXT("[Camera] 镜头重合处理：当前隐藏 %d 个构件"), LastLoggedOccluderCount);
	}
}

float AMetamorphosisBeetleCharacter::GetCameraWorldYaw() const
{
	// 吊臂是绝对旋转，它的世界 Yaw 就是相机朝向
	return CameraBoom != nullptr ? CameraBoom->GetComponentRotation().Yaw : (CameraYaw + CameraYawOffset);
}

FVector AMetamorphosisBeetleCharacter::GetCameraWorldLocation() const
{
	return Camera != nullptr ? Camera->GetComponentLocation() : GetActorLocation();
}

// ---------------------------------------------------------------------------
// 升空 / 落地
// ---------------------------------------------------------------------------

void AMetamorphosisBeetleCharacter::TakeOff()
{
	if (bAirborne)
	{
		return;
	}

	bAirborne = true;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxFlySpeed = FlySpeed * AppliedSpeedMultiplier;   // 空中水平/升降速度（同样吃道具速度加成）
	Move->BrakingDecelerationFlying = 400.f;
	Move->SetMovementMode(MOVE_Flying);           // 不受重力，可以悬停

	UE_LOG(LogMetamorphosis, Log, TEXT("[Character] 起飞（空中速度 %.0f cm/s，基础 %.0f × 速度倍率 %.2f）"),
		Move->MaxFlySpeed, FlySpeed, AppliedSpeedMultiplier);
}

void AMetamorphosisBeetleCharacter::SyncSpeedBonus()
{
	const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this);
	const float Multiplier = Stats != nullptr ? Stats->GetSpeedMultiplier() : 1.f;

	// 脏检查：倍率没变就直接返回（绝大多数帧都是这样）
	if (FMath::IsNearlyEqual(Multiplier, AppliedSpeedMultiplier, 0.001f))
	{
		return;
	}

	AppliedSpeedMultiplier = Multiplier;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr)
	{
		return;
	}

	const float OldWalkSpeed = Move->MaxWalkSpeed;
	Move->MaxWalkSpeed = WalkSpeed * Multiplier;
	Move->MaxFlySpeed = FlySpeed * Multiplier;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Character] 速度加成变化 -> 倍率 %.2f：MaxWalkSpeed %.1f -> %.1f，MaxFlySpeed -> %.1f（%s）"),
		Multiplier, OldWalkSpeed, Move->MaxWalkSpeed, Move->MaxFlySpeed,
		bAirborne ? TEXT("空中") : TEXT("地面"));
}

void AMetamorphosisBeetleCharacter::LandNow()
{
	if (!bAirborne)
	{
		return;
	}

	bAirborne = false;
	VerticalInput = 0.f;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);          // 交给重力落地
	Move->Velocity.Z = FMath::Min(Move->Velocity.Z, -FMath::Abs(LandDownSpeed));

	UE_LOG(LogMetamorphosis, Log, TEXT("[Character] 立刻落地（下压速度 %.0f cm/s）"), LandDownSpeed);
}

void AMetamorphosisBeetleCharacter::TeleportToLoopStart(const FVector& WorldLocation)
{
	const FVector Before = GetActorLocation();

	// ① 清输入：升降归零、脱离悬停
	VerticalInput = 0.f;
	bAirborne = false;

	// ② 传送（TeleportPhysics：清掉物理速度，避免"传送完还带着旧惯性飞出去"）
	SetActorLocation(WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Falling);   // 交给重力；随后由 SettleOnGroundAtSpawn 贴地
	}

	// ③ 贴地：复用出生时那套（多段射线只认朝上的面 + 卡住时上抬）
	SettleOnGroundAtSpawn();

	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[Character] 周目重生：从 %s 放回 %s（要求 %s）"),
		*Before.ToCompactString(), *GetActorLocation().ToCompactString(), *WorldLocation.ToCompactString());
}

bool AMetamorphosisBeetleCharacter::IsAirborne() const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	return bAirborne || (Move != nullptr && Move->IsFalling());
}

void AMetamorphosisBeetleCharacter::SetVerticalInput(float Value)
{
	VerticalInput = FMath::Clamp(Value, -1.f, 1.f);

	// 在地面上按上升键 = 起飞（不需要单独的飞行模式开关）
	if (VerticalInput > 0.f && !bAirborne)
	{
		TakeOff();
	}
}

// ---------------------------------------------------------------------------
// 机位
// ---------------------------------------------------------------------------

void AMetamorphosisBeetleCharacter::ApplyCameraSettings()
{
	if (CameraBoom == nullptr)
	{
		return;
	}

	CameraBoom->TargetArmLength = CameraDistance;
	CameraBoom->SetRelativeRotation(FRotator(CameraPitch, CameraYaw + CameraYawOffset, 0.f));
	CameraBoom->SetRelativeLocation(CameraTargetOffset);
	CameraBoom->bDoCollisionTest = bCameraCollisionTest;

	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(CameraFOV);
	}
}

void AMetamorphosisBeetleCharacter::HandleBodyStageChanged(int32 NewStage)
{
	ApplyMoodStage(NewStage);
}

void AMetamorphosisBeetleCharacter::ApplyMoodStage(int32 NewStage)
{
	MoodStage = FMath::Clamp(NewStage, 1, StageTextures.Num() > 0 ? StageTextures.Num() : 3);

	// 换形态后立刻切到对应序列的正确一帧（没有序列就退回阶段单图）
	AnimTime = 0.f;
	AnimFrameIndex = 0;
	if (WalkFrames.Num() == 0 && FlyFrames.Num() == 0)
	{
		UTexture2D* Texture = StageTextures.IsValidIndex(MoodStage - 1) ? StageTextures[MoodStage - 1] : nullptr;
		ApplyFrameTexture(Texture);
	}
	else
	{
		UpdateSpriteAnimation(0.f);
	}

	ApplyCameraSettings();

	UE_LOG(LogMetamorphosis, Log, TEXT("[Character] 体型阶段 -> %d（精灵高 %.1f cm，%s序列）"),
		MoodStage, GetSpriteHeightCm(),
		(MoodStage >= 3 && FlyFrames.Num() > 0) ? TEXT("飞行") : (WalkFrames.Num() > 0 ? TEXT("走路") : TEXT("单图")));
}

// ---------------------------------------------------------------------------
// 序列帧动画
// ---------------------------------------------------------------------------

void AMetamorphosisBeetleCharacter::LoadSequenceFrames()
{
	auto LoadInto = [this](TArray<TObjectPtr<UTexture2D>>& OutFrames, const FString& PathPrefix, int32 Count, const TCHAR* Label)
	{
		OutFrames.Reset();
		for (int32 i = 1; i <= Count; ++i)
		{
			const FString Path = FString::Printf(TEXT("%s%02d"), *PathPrefix, i);
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
			if (Texture == nullptr)
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Character] %s序列第 %d 帧没找到：%s"), Label, i, *Path);
				continue;
			}
			OutFrames.Add(Texture);
		}
	};

	LoadInto(WalkFrames, WalkFramePathPrefix, WalkFrameCount, TEXT("走路"));
	LoadInto(FlyFrames, FlyFramePathPrefix, FlyFrameCount, TEXT("飞行"));
	LoadInto(Idle12Frames, Idle12FramePathPrefix, Idle12FrameCount, TEXT("1/2形态待机"));
	LoadInto(Idle3Frames, Idle3FramePathPrefix, Idle3FrameCount, TEXT("3形态待机"));

	if (!bLoggedSequenceFrames)
	{
		bLoggedSequenceFrames = true;
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Character] 序列帧加载：走路 %d 帧（%.1f fps）飞行 %d 帧（%.1f fps）1/2形态待机 %d 帧（%.1f fps）3形态待机 %d 帧（%.1f fps）"),
			WalkFrames.Num(), WalkFps, FlyFrames.Num(), FlyFps,
			Idle12Frames.Num(), Idle12Fps, Idle3Frames.Num(), Idle3Fps);
	}
}

const TArray<TObjectPtr<UTexture2D>>& AMetamorphosisBeetleCharacter::GetActiveFrames(bool& bOutWinged) const
{
	// 兼容旧接口（调试字符串用）：返回"在动"时该用的那套
	bOutWinged = (MoodStage >= 3) && (FlyFrames.Num() > 0);
	return bOutWinged ? FlyFrames : WalkFrames;
}

void AMetamorphosisBeetleCharacter::ApplyFrameTexture(UTexture2D* Frame)
{
	if (Frame == nullptr || Frame == CurrentFrameTexture)
	{
		return;
	}

	CurrentFrameTexture = Frame;
	if (SpriteMID != nullptr)
	{
		SpriteMID->SetTextureParameterValue(TEXT("SpriteTexture"), Frame);
	}
}

void AMetamorphosisBeetleCharacter::UpdateSpriteAnimation(float DeltaSeconds)
{
	// 策划口径：只有 3 形态带翅膀
	const bool bWinged = (MoodStage >= 3) && (FlyFrames.Num() > 0);
	const bool bAirborneNow = IsAirborne();
	const float Speed2D = GetVelocity().Size2D();
	const bool bMoving = Speed2D > AnimMoveSpeedThreshold;
	const float SpeedRate = FMath::Clamp(Speed2D / FMath::Max(1.f, WalkAnimReferenceSpeed), 0.4f, 2.f);

	// 选序列：**不动**用待机序列（没有待机素材就退回"定格一帧"）；**在动**用走路/飞行序列
	const TArray<TObjectPtr<UTexture2D>>* ActiveFrames = nullptr;
	float BaseFps = bWinged ? FlyFps : WalkFps;
	float PlayRate = 1.f;
	int32 HoldFrame = INDEX_NONE;

	if (bWinged)
	{
		if (!bAirborneNow && !bMoving)
		{
			if (Idle3Frames.Num() > 0)
			{
				ActiveFrames = &Idle3Frames;
				BaseFps = Idle3Fps;
				CurrentAction = TEXT("3形态待机");
			}
			else
			{
				ActiveFrames = &FlyFrames;
				HoldFrame = FlyGroundedFrame - 1;   // 没待机素材：停在翅膀收拢帧
				CurrentAction = TEXT("飞行(收翅定格)");
			}
		}
		else
		{
			ActiveFrames = &FlyFrames;
			CurrentAction = bAirborneNow ? TEXT("飞行(空中)") : TEXT("飞行(地面移动)");
			if (!bAirborneNow)
			{
				// 地面上水平移动也振翅，速率跟着移动速度（别像一直狂扇）
				PlayRate = SpeedRate;
			}
		}
	}
	else
	{
		if (!bMoving && !bAirborneNow)
		{
			if (Idle12Frames.Num() > 0)
			{
				ActiveFrames = &Idle12Frames;
				BaseFps = Idle12Fps;
				CurrentAction = TEXT("待机");
			}
			else
			{
				ActiveFrames = &WalkFrames;
				HoldFrame = WalkIdleFrame - 1;      // 没待机素材：定格一帧
				CurrentAction = TEXT("走路(定格)");
			}
		}
		else
		{
			ActiveFrames = &WalkFrames;
			CurrentAction = bAirborneNow ? TEXT("走路(空中划水)") : TEXT("走路");
			PlayRate = SpeedRate;
			if (bAirborneNow)
			{
				PlayRate *= FMath::Max(0.05f, WinglessAirAnimRate);
			}
		}
	}

	if (ActiveFrames == nullptr || ActiveFrames->Num() == 0)
	{
		return;
	}

	const int32 FrameCount = ActiveFrames->Num();
	CurrentFrameTotal = FrameCount;
	if (HoldFrame != INDEX_NONE)
	{
		AnimTime = 0.f;
		AnimFrameIndex = FMath::Clamp(HoldFrame, 0, FrameCount - 1);
	}
	else
	{
		AnimTime += DeltaSeconds * FMath::Max(0.01f, BaseFps) * PlayRate;
		AnimFrameIndex = FMath::FloorToInt(AnimTime) % FrameCount;
		if (AnimFrameIndex < 0)
		{
			AnimFrameIndex += FrameCount;
		}
	}

	ApplyFrameTexture((*ActiveFrames)[AnimFrameIndex]);
}

float AMetamorphosisBeetleCharacter::GetSpriteHeightCm() const
{
	// 阶段1 是基准高度；阶段2/3 的高度按配置表的体积倍率放大
	float Multiplier = 1.f;
	if (const UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		Multiplier = Stats->GetStageSizeMultiplier(MoodStage);
	}
	return Stage1HeightCm * FMath::Max(0.01f, Multiplier);
}

void AMetamorphosisBeetleCharacter::UpdateSpriteTransform()
{
	if (SpriteQuad == nullptr)
	{
		return;
	}

	// ---- 朝向：法线指向相机、图像竖直向上（锁定视角下等于固定朝向） ----
	if (bFaceCamera && Camera != nullptr)
	{
		FVector ToCamera = Camera->GetComponentLocation() - SpriteQuad->GetComponentLocation();
		ToCamera.Z = 0.f;
		if (!ToCamera.Normalize())
		{
			ToCamera = FVector(-1.f, 0.f, 0.f);
		}

		const FVector Z = ToCamera;
		const FVector Y = FVector::UpVector;
		const FVector X = Y ^ Z;
		SpriteQuad->SetWorldRotation(FRotationMatrix::MakeFromXZ(X, Z).Rotator());
	}

	// ---- 左右镜像：按移动方向在相机右轴上的分量决定 ----
	// 基础朝向（未镜像）= 精灵朝向屏幕左（与概念图一致），所以向右移动时才镜像
	if (bFlipByMovement && Camera != nullptr)
	{
		FVector Velocity = GetVelocity();
		Velocity.Z = 0.f;
		const FVector Direction = Velocity.GetSafeNormal();
		if (!Direction.IsNearlyZero())
		{
			const float Side = FVector::DotProduct(Direction, Camera->GetRightVector());
			if (FMath::Abs(Side) > 0.15f)
			{
				bSpriteMirrored = Side > 0.f;
			}
		}
	}

	// ---- 尺寸与位置：图像高度按阶段，底边贴地 ----
	// 画布高度 = 虫子本体高度 / 内容占比（美术序列图里虫子只占画布约 57%）
	const float ContentRatio = FMath::Clamp(SpriteContentHeightRatio, 0.05f, 1.f);
	const float Height = GetSpriteHeightCm() / ContentRatio;
	float Aspect = 1.f;
	// 宽高比按"当前显示的那一帧"算；没有序列帧时退回阶段单图
	const UTexture2D* SizeRef = CurrentFrameTexture;
	if (SizeRef == nullptr && StageTextures.IsValidIndex(MoodStage - 1))
	{
		SizeRef = StageTextures[MoodStage - 1];
	}
	if (SizeRef != nullptr)
	{
		const float TexW = SizeRef->GetSurfaceWidth();
		const float TexH = SizeRef->GetSurfaceHeight();
		if (TexH > 0.f)
		{
			Aspect = TexW / TexH;
		}
	}

	const float Width = Height * Aspect;
	// 引擎 Plane 的 UV 方向与期望差了 180°，所以基础缩放两轴都取负（见 SpriteBaseSignX/Y）；
	// 左右镜像只在此基础上翻 X。
	const float SignX = (bSpriteMirrored ? -1.f : 1.f) * SpriteBaseSignX;
	const float SignY = SpriteBaseSignY;
	SpriteQuad->SetRelativeScale3D(FVector(SignX * Width / BasicPlaneSizeCm, SignY * Height / BasicPlaneSizeCm, 1.f));

	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SpriteQuad->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + Height * 0.5f));
}

FString AMetamorphosisBeetleCharacter::GetAnimDebugString() const
{
	const int32 Total = CurrentFrameTotal;
	return FString::Printf(
		TEXT("阶段=%d 动作=%s 帧=%d/%d 帧贴图=%s 速度=%.1fcm/s 空中=%d 精灵高=%.1fcm 内容占比=%.2f"),
		MoodStage, *CurrentAction,
		Total > 0 ? (AnimFrameIndex + 1) : 0, Total,
		*GetNameSafe(CurrentFrameTexture),
		GetVelocity().Size2D(), IsAirborne() ? 1 : 0,
		GetSpriteHeightCm(), SpriteContentHeightRatio);
}

void AMetamorphosisBeetleCharacter::SettleOnGroundAtSpawn()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 1) 先按 PlayerStart 归位。
	//    新地图首次进关要编译着色器（实测 11 秒），这期间角色已经在跑物理了，
	//    等 BeginPlay 执行时可能已经掉到几百米以下（旧地图只加载 1.7 秒，所以没暴露这个问题）。
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		const FVector StartLoc = It->GetActorLocation();
		if (GetActorLocation().Z < StartLoc.Z - 500.f)
		{
			SetActorLocation(StartLoc + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Character] 出生点归位：从 Z=%.1f 拉回 PlayerStart Z=%.1f（加载期间掉下去了）"),
				GetActorLocation().Z, StartLoc.Z);
		}
		break;
	}

	// 2) 站到地面上：用**线射线**找地表，然后把胶囊**刚好**放在地表上方 1cm。
	//
	//    这里踩过两个坑（都表现为"移动键完全失效"）：
	//      a) 直接把角色放在 PlayerStart 的位置：PlayerStart 的 Z 通常就是地表，而角色位置是**胶囊中心**，
	//         于是胶囊下半截扎进地面 -> 引擎把它挤出来时会选择"向下" -> 角色掉到地形**下面**，
	//         之后站在地形背面（法线朝下 = 不是 walkable floor），CharacterMovement 就拒绝一切水平移动；
	//      b) 改成"胶囊形状从上方 400cm 扫下来"：长距离下落会**穿透**薄地形，同样掉到下面去。
	//    所以正确做法是：线射线定位地表 + 只抬高一个胶囊半高 + 1cm（不下落、不穿透）。
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float Radius = Capsule != nullptr ? Capsule->GetScaledCapsuleRadius() : 5.f;
	const float CapsuleHalf = Capsule != nullptr ? Capsule->GetScaledCapsuleHalfHeight() : 7.f;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, CapsuleHalf);

	const FVector Origin = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SettleOnGroundAtSpawn), false, this);

	// 多段射线：只认**朝上**的面（跳过天空球内壁、反法线的面），并限制搜索范围（±20 米）——
	// 否则天空球（半径 500 米、把整个关卡包在里面）永远挡在最前面（之前用胶囊扫描就是被它卡住的）。
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Origin + FVector(0.f, 0.f, 300.f), Origin - FVector(0.f, 0.f, 2000.f),
		ECC_Pawn, Params);

	const FHitResult* FloorHit = nullptr;
	for (const FHitResult& H : Hits)
	{
		if (H.ImpactNormal.Z >= 0.5f)
		{
			FloorHit = &H;
			break;
		}
	}

	if (FloorHit != nullptr)
	{
		const FVector Desired(Origin.X, Origin.Y, FloorHit->ImpactPoint.Z + CapsuleHalf + 1.f);
		if (FMath::Abs(Desired.Z - Origin.Z) > 0.5f)
		{
			SetActorLocation(Desired, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Character] 出生点贴地修正：从 Z=%.1f 放到 Z=%.1f（地板 %.1f，来源 %s）"),
				Origin.Z, Desired.Z, FloorHit->ImpactPoint.Z,
				*GetNameSafe(FloorHit->GetComponent() != nullptr ? FloorHit->GetComponent()->GetOwner() : nullptr));
		}
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Character] 出生点往下 20 米内没有「朝上」的地板（Z=%.1f）：出生点可能在地形内部"), Origin.Z);
	}

	// 3) 最后确认"没被卡住"：如果胶囊与几何体重叠，往上挪，直到找到能站的地方。
	const FVector FinalLoc = GetActorLocation();
	if (World->OverlapBlockingTestByChannel(FinalLoc, FQuat::Identity, ECC_Pawn, Shape, Params))
	{
		bool bFixed = false;
		for (float Lift = 2.f; Lift <= 200.f && !bFixed; Lift += 2.f)
		{
			const FVector TryLoc = FinalLoc + FVector(0.f, 0.f, Lift);
			if (!World->OverlapBlockingTestByChannel(TryLoc, FQuat::Identity, ECC_Pawn, Shape, Params))
			{
				SetActorLocation(TryLoc, false, nullptr, ETeleportType::TeleportPhysics);
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Character] 出生点被几何体卡住：向上抬 %.0f cm 到 %s"), Lift, *TryLoc.ToCompactString());
				bFixed = true;
			}
		}
		if (!bFixed)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Character] 出生点被几何体卡住，且向上 200cm 内找不到空地"));
		}
	}
}










