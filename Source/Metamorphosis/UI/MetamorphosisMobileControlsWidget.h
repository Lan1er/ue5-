// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 移动端屏幕按钮：右侧上下两个 —— 上面「上升」、下面「下降」，替代原来的右虚拟摇杆。
//
// 换皮（不用改代码）：
//   * 位置：Project Settings -> Game -> Metamorphosis UI -> Mobile Controls Margin
//   * 尺寸/间距/文字/字体/颜色：下面这些 EditAnywhere 属性
//   * 按钮样式：勾上 bApplyButtonStyles 后，用 AscendButtonStyle / DescendButtonStyle 覆盖
//     （不勾就用引擎 UButton 的默认样式）
//
// 做 WBP 子类时，控件按这些名字命名会被自动接管：
//   AscendButton / DescendButton（UButton）、AscendLabel / DescendLabel（UTextBlock）

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisMobileControlsWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisMobileControlsWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisMobileControlsWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---------------------------------------------------------------
	// 布局 / 外观（美术可改）
	// ---------------------------------------------------------------
	/** 按钮组距屏幕右下角的边距（右 / 下） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Layout")
	FMargin PanelMargin = FMargin(0.f, 0.f, 70.f, 70.f);

	/** 每个按钮的边长（Slate 单位，越大越好按） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Layout")
	float ButtonSize = 130.f;

	/** 两个按钮之间的竖直间距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Layout")
	float ButtonSpacing = 18.f;

	/** 上面（上升）按钮的文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Text")
	FText AscendText;

	/** 下面（下降）按钮的文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Text")
	FText DescendText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Text")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Text")
	FLinearColor LabelColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** 是否用下面的 Style 覆盖按钮样式（不勾就用引擎默认按钮样式） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Art")
	bool bApplyButtonStyles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Art")
	FButtonStyle AscendButtonStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Art")
	FButtonStyle DescendButtonStyle;

	/** 默认按钮的不透明度（占位按钮看起来柔和一点，不挡视线） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Mobile|Art")
	float ButtonOpacity = 0.55f;

protected:
	UFUNCTION() void HandleAscendPressed();
	UFUNCTION() void HandleAscendReleased();
	UFUNCTION() void HandleDescendPressed();
	UFUNCTION() void HandleDescendReleased();

	/** 纯 C++ 默认布局（只在没有设计器树时生成） */
	void BuildDefaultLayout();
	void CacheWidgets();
	void ApplyStaticStyle();
	/** 把 PanelMargin 应用到按钮组（换 WBP/改边距后可重复调用） */
	void ApplyPanelMargin();

	/** 建一个按钮（含文字），返回按钮本身 */
	UButton* MakeButton(class UVerticalBox* Column, const TCHAR* ButtonName, const TCHAR* LabelName,
		const FText& Label, bool bUseCustomStyle, const FButtonStyle& Style);

	UPROPERTY(Transient) TObjectPtr<UButton> AscendButton;
	UPROPERTY(Transient) TObjectPtr<UButton> DescendButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AscendLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescendLabel;

	/** 两个按钮外面的 SizeBox（缩放时改它） */
	UPROPERTY(Transient) TArray<TObjectPtr<class USizeBox>> ButtonBoxes;

	/** 当前缩放倍率（设置里的"上下按钮大小"） */
	float CurrentScale = 1.f;

	bool bBuiltLayout = false;

public:
	/** 按设置里的倍率缩放两个按钮（设置界面改动时调用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Mobile")
	void ApplyScale(float Scale);

	/** 当前缩放倍率 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Mobile")
	float GetCurrentScale() const { return CurrentScale; }
};

