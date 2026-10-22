// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Character/MetamorphosisWifeSprite.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 参考基准网格 Plane 是 100x100 cm */
	constexpr float WifeBasicPlaneSizeCm = 100.f;

	/**
	 * 引擎 Plane 的 UV 朝向与"美术图正着看"差 180°，所以基础缩放两轴都取负
	 * （与甲虫精灵同一套处理，见 MetamorphosisBeetleCharacter::SpriteBaseSignX/Y）。
	 */
	constexpr float WifeSpriteBaseSignX = -1.f;
	constexpr float WifeSpriteBaseSignY = -1.f;
}

AMetamorphosisWifeSprite::AMetamorphosisWifeSprite()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	// ---- 精灵面片 ----
	SpriteQuad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpriteQuad"));
	SpriteQuad->SetupAttachment(SceneRoot);
	SpriteQuad->SetMobility(EComponentMobility::Movable);
	SpriteQuad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpriteQuad->SetGenerateOverlapEvents(false);
	SpriteQuad->SetCanEverAffectNavigation(false);
	SpriteQuad->bReceivesDecals = false;
	SpriteQuad->SetCastShadow(false);
	SpriteQuad->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		SpriteQuad->SetStaticMesh(PlaneMesh.Object);
	}

	// 复用甲虫精灵材质（Unlit + Masked + 参数 SpriteTexture），不新建材质
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteMatFinder(
		TEXT("/Game/Characters/Beetle/M_BeetleSprite.M_BeetleSprite"));
	if (SpriteMatFinder.Succeeded())
	{
		SpriteQuad->SetMaterial(0, SpriteMatFinder.Object);
	}

	// ---- 脚下软阴影（Unlit + Translucent，参数 ShadowTexture） ----
	ShadowQuad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShadowQuad"));
	ShadowQuad->SetupAttachment(SceneRoot);
	ShadowQuad->SetMobility(EComponentMobility::Movable);
	ShadowQuad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShadowQuad->SetGenerateOverlapEvents(false);
	ShadowQuad->SetCanEverAffectNavigation(false);
	ShadowQuad->bReceivesDecals = false;
	ShadowQuad->SetCastShadow(false);
	ShadowQuad->SetVisibility(false);

	if (PlaneMesh.Succeeded())
	{
		ShadowQuad->SetStaticMesh(PlaneMesh.Object);
	}

	// 阴影材质：**故意复用甲虫的 Masked 精灵材质**（已验证能渲染），贴一张"抖动渐进"的阴影图。
	// 踩过的坑：本工程里"运行时用 Python 新建的 Translucent 材质"实测画不出来（面片位置/尺寸/可见性都对，
	// 就是不出现），和 UI 交付里那几张"贴图不渲染"同族；所以这里不走 Translucent，走 Masked + 抖动。
	if (SpriteMatFinder.Succeeded())
	{
		ShadowQuad->SetMaterial(0, SpriteMatFinder.Object);
	}
}

void AMetamorphosisWifeSprite::BeginPlay()
{
	Super::BeginPlay();

	if (SpriteQuad != nullptr && SpriteQuad->GetMaterial(0) != nullptr)
	{
		SpriteMID = UMaterialInstanceDynamic::Create(SpriteQuad->GetMaterial(0), this);
		if (SpriteMID != nullptr)
		{
			SpriteQuad->SetMaterial(0, SpriteMID);
		}
	}

	if (ShadowQuad != nullptr && ShadowQuad->GetMaterial(0) != nullptr)
	{
		ShadowMID = UMaterialInstanceDynamic::Create(ShadowQuad->GetMaterial(0), this);
		if (ShadowMID != nullptr)
		{
			ShadowQuad->SetMaterial(0, ShadowMID);

			UTexture2D* ShadowTex = LoadObject<UTexture2D>(
				nullptr, TEXT("/Game/Characters/Wife/T_Wife_ShadowDitherWhite.T_Wife_ShadowDitherWhite"));
			if (ShadowTex != nullptr)
			{
				ShadowMID->SetTextureParameterValue(TEXT("SpriteTexture"), ShadowTex);
			}
			else if (bLogWifeSprite)
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[WifeSprite] 阴影贴图没找到：/Game/Characters/Wife/T_Wife_ShadowDither"));
			}
		}
	}

	LoadAllFrames();

	if (bLogWifeSprite)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[WifeSprite] 就绪：身高 %.0fcm｜序列 正 %d / 侧 %d / 背 %d 帧｜fps 移动 %.1f 停下 %.1f 追逐 %.1f｜假阴影=%d"),
			WifeHeightCm, FrontFrames.Num(), SideFrames.Num(), BackFrames.Num(),
			SpriteFps, SpriteIdleFps, SpriteChaseFps, bCastFakeShadow ? 1 : 0);
	}
}

