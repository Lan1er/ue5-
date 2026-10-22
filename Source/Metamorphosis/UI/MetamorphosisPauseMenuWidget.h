// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 游戏内弹窗（暂停菜单）：右上角设置按钮点开后出现，默认两个选项 —— 返回游戏 / 打开设置。
//
// **扩充方式**（以后要加"重新开始/回主菜单/退出游戏"之类）：
//   调用 AddOption(Tag, Label) 往列表里加一条，然后在 PlayerController 的
//   HandlePauseMenuAction(FName Tag) 里接一个新的 Tag 分支即可，UI 不用改。
//   也可以直接改 DefaultOptions（在 Project Settings 或子类里配）。
//
// 打开时由控制器 SetGamePaused(true) 时停；返回游戏时恢复。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisPauseMenuWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UVerticalBox;

/** 一个弹窗选项：Tag 给控制器识别用，Label 是显示文字 */
USTRUCT(BlueprintType)
struct FMetamorphosisPauseMenuOption
{
	GENERATED_BODY()

	/** 控制器据此决定干什么，例如 Resume / OpenSettings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pause Menu")
	FName Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pause Menu")
	FText Label;
};

/** 选项被点击（参数是 Option.Tag） */
DECLARE_MULTICAST_DELEGATE_OneParam(FMetamorphosisPauseMenuOptionChosen, FName);

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisPauseMenuWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 选项被点击时广播（控制器接） */
	FMetamorphosisPauseMenuOptionChosen OnOptionChosen;

	/** 默认选项（可在子类/默认值里改；代码里也能用 AddOption 追加） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Options")
	TArray<FMetamorphosisPauseMenuOption> Options;

	/** 追加一个选项（会重建列表；用于运行时扩充） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Pause|Options")
	void AddOption(FName Tag, const FText& Label);

	/** 让第一个按钮拿到焦点（手柄/键盘能直接选） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Pause")
	void FocusFirstOption();

	// ---------------- 外观（美术可改） ----------------

	/** 全屏遮罩色（把游戏画面压暗一点，同时吃掉身后的点击） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	FLinearColor PanelColor = FLinearColor(0.97f, 0.97f, 0.98f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Text")
	FText TitleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Text")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Text")
	FSlateFontInfo ButtonFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Text")
	FLinearColor TitleColor = FLinearColor(0.06f, 0.06f, 0.08f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Layout")
	FVector2D PanelSize = FVector2D(420.f, 300.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Layout")
	float ButtonWidth = 407.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Layout")
	float ButtonHeight = 81.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Layout")
	float OptionSpacing = 14.f;

	/** 整个「标题 + 选项」块整体上移的量（Slate 单位）。
	 *  2026-10-20：清掉幽灵按钮后选项块会重新垂直居中；如果还想更靠上，把这个值调大（40~100 之间比较合适）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Layout")
	float ContentVerticalOffset = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	FButtonStyle OptionButtonStyle;

	// ---- 美术交付 1（暂停页）：拿不到就退回纯色面板 ----

	/** 全屏背景（1672x941 房间插画） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	TSoftObjectPtr<UTexture2D> BackgroundTexture;

	/** 标题「暂停」毛笔字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	TSoftObjectPtr<UTexture2D> TitleTexture;

	/** 按钮底板（默认 / 悬停；九宫格拉伸） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	TSoftObjectPtr<UTexture2D> ButtonPlateNormalTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	TSoftObjectPtr<UTexture2D> ButtonPlateHoverTexture;

	/** 右下角的虫子装饰 + 影子 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	TSoftObjectPtr<UTexture2D> BeetleTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Art")
	TSoftObjectPtr<UTexture2D> ShadowTexture;

	/** 按钮文字颜色（底板是深绿纸，用浅米字） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Pause|Text")
	FLinearColor ButtonTextColor = FLinearColor(0.93f, 0.87f, 0.74f, 1.f);

protected:
	UFUNCTION() void HandleOption0();
	UFUNCTION() void HandleOption1();
	UFUNCTION() void HandleOption2();
	UFUNCTION() void HandleOption3();
	UFUNCTION() void HandleOption4();
	UFUNCTION() void HandleOption5();

	// 悬停高亮（底板是 child UImage，靠透明度区分默认/悬停）
	UFUNCTION() void HandleOption0Hover();
	UFUNCTION() void HandleOption0Unhover();
	UFUNCTION() void HandleOption1Hover();
	UFUNCTION() void HandleOption1Unhover();

	/** 统一的"第 Index 个选项被点"处理 */
	void HandleOptionClicked(int32 Index);

	void BuildDefaultLayout();
	void RebuildOptionButtons();

	/** 运行时加载美术底板并重建按钮样式（构造函数里做不可靠：CDO 阶段资源可能还没就绪） */
	void RebuildArtButtonStyle();

	/** 设置第 Index 个按钮底板的透明度（悬停/默认） */
	void SetOptionPlateHighlighted(int32 Index, bool bHighlighted);

	UPROPERTY(Transient) TObjectPtr<UImage> BackdropImage;
	UPROPERTY(Transient) TObjectPtr<class UBorder> PanelBorder;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleLabel;
	UPROPERTY(Transient) TObjectPtr<UImage> TitleImage;

	/** 标题那一块（有美术字时是包着 TitleImage 的 SizeBox，否则是 TitleLabel 本身）。
	 *  RebuildOptionButtons 会整列清空再重建，所以必须记住它并把标题重新挂回去。 */
	UPROPERTY(Transient) TObjectPtr<UWidget> TitleWidget;

	/** 标题与第一个选项之间的间距（建标题时记录下来，重建时复用） */
	float TitleBottomPadding = 26.f;

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> OptionColumn;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OptionButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> OptionLabels;

	/** 每个选项的外层 USizeBox（真正挂在 OptionColumn 里的那个）；
	 *  2026-10-20 修：原来重建时只移除 UButton，SizeBox/Overlay/底板 UImage 全留在列里
	 *  -> 每重建一次就多出 N 个"没有文字的空底板"（用户看到的幽灵按钮）。 */
	UPROPERTY(Transient) TArray<TObjectPtr<class USizeBox>> OptionBoxes;

	/** 最多支持几个选项（要更多就在 HandleOptionN 里加分支） */
	static constexpr int32 MaxOptions = 6;

	bool bBuiltLayout = false;

	/** 美术按钮底板是否加载成功（只用于日志/排障） */
	bool bArtPlateLoaded = false;
};


