// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 周目计数 HUD：左上角显示「第 N 周目」，位置在数值 HUD（怀疑值/躁郁值）的**上方**。
//
// 语义（与周目逻辑对齐）：周目号 = UMetamorphosisLoopSubsystem::GetLoopCount() + 1
//   * GetLoopCount() 是"本次运行已完成的结算次数"（被捉 / 入夜各算一次）；
//   * 所以开局是**第 1 周目**，结算一次之后变**第 2 周目** —— 与策划说的"重开一周目"一致。
//
// 换皮设计（与 MetamorphosisStatsWidget / MetamorphosisTaskWidget 同一套调色板，美术换贴图不用改代码）：
//   * 颜色：PanelBackgroundColor / TextColor / AccentColor
//   * 字体：LoopFont
//   * 文案：FormatText（默认「第 {0} 周目」）
//   * 布局：PanelPadding / AccentWidth / AccentToTextGap
//
// 定位：本控件**不自带屏幕坐标** —— 由 PlayerController 读数值 HUD 的位置与自身实测高度后摆在它上方
//      （见 AMetamorphosisPlayerController::RepositionLoopCountHUD）。
//
// 纯显示：HitTestInvisible，绝不挡输入；**只折叠内部面板，绝不折叠整个控件**
//        （Slate 不会 tick 被整体 Collapsed 的控件，本工程踩过这个坑）。
// 刷新：NativeTick 里轮询周目号 + 脏检查（只在变化时改文本）。
// 数据源缺失（比如在非 Game 世界）时默认显示第 1 周目，不崩。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "MetamorphosisLoopCountWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisLoopCountWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisLoopCountWidget(const FObjectInitializer& ObjectInitializer);

	// UUserWidget
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 立刻按当前周目号刷新一次（正常由 NativeTick 的脏检查驱动） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void RefreshLoopCount();

	/** 当前显示的周目号（1 起） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|UI")
	int32 GetDisplayedLoopNumber() const { return DisplayedLoopNumber; }

protected:
	/** 纯 C++ 默认布局（名字带 LoopCount，避免 unity build 撞名：本工程的 UI 控件里已有 BuildDefaultLayout / BuildTaskLayout） */
	void BuildLoopCountLayout();
	void ApplyLoopCountStaticStyle();

	// ------------------------------------------------------------------
	// 文案 / 字体 / 颜色（与数值 HUD 同一套调色板）
	// ------------------------------------------------------------------
	/** {0} 会被替换成周目号 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Text")
	FText FormatText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Text")
	FSlateFontInfo LoopFont;

	/** 文字颜色（与数值 HUD 的 LabelColor 同色：暖白） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Text")
	FLinearColor TextColor = FLinearColor(0.93f, 0.87f, 0.74f, 1.f);

	/** 面板底色：与数值 HUD 同一块半透明深褐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Art")
	FLinearColor PanelBackgroundColor = FLinearColor(0.09f, 0.07f, 0.05f, 0.72f);

	/** 左侧强调竖条颜色（描金） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Art")
	FLinearColor AccentColor = FLinearColor(0.72f, 0.60f, 0.38f, 0.85f);

	// ------------------------------------------------------------------
	// 布局
	// ------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Layout")
	float PanelPadding = 6.f;

	/** 左侧竖条（强调色）宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Layout")
	float AccentWidth = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Layout")
	float AccentToTextGap = 6.f;

	/** 周目文字与时间文字之间的间距（2026-10-19：时间放在周目旁边） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Layout")
	float TimeTextGap = 12.f;

	/** 时间文字颜色（比周目文字稍暗一点，主次分明） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Loop|Text")
	FLinearColor TimeTextColor = FLinearColor(0.88f, 0.80f, 0.62f, 1.f);

private:
	UPROPERTY(Transient) TObjectPtr<UBorder> PanelBorder;
	UPROPERTY(Transient) TObjectPtr<UBorder> AccentBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LoopText;
	/** 当天时间「7:40」（游戏内分钟，24 小时制） */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;

	/** 脏检查用：上次显示的周目号（0 = 还没刷过） */
	int32 DisplayedLoopNumber = 0;
	/** 脏检查用：上次显示的游戏内分钟（-1 = 还没刷过） */
	int32 DisplayedClockMinute = -1;
	bool bBuiltLayout = false;
};
