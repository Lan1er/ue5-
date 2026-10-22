// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetamorphosisWifeSprite.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class AActor;
class UCameraComponent;

/** 妻子朝向玩家的哪一面（决定用哪套序列帧） */
UENUM(BlueprintType)
enum class EWifeSpriteFacing : uint8
{
	Front,   // 面对面：她朝玩家
	Side,    // 侧向：玩家在她侧面
	Back     // 背对：她背朝玩家
};

/**
 * 妻子的 2D 精灵（billboard + 三方向序列帧）。
 *
 * 为什么单独一个 Actor：妻子本体是**关卡里摆的一个 Actor**（Cylinder 占位），我们不在它身上加组件
 * （StaticMeshActor 的根是 Static，运行时挂 Movable 子组件会出问题），而是生成这个"精灵替身"，
 * 每帧贴到妻子身上。妻子本体的网格**只隐藏渲染、保留碰撞**（挡玩家 + NavMesh 都靠它）。
 *
 * 尺寸口径（用户要求：1 uu = 1 cm，符合现实）：
 *   画布世界高度 == WifeHeightCm（165cm 的女主），底部中点贴地；宽按素材宽高比。
 *   美术素材已由 Tools/stage_wife_sprites.py **归一化**（人物填满画布、脚底=画布底），
 *   所以这里不需要任何"内容占比"的魔法数字。
 *
 * 方向判定：用 (她的朝向 · 相机方向) 判断正/背/侧；侧面只有一套素材，另一侧用水平镜像。
 */
UCLASS()
class METAMORPHOSIS_API AMetamorphosisWifeSprite : public AActor
{
	GENERATED_BODY()

public:
	AMetamorphosisWifeSprite();

	// ---- 跟随目标 ----
	/** 绑定妻子本体 Actor：位置/朝向都从它取 */
	void SetFollowTarget(AActor* InTarget);
	AActor* GetFollowTarget() const { return FollowTarget.Get(); }

	/** 妻子 Actor 原点到她脚底的距离（子系统的 BottomOffsetCm），用来把精灵底边贴到地面 */
	void SetBodyBottomOffsetCm(float InOffset) { BodyBottomOffsetCm = InOffset; }

	/** 追逐态（加快帧率）；由子系统每帧推 */
	void SetChaseMode(bool bInChase) { bChaseMode = bInChase; }

	/** 用哪个相机做 billboard（不设就用玩家相机）。实现在 .cpp：TWeakObjectPtr 赋值需要完整类型 */
	void SetViewCamera(UCameraComponent* InCamera);

	// ---- 查询（日志/调试用）----
	float GetSpriteHeightCm() const { return WifeHeightCm; }
	float GetCurrentCanvasHeightCm() const { return CanvasHeightCm; }
	EWifeSpriteFacing GetFacing() const { return CurrentFacing; }
	int32 GetCurrentFrameIndex() const { return FrameIndex; }
	int32 GetCurrentFrameCount() const { return FrameCount; }
	FString GetDebugString() const;

	// ---- 可调参数 ----
	/** 女主真实身高（厘米）。1 uu = 1 cm，所以精灵画布世界高度就是它 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Size")
	float WifeHeightCm = 165.f;

	/** 素材路径前缀（与 Tools/import_wife_sprites.py 对应） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	FString FrontFramePathPrefix = TEXT("/Game/Characters/Wife/Front/T_Wife_Front_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	int32 FrontFrameCount = 6;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	FString SideFramePathPrefix = TEXT("/Game/Characters/Wife/Side/T_Wife_Side_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	int32 SideFrameCount = 9;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	FString BackFramePathPrefix = TEXT("/Game/Characters/Wife/Back/T_Wife_Back_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	int32 BackFrameCount = 9;

	/** 移动时的帧率 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	float SpriteFps = 9.f;

	/** 停下时的帧率；<=0 表示定格在第 1 帧 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	float SpriteIdleFps = 0.f;

	/** 追逐时的帧率 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	float SpriteChaseFps = 14.f;

	/** 判定"在动"的速度阈值（cm/s） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Anim")
	float MoveSpeedThresholdCmPerSec = 5.f;

	/**
	 * 方向判定用"实际位移速度"的最低阈值（cm/s）。
	 * 为什么要单独一个：子系统里 actor 的转身是 FInterpTo 平滑的（TurnRateDegPerSec=360 -> 约 0.5s 才转完 180°），
	 * 所以**转向期间 actor forward 会落后于真实移动方向** -> 精灵如果只看 actor forward，就会出现"倒着走 / 侧着滑" ✗。
	 * 这里改为优先用"她这一帧实际位移的方向"判定正/背/侧，静止或位移太小时才退回 actor forward（这样停留时左右看仍然生效）。
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Facing")
	float MinSpeedForDirCm = 5.f;

	/** 慢速/静止后，仍沿用"上一次移动方向"的保留时长（秒）—— 超过才退回 actor forward。
	 *  作用：吃掉"减速/转向瞬间"的逐帧速度抖动（那会让朝向瞬间翻转 = 偶尔倒着走）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Facing")
	float SlowKeepDirSeconds = 0.25f;

	/** 速度平滑系数（每秒的指数平滑速率，越大越跟手） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Facing")
	float FacingSpeedSmoothRate = 10.f;

	/** 方向判定阈值：|朝向·视线| 大于它算"正/背对"，否则算"侧面" */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Facing")
	float FrontBackDotThreshold = 0.45f;

