// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 「妻子」巡视 AI（临时圆柱体替身，2026-10-14）
//
// 定位方式：**运行时按标签找 Actor**（关卡里 label=Wife 的普通 StaticMeshActor 已打上 `Meta.Wife` 标签），
//   不要求换成自定义 Actor 类、不动地图；找不到标签时用 label 兜底（PIE 下 label 可用）并打警告。
//
// 移动方式（2026-10-14 改版）：**基于 NavMesh 的正规寻路**。
//   为什么改：上一版是"手写航点"——取点只用「向下探地面 + 一条单高度的线」校验，
//   一条线代表不了她 100cm 直径/200cm 高的圆柱 -> 她直接穿床、穿桌、穿墙；
//   而且挂点逻辑是"以初始位置为家、半径 250cm"，被锁死在卧室去不了客厅。
//   现在：目标点用 `GetRandomReachablePointInRadius` 在**整个可达 NavMesh**上取（默认半径 2000cm），
//   走路用 `FindPathToLocationSynchronously` 得到路径点后沿点走 —— NavMesh 按 agent 半径生成，
//   天然留出体积间隙，不再穿模；范围给大后她能自己穿过门洞在卧室/客厅之间往返。
//
//   NavMesh 从哪来（两种都支持）：
//     * 地图里正规铺过 -> 直接用（性能最好，推荐正式关卡这么做；编辑器步骤见文档）
//     * 没铺 -> 运行时自己生成：动态生成（`[/Script/NavigationSystem.RecastNavMesh] RuntimeGeneration=Dynamic`，
//       工程 DefaultEngine.ini 里已有）+ 运行时 Spawn 一个 `ANavMeshBoundsVolume` 覆盖公寓范围。
//       ⚠️ 运行时 Spawn 的 Volume **没有 brush**（那是编辑器才有的），`GetComponentsBoundingBox()` 会是 0 体积，
//          导航系统会当成"无效包围体"直接忽略 —— 所以给它挂一个**不碰撞、不影响导航**的 BoxComponent 提供有效包围盒。
//
// 状态机（两态）：Patrol 沿路径走向目标点  ↔  Pause 到点后停留 + 缓慢左右看。
//
// 受主控（虫子）躁郁值影响（未变）：
//   * 视线范围 = BaseSightRangeCm ×（主控阶段 ≥ 2 时 × 配表 Mood.Stage2PlusNoCoatSightRangeMult）
//     ⚠️ 工程里**还没有"是否穿风衣"的状态**（只有配表字段 TrenchCoatMaxStage / Stage2PlusNoCoatSightRangeMult），
//        所以现在按"阶段2+ = 无风衣"的占位规则；风衣做出来后把这一处换成真实判断即可（见文档 TODO）。
//   * 移动速度 ×(1 + SpeedBonusPerStage × (阶段-1))  —— 越躁走得越快
//   * 停留时长 ×Lerp(1.0, PauseScaleAtMaxMood, 躁郁比例) —— 越躁越坐不住
//   * 看见主控（在视线范围 + 视锥内 + 视线不被挡）→ AddSuspicionByTrigger("SightGlance")，自带 GlanceIntervalSeconds 限频
//
// 她的碰撞与导航：
//   * 默认把驱动用的 Mesh 设为**不阻挡 Pawn**（bWifeBlocksPlayer=false）—— 大圆柱会把 16cm 的虫子挤住甚至卡死
//     （本项目踩过"完全动不了"的事故）。
//   * `SetCanEverAffectNavigation(false)` —— 否则她自己会在 NavMesh 上挖个洞，而且随她移动不停重建 NavMesh。
//   * 运行时把她的 XY 缩放对齐到 NavMesh 的 agent 半径（默认 AgentRadius=34cm，引擎 Cylinder 半径 50cm
//     -> 缩放 ≈0.68），让她"卡在 agent 尺寸内"，NavMesh 给的间隙才真正够她过。
//
// 调试：控制台 Meta.Wife.Info / Nav / Pause / Resume / Goto <x> <y> / Glance

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MetamorphosisWifeSubsystem.generated.h"

