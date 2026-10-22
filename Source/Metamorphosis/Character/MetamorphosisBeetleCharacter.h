// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 虫形主角（2D 精灵 + 3D 场景 + 锁定视角）
//
// 设计要点（2026-10-09）：
//   * 目标平台移动端；场景是真实尺度的公寓（1 uu = 1 cm），主角是「手掌大小的虫子」，
//     所以碰撞胶囊按厘米级做（半径 4 cm / 半高 5 cm），移动速度也按虫子尺度给（默认 45 cm/s）；
//   * 形象是一张正对相机的 2D 精灵（Plane + Unlit/Masked 材质），按躁郁值体型阶段换贴图：
//       阶段1 -> 侧视（小虫） 阶段2 -> 正视 阶段3 -> 展翅；
//     阶段2/3 的世界尺寸 = Stage1HeightCm × 配置表体积倍率（2x / 4x）；
//   * 相机：SpringArm + 绝对旋转 = 角度永远锁死，玩家无法转视角；只跟随位置。
//     想调整构图只改 CameraDistance / LockedCameraAngle / CameraFOV 三个参数。
//
// 贴图与材质由 Tools/make_beetle_assets.py 生成，路径：
//   /Game/Characters/Beetle/Textures/T_Beetle_Stage1..3
//   /Game/Characters/Beetle/M_BeetleSprite  (Unlit + Masked + 参数 SpriteTexture)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MetamorphosisBeetleCharacter.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USpringArmComponent;
class UStaticMeshComponent;
class UTexture2D;

