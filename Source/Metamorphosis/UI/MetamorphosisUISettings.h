// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 界面入口设置：出现在 Project Settings -> Game -> Metamorphosis UI。
// 换 UI 的正规流程（不用改代码）：
//   1. 美术/策划做 WBP_StatsHUD（父类选 MetamorphosisStatsWidget），在 Details 里换贴图/字体/颜色；
//   2. Project Settings -> Game -> Metamorphosis UI -> Stats Widget Class 指到 WBP_StatsHUD；
//   3. 重启游戏。运行时也可用控制台 Meta.UI.Show 立刻重建。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Templates/SubclassOf.h"
#include "MetamorphosisUISettings.generated.h"

class UUserWidget;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Metamorphosis UI"))
class METAMORPHOSIS_API UMetamorphosisUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetamorphosisUISettings();

	/** 是否在游戏里自动显示怀疑值/躁郁值 HUD */
	UPROPERTY(config, EditAnywhere, Category = "HUD")
	bool bShowStatsHUD = true;

	/** 要生成的 HUD 控件类；留空则用 C++ 自带的 UMetamorphosisStatsWidget 默认样式 */
	UPROPERTY(config, EditAnywhere, Category = "HUD", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> StatsWidgetClass;

	/** HUD 左上角位置（Slate 单位） */
	UPROPERTY(config, EditAnywhere, Category = "HUD")
	FVector2D StatsHUDPosition = FVector2D(36.f, 36.f);

	/** HUD 的 ZOrder */
	UPROPERTY(config, EditAnywhere, Category = "HUD")
	int32 StatsHUDZOrder = 10;

	// ---------------- 当前任务 HUD（挂在数值 HUD 正下方） ----------------

	/** 是否显示「当前任务」HUD */
	UPROPERTY(config, EditAnywhere, Category = "Task HUD")
	bool bShowTaskHUD = true;

	/** 任务 HUD 控件类；留空则用 C++ 自带的 UMetamorphosisTaskWidget 默认样式 */
	UPROPERTY(config, EditAnywhere, Category = "Task HUD", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> TaskWidgetClass;

	/** 任务 HUD 距数值 HUD 底部的间距（Slate 单位）。X 不用管：左边距始终与数值 HUD 对齐 */
	UPROPERTY(config, EditAnywhere, Category = "Task HUD")
	float TaskHUDGap = 8.f;

	/** 任务 HUD 的 ZOrder（与数值 HUD 同层即可） */
	UPROPERTY(config, EditAnywhere, Category = "Task HUD")
	int32 TaskHUDZOrder = 11;

	/** 数值 HUD 高度还没量出来时的兜底 Y 偏移（正常情况下用实测高度，不会用到它） */
	UPROPERTY(config, EditAnywhere, Category = "Task HUD")
	float TaskHUDFallbackOffsetY = 190.f;

	/** 数值 HUD 的"约定高度"（Slate 单位）：>0 时优先用它来摆下方 HUD，
	 *  避免"视口控件在 Slate 排版完成前量到 0 高度"导致任务条压到数值条上。
	 *  交付 2 的三值 HUD = 设计高 289 / 设计宽 1118 × 屏宽 420 ≈ 109；改屏宽要同步改这里。 */
	UPROPERTY(config, EditAnywhere, Category = "Task HUD")
	float StatsHUDFixedHeight = 0.f;

	// ---------------- 移动端屏幕按钮（上升 / 下降） ----------------

	/** 是否显示移动端上下按钮（放在原来右虚拟摇杆的位置）。PC 上想隐藏就关掉 */
	UPROPERTY(config, EditAnywhere, Category = "Mobile")
	bool bShowMobileControls = true;

	/** 按钮控件类；留空则用 C++ 自带的默认样式 */
	UPROPERTY(config, EditAnywhere, Category = "Mobile", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> MobileControlsClass;

	/** 按钮组距屏幕右下角的边距（Slate 单位） */
	UPROPERTY(config, EditAnywhere, Category = "Mobile")
	FVector2D MobileControlsMargin = FVector2D(70.f, 70.f);

	/** 按钮组的 ZOrder（要比数值 HUD 高，否则会被压住） */
	UPROPERTY(config, EditAnywhere, Category = "Mobile")
	int32 MobileControlsZOrder = 20;

	// ---------------- 开始界面 ----------------

	/** 开始界面控件类；留空则用 C++ 自带默认样式（纯白底 + 标题 + 进度条 + 开始按钮） */
	UPROPERTY(config, EditAnywhere, Category = "Main Menu", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> MainMenuClass;

	/** 开始界面的 ZOrder */
	UPROPERTY(config, EditAnywhere, Category = "Main Menu")
	int32 MainMenuZOrder = 100;

	// ---------------- 快捷栏（8 格物品栏） ----------------

	/** 是否显示快捷栏 */
	UPROPERTY(config, EditAnywhere, Category = "Hotbar")
	bool bShowHotbar = true;

	/** 快捷栏控件类；留空则用 C++ 自带样式（屏幕底部居中，MC 风格） */
	UPROPERTY(config, EditAnywhere, Category = "Hotbar", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> HotbarClass;

	/** 快捷栏 ZOrder（比数值 HUD 高、比开始界面低） */
	UPROPERTY(config, EditAnywhere, Category = "Hotbar")
	int32 HotbarZOrder = 15;

	// ---------------- 设置界面 ----------------

	/** 设置界面控件类；留空则用 C++ 自带默认样式 */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> SettingsClass;

	UPROPERTY(config, EditAnywhere, Category = "Settings")
	int32 SettingsZOrder = 260;     // 必须在暂停弹窗(250)之上：从弹窗进设置时不能被弹窗盖住

	// ---------------- 移动端移动摇杆（自己做的） ----------------

	/** 是否启用自带的移动摇杆（引擎那套触摸界面已在 DefaultInput.ini 里关掉） */
	UPROPERTY(config, EditAnywhere, Category = "Joystick")
	bool bShowVirtualJoystick = true;

	/** 摇杆控件类；留空则用 C++ 自带样式（两个实心圆） */
	UPROPERTY(config, EditAnywhere, Category = "Joystick", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> JoystickClass;

	UPROPERTY(config, EditAnywhere, Category = "Joystick")
	int32 JoystickZOrder = 12;

	// ---------------- 游戏内弹窗（暂停菜单）与右上角设置按钮 ----------------

	/** 是否显示右上角「设置」按钮 */
	UPROPERTY(config, EditAnywhere, Category = "Pause Menu")
	bool bShowGameplayMenuButton = true;

	/** 右上角按钮控件类；留空用 C++ 自带样式（引擎齿轮图标 + 圆底） */
	UPROPERTY(config, EditAnywhere, Category = "Pause Menu", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> GameplayMenuButtonClass;

	UPROPERTY(config, EditAnywhere, Category = "Pause Menu")
	int32 GameplayMenuButtonZOrder = 40;

	/** 按钮距右上角的边距（X=右，Y=上） */
	UPROPERTY(config, EditAnywhere, Category = "Pause Menu")
	FVector2D GameplayMenuButtonMargin = FVector2D(26.f, 26.f);

	/** 弹窗控件类；留空用 C++ 自带样式 */
	UPROPERTY(config, EditAnywhere, Category = "Pause Menu", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> PauseMenuClass;

	/** 弹窗 ZOrder（要盖住 HUD/快捷栏/摇杆【剧情过场】，但低于设置界面） */
	UPROPERTY(config, EditAnywhere, Category = "Pause Menu")
	int32 PauseMenuZOrder = 250;   // 2026-10-17：提到剧情过场(200)之上，否则过场里按 Esc 弹窗看不见

	// ---------------- 交互 / 拾取提示（仿原神：右侧偏中） ----------------

	/** 是否显示交互提示（走近可交互物时出现「拾取 XX」/「吃掉 XX」） */
	UPROPERTY(config, EditAnywhere, Category = "Interact")
	bool bShowInteractPrompt = true;

	/** 交互提示控件类；留空用 C++ 自带样式（圆徽标 + 道具图标 + 文字） */
	UPROPERTY(config, EditAnywhere, Category = "Interact", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> InteractPromptClass;

	/** 交互提示 ZOrder（比摇杆高一点，别被摇杆压住） */
	UPROPERTY(config, EditAnywhere, Category = "Interact")
	int32 InteractPromptZOrder = 18;

	// ---------------- 周目计数 HUD（挂在数值 HUD 上方） ----------------

	/** 是否显示「第 N 周目」（周目号 = 周目结算子系统已结算次数 + 1，开局为第 1 周目） */
	UPROPERTY(config, EditAnywhere, Category = "Loop Count HUD")
	bool bShowLoopCount = true;

	/** 周目 HUD 控件类；留空则用 C++ 自带的 UMetamorphosisLoopCountWidget 默认样式 */
	UPROPERTY(config, EditAnywhere, Category = "Loop Count HUD", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> LoopCountWidgetClass;

	/** 周目 HUD 距数值 HUD 顶部的间距（Slate 单位）。X 不用管：左边距始终与数值 HUD 对齐 */
	UPROPERTY(config, EditAnywhere, Category = "Loop Count HUD")
	float LoopCountHUDGap = 8.f;

	/** 周目 HUD 的 ZOrder（与数值 HUD 同层即可） */
	UPROPERTY(config, EditAnywhere, Category = "Loop Count HUD")
	int32 LoopCountHUDZOrder = 11;

	/** 自身高度还没量出来时的兜底高度（正常情况下用实测高度，不会用到它） */
	UPROPERTY(config, EditAnywhere, Category = "Loop Count HUD")
	float LoopCountHUDFallbackHeight = 30.f;

	// ---------------- 周目结算反馈（被捉 / 入夜：暗化 + 大字） ----------------

	/** 是否显示周目结算反馈（关掉就只剩日志，玩家看不到任何提示） */
	UPROPERTY(config, EditAnywhere, Category = "Settlement")
	bool bShowSettlementFeedback = true;

	/** 结算反馈控件类；留空用 C++ 自带样式（全屏暗化 + 居中大字 + 一行小字） */
	UPROPERTY(config, EditAnywhere, Category = "Settlement", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> SettlementWidgetClass;

	/** 结算反馈 ZOrder（要盖住 HUD/快捷栏/摇杆/交互提示，但低于暂停弹窗） */
	UPROPERTY(config, EditAnywhere, Category = "Settlement")
	int32 SettlementZOrder = 80;

	// ---------------- 通关界面（2026-10-18：从门缝出去 = 直接通关） ----------------

	/** 是否显示通关界面（关掉就只剩日志，玩家看不到"你赢了"） */
	UPROPERTY(config, EditAnywhere, Category = "Ending")
	bool bShowWinEnding = true;

	/** 通关界面控件类；留空用 C++ 自带样式（全屏黑幕 + 大字「通关」+ 副标题 + 数据行 + 两个按钮） */
	UPROPERTY(config, EditAnywhere, Category = "Ending", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> WinEndingClass;

	/** 通关界面 ZOrder：**最上层**（终局界面，要盖住剧情过场 200 / 暂停 250 / 设置 260） */
	UPROPERTY(config, EditAnywhere, Category = "Ending")
	int32 WinEndingZOrder = 300;

	// ---------------- 剧情过场（全黑 + 文字 + 分支按钮） ----------------

	/** 是否启用剧情过场控件（关掉就只有剧情子系统日志，玩家看不到） */
	UPROPERTY(config, EditAnywhere, Category = "Story")
	bool bShowStory = true;

	/** 剧情过场控件类；留空用 C++ 自带样式（全屏纯黑 + 居中文字 + 两个选项按钮 + 右上角跳过） */
	UPROPERTY(config, EditAnywhere, Category = "Story", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> StoryWidgetClass;

	/** 剧情过场 ZOrder（必须盖住所有 HUD / 摇杆 / 快捷栏 / 结算反馈） */
	UPROPERTY(config, EditAnywhere, Category = "Story")
	int32 StoryZOrder = 200;

	// ---------------- 背包（书本式道具库） ----------------

	/** 是否允许打开背包（关掉后 B 键与快捷栏背包按钮都不响应） */
	UPROPERTY(config, EditAnywhere, Category = "Backpack")
	bool bShowBackpack = true;

	/** 背包控件类；留空用 C++ 自带样式（书本式道具库：左页 8 格 + 右页详情） */
	UPROPERTY(config, EditAnywhere, Category = "Backpack", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> BackpackClass;

	/** 背包 ZOrder（要盖住 HUD / 快捷栏 / 摇杆，但低于设置界面与剧情过场） */
	UPROPERTY(config, EditAnywhere, Category = "Backpack")
	int32 BackpackZOrder = 60;

	// ---------------- 点击反馈（点击 / 触摸的视觉反馈） ----------------

	/** 是否显示点击/触摸的点击特效（美术交付的 stylized_click_v2：15 帧一次 500ms） */
	UPROPERTY(config, EditAnywhere, Category = "Click FX")
	bool bShowClickFX = true;

	/** 点击特效控件类；留空用 C++ 自带样式（/Game/UI/Click 逐帧播放） */
	UPROPERTY(config, EditAnywhere, Category = "Click FX", meta = (MetaClass = "/Script/UMG.UserWidget"))
	TSubclassOf<UUserWidget> ClickFXClass;

	/** 点击特效 ZOrder：在 HUD / 快捷栏 / 摇杆之上，在背包 / 暂停 / 剧情过场之下 */
	UPROPERTY(config, EditAnywhere, Category = "Click FX")
	int32 ClickFXZOrder = 30;
};