class AActor;
class ANavMeshBoundsVolume;
class ANavigationData;
class AMetamorphosisWifeSprite;
class UBoxComponent;
class UPrimitiveComponent;

/** 妻子的行为状态 */
UENUM()
enum class EWifeState : uint8
{
	Patrol,   // 正在沿路径走向目标点
	Pause,    // 停留中（含左右看）
	Chase,    // 追逐战（2026-10-14 新增）：主控进入阶段3 -> 用 NavMesh 追过去
	Idle      // 没找到妻子 Actor / 被暂停 / NavMesh 还没就绪（调试用）
};

UCLASS(Config = Game)
class METAMORPHOSIS_API UMetamorphosisWifeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---------- USubsystem / UTickableWorldSubsystem ----------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** 从任意 WorldContext 取本系统（拿不到返回 nullptr） */
	static UMetamorphosisWifeSubsystem* Get(const UObject* WorldContextObject);

	// ---------- 对外查询（调试/UI/后续系统用）----------
	AActor* GetWifeActor() const { return WifeActor.Get(); }

	/** 她的 2D 精灵（还没生成时返回 nullptr） */
	AMetamorphosisWifeSprite* GetWifeSprite() const;

	/** 现实身高 / 眼睛世界高度（调试与视线用） */
	float GetWifeHeightCm() const { return WifeHeightCm; }
	float GetEyeWorldZ() const { return EyeWorldZ; }
	float GetWifeHalfHeightCm() const { return HalfHeightCm; }
	float GetWifeBottomOffsetCm() const { return BottomOffsetCm; }

	EWifeState GetState() const { return State; }
	FVector GetHomeLocation() const { return HomeLocation; }
	FVector GetTargetLocation() const { return TargetLocation; }
	/** 当前实际生效的视线范围（已含躁郁值阶段修正） */
	float GetEffectiveSightRangeCm() const;
	/** 当前实际生效的移动速度（已含躁郁值阶段修正） */
	float GetEffectiveSpeedCmPerSec() const;
	/** 主控是否在视线内（每 GlanceIntervalSeconds 更新一次） */
	bool IsSeeingPlayer() const { return bSeeingPlayer; }

	/** 主控当前躁郁比例（0~1） */
	float GetMoodRatio() const;
	/** 主控当前体型阶段（1~3） */
	int32 GetPlayerStage() const;

	// ---------- NavMesh 相关查询 ----------
	/** 导航数据是否已就绪（就绪前她不会移动，避免又穿模） */
	bool IsNavReady() const { return bNavReady; }
	/** 当前路径点数量（0 = 没在沿路径走） */
	int32 GetPathPointCount() const { return PathPoints.Num(); }
	/** 当前正走向第几个路径点 */
	int32 GetPathIndex() const { return PathIndex; }
	/** 当前正走向的路径点（没有就返回目标点） */
	FVector GetCurrentPathPoint() const;
	/** 她的圆柱半径（厘米，已按 agent 半径缩放后的实测值） */
	float GetWifeRadiusCm() const { return WifeRadiusCm; }
	/** 她此刻是否与静态几何体重叠（穿模的客观判据）；重叠时把命中的组件名写进 OutHitName */
	bool IsOverlappingStaticGeometry(FString& OutHitName) const;

	/** 指定位置是否与关卡静态几何体重叠（移动前预检用，复用同一套胶囊/忽略规则） */
	bool IsOverlappingStaticGeometryAt(const FVector& TestLocation, FString& OutHitName) const;

	/** 带预检的移动：目标位置会与静态几何体重叠就不移动（返回 false，调用方负责重规划/换点） */
	bool TryMoveWife(const FVector& NewLocation);

	/** 移动预检被拦下的次数（被家具挡住 -> 不移动 + 重规划） */
	int32 GetMoveBlockedCount() const { return MoveBlockedCount; }

	/** 累计`与静态几何体重叠`的自检次数（验证用：必须为 0） */
	int32 GetOverlapFrameCount() const { return OverlapFrameCount; }

	/** 严格口径：用**视觉半径**（不 shrink、不按台阶高度忽略）判断她的视觉圆柱是否蹭进静态几何体。
	 *  与碰撞胶囊口径分开统计 —— 回答"视觉上到底有没有穿模"（2026-10-17 用户要求复核）。 */
	bool IsPenetratingFurnitureStrict(FString& OutHitName) const;

	/** 严格口径的指定位置版本（移动预检也用它） */
	bool IsPenetratingFurnitureStrictAt(const FVector& TestLocation, FString& OutHitName) const;

	/** 严格口径累计命中次数（验证用：目标 = 0） */
	int32 GetStrictPenetrationCount() const { return StrictPenetrationCount; }

	/** 严格口径命中过的物件与次数（"撞到了谁"），用于 Meta.Wife.Info */
	FString GetStrictPenetrationSummary() const;

	/** 一行 NavMesh 状态摘要（给 Meta.Wife.Nav / Info 打日志用） */
	FString GetNavSummary() const;

	// ---------- 公寓范围（2026-10-14：把她的活动范围锁在公寓内，不再可能走到户外）----------
	/** 公寓盒（世界坐标；未量到时为无效盒）。来源：优先 ini 覆盖，否则用房间构件名自动量 */
	const FBox& GetApartmentBox() const { return ApartmentBox; }
	/** 某个位置是否在公寓范围内（bLockToApartment=false 时恒为 true） */
	bool IsInsideApartment(const FVector& Location) const;
	/** 一行公寓范围摘要（中心/半径/来源/她是否在盒内） */
	FString GetApartmentSummary() const;

	// ---------- 追逐战（阶段3）----------
	/** 主控"最后已知位置"（她追过去的目标；看不见时用） */
	FVector GetLastKnownPlayerLocation() const { return LastKnownPlayerLocation; }
	/** 主控当前躁郁值（0~100） */
	float GetPlayerMood() const;
	/** 强制进入追逐战（调试；等价于躁郁值拉满）。再调用一次取消 */
	void DebugChase();
	/** 打过几次追逐战 */
	int32 GetChaseCount() const { return ChaseCount; }

	/** 周目结算时由 UMetamorphosisLoopSubsystem 调用：立刻收手（停追逐、清目标、清调试强制），回巡视待命 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Wife")
	void NotifyLoopReset();

	// ---------- 调试控制 ----------
	void DebugPause();
	void DebugResume();
	bool DebugGoto(float X, float Y);
	/** 立即做一次视线判定（不受限频影响），返回是否看见 */
	bool DebugGlanceNow();
	/** 打印 NavMesh 状态（含可达区域包围盒） */
	void DebugPrintNav();

	// ---------- 可调参数（ini: [/Script/Metamorphosis.MetamorphosisWifeSubsystem]）----------
	/** 找她用的标签 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Find")
	FName WifeTag = TEXT("Meta.Wife");

	/** 标签找不到时的 label 兜底（仅编辑器/PIE 有 label） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Find")
	FString WifeLabelFallback = TEXT("Wife");

	/** 是否让她阻挡主控（默认 true：她现在是实体，玩家不能穿过；防卡死见 StopApproachDistanceCm） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Find")
	bool bWifeBlocksPlayer = true;

	// ---- NavMesh ----

	/** 没铺 NavMesh 时是否运行时自己生成（已铺过则自动直接用现成的） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	bool bRuntimeNavMesh = true;

	/** 运行时包围体中心（0 = 用她的家）。想让她只在公寓里活动就把它对准公寓中心、半径收小 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	FVector NavVolumeCenterOverride = FVector::ZeroVector;

	/** 运行时包围体半径（厘米）。默认 2000：从她的家出发能覆盖卧室+客厅 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	float NavVolumeRadiusCm = 2000.f;

	/** 运行时包围体半高（厘米）。默认 300：覆盖地板到天花板 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	float NavVolumeHalfHeightCm = 300.f;

	/** 等 NavMesh 重建的最长时间（秒），超时就报"没铺出来"并静止 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	float NavRebuildWaitSeconds = 20.f;

	/** NavMesh 的 agent 半径（厘米）。**必须与 [/Script/NavigationSystem.RecastNavMesh] 的 AgentRadius 一致**
	 *  （引擎默认 34；工程 DefaultEngine.ini 没改过）。她会被缩放到这个半径之内 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	float NavAgentRadiusCm = 34.f;

	/** 安全系数：她的半径 = agent 半径 × 这个值（<1 留点余量，默认 0.95） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	float WifeAgentRadiusSafetyFactor = 0.95f;

	/** 是否在运行时把她的 XY 缩放对齐到 agent 半径（只改缩放，不动地图资产） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Nav")
	bool bMatchAgentRadius = true;

	// ---- 巡视 ----

	/** 巡视半径（厘米，以"家"为圆心，在可达 NavMesh 上取随机点）。
	 *  默认 2000 = 全公寓游走（她能自己穿过门洞去客厅）；想让她只在一个房间就改小 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float PatrolRadiusCm = 2000.f;

	/** 目标点与当前位置的最小距离（厘米）：太近的点没意义，重取 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float MinPatrolPointDistanceCm = 200.f;

	/** 基础巡航速度（厘米/秒），默认与虫子同速便于观察 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float PatrolSpeedCmPerSec = 60.f;

	/** 停留时长下限/上限（秒） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float PauseSecondsMin = 1.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float PauseSecondsMax = 3.5f;

	/** 取点重试次数 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	int32 PatrolPointRetryCount = 12;

	/** 沿路径走到下一个路径点的判定容差（厘米） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float PathPointToleranceCm = 15.f;

	/** 超过这个时间没走到目标点就重新规划/换点（秒，防卡住） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float MoveTimeoutSeconds = 15.f;

	/** 停留时左右看的幅度（度，0 = 不看） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Patrol")
	float PauseLookAroundDeg = 40.f;

	// ---- 视线与怀疑值 ----

	/** 基础视线范围（厘米） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sight")
	float BaseSightRangeCm = 400.f;

	/** 视锥总角度（度，以她朝向为中心） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sight")
	float SightConeDeg = 100.f;

	/** 视线判定的最小间隔（秒）——配表 AddSuspicionByTrigger 没有自带冷却，这里自己限频 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sight")
	float GlanceIntervalSeconds = 1.5f;

	/** 向下找地面的探测范围（厘米）：从 目标点+上面这个值 往下探 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sight")
	float GroundProbeUpCm = 60.f;

	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sight")
	float GroundProbeDownCm = 300.f;

	// ---- 现实尺寸与 2D 精灵（2026-10-18）----

	/**
	 * 妻子的现实身高（厘米）。1 uu = 1 cm，所以精灵画布的世界高度就是它。
	 * 半径按 NavMesh agent 对齐（不受它影响），只有身高/眼睛高度跟着它走。
	 * ⚠ 开启 bDeriveHeightFromScene（默认）时，这个值只在"推不出来"时兜底。
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	float WifeHeightCm = 165.f;

	/**
	 * 从场景里"已知真实尺寸的物件"反推人体尺度。
	 *
	 * 为什么需要：本图家具是**4 倍缩放**（床 832×520×374cm），165cm 的她站在床边只有床沿高 ✗，
	 * 看起来像玩偶。所以拿一张双人床的真实长度（208cm）当标尺：
	 *     场景倍数 = 床长 / 208          （832 / 208 = 4.0）
	 *     她的身高 = 165 × 场景倍数      （= 660cm）
	 * 这样她在这个"放大的房子"里才是**正常人比例**（比床高），而不是玩偶。
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	bool bDeriveHeightFromScene = true;

	/**
	 * 直接指定"场景倍数"（<=0 = 按标尺自动反推）。
	 * 用户口径（2026-10-18）：按标尺反推是 4 倍（=660cm），但**太大** -> 固定 **2 倍**（=330cm）。
	 * 这样"人比床高"仍成立，又不会占满整个视野。
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	float HeightSceneScaleOverride = 2.f;

	/** 标尺网格名（包含即匹配）：一张双人床 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	FString HeightReferenceMeshName = TEXT("SM_Bed_twin");

	/** 标尺物件的真实世界长度（厘米）：双人床约 208cm */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	float HeightReferenceRealLengthCm = 208.f;

	/** 反推倍数限制区间，防止量错（例如量到墙）把她放大成怪物 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	float DerivedHeightFactorMin = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	float DerivedHeightFactorMax = 8.f;

	/** 眼睛高度占身高的比例（成人约 0.92）：身高随场景缩放时眼睛跟着走 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Body")
	float WifeEyeHeightRatio = 0.92f;

	/** 是否把本体网格换成 2D 三方向精灵（关掉就退回圆柱占位） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sprite")
	bool bWifeSpriteEnabled = true;

	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sprite")
	float WifeSpriteFps = 9.f;

	/** <=0 = 停下时定格 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sprite")
	float WifeSpriteIdleFps = 0.f;

	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sprite")
	float WifeSpriteChaseFps = 14.f;

	/** 脚下软阴影（2D 角色没有它看起来会飘） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sprite")
	bool bWifeSpriteShadow = true;

	/** 侧面素材镜像取反（素材是"人朝画面右侧"，若观感反了改这个） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Sprite")
	bool bWifeSpriteMirrorSideInvert = false;

	// ---- 躁郁值影响 ----

	/** 每高一个阶段，移动速度增加的比例（0.25 = +25%/阶段） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Mood")
	float SpeedBonusPerStage = 0.25f;

	/** 躁郁比例拉满时，停留时长缩到原来的多少（0.35 = 缩到 35%） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Mood")
	float PauseScaleAtMaxMood = 0.35f;

	// ---- 公寓范围（2026-10-14）----

	/** 是否把她的活动范围锁在公寓盒内（随机巡视点/追逐目标点超界就丢弃）。
	 *  这样即使 NavMesh 覆盖到户外地形，她也走不出去 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Apartment")
	bool bLockToApartment = true;

	/** 公寓盒中心（0 = 自动量）。自动量失败或想手填时用 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Apartment")
	FVector ApartmentCenterOverride = FVector::ZeroVector;

	/** 公寓盒半径（0 = 自动量） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Apartment")
	FVector ApartmentExtentOverride = FVector::ZeroVector;

	/** 自动量公寓范围时，认哪些房间构件（逗号分隔的网格名前缀）。
	 *  只认"墙/地板/天花板/窗台/勒脚"这类**建筑构件**，避免把户外地形/树算进来 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Apartment")
	FString ApartmentMeshPrefixes = TEXT("SM_Wall,SM_Floor,SM_Ceiling,SM_Sill,SM_Plinth");

	/** 公寓盒外扩（厘米）：留一点墙厚/门洞余量 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Apartment")
	float ApartmentPaddingCm = 50.f;

	/** 运行时导航包围体相对公寓盒再外扩（厘米）：NavMesh 要略大于活动范围，
	 *  否则边界处的点会取不到 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Apartment")
	float ApartmentNavPaddingCm = 100.f;

	// ---- 追逐战（阶段3）----

	/** 主控躁郁值 ≥ 这个值就进入追逐战（配表 Mood.Stages 的阶段3 = 90） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseEnterMood = 90.f;

	/** 追逐中躁郁值掉到这个值以下才退出（迟滞，避免在阈值上抖） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseExitMoodBelow = 85.f;

	/** 追逐速度（厘米/秒）。设计理由：主角步行 60、飞行 80（吃到膏药贴后 ×1.2 = 96）。
	 *  取 70 -> **只走路一定被追上，必须起飞才能甩掉**，压力明显但给玩家活路 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseSpeedCmPerSec = 70.f;

	/** 追逐中重新规划路径的间隔（秒） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseRepathIntervalSeconds = 0.5f;

	/** 连续这么久既看不见主控、又到不了最后已知位置 -> 放弃追逐（秒） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseGiveUpSeconds = 6.f;

	/** 抓到判定半径（厘米）：与主控的**水平**距离小于它就视为被捉。
	 *  ⚠️ 用水平距离而不是 3D 距离：她是 200cm 高的圆柱、位置在胶囊中心（约 +100cm），
	 *  虫子只有 16cm 高，两者高度差本来就有 ~90cm，用 3D 距离永远抓不到（踩过）。 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float CatchRadiusCm = 80.f;

	/** 抓到判定的高度容差（厘米）：水平够近、且高度差在这个范围内才算抓到 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float CatchHeightToleranceCm = 150.f;

	/** 放弃/被抓后的冷却（秒）：这段时间内即使躁郁值仍 ≥ 进入线也不会立刻再追 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseCooldownSeconds = 8.f;

	/** 主控跑出公寓范围时是否放弃（true = 她守在公寓里，符合"锁在公寓"的设定） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	bool bOnlyChaseInsideApartment = true;

	/** 怀疑值进入线：<0（默认）= 自动读配表 Suspicion.ThresholdExposed（"暴露"档，当前 90）；
	 *  0 = 禁用"怀疑值触发追逐"这条；>0 = 手动覆盖 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseEnterSuspicion = -1.f;

	/** 怀疑值退出线（迟滞）：**躁郁与怀疑都低于各自退出线**才退出追逐，避免在阈值上抖 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float ChaseExitSuspicionBelow = 70.f;

	/** 怀疑值【满】时的"持续追逐"进入线（>= 此值就进入，且在解除前不因追丢而放弃）。
	 *  <=0 表示关闭这条。默认 100（满值）= 玩家把怀疑值拉满后她会一路追到你被抓住为止。 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float PersistentChaseAtSuspicion = 100.f;

	/** 持续追逐的解除线：怀疑值降到此值**以下**才放弃（回到巡视/搜索） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float PersistentChaseReleaseSuspicion = 95.f;

	/** 持续追逐时是否无视"主控跑出公寓"的放弃条件（默认 false：保持"锁在公寓"的设定） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	bool bPersistentChaseIgnoreApartment = false;

	/** 与主控水平距离小于此值时就停止推进（人已贴上来，再推会把 16cm 的虫子顶进墙里卡死）；
	 *  她现在是实体（bWifeBlocksPlayer=true），所以这条是防卡死的关键；<=0 表示关闭 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	float StopApproachDistanceCm = 100.f;

	/** 是否打印追逐战日志 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Chase")
	bool bLogChase = true;

	// ---- 自检 ----

	/** 自检用的形状缩比：用她的半径/半高 × 这个值做重叠检测，避免"刚好贴地"被误判成穿模 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Debug")
	float OverlapCheckShrink = 0.9f;

	/** 低于"她的脚 + 这个高度"的东西不算穿模（鞋/地毯/小杂物本来就被 NavMesh 当成可行走地面跨过去）。
	 *  默认 35 = RecastNavMesh 的 AgentMaxStepHeight；用户报的穿模是"穿床/穿桌/穿墙"这种大件 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Debug")
	float OverlapIgnoreBelowStepCm = 35.f;

	/** 严格口径复核（视觉半径 / 不 shrink / 不按台阶忽略）时，把探测胶囊底抬多高来避开"站立时贴地"。
	 *  只抬这一点点，其余一律不忽略 -> 任何家具（含矮于 35cm 的）蹭到都会被记进 StrictPenetrationCount。 */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Debug")
	float StrictProbeFloorLiftCm = 3.f;

	/** 是否每次检测到与静态几何体重叠都打日志（默认开，发现穿模第一时间能看到） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Debug")
	bool bWarnOnOverlap = true;

	/** 是否打印每次视线判定/状态变化日志（排查用，默认开） */
	UPROPERTY(EditAnywhere, Config, Category = "Wife|Debug")
	bool bLogWife = true;

