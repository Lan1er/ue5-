// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 快捷栏（8 格物品栏）：屏幕底部居中。
//
// 格子外观用美术交付的「道具库格子」素材（/Game/UI/Inventory/Slot_Default、Slot_Hover，
// 104x128，九宫格拉伸）；素材拿不到时退回纯色画法（外深边 + 亮边 + 灰底）。
// 右侧带一个「背包」按钮，点了广播 OnBackpackRequested（由 PlayerController 开背包界面）。
//
// 数据来自 UMetamorphosisInventoryComponent（挂在虫形主角身上）：
//   PC —— 数字键 1..8 直选，滚轮翻页（绑定在 PlayerController 上）
//   移动端 —— 直接点格子
//
// 控件名（做 WBP 子类时按这些名字命名会被自动接管）：
//   HotbarRow（UHorizontalBox）、Slot_0..Slot_7（UButton）、
//   SlotIcon_0..7（UImage）、SlotCount_0..7（UTextBlock）、BackpackButton（UButton）

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisHotbarWidget.generated.h"

class UButton;
class UHorizontalBox;
class UImage;
class UMetamorphosisInventoryComponent;
class UTextBlock;
class UBorder;

/** 快捷栏里每格用到的控件引用 */
USTRUCT()
struct FMetamorphosisHotbarSlotWidgets
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<class USizeBox> SlotBox = nullptr;
	UPROPERTY(Transient) TObjectPtr<class USizeBox> IconBox = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Button = nullptr;

	/** 最外层那一圈：选中时变白（MC 的高亮框） */
	UPROPERTY(Transient) TObjectPtr<UBorder> EdgeBorder = nullptr;

	/** 美术格底（九宫格）；素材缺失时为 nullptr，退回纯色边框 */
	UPROPERTY(Transient) TObjectPtr<UImage> SlotArt = nullptr;

	UPROPERTY(Transient) TObjectPtr<UImage> Icon = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Count = nullptr;
};

DECLARE_MULTICAST_DELEGATE(FMetamorphosisBackpackRequested);

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisHotbarWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisHotbarWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	/** 点了「背包」按钮（PlayerController 订阅它来打开背包） */
	FMetamorphosisBackpackRequested OnBackpackRequested;

	// ---------------- 外观（美术可改） ----------------

	/** 每格边长（Slate 单位）。**格子必须是正方形**：8×56 + 7×6 间距 ≈ 490px，1280 宽屏与缩放 160% 都不越界 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	float SlotSize = 56.f;

	/** 格子高宽比。固定 1.0 = 正方形；美术素材 104x128 靠**九宫格拉伸**填满正方形
	 *  （四角装饰保持原样、只拉伸中间纸面，所以不会被压扁变形）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	float SlotHeightRatio = 1.f;

	/** 右上角「背包」按钮边长（与右上角设置按钮同尺寸同风格） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Backpack")
	float BackpackButtonSize = 56.f;

	/** 背包按钮距屏幕右边距。默认 94 = 设置按钮右边距 26 + 设置按钮 56 + 间距 12，
	 *  也就是**紧挨在齿轮左边**（要跟着 GameplayMenuButtonMargin 一起改）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Backpack")
	float BackpackButtonRightMargin = 94.f;

	/** 背包按钮距屏幕顶部边距（与设置按钮同高） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Backpack")
	float BackpackButtonTopMargin = 26.f;

	/** 格子间距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	float SlotSpacing = 6.f;

	/** 距屏幕底部的边距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	float BottomMargin = 30.f;

	/** 选中框粗细（白色描边宽度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	float SelectionFrameWidth = 2.f;

	/** 选中框颜色（MC 是白色高亮） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	FLinearColor SelectionColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** 未选中格子的外边颜色（纯色兜底用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	FLinearColor SlotEdgeColor = FLinearColor(0.16f, 0.16f, 0.18f, 1.f);

	/** 凹槽亮边（纯色兜底用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	FLinearColor SlotBevelColor = FLinearColor(0.62f, 0.62f, 0.64f, 1.f);

	/** 格子底色（纯色兜底用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	FLinearColor SlotFillColor = FLinearColor(0.42f, 0.42f, 0.44f, 1.f);

	/** 整条快捷栏的底板色（MC 是半透明黑），Alpha=0 表示不画底板 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	FLinearColor RowBackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.35f);

	/** 底板内边距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	FMargin RowPadding = FMargin(6.f, 5.f);

	/** 物品图标相对格子的占比（0.66 -> 0.86：美术图标到位后整体放大，58px 格子里约 50px，手机上更看得清） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Layout")
	float IconSizeRatio = 0.86f;

	/** 数量文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Text")
	FSlateFontInfo CountFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Text")
	FLinearColor CountColor = FLinearColor::White;

	/** 美术格底（默认/悬停）；留空则用纯色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	TSoftObjectPtr<UTexture2D> SlotDefaultTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	TSoftObjectPtr<UTexture2D> SlotHoverTexture;

	/** 九宫格边距（像素） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Art")
	float SlotTextureMargin = 12.f;

	/** 背包按钮文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Backpack")
	FText BackpackLabel;

	/** 背包按钮字体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Hotbar|Backpack")
	FSlateFontInfo BackpackFont;

	/** 取当前 Pawn 上的物品栏组件（可能为空） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Hotbar")
	UMetamorphosisInventoryComponent* GetInventory() const;

	/** 按设置里的倍率缩放整条快捷栏（设置界面改动时调用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Hotbar")
	void ApplyScale(float Scale);

	/** 当前缩放倍率 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Hotbar")
	float GetCurrentScale() const { return CurrentScale; }

protected:
	UFUNCTION() void HandleSlotClicked0();
	UFUNCTION() void HandleSlotClicked1();
	UFUNCTION() void HandleSlotClicked2();
	UFUNCTION() void HandleSlotClicked3();
	UFUNCTION() void HandleSlotClicked4();
	UFUNCTION() void HandleSlotClicked5();
	UFUNCTION() void HandleSlotClicked6();
	UFUNCTION() void HandleSlotClicked7();

	UFUNCTION() void HandleBackpackClicked();

	/** 点第 Index 格 */
	void HandleSlotClicked(int32 Index);

	void BuildDefaultLayout();
	void CacheWidgets();
	void ApplyStaticStyle();
	void RefreshFromInventory();

	/** 取美术格底（拿不到返回 nullptr） */
	UTexture2D* GetSlotArtTexture(bool bHovered) const;

	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> HotbarRow;
	UPROPERTY(Transient) TObjectPtr<UButton> BackpackButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BackpackButtonLabel;

	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SlotButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SlotIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotCounts;
	UPROPERTY(Transient) TArray<FMetamorphosisHotbarSlotWidgets> SlotWidgets;

	bool bBuiltLayout = false;

	/** 当前缩放倍率（设置里的"物品栏大小"） */
	float CurrentScale = 1.f;

	/** 上一次刷新时的状态，没变就不动控件 */
	int32 LastSelectedIndex = -1;
	TArray<bool> LastOccupied;
	TArray<int32> LastCounts;
	/** 道具 ID 与图标软引用也要比：换了道具（数量恰好相同）时同样得刷图标 */
	TArray<FName> LastItemIds;
	TArray<TSoftObjectPtr<UTexture2D>> LastIcons;
	bool bLastHadInventory = false;
};