void AMetamorphosisWifeSprite::LoadAllFrames()
{
	if (bLoadAttempted)
	{
		return;
	}
	bLoadAttempted = true;

	auto LoadInto = [this](TArray<TObjectPtr<UTexture2D>>& OutFrames, const FString& PathPrefix, int32 Count, const TCHAR* Label)
	{
		OutFrames.Reset();
		for (int32 i = 1; i <= Count; ++i)
		{
			const FString Path = FString::Printf(TEXT("%s%02d"), *PathPrefix, i);
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
			if (Tex == nullptr)
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[WifeSprite] %s序列第 %d 帧没找到：%s"), Label, i, *Path);
				continue;
			}
			OutFrames.Add(Tex);
		}
	};

	LoadInto(FrontFrames, FrontFramePathPrefix, FrontFrameCount, TEXT("正面"));
	LoadInto(SideFrames, SideFramePathPrefix, SideFrameCount, TEXT("侧面"));
	LoadInto(BackFrames, BackFramePathPrefix, BackFrameCount, TEXT("背面"));

	// 至少得有一张，否则把面片藏起来
	const bool bAny = FrontFrames.Num() > 0 || SideFrames.Num() > 0 || BackFrames.Num() > 0;
	if (SpriteQuad != nullptr)
	{
		SpriteQuad->SetVisibility(bAny);
	}
	if (ShadowQuad != nullptr)
	{
		ShadowQuad->SetVisibility(bAny && bCastFakeShadow);
	}
}

bool AMetamorphosisWifeSprite::GetViewLocation(FVector& OutLocation) const
{
	if (const UCameraComponent* Cam = ViewCamera.Get())
	{
		OutLocation = Cam->GetComponentLocation();
		return true;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (const APlayerCameraManager* Mgr = PC->PlayerCameraManager)
			{
				OutLocation = Mgr->GetCameraLocation();
				return true;
			}
		}
	}
	return false;
}