private:
	/** 找妻子 Actor（标签优先，label 兜底），并做好碰撞/移动性/缩放设置 */
	bool FindAndPrepareWife();

	/** 按 agent 半径把她的 XY 缩放调小（只改运行时缩放，不动地图资产），并重算半径/半高/贴地偏移 */
	void ApplyAgentRadiusScale();

	/**
	 * 从场景标尺（默认双人床 SM_Bed_twin，真实长度 208cm）反推"这个场景的人体尺度"，
	 * 用它覆盖 WifeHeightCm。本图家具 4 倍缩放 -> 165×4 = 660cm，比床高，看着才是正常人。
	 */
	void DeriveWifeHeightFromScene();

	/**
	 * 把身高拉到 WifeHeightCm（现实尺寸，1 uu = 1 cm），并重算半高 / 贴地偏移 / 眼睛高度。
	 * 只动 Z 缩放：XY 已经被 ApplyAgentRadiusScale 对齐到 NavMesh agent 半径，不能再动。
	 */
	void ApplyRealBodyHeight();

	/** 确保 2D 精灵存在（关卡重载/销毁后自动重建），并把当前状态同步给它 */
	void EnsureWifeSprite();

	/** 确保有可用 NavMesh：已铺过就直接用；没铺就在运行时生成包围体并等重建完成 */
	bool EnsureNavReady();

	/** 运行时包围体的中心（NavVolumeCenterOverride 为 0 时用她的家） */
	FVector GetNavVolumeCenter() const;

	/** 在可达 NavMesh 上随机取一个巡视点（并保证落在公寓盒内） */
	bool PickPatrolPoint(FVector& OutPoint);

	/** 自动量公寓范围（房间构件的包围盒并集；ini 有覆盖值就用覆盖值） */
	void MeasureApartmentBounds();

	/** 进入/退出追逐战 */
	void EnterChase(const TCHAR* Reason);
	void ExitChase(const TCHAR* Reason);

	/** 追逐战每帧逻辑（追主控 / 追最后已知位置 / 放弃 / 抓人判定） */
	void TickChase(float DeltaTime);

	/** 抓到了：走**现有**的"被捉/周目重置"入口（SetSuspicion(CaughtValue) + ResetForNewLoop） */
	void HandleCaught();

	/** 沿当前路径走一步；返回 true 表示路径已走完 */
	bool StepAlongPath(float SpeedCmPerSec, float DeltaTime);

	/** 规划到 Dest 的路径（成功则填 PathPoints 并把 PathIndex 置 1） */
	bool BuildPathTo(const FVector& Dest);

	/** 贴地：把 Z 吸附到"地面 + 初始时的底部偏移" */
	void SnapToGround();

	/** 每帧的视线判定（内部限频） */
	void UpdateSight(float DeltaTime);

	/** 计算当前"看见主控"的判定（不受限频） */
	bool ComputeSeeingPlayer(float& OutDistance) const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> WifeActor;

	/** 是否已经找到过妻子（用于"找到/丢失"只打一次日志，避免每帧刷屏） */
	bool bWifeActorFound = false;

	/** 是否已经提示过"没找到妻子"（本局只提示一次，避免每帧 Warning 刷屏） */
	bool bLoggedMissingWife = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> WifeMesh;

	/** 妻子的 2D 三方向精灵（运行时生成，跟随 WifeActor） */
	UPROPERTY(Transient)
	TWeakObjectPtr<AMetamorphosisWifeSprite> WifeSpriteActor;

	/** 眼睛的世界高度（脚底 + WifeEyeHeightCm），每帧随她位置刷新；视线判定用它 */
	float EyeWorldZ = 0.f;

	/** 运行时生成的导航包围体（只有"没铺 NavMesh"这一路才有） */
	UPROPERTY(Transient)
	TWeakObjectPtr<ANavMeshBoundsVolume> NavBoundsVolume;

	EWifeState State = EWifeState::Idle;
	bool bInitialized = false;
	bool bPausedByDebug = false;

	FVector HomeLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	/** 她 Mesh 包围盒底边相对 Actor 位置的偏移（圆柱体 pivot 常在底部，用这个做贴地更稳） */
	float BottomOffsetCm = 0.f;
	float HalfHeightCm = 100.f;
	/** 实测半径（厘米，缩放后） */
	float WifeRadiusCm = 50.f;
	float TurnRateDegPerSec = 360.f;

	// ---- NavMesh 状态 ----
	bool bNavReady = false;
	/** 是否已经尝试过生成运行时包围体（只生成一次） */
	bool bNavSetupTried = false;
	/** 是否已经把"铺不出来"这条警告打过（避免刷屏） */
	bool bNavFailureLogged = false;
	float NavWaitElapsed = 0.f;

	/** 当前路径（世界坐标），PathIndex 指向正在走向的那个点 */
	TArray<FVector> PathPoints;
	int32 PathIndex = 0;

	float StateTimer = 0.f;        // 当前状态已持续时间
	float PauseDuration = 0.f;     // 本次停留总时长
	float MoveElapsed = 0.f;       // 本次行走已用时间
	float GlanceTimer = 0.f;       // 视线判定限频计时
	float OverlapCheckTimer = 0.f; // 穿模自检计时
	float BaseYaw = 0.f;           // 停留左右看的基准朝向
	bool bSeeingPlayer = false;
	bool bOverlapWarned = false;   // 重叠告警去重（恢复后允许再报）
	int32 OverlapFrameCount = 0;   // 累计"与静态几何体重叠"的自检次数（验证用：必须为 0）
	int32 MoveBlockedCount = 0;    // 移动预检拦下次数（被家具挡住 -> 不移动 + 重规划）

	/** 严格口径（视觉半径）累计「蹭进家具」次数 —— 与碰撞胶囊的 OverlapFrameCount 分开看 */
	int32 StrictPenetrationCount = 0;

	/** 严格口径命中过的物件 -> 次数（诊断"到底撞到了谁"） */
	TMap<FString, int32> StrictPenetrationHits;
	float MoveBlockedLogTimer = 0.f; // 预检日志限频
	int32 LoggedStage = -1;        // 阶段变化时打一次日志

	// ---- 公寓范围（2026-10-14）----
	FBox ApartmentBox = FBox(ForceInit);      // 无效盒 = 没量到
	bool bApartmentMeasured = false;
	int32 ApartmentMeshCount = 0;

	// ---- 追逐战（2026-10-14）----
	FVector LastKnownPlayerLocation = FVector::ZeroVector;
	bool bHasLastKnown = false;
	float TimeSinceSeenPlayer = 0.f;   // 追逐中"连续看不见"的时长（放弃用）
	float ChaseRepathTimer = 0.f;
	float ChaseCooldownTimer = 0.f;    // >0 时即使躁郁值够高也不追
	float ChaseCatchCooldownTimer = 0.f; // 抓到后短暂停手（避免同一帧反复触发）
	int32 ChaseCount = 0;              // 打过几次追逐战（日志/调试用）
	bool bDebugForceChase = false;     // Meta.Wife.Chase 强制追逐（任何一次退出都会清掉）

	/** 当前是否处于"怀疑值满 -> 持续追逐"（追丢不放弃、只看到怀疑值降下来） */
	bool bPersistentChase = false;
};


