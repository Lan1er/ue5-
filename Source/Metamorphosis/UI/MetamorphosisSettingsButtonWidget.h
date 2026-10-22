// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 右上角的「设置」按钮：点了打开游戏内弹窗（暂停菜单）。
//
// 图标用的是**引擎自带**的矢量图标 `Icons.Settings`（SlateCore 的 Starship 图标集，
// 授权干净、SVG 放大不糊、不用联网下载）。要换成美术出的图：
// 把 IconBrush 换成贴图刷子即可（或在 WBP 子类里改），代码不用动。
//
// 默认位置：屏幕右上角（距上/右边距由 PlayerController 或默认值给），
// 移动端手指也能点（按钮就是普通 UButton）。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Styling/SlateBrush.h"
#include "MetamorphosisSettingsButtonWidget.generated.h"

class UButton;
class UImage;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisSettingsButtonWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisSettingsButtonWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 按钮被点（控制器接：打开弹窗 + 时停） */
	DECLARE_MULTICAST_DELEGATE(FMetamorphosisSettingsButtonClicked);
	FMetamorphosisSettingsButtonClicked OnClicked;

	// ---------------- 外观（美术可改） ----------------

	/** 按钮边长（Slate 单位） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Layout")
	float ButtonSize = 56.f;

	/** 图标相对按钮的占比 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Layout")
	float IconRatio = 0.62f;

	/** 圆底颜色（半透明，压在游戏画面上） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Art")
	FLinearColor BackgroundColor = FLinearColor(0.06f, 0.06f, 0.08f, 0.42f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Art")
	FLinearColor HoveredColor = FLinearColor(0.16f, 0.17f, 0.22f, 0.62f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Art")
	FLinearColor PressedColor = FLinearColor(0.90f, 0.72f, 0.25f, 0.85f);

	/** 图标颜色（引擎图标是白色的，用颜色染） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Art")
	FLinearColor IconColor = FLinearColor(1.f, 1.f, 1.f, 0.92f);

	/** 图标刷子：默认用引擎的 Icons.Settings；换美术图就设这里 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Art")
	FSlateBrush IconBrush;

	/** 按钮整体不透明度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Art")
	float ButtonOpacity = 1.f;

	/** 距屏幕右上角的边距（左=0 不用，右=X，下=0 不用，上=Y） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|SettingsButton|Layout")
	FMargin CornerMargin = FMargin(0.f, 24.f, 24.f, 0.f);

protected:
	UFUNCTION() void HandleClicked();

	void BuildDefaultLayout();
	/** 把 CornerMargin 应用到按钮槽位（NativeConstruct 里调，控制器可先改属性） */
	void ApplyCornerMargin();

	UPROPERTY(Transient) TObjectPtr<class USizeBox> ButtonBox;
	UPROPERTY(Transient) TObjectPtr<UButton> IconButton;
	UPROPERTY(Transient) TObjectPtr<UImage> IconImage;

	bool bBuiltLayout = false;
};