void AMetamorphosisWifeSprite::UpdateFacing(float DeltaSeconds)
{
	const AActor* Wife = FollowTarget.Get();
	if (Wife == nullptr)
	{
		return;
	}

	FVector ViewLoc;
	if (!GetViewLocation(ViewLoc))
	{
		return;
	}

	// 相机 -> 她的水平方向（"她面对相机的方向"）
	FVector ToCamera = ViewLoc - Wife->GetActorLocation();
	ToCamera.Z = 0.f;
	if (!ToCamera.Normalize())
	{
		return;
	}

	// 她的朝向在"朝向相机"上的投影：>0 说明她正朝相机（面对面），<0 说明背对。
	// ⚠ 这里**优先用"她这一帧实际位移的方向"**，而不是 actor forward —— 原因（2026-10-18 修的 bug）：
	//    子系统里她靠 SetActorLocation 沿路径推进，转身是 FInterpTo 平滑的（TurnRateDegPerSec=360 -> 约 0.5s 转完 180°），
	//    所以**转向过程中 actor forward 会落后于真实移动方向** -> 只看 actor forward 就会在她转身的那半秒里
	//    "朝着旧方向显示、实际往新方向走" = 玩家看到的"倒着走路 / 侧着滑" ✗。
	//    位移方向来自位置差，天然和"看起来在往哪走"一致；静止（或位移太小）时才退回 actor forward，
	//    这样"停留时缓慢左右看"（Pause 状态转 actor）仍然照常生效 ✓。
	const FVector NowLoc = Wife->GetActorLocation();
	float InstantSpeed = 0.f;
	if (bFaceHasLastLocation && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		FVector Delta = NowLoc - FaceLastLocation;
		Delta.Z = 0.f;
		InstantSpeed = static_cast<float>(Delta.Size()) / DeltaSeconds;
		LastFacingSpeedCmPerSec = InstantSpeed;
		// 指数平滑：逐帧速度在帧率抖动时会偶尔掉到阈值以下，直接用会让朝向瞬间翻转
		FacingSpeedSmooth = FMath::Lerp(FacingSpeedSmooth, InstantSpeed,
			FMath::Clamp(DeltaSeconds * FacingSpeedSmoothRate, 0.f, 1.f));
		if (InstantSpeed > MinSpeedForDirCm)
		{
			const FVector MoveDir = Delta.GetSafeNormal();
			if (!MoveDir.IsNearlyZero())
			{
				LastMoveDir = MoveDir;
			}
			TimeSinceSlowMove = 0.f;
		}
		else
		{
			TimeSinceSlowMove += DeltaSeconds;
		}
	}
	FaceLastLocation = NowLoc;
	bFaceHasLastLocation = true;

	// 慢速时的方向来源：先沿用"上一次的移动方向"（SlowKeepDirSeconds 内），
	// 之后才退回 actor forward —— 这样 Pause 状态的缓慢左右看仍然生效，而"减速瞬间"不会抖。
	const bool bSpeedEnough = (FacingSpeedSmooth > MinSpeedForDirCm) || (TimeSinceSlowMove < SlowKeepDirSeconds);
	const bool bUseMoveDir = bSpeedEnough && !LastMoveDir.IsNearlyZero();
	const FVector Facing = bUseMoveDir
		? LastMoveDir
		: Wife->GetActorForwardVector().GetSafeNormal2D();
	if (Facing.IsNearlyZero())
	{
		return;
	}

	const float Dot = FVector::DotProduct(Facing, ToCamera);
	if (Dot > FrontBackDotThreshold)
	{
		CurrentFacing = EWifeSpriteFacing::Front;
	}
	else if (Dot < -FrontBackDotThreshold)
	{
		CurrentFacing = EWifeSpriteFacing::Back;
	}
	else
	{
		CurrentFacing = EWifeSpriteFacing::Side;
	}

	// 镜像：侧面素材是"人朝画面右侧"，所以她在屏幕上朝右时不镜像。
	// 屏幕上朝哪边 = 她的朝向在相机右轴上的投影。
	const FVector CamRight = FVector::CrossProduct(FVector::UpVector, ToCamera).GetSafeNormal();
	const float SideDot = FVector::DotProduct(Facing, CamRight);

	if (CurrentFacing == EWifeSpriteFacing::Side)
	{
		// SideDot > 0：她在屏幕上朝右 -> 用原始素材（不镜像）
		bool bMirror = SideDot < 0.f;
		if (bMirrorSideInvert)
		{
			bMirror = !bMirror;
		}
		bMirrored = bMirror;
	}
	else if (bMirrorFrontBack)
	{
		bMirrored = SideDot < 0.f;
	}
	else
	{
		bMirrored = false;
	}

	// 朝向/镜像变化时打一条日志（排查"偶尔倒着走"用：能看到速度、位移方向与 actor 朝向是否矛盾）
	if (bLogWifeSprite && (!bHasLoggedFacing || LastLoggedFacing != CurrentFacing || bLastLoggedMirror != bMirrored))
	{
		bHasLoggedFacing = true;
		LastLoggedFacing = CurrentFacing;
		bLastLoggedMirror = bMirrored;
		static const TCHAR* FacingNames2[] = { TEXT("正面"), TEXT("侧面"), TEXT("背面") };
		const float MoveYaw = LastMoveDir.IsNearlyZero() ? 0.f : LastMoveDir.Rotation().Yaw;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Wife] 朝向变化 -> %s（镜像=%d）｜速度 %.1f（平滑 %.1f）位移 yaw %.0f actor yaw %.0f 用位移=%d"),
			FacingNames2[FMath::Clamp(static_cast<int32>(CurrentFacing), 0, 2)],
			bMirrored ? 1 : 0, LastFacingSpeedCmPerSec, FacingSpeedSmooth, MoveYaw,
			Wife->GetActorRotation().Yaw, bUseMoveDir ? 1 : 0);
	}
}

