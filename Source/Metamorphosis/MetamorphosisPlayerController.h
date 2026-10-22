// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "GameFramework/PlayerController.h"
#include "MetamorphosisPlayerController.generated.h"

class UInputAction;
class UUserWidget;
class UMetamorphosisEndingWidget;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  Metamorphosis 玩家控制器。
 *
 *  输入方案（2026-10-09）：
 *    * 移动：WASD / 手柄左摇杆 / 移动端左虚拟摇杆（方向以相机朝向为基准）
 *    * 转视角：Q E、鼠标左右、手柄右摇杆、移动端右虚拟摇杆；
 *      移动端还可以直接**手指在屏幕上左右搓**（触摸拖动，见 PlayerTick 里的 Touch 处理）
 *    * 升空：没有"飞行模式"开关 —— 空格上升/起飞、X 下降、双击空格立刻落地
 *    * 屏幕点击不驱动移动（模板原本的"点哪走哪"已删除），点击留给后续的交互/拾取
 *
 *  所有输入资源都在代码里构建，见 Input/MetamorphosisMoveInput.h。
 */
UCLASS(config = Game)
class AMetamorphosisPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	/** 移动输入（WASD / 手柄摇杆 / 移动端虚拟摇杆） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** 视角水平旋转（鼠标左右 / Q E / 手柄右摇杆 / 移动端右虚拟摇杆） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** 上升 / 起飞（空格；双击 = 落地） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* AscendAction;

	/** 下降（X 键 / 手柄 B；移动端用屏幕「下」按钮） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* DescendAction;

	/** 触摸按住（移动端手指搓屏幕转视角的判定） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* TouchLookAction;

	/** 手指搓屏幕转视角的灵敏度（度/像素） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Touch")
	float TouchLookSensitivity = 0.3f;

	/** 反转手指滑动方向（觉得转向反了就勾上） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Touch")
	bool bInvertTouchLook = false;

	/** 双击空格的判定间隔（秒） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input")
	float DoubleTapLandTime = 0.35f;

	// ---------------- 点击反馈（点击 / 触摸的视觉反馈） ----------------

	/** 是否显示点击/触摸的点击特效 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	bool bShowClickFX = true;

	/** 一次点击特效的显示尺寸（UMG 本地单位） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	float ClickFXSize = 96.f;

	/** 一次播放总时长（秒）。美术规格 500ms */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	float ClickFXDurationSeconds = 0.5f;

	/** 同一位置的限频（秒）：这段时间内同位置不重复播 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	float ClickFXMinIntervalSeconds = 0.12f;

	/** 同时最多几个点击特效（超出先移除最老的） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	int32 ClickFXMaxAlive = 6;

	/** 帧图资源前缀（美术交付：/Game/UI/Click/click_000 ... click_014） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	FString ClickFXFrameFolder = TEXT("/Game/UI/Click/click_");

	/** 帧数（美术交付 15 帧） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Input|Click FX")
	int32 ClickFXFrameCount = 15;

	/** 当前挂着的数值 HUD */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> StatsWidget;

	/** 当前任务 HUD（挂在数值 HUD 正下方） */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> TaskWidget;

	/** 任务 HUD 是否已经按数值 HUD 的实测高度摆好（没量到前用兜底偏移，每帧重试） */
	bool bTaskHUDPlaced = false;
	/** 上次量到的数值 HUD 高度（用来判断要不要重摆） */
	float LastStatsHeight = -1.f;

	/** 周目计数 HUD（挂在数值 HUD 正上方，显示「第 N 周目」） */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> LoopCountWidget;

	/** 周目 HUD 是否已经按数值 HUD 的位置与自身实测高度摆好（没量到前用兜底高度，每帧重试） */
	bool bLoopCountHUDPlaced = false;
	/** 上次量到的周目 HUD 自身高度（用来判断要不要重摆） */
	float LastLoopCountHeight = -1.f;

	/** 移动端上下按钮 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** 开始界面 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MainMenuWidget;

	/** 快捷栏（8 格物品栏） */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HotbarWidget;

	/** 背包界面（书本式道具库，首次打开时创建） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisBackpackWidget> BackpackWidget;

	/** 设置界面 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SettingsWidget;

	/** 游戏内弹窗（暂停菜单） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisPauseMenuWidget> PauseMenuWidget;

	/** 右上角「设置」按钮 */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisSettingsButtonWidget> GameplayMenuButton;

	/** 移动端移动摇杆（纯显示层） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisJoystickWidget> JoystickWidget;

	/** 交互 / 拾取提示（右侧偏中：圆徽标 + 道具图标 + 「拾取 XX」） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisInteractPromptWidget> InteractPromptWidget;

	/** 点击反馈覆盖层（全屏 HitTestInvisible，点击/触摸时在手指位置播 15 帧动效） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisClickFXWidget> ClickFXWidget;

	/** 快捷栏滚轮切换动作 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* CycleSlotAction;

	/** 交互 / 拾取动作（PC = R；移动端点交互提示上的圆徽标） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* InteractAction;

	/** 背包开 / 关动作（PC = B；移动端点快捷栏右侧的「背包」按钮） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* BackpackAction;

public:

	/** Constructor */
	AMetamorphosisPlayerController();

	/** 开局把数值 HUD 挂到视口（类/位置/开关见 Project Settings -> Game -> Metamorphosis UI） */
	virtual void BeginPlay() override;

	/** 每帧处理触摸拖动（手指搓屏幕转视角） */
	virtual void PlayerTick(float DeltaTime) override;

	/** 生成（或重新生成）怀疑值/躁郁值 HUD。控制台 Meta.UI.Show 也是调它 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowStatsHUD();

	/** 移除 HUD */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void HideStatsHUD();

	/** 生成（或重新生成）「当前任务」HUD。位置由 RepositionTaskHUD 按数值 HUD 的实测高度决定 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowTaskHUD();

	/** 移除任务 HUD */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void HideTaskHUD();

	/** 把任务 HUD 摆到数值 HUD 正下方（数值 HUD 换皮变高变矮都不会错位）。
	 *  数值 HUD 的高度要等 Slate 排版完才量得出来，所以 PlayerTick 里会重试到量到为止。 */
	void RepositionTaskHUD();

	/** 生成（或重新生成）「第 N 周目」HUD（挂在数值 HUD 正上方） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowLoopCountHUD();

	/** 移除周目计数 HUD */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void HideLoopCountHUD();

	/** 把周目 HUD 摆到数值 HUD 正上方（需要自身实测高度，所以 PlayerTick 里会重试到量到为止） */
	void RepositionLoopCountHUD();

	/** 手指横向位移 -> 转视角（度）。触摸拖动与调试命令都走这里 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Input")
	void ApplyTouchLookDelta(float DeltaX);

	/** 移动端屏幕按钮：上升按住/松开 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Input")
	void SetAscendButtonHeld(bool bHeld);

	/** 移动端屏幕按钮：下降按住/松开 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Input")
	void SetDescendButtonHeld(bool bHeld);

	/** 生成（或重新生成）移动端上下按钮。控制台 Meta.UI.Mobile 也是调它 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowMobileControls();

	/** 挂开始界面（"开始界面"关卡里由 BeginPlay 调；界面上会提前把游戏资源加载完） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowMainMenu();

	/** 当前存活的开始界面控件（调试命令 Meta.Menu.Press / Meta.Menu.Tree 用） */
	UUserWidget* GetMainMenuWidget() const { return MainMenuWidget; }

	/** 当前存活的通关界面控件（调试命令 Meta.Ending.Press / Meta.Ending.Tree 用） */
	UMetamorphosisEndingWidget* GetWinEndingWidget() const { return WinEndingWidget; }

	/** 当前存活的设置界面控件（调试命令 Meta.Settings.Dump / Grab / Toggle 用） */
	class UMetamorphosisSettingsWidget* GetSettingsWidget() const;

	/** 挂快捷栏（8 格物品栏） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowHotbar();

	/** 打开背包界面（书本式道具库） */
	void ShowBackpack();

	/** 收起背包界面 */
	void HideBackpack();

	/** 背包开着就关、没开就开（B 键 / 快捷栏背包按钮 / 关闭按钮都走它） */
	void ToggleBackpack();

	/** 背包是否开着（输入屏蔽与调试命令用） */
	bool IsBackpackOpen() const { return bBackpackOpen; }

	/** 调试：选中背包里的第 Index 格并刷新详情（0 起，越界返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Backpack")
	bool BagSelect(int32 Index);

	/** 调试：对背包当前查看的格子执行 使用/查看 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Backpack")
	bool BagUse();

	/** 调试：对背包当前查看的格子执行 丢弃 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Backpack")
	bool BagDrop();

	/** 生成点击反馈覆盖层控件（全屏 HitTestInvisible，不挡输入） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowClickFX();

	/** 移除点击反馈覆盖层 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void HideClickFX();

	/**
	 * 在**视口像素**坐标处播一次点击特效（触摸与鼠标两条链路都走这里）。
	 * 内部会换算成 UMG 本地坐标（本工程 1.5 倍 DPI：视口像素 ≠ UMG 单位）。
	 * 过场/背包/暂停/结算期间不播；同位置限频、并发上限由控件内部处理。
	 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	bool PlayClickFX(float ViewportX, float ViewportY);

	/** 当前正在播的点击特效数量（调试用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	int32 GetClickFXAliveCount() const;

	/** 清掉正在播的点击特效（控件保留，下一帧还能继续播） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ClearClickFX();

	/** 实际载入到的帧数（素材没导入时为 0） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	int32 GetClickFXFrameCount() const;

	float GetClickFXSize() const { return ClickFXSize; }
	float GetClickFXDurationSeconds() const { return ClickFXDurationSeconds; }
	int32 GetClickFXMaxAlive() const { return ClickFXMaxAlive; }

	/** 挂设置界面（开始界面里点「设置」进来的） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowSettings();

	/** 关掉设置界面（返回开始界面） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void HideSettings();

	/** 设置界面返回按钮的回调（也用于无头验证：Meta.UI.SettingsBack） */
	void HandleSettingsBackRequested();

	// ---------------- 游戏内弹窗（暂停菜单） ----------------

	/** 挂右上角"设置"按钮（只有游戏关卡里才有） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowGameplayMenuButton();

	/** 打开弹窗并时停 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void OpenPauseMenu();

	/** 关掉弹窗并恢复游戏 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ClosePauseMenu();

	/** 是否被这个弹窗暂停着 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|UI")
	bool IsGameplayPaused() const { return bGameplayPaused; }

	/** 时停 / 恢复 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void SetGameplayPaused(bool bPaused);

	// ---------------- 周目结算（被捉 / 入夜） ----------------
	// 由 UMetamorphosisLoopSubsystem 驱动：它负责四阶段流程，控制器只负责
	// "冻结输入" 与 "显示/收起反馈 UI"，避免两边各持一份状态。

	/** 是否正在周目结算中（结算中所有游戏输入都应早退） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	bool IsInSettlement() const { return bInSettlement; }

	/** 进入/退出"结算中"：进入时清掉升降输入并停住角色，退出时什么都不用补 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void SetSettlementActive(bool bActive);

	/** 显示结算反馈（暗化遮罩 + 大字），由周目结算子系统在"反馈"阶段调用 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void ShowSettlementFeedback(const FText& Title, const FText& Subtitle, float MaxOpacity, float FadeInSeconds);

	/** 收起结算反馈 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void HideSettlementFeedback();

	/** 结算界面的「等待玩家选择」输入态：true = 显示鼠标 + GameAndUI（按钮可点），false = 恢复 GameOnly。
	 *  2026-10-19：反馈播完后停在 AwaitChoice 阶段，必须把输入交给 UI，否则按钮点不到 ✗。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void SetSettlementChoiceInput(bool bEnable);

	/** 结算界面按钮回调（绑定到控件上的两个动态委托） */
	UFUNCTION()
	void HandleSettlementRestartRequested();

	UFUNCTION()
	void HandleSettlementReturnToTitleRequested();

	// ------------------------------------------------------------------
	// 通关界面（2026-10-18：从门缝出去 = 直接通关）
	// 终局界面：不自动消失，玩家在界面上选「重新开始」/「返回标题」。
	// ------------------------------------------------------------------

	/** 显示通关界面：隐藏其它 HUD + 冻结输入（bFreezeGame 时暂停游戏，冻结时钟/妻子/玩法门） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void ShowWinEnding(const FText& Title, const FText& Subtitle, const FText& StatsLine, bool bFreezeGame);

	/** 收起通关界面并恢复运行（"已通关"标记由 LoopSubsystem 负责清） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void HideWinEnding();

	/** 通关界面的「返回标题」：清掉"已进入游戏"标志 + 回到开始界面（游戏保持暂停，避免菜单背后被追） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void ReturnToTitleFromEnding();

	/** 回到主界面 1 秒后的自检日志（剧情/结算/通关界面是否残留） */
	void LogMainMenuSelfCheck();

	FTimerHandle MainMenuSelfCheckTimer;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Loop")
	bool IsWinEndingVisible() const { return bWinEndingVisible; }

	/** 设置界面的「返回主界面」：先存档 -> 关设置 -> 清 bEntered -> 挂开始界面（游戏保持暂停） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Save")
	void ReturnToTitleFromSettings();

	/** 设置界面的「下一周目」：手动跳到下一个周目（完整重置，不播被捉反馈），然后关设置回游戏 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Loop")
	void HandleSettingsNextLoop();

	/** 从主界面**回到游戏**时调用（「开始游戏」/「继续游戏」都要走）：
	 *  解除"菜单背后的暂停" + 输入模式回 GameOnly + 隐藏鼠标 + 收起开始界面。
	 *  ⚠️ 踩过的坑：返回主界面时为了"菜单背后不跑时钟"把游戏暂停了，但重新进关卡时如果不成对恢复，
	 *     剧情子系统会认为"游戏暂停中"而**忽略所有推进**（点屏幕不换句），表现就是"剧情卡住"。 */
	void ExitFrontendMode(const TCHAR* Reason);

	/** 公开的物品栏访问（存档子系统需要它；内部就是 GetInventory()，组件挂在 Beetle 角色上） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Inventory")
	class UMetamorphosisInventoryComponent* GetInventoryComponent() const;

	/** 输入是否应当被完全屏蔽（暂停 / 结算中 / 剧情过场中 / 引擎暂停）。所有输入入口都用它早退 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Input")
	bool IsInputBlocked() const;

	// ---------------- 剧情过场（全黑 + 文字 + 分支按钮） ----------------

	/** 显示剧情过场：挂控件 + 隐藏其它 HUD + 屏蔽游戏输入 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	void ShowStoryOverlay();

	/** 收起剧情过场：移除控件 + 恢复其它 HUD + 恢复输入 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	void HideStoryOverlay();

	/** 剧情是否正在播放（播放中所有游戏输入早退） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool IsStoryPlaying() const { return bStoryPlaying; }

	/** 统一开关"游戏内 HUD"（过场期间关掉；恢复时按 UISettings 重新挂一遍）
	 *  bKeepBackpack：隐藏其它 HUD 但**保留背包**（背包自己就是被打开的那个界面，不能被一起摘掉） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void SetGameplayHUDVisible(bool bVisible, bool bKeepBackpack = false);

	/** 挂移动端移动摇杆（自己做的，支持固定/浮动 + 大小） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowVirtualJoystick();

	/** 挂交互 / 拾取提示（走近可交互物时显示「拾取 XX」/「吃掉 XX」） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void ShowInteractPrompt();

	/**
	 * 当前是不是「移动端 UI 模式」—— UI 用它决定用哪套外观（例如交互提示：PC = R 键小徽标，
	 * 移动端 = 大圆按钮 + 圆内图标）。
	 *
	 * 判据**只有这一处**，别在别的地方再写一套平台判断：
	 *   * 真机（Android / iOS）永远算移动端；
	 *   * PC 上默认是键盘鼠标外观，用控制台 `Meta.UI.Mobile` 可以**强制切成移动端外观**
	 *     （方便调试与截图），`Meta.UI.Desktop` 切回来。
	 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|UI")
	bool IsMobileUI() const;

	/** 强制 / 取消「移动端 UI 模式」（PC 上调试与截图用；真机上不影响结果） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void SetForceMobileUI(bool bForce);

	/** 交互 / 拾取：按 E 或点交互提示上的圆徽标都走这里（转给主角身上的交互组件） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Interact")
	bool TryInteract();

	/** 取当前主角身上的交互组件（可能为空） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	class UMetamorphosisInteractionComponent* GetInteraction() const;

	/** 记录"刚按下交互键"（剧情玩法门 GateKind=Interact 用；真实按键与调试命令都走这里） */
	void NotifyInteractInput() { LastInteractInputTime = FPlatformTime::Seconds(); }

	/** 最近一次交互输入的时刻（FPlatformTime::Seconds；从没按过时是很小的值） */
	double GetLastInteractInputTime() const { return LastInteractInputTime; }

	/** 选中快捷栏第 Index 格（0 起）；数字键与 UI 点击都走这里 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void SelectHotbarSlot(int32 Index);

	// ---------------- 移动端摇杆参数 ----------------

	/** 摇杆底盘基准半径（像素，实际乘设置里的倍率） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickRadiusPx = 120.f;

	/** 摇杆头基准半径（像素） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickKnobRadiusPx = 52.f;

	/** 死亡区（占半径比例） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickDeadZone = 0.15f;

	/** 固定模式下距屏幕左边 / 下边的边距（像素） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickLeftMargin = 70.f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickBottomMargin = 80.f;

	/** 触摸分区：屏幕左半边是摇杆、右半边是转视角（0.5 = 正中分界） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float TouchJoystickScreenRatio = 0.5f;

protected:

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;

	/** 移动输入：WASD / 摇杆，每帧触发；Completed/Canceled 再补一帧零值，保证松开即停 */
	void OnMoveTriggered(const struct FInputActionValue& Value);

	/** 视角水平旋转（摇杆/键鼠的"速率"式输入） */
	void OnLookTriggered(const struct FInputActionValue& Value);

	/** 上升键按下（含双击判定 = 立刻落地）/ 松开 */
	void OnAscendStarted();
	void OnAscendCompleted();

	/** 下降键（X）按下 / 松开 */
	void OnDescendStarted();
	void OnDescendCompleted();

	/** 快捷栏：滚轮/手柄左右方向键切换（值 > 0 往右一格） */
	void OnCycleHotbarSlot(const struct FInputActionValue& Value);

	/** 交互键（E）按下 */
	void OnInteractStarted();

	/** B 键：开/关背包 */
	void OnBackpackStarted();

	/** 取当前 Pawn 上的物品栏组件（可能为空） */
	class UMetamorphosisInventoryComponent* GetInventory() const;

	/** 把一份移动轴应用到角色（键盘/手柄输入与虚拟摇杆共用） */
	void ApplyMoveAxis(const FVector2D& AxisValue);

	/** 每帧处理触摸：按屏幕左右分区分配给"摇杆"或"转视角" */
	void UpdateTouchInput();

	/** 摇杆是否正在被操控（操控时移动以摇杆为准） */
	bool bJoystickActive = false;

	/** 摇杆屏幕位置（像素）：底盘中心 / 当前手指位置 */
	FVector2D JoystickCenterPx = FVector2D::ZeroVector;
	FVector2D JoystickCurrentPx = FVector2D::ZeroVector;

	/** 摇杆算出来的移动轴（X 右 / Y 前） */
	FVector2D VirtualMoveAxis = FVector2D::ZeroVector;

	/** 触摸角色分配：每个手指一个角色 */
	enum class ETouchRole : uint8
	{
		None,
		Joystick,
		Look
	};

	static constexpr int32 MaxTrackedTouches = 4;
	ETouchRole TouchRoles[MaxTrackedTouches] = { ETouchRole::None, ETouchRole::None, ETouchRole::None, ETouchRole::None };
	bool TouchWasPressed[MaxTrackedTouches] = { false, false, false, false };

	/** 这根手指是不是被 UI（快捷栏/按钮/弹窗…）认领了 */
	bool TouchClaimedByUI[MaxTrackedTouches] = { false, false, false, false };

	FVector2D LastLookPosition = FVector2D::ZeroVector;

