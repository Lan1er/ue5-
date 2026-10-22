// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 周目结算「被抓住」界面 —— 按美术《失败页面 同步 v2》复刻（1672×941 设计空间）。
//
// 结构（自底向上）：
//   卧室插画(art_room_clean) -> 可读性压暗(art_readability_shade) -> 记忆碎片格/标签/甲虫图标
//   -> FAILED 大字(art_failed) -> 甲虫影子/仰面甲虫 -> 返回主菜单底板/文字 -> 重新开始底板/文字
//   -> 左上 HUD（失败记录/分隔线/周目/时间/眼睛/怀疑值/读条） -> 失败主文案/次文案
// 动态部分：周目数、时间、怀疑值读条填充宽度、6 个记忆碎片格（已获得的画道具图标）。
//
// ⚠️ 沿用本项目验证过的三条经验：
//   1. 设计空间缩放：SizeBox(屏幕) -> ScaleBox(ScaleToFit) -> SizeBox(设计尺寸) -> CanvasPanel；
//   2. 空闲时用 HitTestInvisible（不能用 Collapsed：Slate 不会 tick Collapsed 的控件）；
//   3. 运行时 LoadObject 的贴图必须存 UPROPERTY TObjectPtr（否则换关卡被 GC -> 白块）。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "MetamorphosisSettlementWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UTextBlock;

/** 「重新开始」/「返回主菜单」被点击 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMetamorphosisSettlementAction);

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisSettlementWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** ⚠️ 必须在头文件声明：否则 UHT 会生成默认实现 + .cpp 里又定义 -> LNK2005（踩过）。 */
	UMetamorphosisSettlementWidget(const FObjectInitializer& ObjectInitializer);

	/** 显示结算反馈（Title/Subtitle 仅在美术素材缺失时作为兜底文字显示） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settlement")
	void PlayFeedback(const FText& Title, const FText& Subtitle, float MaxOpacity, float InFadeInSeconds);

	/** 收起反馈（回到空闲：不挡输入、不显示） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settlement")
	void StopFeedback();

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Settlement")
	bool IsFeedbackVisible() const { return bFeedbackVisible; }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Settlement")
	float GetCurrentOpacity() const { return CurrentOpacity; }

	/** 玩家点击「重新开始」 */
	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Settlement")
	FMetamorphosisSettlementAction OnRestartRequested;

	/** 玩家点击「返回主菜单」 */
	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Settlement")
	FMetamorphosisSettlementAction OnReturnToTitleRequested;

	/** 调试：模拟点击（无头验证用；Meta.Settle.Press 会调） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settlement")
	void SimulatePress(bool bRestart);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void BuildSettlementLayout();

	/** 从各子系统取当前数据刷新（周目/时间/怀疑值/碎片） */
	void RefreshDynamicValues();

	UFUNCTION() void HandleRestartClicked();
	UFUNCTION() void HandleReturnToTitleClicked();

	// ---- 设计空间 ----
	UPROPERTY(EditAnywhere, Category = "Settlement|Layout")
	FVector2D DesignSize = FVector2D(1672.f, 941.f);

	// ---- 控件引用 ----
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> DesignCanvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CycleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient) TObjectPtr<UImage> SuspicionFillImage;
	/** 怀疑值填充的"纯色兜底"（美术填充贴图在部分机器/打包后画不出来，见 cpp 注释） */
	UPROPERTY(Transient) TObjectPtr<class UBorder> SuspicionSolidFill;
	UPROPERTY(Transient) TObjectPtr<UButton> RestartButton;
	UPROPERTY(Transient) TObjectPtr<UButton> MenuButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MemorySlotImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> FragmentIconImages;

	/** 静态图层的贴图缓存（UPROPERTY = GC 安全） */
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> LayerCache;

	/** 碎片图标缓存 */
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> FragmentIconCache;

	// ---- 样式 ----
	UPROPERTY(EditAnywhere, Category = "Settlement|Style") FSlateFontInfo HudFont;
	UPROPERTY(EditAnywhere, Category = "Settlement|Style") FLinearColor HudTextColor = FLinearColor(0.92f, 0.88f, 0.78f, 1.f);
	UPROPERTY(EditAnywhere, Category = "Settlement|Style") FLinearColor FallbackTextColor = FLinearColor(0.98f, 0.94f, 0.86f, 1.f);

	/** 记忆碎片（6 件，顺序与格子一致；已获得则画图标） */
	UPROPERTY(EditAnywhere, Category = "Settlement|Data")
	TArray<FString> FragmentItemIds;

private:
	bool bBuiltLayout = false;
	bool bFeedbackVisible = false;
	float CurrentOpacity = 0.f;
	float TargetOpacity = 0.f;
	float FadeSpeed = 4.f;
	/** 上次刷新的分钟数（脏检查） */
	int32 LastShownMinute = -1;
};