void AMetamorphosisWifeSprite::UpdateTransform()
{
	const AActor* Wife = FollowTarget.Get();
	if (Wife == nullptr || SpriteQuad == nullptr)
	{
		return;
	}

	const FVector WifeLoc = Wife->GetActorLocation();
	const float GroundZ = static_cast<float>(WifeLoc.Z) - BodyBottomOffsetCm;

	// 画布高 = 女主身高（素材已归一化：人物=画布全高、脚底=画布底）
	CanvasHeightCm = FMath::Max(1.f, WifeHeightCm);

	// 面片中心 = 地面 + 画布高/2
	const FVector SpriteLoc(WifeLoc.X, WifeLoc.Y, GroundZ + CanvasHeightCm * 0.5f);

	// billboard：法线指向相机、图像竖直向上（与甲虫同一套）
	FVector ViewLoc;
	if (GetViewLocation(ViewLoc))
	{
		FVector ToCamera = ViewLoc - SpriteLoc;
		ToCamera.Z = 0.f;
		if (!ToCamera.Normalize())
		{
			ToCamera = FVector(1.f, 0.f, 0.f);
		}
		const FVector Z = ToCamera;
		const FVector Y = FVector::UpVector;
		const FVector X = FVector::CrossProduct(Y, Z);
		SpriteQuad->SetWorldLocationAndRotation(
			SpriteLoc, FRotationMatrix::MakeFromXZ(X, Z).Rotator());
	}
	else
	{
		SpriteQuad->SetWorldLocation(SpriteLoc);
	}

	// 宽度按当前方向那套素材的宽高比
	const TArray<TObjectPtr<UTexture2D>>& Frames = GetActiveFrames();
	float Aspect = 0.5f;
	if (Frames.Num() > 0 && Frames[0] != nullptr)
	{
		const float TexW = Frames[0]->GetSurfaceWidth();
		const float TexH = Frames[0]->GetSurfaceHeight();
		if (TexH > 0.f)
		{
			Aspect = TexW / TexH;
		}
	}
	const float Width = CanvasHeightCm * Aspect;

	const float SignX = (bMirrored ? -1.f : 1.f) * WifeSpriteBaseSignX;
	const float SignY = WifeSpriteBaseSignY;
	SpriteQuad->SetRelativeScale3D(FVector(
		SignX * Width / WifeBasicPlaneSizeCm,
		SignY * CanvasHeightCm / WifeBasicPlaneSizeCm,
		1.f));

	// 阴影：贴地的一块压扁圆片
	if (ShadowQuad != nullptr)
	{
		const bool bShowShadow = bCastFakeShadow && SpriteQuad->IsVisible();
		ShadowQuad->SetVisibility(bShowShadow);
		if (bShowShadow)
		{
			const float Diameter = FMath::Max(4.f, ShadowRadiusCm * 2.f);
			ShadowQuad->SetWorldLocation(FVector(WifeLoc.X, WifeLoc.Y, GroundZ + ShadowGroundOffsetCm));
			ShadowQuad->SetWorldRotation(FRotator::ZeroRotator);
			ShadowQuad->SetWorldScale3D(FVector(
				Diameter / WifeBasicPlaneSizeCm,
				Diameter * 0.72f / WifeBasicPlaneSizeCm,
				1.f));
		}
	}
}

const TArray<TObjectPtr<UTexture2D>>& AMetamorphosisWifeSprite::GetActiveFrames() const
{
	switch (CurrentFacing)
	{
	case EWifeSpriteFacing::Side:
		if (SideFrames.Num() > 0)
		{
			return SideFrames;
		}
		break;
	case EWifeSpriteFacing::Back:
		if (BackFrames.Num() > 0)
		{
			return BackFrames;
		}
		break;
	case EWifeSpriteFacing::Front:
	default:
		break;
	}

	if (FrontFrames.Num() > 0)
	{
		return FrontFrames;
	}
	if (BackFrames.Num() > 0)
	{
		return BackFrames;
	}
	return SideFrames;
}

void AMetamorphosisWifeSprite::ApplyFrameTexture(UTexture2D* Frame)
{
	if (Frame == nullptr || Frame == CurrentFrame)
	{
		return;
	}
	CurrentFrame = Frame;
	if (SpriteMID != nullptr)
	{
		SpriteMID->SetTextureParameterValue(TEXT("SpriteTexture"), Frame);
	}
}

void AMetamorphosisWifeSprite::UpdateAnimation(float DeltaSeconds)
{
	const TArray<TObjectPtr<UTexture2D>>& Frames = GetActiveFrames();
	FrameCount = Frames.Num();
	if (FrameCount == 0)
	{
		return;
	}

	// 是否在动：用她本体的位移速度（子系统是 SetActorLocation 挪她，GetVelocity 未必有值）
	const AActor* Wife = FollowTarget.Get();
	float Speed = 0.f;
	if (Wife != nullptr)
	{
		const FVector Now = Wife->GetActorLocation();
		if (bHasLastLocation && DeltaSeconds > KINDA_SMALL_NUMBER)
		{
			Speed = static_cast<float>(FVector::Dist2D(Now, LastLocation)) / DeltaSeconds;
		}
		LastLocation = Now;
		bHasLastLocation = true;
	}

	const bool bMoving = Speed > MoveSpeedThresholdCmPerSec;

	float Fps = bMoving ? SpriteFps : SpriteIdleFps;
	if (bChaseMode && bMoving)
	{
		Fps = SpriteChaseFps;
	}

	if (Fps <= 0.f)
	{
		// 定格（待机默认停在第 1 帧）
		AnimTime = 0.f;
		FrameIndex = 0;
	}
	else
	{
		AnimTime += DeltaSeconds * Fps;
		FrameIndex = FMath::FloorToInt(AnimTime) % FrameCount;
		if (FrameIndex < 0)
		{
			FrameIndex += FrameCount;
		}
	}

	ApplyFrameTexture(Frames[FrameIndex]);
}

