// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 当前任务 HUD：显示"当前任务"标题 + 补充说明，位置在左上角数值 HUD（怀疑值/躁郁值）的正下方。
//
// 换皮设计（与 MetamorphosisStatsWidget 一致，美术换贴图不用改代码）：
//   * 颜色：PanelBackgroundColor / LabelColor / TitleColor / DetailColor / AccentColor
//   * 字体：LabelFont / TitleFont / DetailFont
//   * 文案：HeaderText（默认「当前任务」）、AllDoneText（全部完成时显示）
//   * 布局：PanelWidth / PanelPadding / RowSpacing / AccentWidth
//
// 定位：本控件**不自带屏幕坐标** —— 由 PlayerController 读数值 HUD 的实测高度后摆在它下面
//      （见 AMetamorphosisPlayerController::RepositionTaskHUD），这样数值 HUD 换皮变高变矮都不会错位。
//
// 数据：订阅 UMetamorphosisTaskSubsystem::OnTaskChanged，带脏检查（不每帧刷文本）。
// 纯显示：根节点 SelfHitTestInvisible，绝不挡输入。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "MetamorphosisTaskWidget.generated.h"

class UBorder;
class USizeBox;
class UTextBlock;
class UVerticalBox;
struct FTaskConfigEntry;

UCLASS(Blueprintable, Config = Game)
class METAMORPHOSIS_API UMetamorphosisTaskWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisTaskWidget(const FObjectInitializer& ObjectInitializer);

	// UUserWidget
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 定时刷新：玩法门的倒计时是"随时间变化"的文字，只靠 OnTaskChanged 委托驱动会永远不更新
	 *  （门激活时不会触发任务变更事件 -> 目标文字与倒计时不出现 / 不递减）。
	 *  内部做限频 + 脏检查，没变化时不碰文本，开销可以忽略。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 立刻按当前任务刷新一次（正常由任务系统委托驱动） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void RefreshTask();

	/** 任务刷新后的蓝图事件：换皮后要做"划掉/打勾/闪光"动效挂这里 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Metamorphosis|UI")
	void OnTaskUpdated(const FText& Title, const FText& Detail, bool bAllCompleted);

	/** 面板实测宽度（供 PlayerController 对齐左边距用）—— 已含 HudScale 与屏宽上限钳制 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|UI")
	float GetPanelWidth() const { return CurrentPanelWidth; }

protected:
	UFUNCTION()
	void HandleTaskChanged(FName TaskId, int32 TaskIndex, const FText& Title);

	/** 纯 C++ 默认布局（缩进一点，避免 unity build 撞名：本工程的 UI 控件里已有 BuildDefaultLayout） */
	void BuildTaskLayout();
	void ApplyTaskStaticStyle();

	// ------------------------------------------------------------------
	// 布局
	// ------------------------------------------------------------------
	/** 整体缩放：用户反馈"任务 HUD 有点不明显了" -> 默认放大到 1.5 倍。
	 *  改这一个值就能整体调大小（PanelWidth / 内边距 / 间距 / 竖条宽度 / 三档字号 一起乘）。
	 *  可通过 ini 覆盖：[/Script/Metamorphosis.MetamorphosisTaskWidget] HudScale=1.5 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float HudScale = 1.5f;

	/** 面板宽度占屏宽的上限（竖屏/小屏保护）：超过就按屏宽压回去，避免 720 宽的竖屏上占掉 70% */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float MaxPanelScreenRatio = 0.55f;

	/** 面板宽度；默认与数值 HUD 的 PanelWidth 一致，视觉上左边对齐、右边也齐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float PanelWidth = 340.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float PanelPadding = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float RowSpacing = 2.f;

	/** 左侧竖条（强调色）宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float AccentWidth = 3.f;

	/** 定时刷新间隔（秒）：0.1 秒刷一次足够让玩法门的「（N 秒）」每秒掉一格，又不浪费 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Layout")
	float RefreshIntervalSeconds = 0.1f;

	// ------------------------------------------------------------------
	// 文案 / 字体 / 颜色（与数值 HUD 同一套调色板）
	// ------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FText HeaderText;

	/** 全部任务完成时替换标题的文案 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FText AllDoneText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FSlateFontInfo DetailFont;

	/** 小标题「当前任务」的颜色（与数值 HUD 的 LabelColor 同色：暖白） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FLinearColor LabelColor = FLinearColor(0.93f, 0.87f, 0.74f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FLinearColor TitleColor = FLinearColor(0.98f, 0.94f, 0.86f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Text")
	FLinearColor DetailColor = FLinearColor(0.74f, 0.71f, 0.66f, 1.f);

	/** 面板底色：与数值 HUD 同一块半透明深褐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Art")
	FLinearColor PanelBackgroundColor = FLinearColor(0.09f, 0.07f, 0.05f, 0.72f);

	/** 左侧强调竖条颜色（描金） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Task|Art")
	FLinearColor AccentColor = FLinearColor(0.72f, 0.60f, 0.38f, 0.85f);

private:
	UPROPERTY(Transient) TObjectPtr<UBorder> PanelBorder;
	UPROPERTY(Transient) TObjectPtr<UBorder> AccentBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HeaderLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ContentColumn;
	/** 根 SizeBox：屏宽钳制与"放大后的实际宽度"都作用在它身上 */
	UPROPERTY(Transient) TObjectPtr<USizeBox> RootSizeBox;

	/** 当前实际使用的面板宽度（已含 HudScale 与屏宽上限） */
	float CurrentPanelWidth = 0.f;

	/** 屏宽上限只钳一次（布局建好后第一帧拿到几何尺寸时算） */
	bool bAppliedScreenClamp = false;

	/** 脏检查用：上次显示的内容 */
	FString LastTitle;
	FString LastDetail;
	bool bLastAllCompleted = false;
	bool bBuiltLayout = false;

	/** NativeTick 的限频（秒）：0.1 秒刷一次足够让"（N 秒）"每秒掉一格，又不浪费 */
	float RefreshAccumulator = 0.f;
};