UCLASS(config = Game)
class METAMORPHOSIS_API AMetamorphosisBeetleCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMetamorphosisBeetleCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 按体型阶段换形象与尺寸（1/2/3）。由数值系统的 OnBodyStageChanged 自动调用 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Character")
	void ApplyMoodStage(int32 NewStage);

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Character")
	int32 GetMoodStage() const { return MoodStage; }

	/** 当前精灵的世界高度（厘米） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Character")
	float GetSpriteHeightCm() const;

	/** 调试用：一行描述当前动画状态（阶段/序列/帧/速度）；给 Meta.Anim.Info 用 */
	FString GetAnimDebugString() const;

	// ---- 升空（没有"飞行模式"开关：空格就是上升、S 就是下降、双击空格落地） ----

	/** 起飞并悬停（在地面按上升键时自动调用，也供调试命令用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Fly")
	void TakeOff();

	/** 立刻落地（双击空格触发）：脱离悬停交给重力，并往下压一下速度，落地更干脆 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Fly")
	void LandNow();

	/** 当前是否在空中（悬停中或下落中） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Fly")
	bool IsAirborne() const;

	/** 升降输入（+1 上升 / -1 下降 / 0 不输入），由控制器转发 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Fly")
	void SetVerticalInput(float Value);

	/** 周目结算：把主角放回安全起点（清速度 + 清升降输入 + 落地 + 贴地）。
	 *  ⚠️ 不要改用 PlayerStart：本图（Untitled）的它埋在地形下 3 米多，会把主角放进地里。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void TeleportToLoopStart(const FVector& WorldLocation);

	// ---- 视角水平旋转（由控制器转发输入） ----

	/** AxisValue 是 -1~1 的速率，DeltaSeconds 用帧时间 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void AddCameraYawInput(float AxisValue, float DeltaSeconds);

	/** 直接叠加一个水平角度（移动端搓屏幕用：手指位移直接映射成角度） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void AddCameraYawDelta(float DeltaDegrees);

	/** 直接把水平偏移设成某个角度（调试用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void SetCameraYawOffset(float NewOffsetDegrees);

	// ---- 出生朝向（用户可配：ini 的 CameraYaw = "初始水平朝向（度）"）----

	/** 当前配置的出生朝向（= CameraYaw；玩家转视角只叠加 Offset） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	float GetSpawnYawDegrees() const;

	/** 改出生朝向：立刻生效（CameraYaw 置为该值、偏移清零、刷新相机）。ini 里改 CameraYaw 可持久化。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void SetSpawnYawDegrees(float NewYawDegrees);

	/** 把视角复位到出生朝向（周目重生时调用；清掉玩家转过的偏移） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void ResetCameraYawToSpawn();

	/** 直接改机位距离（调试/调参用；会立刻刷新相机） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void SetCameraDistance(float NewDistanceCm);

	/** 立刻恢复所有被近墙剖切隐藏的构件 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void RestoreHiddenOccluders();

	/** 当前被藏起来的构件数量（调试用） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Camera")
	int32 GetHiddenOccluderCount() const { return HiddenOccluders.Num(); }

	/** 调试探针：从相机往注视点用几种查询方式各扫一遍，把结果打进日志（查"为什么没剖切掉"） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void DebugCameraProbe();

	/** 相机状态一览（`Meta.Cam.Info`）：相机/角色坐标、是否在几何体内、被挡方向比例、
	 *  三条规则开关与关键参数、以及**当前被隐藏的构件清单**。
	 *  现场排查"这个物件为什么被藏了/为什么没被藏"时先敲它。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void LogCameraInfo() const;

	/** 包围盒诊断（`Meta.Cam.Bounds [X Y Z]`，省略坐标则用当前相机位置）：
	 *  列出**世界包围盒包含该点**的可见构件，逐个打印 名称/类/Mobility/碰撞/是否已隐藏/
	 *  包围盒尺寸/会不会被剖切规则跳过（含原因）/会不会被包围盒规则隐藏。
	 *
	 *  这是"相机埋在没有碰撞的空心物体里"这类问题的**定位工具**：
	 *  射线与 overlap 都感知不到无碰撞物体，但包围盒能列出来。 */
	void DebugCameraBoundsAt(const FVector& Point);

	/** 调试用：锁定视角水平偏移（`Meta.Cam.Lock <度>`；解锁 `Meta.Cam.Unlock`）。
	 *  锁定后玩家的转视角输入不再改变它 —— 无头/自动化验证里存在"幻影输入"，
	 *  相机会自己飘，"某个机位"因此不可复现（之前的遮挡验证就吃过这个亏）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Camera")
	void SetCameraYawLocked(bool bLocked, float LockedYawDegrees = 0.f);

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Camera")
	bool IsCameraYawLocked() const { return bCameraYawLocked; }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Camera")
	float GetCameraYawOffset() const { return CameraYawOffset; }

	/** 相机当前的世界水平朝向（度）。移动输入要以它为基准换算成世界方向 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Camera")
	float GetCameraWorldYaw() const;

	/** 相机当前的世界坐标（诊断命令 `Meta.Cam.Bounds` 不带参数时用它） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Camera")
	FVector GetCameraWorldLocation() const;

protected:
	UFUNCTION()
	void HandleBodyStageChanged(int32 NewStage);

	/** 每帧对齐精灵朝向/尺寸（锁定视角下等于固定朝向，但仍做成跟随相机，方便调机位） */
	void UpdateSpriteTransform();

	/** 把 Stats 的速度加成同步到 MaxWalkSpeed / MaxFlySpeed。
	 *  用的是**每帧脏检查**而不是事件通知：速度加成变化不一定会触发 Stats 的 OnStatsChanged
	 *  （那是怀疑/躁郁变化的广播），而且角色可能比 Stats 子系统先就绪；脏检查两种情况都能兜住，
	 *  比较一次 float 的开销可以忽略。 */
	void SyncSpeedBonus();

	/** 运行期加载序列帧（找不到就退回阶段单图） */
	/** 出生时若 PlayerStart 贴地/卡进地面，先把角色摆到地面上（否则小胶囊会从地面漏下去） */
	void SettleOnGroundAtSpawn();

	void LoadSequenceFrames();

	/** 每帧推进序列动画（按形态/移动/空中状态选序列与帧） */
	void UpdateSpriteAnimation(float DeltaSeconds);

	/** 当前形态该用哪套帧（3 形态且有飞行帧 = 飞行；否则走路） */
	const TArray<TObjectPtr<UTexture2D>>& GetActiveFrames(bool& bOutWinged) const;

	/** 把某一帧贴到材质上（并记下来供尺寸计算用） */
	void ApplyFrameTexture(UTexture2D* Frame);

	/** 按当前机位设置相机（距离/俯角/水平朝向），换阶段或转视角时调用 */
	void ApplyCameraSettings();

	// ---------------------------------------------------------------
	// 组件
	// ---------------------------------------------------------------
	/** 2D 精灵（Plane + Unlit/Masked 材质） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SpriteQuad;

	/** 相机吊臂：绝对旋转 = 视角锁死 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	// ---------------------------------------------------------------
	// 形象
	// ---------------------------------------------------------------
	/** 阶段1/2/3 的贴图，索引 0/1/2 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metamorphosis|Sprite")
	TArray<TObjectPtr<UTexture2D>> StageTextures;

	/** 精灵材质，需要有一个名为 SpriteTexture 的贴图参数 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metamorphosis|Sprite")
	TObjectPtr<UMaterialInterface> SpriteMaterial;

	/** 阶段1 的精灵世界高度（厘米）。阶段2/3 再乘配置表的体积倍率 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite")
	float Stage1HeightCm = 16.f;

	/** 序列图里"虫子轮廓高度 / 画布高度"的比例。
	 *  美术序列图统一是 384x384 画布、虫子约占 57%，所以四边形实际高度要除以这个比例，
	 *  这样 Stage1HeightCm 表示的仍然是**虫子本体**的高度（旧占位图内容几乎占满画布，比例≈1）。
	 *  美术改画布留白时同步改这一个数即可。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite")
	float SpriteContentHeightRatio = 0.57f;

	// ---------------------------------------------------------------
	// 序列帧动画（美术给的序列图；没有序列时自动退回上面的阶段单图）
	//   策划口径（2026-10-11）：1/2 形态共用「1形态走路」，只有 3 形态带翅膀（「3形态飞行1」）；
	//   二形态的体积照常按阶段倍率放大。
	// ---------------------------------------------------------------
	/** 走路序列帧的资产路径前缀（后面接两位帧号 01..NN）；1/2 形态用 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	FString WalkFramePathPrefix = TEXT("/Game/Characters/Beetle/Sequences/Walk/T_Beetle_Walk_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	int32 WalkFrameCount = 9;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float WalkFps = 10.f;

	/** 站着不动时停在走路序列的第几帧（1 起） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	int32 WalkIdleFrame = 1;

	/** 飞行（带翅膀）序列帧的资产路径前缀；3 形态用 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	FString FlyFramePathPrefix = TEXT("/Game/Characters/Beetle/Sequences/Fly/T_Beetle_Fly_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	int32 FlyFrameCount = 9;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float FlyFps = 14.f;

	/** 1/2 形态待机序列路径前缀（美术「12形态待机」） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	FString Idle12FramePathPrefix = TEXT("/Game/Characters/Beetle/Sequences/Idle12/T_Beetle_Idle12_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	int32 Idle12FrameCount = 4;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float Idle12Fps = 5.f;

	/** 3 形态待机序列路径前缀（美术「3形态待机」，翅膀慢速扇动） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	FString Idle3FramePathPrefix = TEXT("/Game/Characters/Beetle/Sequences/Idle3/T_Beetle_Idle3_");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	int32 Idle3FrameCount = 9;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float Idle3Fps = 8.f;

	/** 3 形态落在地面时停在飞行序列的第几帧（1 起）= 翅膀收拢那一帧（没有待机序列时的兜底） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	int32 FlyGroundedFrame = 5;

	/** 速度超过这个值就算"在移动"（厘米/秒） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float AnimMoveSpeedThreshold = 3.f;

	/** 走路动画的基准速度：实际速率 = 当前速度 / 此值（避免脚步"打滑"），再夹在 0.4~2 倍之间 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float WalkAnimReferenceSpeed = 60.f;

	/** 1/2 形态在空中（没翅膀）时的动画速率倍率：慢一点，像划水 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite|Anim")
	float WinglessAirAnimRate = 0.6f;

	// ---------------------------------------------------------------
	// 体型 / 移动（2026-10-09：整体放大一倍）
	// ---------------------------------------------------------------
	/** 碰撞胶囊半径（厘米） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Body")
	float CapsuleRadiusCm = 5.f;

	/** 碰撞胶囊半高（厘米） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Body")
	float CapsuleHalfHeightCm = 7.f;

	/** 地面移动速度（厘米/秒） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Body")
	float WalkSpeed = 60.f;

	/** 能直接迈上去的最大台阶高度（厘米）。调大就能爬上家具 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Body")
	float MaxStepHeightCm = 2.5f;

	/** 可走坡度上限（度）。户外雪地/起伏地形上，引擎默认的 44.8 度会让虫子「站在地上却走不动」
	 *  （CharacterMovement 只要 CurrentFloor 不是 walkable floor，就拒绝一切水平移动）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Body")
	float WalkableFloorAngleDeg = 60.f;

	/** 地板检测用「平底」而不是胶囊底：小胶囊在起伏地面上更稳 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Body")
	bool bUseFlatBaseForFloorChecks = true;


	/** 精灵始终正对相机 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite")
	bool bFaceCamera = true;

	/** 按移动方向左右镜像，做出 2D 人物的朝向感 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite")
	bool bFlipByMovement = true;

	/** 物品栏（8 格快捷栏的数据与选中格；UI 在 PlayerController 上） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Metamorphosis|Inventory")
	TObjectPtr<class UMetamorphosisInventoryComponent> InventoryComponent;

	/** 交互系统（走近 -> 提示 -> 按 E/点徽标 -> 拾取进背包 或 就地吃掉）。
	 *  参数（交互范围/扫描间隔）在 ini 的 [/Script/Metamorphosis.MetamorphosisInteractionComponent] */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Metamorphosis|Interact")
	TObjectPtr<class UMetamorphosisInteractionComponent> InteractionComponent;

	/** 取交互组件（给 UI/调试用；拿不到返回 nullptr） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	class UMetamorphosisInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	/** 精灵基础缩放符号：引擎 Plane 的 UV 与期望差 180°，所以两轴都取 -1。
	 *  如果以后换成自制的四边形网格，把这两个值改回 1 即可。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite")
	float SpriteBaseSignX = -1.f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Sprite")
	float SpriteBaseSignY = -1.f;

	// ---------------------------------------------------------------
	// 相机（锁定视角 + 可水平旋转）
	//
	// 目标是"尽可能把房屋展示出来"（参考图那种整间房的角落广角）：
	// 距离拉到房间尺度、FOV 放宽、俯角轻微向下；玩家可以用右摇杆/鼠标/QE 绕虫子转。
	// 相机碰撞检测关掉，镜头可以待在房间外，靠墙面的单面材质"看进去"。
	// 代价：阶段1 的虫子只有 8cm，这个取景下屏幕上只有二十来像素 —— 几何上绕不开，
	// 想同时看清虫子和房间，只能把虫子做更大（Stage1HeightCm）或者把距离拉近。
	// 全部可在 Config/DefaultGame.ini 里调，不用重编译。
	// ---------------------------------------------------------------
	/** 相机到角色的距离（厘米）。想看得更全就加大；想看清虫子就减小 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	float CameraDistance = 400.f;

	/** 锁定的俯角（负值 = 俯视），玩家改不了 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	float CameraPitch = -25.f;

	/** 初始水平朝向（度）。玩家转视角是在这个基础上叠加偏移 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	float CameraYaw = 0.f;

	/** 水平转视角速度（度/秒），输入值 1.0 时每秒转这么多 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	float CameraYawSpeed = 120.f;

	/** 相机 FOV（UE 里这是水平 FOV）。房间广角一般 70~90 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	float CameraFOV = 75.f;

	/** 相机注视点相对角色原点的偏移。
	 *  看全屋时把注视点往上抬一点（比如 +60），画面会给房间更多空间、虫子落在下方。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	FVector CameraTargetOffset = FVector(0.f, 0.f, 40.f);

	/** 相机是否做碰撞检测（贴墙时自动收近）。
	 *  关掉：镜头可以待在房间外用广角"看进去"；打开：贴墙时镜头收近甚至怼到虫子上。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera")
	bool bCameraCollisionTest = false;

	// ---------------------------------------------------------------
	// 镜头挡墙处理（解决"镜头卡进墙体里"）
	//
	// 规则（2026-10-10 按反馈收紧）：**只有镜头本身要跟物体重合时才隐藏** ——
	// 在相机位置放一个球做重叠检测，重叠到谁就临时把谁藏起来，离开后延迟恢复。
	//
	// 之前用的是"相机到注视点这条线段穿过谁就藏谁"，而且是包围盒 + 外扩判定，
	// 结果角色贴近某个物体（站到桌子边、贴着墙）时那个物体也会被误判成"挡视线"而消失。
	// 那条规则现在默认关掉；想要"娃娃屋剖切"（把挡在镜头和角色之间的墙也隐掉）
	// 可以打开 bHideSightLineBlockers，它是精确射线判定，不会误伤旁边的物体。
	//
	// 隐藏方式刻意**不动 Visibility、也不动碰撞**：只关三个渲染开关。
	// ---------------------------------------------------------------
	/** 镜头挡墙处理总开关 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	bool bHideCameraBlockers = true;

	/** 镜头"重合半径"（厘米）：相机位置这个球重叠到的构件会被临时隐藏。
	 *  想完全不做就填 0（镜头会直接穿进墙里） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraClearanceRadius = 30.f;

	/** 是否隐藏"挡在镜头和角色之间、把角色挡住"的构件（默认开）。
	 *  判定方式是**按角色可视尺寸取一圈采样点**（中心 + 环），每条都从相机打一条精确射线，
	 *  被挡到至少 OccluderBlockedSampleThreshold 条就把该构件藏起来。
	 *  所以：挡住身体一部分也算挡（2026-10-16 修），但**落在角色旁边/前方的物体不会被误藏**
	 *  —— 因为采样点取在角色**自身可视半径**之内（不含精灵四边形的透明边距）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	bool bHideCharacterBlockers = true;

	/** 判定半径 = 角色可视高度 × 这个系数（厘米）。
	 *  可视高度 = GetSpriteHeightCm()，**已随躁郁值体型阶段放大**（阶段1 16cm → 阶段3 64cm），
	 *  所以判定尺寸会自动跟着体型变大 —— 这是 2026-10-16 那个"体型变大后该隐藏没隐藏"的修法。
	 *  默认 0.32：阶段1 ≈ 5.1cm（正好贴近胶囊半径 5cm），阶段3 ≈ 20.5cm。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CharacterSilhouetteRadiusScale = 0.32f;

	/** 采样环的半径比例（0.8 = 环上采样点落在判定半径的 80% 处）。
	 *  刻意小于 1：这样"紧贴角色身体的家具/墙"（距离≈角色半径）不会被误当成遮挡，
	 *  而"切进身体的墙"仍然会被环上的采样点打到。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CharacterSilhouetteSpread = 0.8f;

	/** 环上采样点数量（不含中心点）。越多越不容易漏，也越费性能；默认 8。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	int32 SilhouetteRingSamples = 8;

	/** 至少被挡住几条采样线才隐藏该构件（默认 1 = 遮住身体一部分就藏）。
	 *  调大可以更保守（例如 3 = 必须明显挡住才藏）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	int32 OccluderBlockedSampleThreshold = 1;

	/** 相机"是否在几何体内部"的小球探测半径（厘米，默认 3；0 = 关掉这项）。
	 *
	 *  为什么需要它：相机钻进**管子/空心物体内部**时，从相机出发的射线是"起点即穿透"，
	 *  规则2（挡住角色）拿不到有效命中；而规则1 的 30cm 球可能**小于管内径**也碰不到管壁
	 *  -> 两条规则一起失效，表现就是"画面被管壁糊住，但那根管子没有被剖开"（2026-10-16 用户报的）。
	 *  这个球直接以相机为球心，能立刻碰到管壁。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraInsideProbeRadius = 3.f;

	/** 相机"被包住"探测距离（厘米，默认 120；0 = 关掉这项）。
	 *  从相机往 6 个轴方向 + 8 个对角方向（共 14 条）打这个长度的短射线，
	 *  被挡方向比例 >= CameraEnclosedBlockedRatio 就判定"相机被包住"，把这些近距命中物按遮挡隐藏。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraEnclosedProbeDistance = 120.f;

	/** "相机被包住"的方向比例阈值（默认 0.75）。
	 *  刻意取高值 + 探测距离取短：相机站在**房间/走廊里**时只有少数方向被挡（地面/个别墙），
	 *  比例达不到阈值，所以不会被误判；而管子内部几乎 14/14 个方向都被挡。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraEnclosedBlockedRatio = 0.75f;

	/** 相机落在某构件的**世界包围盒**内时也隐藏它（默认 true）——这是**无碰撞**遮挡物的唯一兜底。
	 *
	 *  为什么必须有这条（2026-10-16 用户现场定位）：
	 *  相机埋在一件**没有碰撞**的空心物体里时，规则1（30cm 球 overlap）、
	 *  规则1b（3cm 球 + 14 方向短射线）、规则2（相机->角色射线）**全都感知不到它** ——
	 *  射线与 overlap 都要求碰撞，没碰撞就打不到；而画面上它的内壁却糊满整个屏幕。
	 *  现场数据正是这个特征：`隐藏清单=0 / 被挡方向=0/14 / 相机在几何体内=0`。
	 *  包围盒（Bounds）是**渲染**数据，没有碰撞也有，所以它能兜住这种情况。
	 *  纯几何判定、与碰撞无关；被判中的构件仍走 MarkCameraOccluder（吃进入/退出迟滞）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	bool bHideCameraBoundsBlockers = true;

	/** 包围盒规则的**最大**尺寸（厘米，取三轴最大边长；默认 2000，0 = 不限）。
	 *  上限很重要：天空球/远景壳这类**巨型**网格会把相机永远包在里面，
	 *  不加限制就会一直把它们藏掉（天空变黑）。只藏"管子/道具/家具"这个量级的包体。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraBoundsMaxSizeCm = 2000.f;

	/** 包围盒规则的**最小**尺寸（厘米，默认 40）：相机蹭过去的小道具不值得藏 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraBoundsMinSizeCm = 40.f;

	/** 包围盒判定的外扩量（厘米，默认 0）：相机离内壁很近时也算"在里面" */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraBoundsInflateCm = 0.f;

	/** 包围盒候选列表的刷新间隔（秒，默认 1.0）。
	 *  遍历全世界的构件太贵，所以缓存一份"可见构件"清单定时刷新、每帧只做 AABB 判定。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float CameraBoundsCacheSeconds = 1.f;

	/** 只剖 Static Mobility 的构件（默认 true）。
	 *  妻子等角色在运行时被设成 Movable，所以**天然不会被剖**；
	 *  床/墙/地板这类 Static 建筑构件照旧会被剖。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	bool bOnlyCutStaticGeometry = true;

	/** 包围盒规则是否要求"角色不在同一个包围盒里"（默认 **true**）。
	 *  这是**防误判的关键闸门**（2026-10-16 实测数据定的）：
	 *  * 用户报的那根管子（StaticMeshActor_39，646×328×705cm）：相机在里面、角色不在 -> 照常隐藏 ✓
	 *  * 地板/薄板（SM_Plank_set_B）、巨大建筑壳：相机与角色**都**在里面 -> 不藏 ✓
	 *    （不这样就会把地面或整间房藏掉）
	 *  代价：相机与角色**都**钻进同一个空心物体内部时不再兜底（少见的边角情况）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	bool bCameraBoundsRequirePlayerOutside = true;

	/** 带这些标签的 Actor 一律不参与剖切（逗号分隔，默认排除角色与可交互道具）。
	 *  对应场景里妻子替身的 `Meta.Wife`、测试道具的 `Meta.Item:*`。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	FString CutawayExcludeTags = TEXT("Meta.Wife,Meta.Item");

	/** 用哪条碰撞通道扫描（只有调试探针 Meta.Cam.Probe 用它；
	 *  正式判定走"按物体类型查询"，不受关卡碰撞通道设置影响） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	TEnumAsByte<ECollisionChannel> BlockerTraceChannel = ECC_Camera;

	/** 只隐藏带这个组件标签的构件（留空 = 重叠到的都藏，自己除外） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	FName BlockerComponentTag;

	/** 不再遮挡后延迟多久恢复显示（秒），防止在边界上反复闪 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float OccluderRestoreDelay = 0.25f;

	/** 开始遮挡后要**连续**挡多久才真的隐藏（秒），防止单帧命中让家具一闪而没。
	 *  与 OccluderRestoreDelay 一起构成进入/退出迟滞。0 = 命中即隐藏（旧行为）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Camera|Occluder")
	float OccluderHideDelay = 0.12f;

	// ---------------------------------------------------------------
	// 升空
	// 没有模式开关：地面按空格 = 起飞，空中空格上升、X 下降（降到贴地自动落地），
	// 双击空格 = 立刻落地（掉出悬停交给重力）。
	// ---------------------------------------------------------------
	/** 空中移动速度（厘米/秒），水平与升降共用 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Fly")
	float FlySpeed = 80.f;

	/** 双击空格落地时额外往下压的速度（厘米/秒） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Fly")
	float LandDownSpeed = 200.f;

	/** 已经同步到移动组件上的速度倍率（来自 Stats 的道具加成；脏检查用） */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Metamorphosis|Body")
	float AppliedSpeedMultiplier = 1.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMID;

	/** 运行期加载的序列帧 */
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> WalkFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> FlyFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> Idle12Frames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> Idle3Frames;

	/** 当前正在播的动作名（调试显示用） */
	FString CurrentAction;

	/** 当前动作的帧总数（调试显示用） */
	int32 CurrentFrameTotal = 0;

	/** 当前显示的那一帧（尺寸/宽高比按它算） */
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CurrentFrameTexture;

	/** 动画计时与帧号 */
	float AnimTime = 0.f;
	int32 AnimFrameIndex = 0;
	bool bLoggedSequenceFrames = false;

	/** 当前体型阶段（1/2/3） */
	int32 MoodStage = 1;

	/** 当前精灵是否左右镜像 */
	bool bSpriteMirrored = false;

	/** 首帧诊断日志是否已打印 */
	bool bLoggedSpriteDiag = false;

	/** 玩家转视角累计的水平偏移（度） */
	float CameraYawOffset = 0.f;

	/** 每帧处理镜头挡墙：把"镜头要重合"的构件临时藏起来 */
	void UpdateCameraOccluders();

	/** 遮挡判定半径（厘米）：由**角色可视高度**算出来，会随躁郁值体型阶段变化。
	 *  刻意不用精灵四边形的外包尺寸（那含画布透明边距，会把半径放大近一倍）。 */
	float GetSilhouetteRadiusCm() const;

	/** 生成"角色轮廓采样点"：轮廓中心 + 垂直于视线的一圈点。
	 *  UpdateCameraOccluders（判定）与 DebugCameraProbe（探针）共用，保证两边永远一致。 */
	void BuildSilhouetteSamples(const FVector& CameraLocation, TArray<FVector>& OutSamples, FVector& OutCenter, float& OutRadius) const;

	/** 把一个构件记为"要隐藏"（记原状态 + 打时间戳），并立即关掉它的渲染开关。
	 *  Reason/BlockedSamples 只用于日志：排查"是谁、被哪条规则、被几条采样线判成遮挡的"。 */
	void MarkCameraOccluder(class UPrimitiveComponent* Component, float Now, const TCHAR* Reason, int32 BlockedSamples = -1);

	/** 这个构件是否应该被排除在剖切之外（玩家自己/角色/道具/非静态/排除标签）。
	 *  OutReason 非空时写入**中文原因**，供诊断命令打印"为什么没剖它"。 */
	bool ShouldSkipCutaway(class UPrimitiveComponent* Component, FString* OutReason = nullptr);

	/** 包围盒规则的核心判定：相机点是否落在该构件的世界包围盒内（含尺寸闸门与外扩）。
	 *  规则本体与 `Meta.Cam.Bounds` 诊断共用这一个函数，保证两边永远不会不一致。 */
	bool IsCameraInsideComponentBounds(const class UPrimitiveComponent* Component, const FVector& Point) const;

	/** 包围盒候选缓存（每 CameraBoundsCacheSeconds 秒重建一次；用弱引用避免悬空） */
	void RefreshCameraBoundsCandidates();

	/** 视角水平偏移是否被锁死（`Meta.Cam.Lock`；锁死后忽略玩家的转视角输入） */
	bool bCameraYawLocked = false;

	/** 包围盒规则的候选构件（可见、有包围盒、非玩家自己） */
	TArray<TWeakObjectPtr<class UPrimitiveComponent>> CameraBoundsCandidates;

	/** 下次重建候选列表的时间 */
	float NextCameraBoundsRefreshTime = 0.f;

	/** 上次打过日志的候选数量（-1=还没打过）。只在数量变化时打日志，避免每帧刷屏 */
	int32 LastLoggedCameraBoundsCount = -1;

	/** 结束/销毁时把藏起来的构件还原，别把关卡改坏了 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 被临时隐藏的构件状态（TWeakObjectPtr 当键，不用 UPROPERTY 也不会悬空）
	 *
	 *  隐藏方式刻意**不动 Visibility、也不动碰撞**：只关这三个渲染开关。
	 *  这样虫子的移动碰撞、游戏逻辑的射线都不受影响（墙只是不画出来，仍然挡人）。 */
	struct FHiddenOccluderState
	{
		bool bWasRenderInMainPass = true;
		bool bWasRenderInDepthPass = true;
		bool bWasCastShadow = true;

		/** 最近一次被判定为遮挡的时间 */
		float LastBlockedTime = 0.f;
	};

	TMap<TWeakObjectPtr<class UPrimitiveComponent>, FHiddenOccluderState> HiddenOccluders;

	/** 已开始遮挡、但还没连续够 OccluderHideDelay 的构件（进入迟滞用） */
	struct FPendingOccluder
	{
		/** 本段连续遮挡的开始时间 */
		float FirstBlockedTime = 0.f;

		/** 最近一次被命中的时间（用来判断"连续"：中间断了就重新计时） */
		float LastSeenTime = 0.f;
	};

	TMap<TWeakObjectPtr<class UPrimitiveComponent>, FPendingOccluder> PendingOccluders;

	/** 上一次打印过的藏起来数量（只在变化时打日志） */
	int32 LastLoggedOccluderCount = 0;

	/** 这个构件该不该参与剖切（只剖 Static + 按标签排除角色/道具 + 排除玩家自己）。
	 *  统一在 MarkCameraOccluder 入口过滤，保证四条规则口径一致。
	 *  声明见上方"相机"段（带 OutReason 的那个）。 */

	/** CutawayExcludeTags 的解析缓存。
	 *  ini 值在构造函数里读不到（UE 先跑构造函数再套配置），所以第一次用到时懒解析一次。 */
	bool bParsedExcludeTags = false;
	TArray<FName> ParsedExcludeTags;

	/** 相机"被包住"最近一次的探测结果（诊断用：Meta.Cam.Info 会打印） */
	int32 LastEnclosedBlockedDirs = 0;
	int32 LastEnclosedTotalDirs = 0;
	float LastEnclosedBlockedRatio = 0.f;
	bool bLastCameraEnclosed = false;

	/** 是否处于悬停/空中状态（起飞后为 true，落地后为 false） */
	bool bAirborne = false;

	/** 升降输入 */
	float VerticalInput = 0.f;
};