public:
	/** UI 认领了这根手指：摇杆与转视角都不许抢（点按钮时人物不该跟着跑） */
	void NotifyUITouchClaimed(int32 Finger);
	void NotifyUITouchReleased(int32 Finger);

	/** 摇杆触发区：以摇杆圆为基准的半径倍率（固定模式）。<=1 表示只在圆内 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickTouchZoneScale = 2.0f;

	/** 浮动模式允许起摇杆的范围：屏幕下方这个比例之上（0.55 = 屏幕下面 55% 的左半屏） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Joystick")
	float JoystickFloatingZoneHeightRatio = 0.55f;

protected:

	/** 触摸位移（像素）-> 移动轴（换算到与摇杆圆同一尺度） */
	FVector2D ComputeJoystickAxis(const FVector2D& DeltaPx) const;

	/** UI 触摸坐标 -> 屏幕像素的缩放（用摇杆控件的几何缩放，保证跟画出来的圆一致） */
	float GetUITouchScale() const;

	/** 固定模式下底盘中心的屏幕像素坐标 */
	FVector2D GetFixedJoystickCenter() const;


	/** 设置变化：即时刷新快捷栏 / 移动端按钮的缩放 */
	void HandleSettingsChanged();

	/** 弹窗选项被点（Resume / OpenSettings / 以后扩展的 Tag） */
	void HandlePauseMenuOption(FName Tag);

	/** 右上角设置按钮被点 */
	void HandleGameplayMenuButtonClicked();

	/** 是否处于「弹窗暂停」状态 */
	bool bGameplayPaused = false;

	/** 背包是否开着（开着时屏蔽游戏输入，但**不**暂停游戏） */
	bool bBackpackOpen = false;

	/** 鼠标左键上一帧是否按着（只在"按下那一帧"播点击反馈，按住不刷屏） */
	bool bWasMouseDown = false;

	/** 是否处于「周目结算中」（结算期间所有游戏输入都早退，反馈 UI 由结算子系统驱动） */
	bool bInSettlement = false;

	/** 是否处于「剧情过场中」（过场期间隐藏所有其它 HUD 并屏蔽游戏输入） */
	bool bStoryPlaying = false;

	/** 剧情过场控件（见 ShowStoryOverlay） */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> StoryWidget;

	/** 最近一次交互输入时刻（剧情玩法门 Interact 判定用） */
	double LastInteractInputTime = -1000.0;

	/** 剧情节点变化回调（订阅剧情子系统；播放中挂 UI，结束时收起并恢复 HUD） */
	UFUNCTION()
	void HandleStoryChanged(bool bPlaying, FName NodeId, const FText& Speaker, const FText& Text, bool bHasChoice);

	/** 结算反馈控件（首次显示时再创建，见 ShowSettlementFeedback） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisSettlementWidget> SettlementWidget;

	/** 通关界面控件（首次通关时再创建，见 ShowWinEnding） */
	UPROPERTY(Transient)
	TObjectPtr<class UMetamorphosisEndingWidget> WinEndingWidget;

	/** 通关界面是否正在显示（Esc / 齿轮要忽略） */
	bool bWinEndingVisible = false;

	/** PC 上被 `Meta.UI.Mobile` 强制切到移动端外观（真机上无所谓，见 IsMobileUI） */
	bool bForceMobileUI = false;

	/** 打开设置前弹窗是不是开着（决定返回时回哪儿） */
	bool bSettingsOpenedFromPauseMenu = false;

	/** 这次设置界面是"我们自己为了它而暂停"的（游戏内点开设置时若还没暂停，就主动暂停；
	 *  关闭设置时只恢复我们自己暂停的那次，避免把弹窗/结算的暂停误解除） */
	bool bSettingsPausedGame = false;

