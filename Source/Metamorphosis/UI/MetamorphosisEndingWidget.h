// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 通关界面（2026-10-18）：玩家从门缝钻出去 = 直接通关。
//
// 为什么单独做一个控件（而不是复用结算控件）：
//   * 结算界面是"被捉 -> 周目重置"的中间态（会自动消失、之后继续玩）；
//   * 通关界面是**终局**（不自动消失，要玩家自己选「重新开始」或「返回标题」）。
//   两者语义相反，混在一起以后必然扯皮，所以分成两个控件。
//
// 结构：全屏压暗 -> 居中竖排：大字「通关」/ 副标题 / 一行数据 / 两个按钮
// ⚠️ 布局必须在 Initialize() 里搭（放 NativeConstruct 时控件尺寸恒为 0，本工程踩过）。
// ⚠️ 不依赖 NativeTick 做淡入：通关界面出现时游戏是**暂停**的，UMG 的 tick 行为在暂停下不如
//    "一次性显示"可靠（结算界面那种淡入是给"不暂停"的场景用的）。
//
// 两个按钮不自己做业务：直接调 UMetamorphosisLoopSubsystem 的 RestartAfterWin / ReturnToTitleAfterWin，
// 保证"重置"和"回标题"只有一处实现。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "MetamorphosisEndingWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisEndingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** ⚠️ 构造函数必须在此声明：否则 UHT 生成的默认实现和 .cpp 里的定义会 LNK2005（踩过） */
	UMetamorphosisEndingWidget(const FObjectInitializer& ObjectInitializer);

	/** 显示通关界面（Title/Subtitle/Stats 全部来自调用方 = 配表/ini，控件不硬编码文案） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Ending")
	void ShowEnding(const FText& Title, const FText& Subtitle, const FText& StatsLine);

	/** 收起（重新开始 / 返回标题时用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Ending")
	void HideEnding();

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Ending")
	bool IsEndingVisible() const { return bEndingVisible; }

	// 无头环境点不了鼠标：用 SimulatePress 验证"按钮回调链路通不通"，
	// 用 LogControlTree 把控件树 + Visibility 打出来排查"点不动"（和开始界面那套一致）。
	// Which = "Restart" / "Title"（大小写不敏感）
	bool SimulatePress(const FString& Which);
	void LogControlTree() const;

	/** 按钮文案（可由子类/蓝图改） */
	UPROPERTY(EditAnywhere, Category = "Ending|Text")
	FText RestartButtonText = FText::FromString(TEXT("重新开始"));

	UPROPERTY(EditAnywhere, Category = "Ending|Text")
	FText ReturnButtonText = FText::FromString(TEXT("返回标题"));

	/** 遮罩不透明度（1 = 全黑） */
	UPROPERTY(EditAnywhere, Category = "Ending|Look")
	float MaskOpacity = 0.96f;

protected:
	virtual bool Initialize() override;

	/** 搭默认布局（纯 C++ 类没有设计器树时走这里） */
	void BuildEndingLayout();

	UFUNCTION()
	void HandleRestartClicked();

	UFUNCTION()
	void HandleReturnToTitleClicked();

	UPROPERTY(Transient) TObjectPtr<UBorder> MaskBorder;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ContentBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatsText;
	UPROPERTY(Transient) TObjectPtr<UButton> RestartButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ReturnButton;

	UPROPERTY(EditAnywhere, Category = "Ending|Font") FSlateFontInfo TitleFont;
	UPROPERTY(EditAnywhere, Category = "Ending|Font") FSlateFontInfo SubtitleFont;
	UPROPERTY(EditAnywhere, Category = "Ending|Font") FSlateFontInfo StatsFont;
	UPROPERTY(EditAnywhere, Category = "Ending|Font") FSlateFontInfo ButtonFont;

	UPROPERTY(EditAnywhere, Category = "Ending|Look") FLinearColor TitleColor = FLinearColor(0.98f, 0.94f, 0.86f, 1.f);
	UPROPERTY(EditAnywhere, Category = "Ending|Look") FLinearColor SubtitleColor = FLinearColor(0.86f, 0.78f, 0.58f, 1.f);
	UPROPERTY(EditAnywhere, Category = "Ending|Look") FLinearColor StatsColor = FLinearColor(0.62f, 0.58f, 0.50f, 1.f);

private:
	bool bBuiltLayout = false;
	bool bEndingVisible = false;
};