void AMetamorphosisWifeSprite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 暂停时精灵也不该动（与妻子 AI/时钟同一口径）
	if (const UWorld* World = GetWorld())
	{
		if (World->IsPaused() || UGameplayStatics::IsGamePaused(World))
		{
			return;
		}
	}

	const AActor* Wife = FollowTarget.Get();
	if (Wife == nullptr)
	{
		if (SpriteQuad != nullptr && SpriteQuad->IsVisible())
		{
			SpriteQuad->SetVisibility(false);
		}
		if (ShadowQuad != nullptr && ShadowQuad->IsVisible())
		{
			ShadowQuad->SetVisibility(false);
		}
		return;
	}

	if (!bLoadAttempted)
	{
		LoadAllFrames();
	}

	UpdateFacing(DeltaSeconds);
	UpdateAnimation(DeltaSeconds);
	UpdateTransform();

	if (!bLoggedSetup && bLogWifeSprite && SpriteQuad != nullptr && SpriteQuad->IsVisible())
	{
		bLoggedSetup = true;
		UE_LOG(LogMetamorphosis, Log, TEXT("[WifeSprite] 首帧就位：%s"), *GetDebugString());

		// 阴影自检：确认面片真的被摆出来了（尺寸/高度/可见性），排查"看不见阴影"
		if (ShadowQuad != nullptr)
		{
			const float GroundZ = static_cast<float>(Wife->GetActorLocation().Z) - BodyBottomOffsetCm;
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[WifeSprite] 阴影自检：可见=%d 世界Z=%.2f（地面Z=%.2f 偏移=%.1f） 缩放=%s 材质=%s"),
				ShadowQuad->IsVisible() ? 1 : 0, ShadowQuad->GetComponentLocation().Z, GroundZ,
				ShadowGroundOffsetCm, *ShadowQuad->GetRelativeScale3D().ToString(),
				*GetNameSafe(ShadowQuad->GetMaterial(0)));
		}
	}
}

void AMetamorphosisWifeSprite::SetFollowTarget(AActor* InTarget)
{
	FollowTarget = InTarget;
	bHasLastLocation = false;
	bFaceHasLastLocation = false;
	LastMoveDir = FVector::ZeroVector;
	LastFacingSpeedCmPerSec = 0.f;
	if (InTarget != nullptr)
	{
		LastLocation = InTarget->GetActorLocation();
		FaceLastLocation = LastLocation;
		bHasLastLocation = true;
	}
}

void AMetamorphosisWifeSprite::SetViewCamera(UCameraComponent* InCamera)
{
	ViewCamera = InCamera;
}

FString AMetamorphosisWifeSprite::GetDebugString() const
{
	static const TCHAR* FacingNames[] = { TEXT("正面"), TEXT("侧面"), TEXT("背面") };
	const int32 FacingIdx = static_cast<int32>(CurrentFacing);

	const AActor* Wife = FollowTarget.Get();
	const float GroundZ = (Wife != nullptr) ? static_cast<float>(Wife->GetActorLocation().Z) - BodyBottomOffsetCm : 0.f;

	// 速度 / 移动方向 yaw / actor yaw / 是否用位移方向判定 —— 排查"倒着走"时一眼能看出方向来源
	const bool bUseMoveDir = (LastFacingSpeedCmPerSec > MinSpeedForDirCm) && !LastMoveDir.IsNearlyZero();
	const float ActorYaw = (Wife != nullptr) ? static_cast<float>(Wife->GetActorRotation().Yaw) : 0.f;
	const FString MoveDirYawText = LastMoveDir.IsNearlyZero()
		? FString(TEXT("(无)"))
		: FString::Printf(TEXT("%.1f"), static_cast<float>(LastMoveDir.Rotation().Yaw));

	return FString::Printf(
		TEXT("朝向=%s 帧=%d/%d 镜像=%d 画布高=%.1fcm 底边Z=%.1f 追逐=%d ｜ 速度=%.1fcm/s 移动yaw=%s actor yaw=%.1f 用位移方向=%d"),
		FacingNames[FMath::Clamp(FacingIdx, 0, 2)],
		FrameCount > 0 ? (FrameIndex + 1) : 0, FrameCount,
		bMirrored ? 1 : 0, CanvasHeightCm, GroundZ, bChaseMode ? 1 : 0,
		LastFacingSpeedCmPerSec, *MoveDirYawText, ActorYaw, bUseMoveDir ? 1 : 0);
}