public:
	/** 调试用：把摇杆摆到"固定中心 + 给定位移"的位置（验证显示与移动，不需要真手指） */
	void DebugSimulateJoystick(const FVector2D& DeltaPx);

	/** 调试用：等价于点弹窗里的某个选项（Resume / OpenSettings / 以后的 Tag） */
	void DebugChoosePauseOption(FName Tag) { HandlePauseMenuOption(Tag); }

	/** 触摸按住 / 松开（移动端搓屏幕） */
	void OnTouchLookStarted();
	void OnTouchLookCompleted();

	/** 把"上升/下降"两个按键状态合成为升降输入发给角色 */
	void PushVerticalInput();

	/** 取当前受控的虫形主角（可能为空） */
	class AMetamorphosisBeetleCharacter* GetBeetle() const;

private:

	/** 上升键是否按住（键盘/手柄） */
	bool bAscendHeld = false;

	/** 下降键（X / 手柄 B）是否按住 */
	bool bDescendHeld = false;

	/** 移动端上升按钮是否按住 */
	bool bAscendButtonHeld = false;

	/** 移动端下降按钮是否按住 */
	bool bDescendButtonHeld = false;

	/** 处理一次"上升键按下"（含双击落地判定），键盘与屏幕按钮共用 */
	void HandleAscendPress();

	/** 上一次按空格的时间（双击判定） */
	double LastAscendPressTime = -1.0;

	/** 手指是否正在搓屏幕 */
	bool bTouchLookActive = false;

	/** 上一次读到的触摸位置 */
	FVector2D LastTouchLocation = FVector2D::ZeroVector;
};