	/** 侧面镜像取反（素材是"人朝画面右侧"；若观感反了改这个） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Facing")
	bool bMirrorSideInvert = false;

	/** 正/背对时是否也按左右镜像（一般不需要） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Facing")
	bool bMirrorFrontBack = false;

	/**
	 * 脚下软阴影面片。
	 * ⚠ 实测缺陷（2026-10-18）：这块面片的位置/尺寸/可见性/材质都验证正确（日志"阴影自检"可查），
	 *   但**渲染不出来** —— 与 UI 交付里那几张"贴图不渲染"同族问题。所以默认关掉，
	 *   接地阴影改成**烘进精灵帧**（Tools/stage_wife_sprites.py 的 bake_contact_shadow，Bayer 抖动点阵）。
	 *   以后若定位到渲染问题，把它打开即可。
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Shadow")
	bool bCastFakeShadow = false;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Shadow")
	float ShadowRadiusCm = 34.f;

	/** 阴影贴地高度（厘米），避免与地板 z-fighting / 被地板面片盖住 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Shadow")
	float ShadowGroundOffsetCm = 4.f;

	/** 精灵是否投射引擎阴影（有假阴影时建议关，避免双影） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Shadow")
	bool bSpriteCastEngineShadow = false;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Wife|Sprite|Debug")
	bool bLogWifeSprite = true;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SpriteQuad;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShadowQuad;

private:
	void LoadAllFrames();
	void UpdateTransform();
	void UpdateAnimation(float DeltaSeconds);
	void UpdateFacing(float DeltaSeconds);
	void ApplyFrameTexture(UTexture2D* Frame);
	const TArray<TObjectPtr<UTexture2D>>& GetActiveFrames() const;

	/** 相机位置（优先 ViewCamera，其次玩家相机管理器） */
	bool GetViewLocation(FVector& OutLocation) const;

	TWeakObjectPtr<AActor> FollowTarget;
	TWeakObjectPtr<UCameraComponent> ViewCamera;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> FrontFrames;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> SideFrames;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> BackFrames;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SpriteMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShadowMID;

	UPROPERTY()
	TObjectPtr<UTexture2D> CurrentFrame;

	EWifeSpriteFacing CurrentFacing = EWifeSpriteFacing::Front;
	int32 FrameIndex = 0;
	int32 FrameCount = 0;
	float AnimTime = 0.f;

	float BodyBottomOffsetCm = 0.f;

	/** 精灵画布的世界高度（按方向不同：侧面更宽但同高） */
	float CanvasHeightCm = 165.f;

	bool bChaseMode = false;
	bool bMirrored = false;
	bool bLoggedSetup = false;
	bool bLoadAttempted = false;

	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;

	// ---- 方向判定单独记一套位移（不和动画那套共用，避免 Tick 顺序耦合） ----
	FVector FaceLastLocation = FVector::ZeroVector;
	bool bFaceHasLastLocation = false;

	/** 最近一次"有效移动方向"（水平单位向量）；静止时保留上一次的值，用于转向刚结束时的稳定 */
	FVector LastMoveDir = FVector::ZeroVector;

	/** 最近一帧用于方向判定的速度（cm/s），调试用 */
	float LastFacingSpeedCmPerSec = 0.f;

	/** 平滑后的速度（cm/s，指数平滑）。
	 *  用原始逐帧速度会在帧率抖动时偶尔掉到阈值以下 -> 方向瞬间切回 actor forward ->
	 *  表现为"偶尔倒着走"（2026-10-20 排查：这是最可能的原因）。 */
	float FacingSpeedSmooth = 0.f;

	/** 连续"慢速/静止"持续了多久（秒）。
	 *  慢下来后的短暂时间内仍沿用上一次移动方向，避免减速/转向瞬间方向抖动；
	 *  超过 SlowKeepDirSeconds 才退回 actor forward（让 Pause 状态的缓慢转身照常生效）。 */
	float TimeSinceSlowMove = 0.f;

	/** 上一次已记录日志的朝向/镜像（只在变化时打，避免刷屏） */
	EWifeSpriteFacing LastLoggedFacing = EWifeSpriteFacing::Front;
	bool bLastLoggedMirror = false;
	bool bHasLoggedFacing = false;
};
