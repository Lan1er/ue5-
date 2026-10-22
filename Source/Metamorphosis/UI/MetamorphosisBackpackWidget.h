// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 背包界面（书本式道具库）：主界面点「背包」按钮或按 B 打开。
//
// 布局照美术交付的道具库素材拼：
//   左页（470x325，纸上印了 4x2 的格子）+ 右页（298x325，放详情）
//   + 中缝、装订孔、外框、标题、关闭按钮
// 8 个格子与快捷栏共用同一个 UMetamorphosisInventoryComponent（不另造一套数据）。
//
// 控件名（做 WBP 子类时按这些名字命名会被自动接管）：
//   BackpackCanvas / BagSlot_0..7 / BagSlotIcon_0..7 / BagDetailName / BagDetailDesc / BagCloseButton

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisBackpackWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UBorder;
class UMetamorphosisInventoryComponent;

DECLARE_MULTICAST_DELEGATE(FMetamorphosisBackpackClosed);

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisBackpackWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisBackpackWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 点了关闭（PlayerController 订阅它来收起背包） */
	FMetamorphosisBackpackClosed OnBackpackClosed;

	/** 书本画布尺寸（美术素材原生 808x350） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Layout")
	FVector2D BookSize = FVector2D(808.f, 350.f);

	/** 书本占屏幕宽度的比例（2026-10-17：0.63 -> **0.75**，用户要求"占屏幕 3/4"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Layout")
	float BookScreenWidthRatio = 0.75f;

	/** 书本在屏幕上的最大高度比例（竖屏/超宽时防溢出） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Layout")
	float BookMaxScreenHeightRatio = 0.92f;

	/** 格子区起点与尺寸（要压在左页印好的格子上） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Layout")
	FVector2D SlotOrigin = FVector2D(24.f, 50.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Layout")
	FVector2D SlotCellSize = FVector2D(104.f, 116.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Layout")
	float SlotGap = 6.f;

	/** 纸上的墨色字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Text")
	FLinearColor InkColor = FLinearColor(0.16f, 0.13f, 0.09f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Text")
	FLinearColor InkSubColor = FLinearColor(0.30f, 0.25f, 0.18f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Backpack|Text")
	FLinearColor HighlightColor = FLinearColor(0.85f, 0.66f, 0.28f, 1.f);

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Backpack")
	UMetamorphosisInventoryComponent* GetInventory() const;

	/** 当前正在查看的格子（-1 = 没选） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Backpack")
	int32 GetInspectIndex() const { return InspectIndex; }

	/** 选中某格并刷新详情 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Backpack")
	void InspectSlot(int32 Index);

	/** 刷新 8 格与详情（打开时/背包内容变化时调用） */
	void RefreshBackpack();

	/** 对"当前查看的格子"执行 使用/查看（按钮与调试命令共用这一处） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Backpack")
	bool UseInspectedSlot();

	/** 对"当前查看的格子"执行 丢弃（按钮与调试命令共用这一处） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Backpack")
	bool DropInspectedSlot();

protected:
	UFUNCTION() void HandleSlotClicked0();
	UFUNCTION() void HandleSlotClicked1();
	UFUNCTION() void HandleSlotClicked2();
	UFUNCTION() void HandleSlotClicked3();
	UFUNCTION() void HandleSlotClicked4();
	UFUNCTION() void HandleSlotClicked5();
	UFUNCTION() void HandleSlotClicked6();
	UFUNCTION() void HandleSlotClicked7();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleUseClicked();
	UFUNCTION() void HandleDropClicked();

	void HandleSlotClicked(int32 Index);
	void BuildDefaultLayout();
	void CacheWidgets();
	void UpdateDetail();

	void UpdateActionButtons(const struct FMetamorphosisItemSlot& ItemSlot, const struct FItemConfigEntry* Entry);

	/** 按视口把书本缩放到"屏宽 75%"（视口变了才重设，避免每帧设 Slot） */
	void UpdateBookTargetSize();
	FVector2D LastBookViewportSize = FVector2D::ZeroVector;

	/** 书本外层尺寸盒（真正决定屏幕上多大；内部画布仍是美术设计尺寸 808x350） */
	UPROPERTY(Transient) TObjectPtr<class USizeBox> BookTargetBox;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> BookCanvas;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SlotButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SlotIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SlotArts;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SlotOutlines;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailMeta;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailDesc;
	UPROPERTY(Transient) TObjectPtr<UButton> UseButton;
	UPROPERTY(Transient) TObjectPtr<UButton> DropButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UseButtonLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DropButtonLabel;

	FSlateFontInfo TitleFont;
	FSlateFontInfo BodyFont;
	FSlateFontInfo SmallFont;

	bool bBuiltLayout = false;
	int32 InspectIndex = -1;

	/** 上次刷新时的内容签名（避免每帧重设贴图） */
	FString LastSignature;
};

