// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisSettlementWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "MetamorphosisPlayerController.h"
#include "Metamorphosis.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Tasks/MetamorphosisTaskSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// ⚠️ 唯一命名（unity build 会把多个 cpp 拼一起，本工程踩过"同名辅助函数"的坑）
	template <typename T>
	T* MakeSettleWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), Name);
	}

	/** 失败页面的一个静态图层：图 id + 设计空间里的 (x, y, w, h) */
	struct FFailureLayerRect
	{
		const TCHAR* Id;
		float X;
		float Y;
		float W;
		float H;
	};

	// 坐标来自美术交付的 文档/图层清单.json（1672×941 画布，数组自底到顶）
	const FFailureLayerRect GFailureLayers[] =
	{
		{ TEXT("art_room_clean"),           0.f,   0.f, 1672.f, 941.f },
		{ TEXT("art_readability_shade"),    0.f,   0.f, 1155.f, 941.f },
		{ TEXT("ui_memory_slot_05"),      371.f, 832.f,   41.f,  41.f },
		{ TEXT("ui_memory_slot_04"),      317.f, 832.f,   41.f,  41.f },
		{ TEXT("ui_memory_slot_03"),      263.f, 832.f,   41.f,  41.f },
		{ TEXT("ui_memory_slot_02"),      209.f, 832.f,   41.f,  41.f },
		{ TEXT("ui_memory_slot_01"),      155.f, 832.f,   41.f,  41.f },
		{ TEXT("ui_memory_label"),        158.f, 801.f,  165.f,  19.f },
		{ TEXT("ui_memory_beetle"),        75.f, 793.f,   49.f,  87.f },
		{ TEXT("art_failed"),              53.f, 247.f,  600.f, 255.f },
		{ TEXT("art_beetle_shadow"),      619.f, 738.f,  690.f, 150.f },
		{ TEXT("art_beetle"),             590.f, 452.f,  733.f, 454.f },
		{ TEXT("ui_button_menu_plate"),   315.f, 518.f, 1310.f, 353.f },
		{ TEXT("ui_button_menu_label"),  1377.f, 541.f,  175.f,  35.f },
		{ TEXT("art_button_restart_plate"), 1298.f, 416.f, 329.f, 84.f },
		{ TEXT("ui_button_restart_label"), 1394.f, 442.f, 138.f, 33.f },
		{ TEXT("ui_hud_divider"),          49.f,  84.f,  302.f,   3.f },
		{ TEXT("ui_hud_title"),            69.f,  47.f,  108.f,  25.f },
		{ TEXT("ui_hud_eye_user_smartobject"), 39.f, 129.f, 84.f, 66.f },
		{ TEXT("ui_hud_suspicion_label"), 137.f, 155.f,   73.f,  22.f },
		{ TEXT("ui_hud_suspicion_track"), 225.f, 155.f,  306.f,  24.f },
		{ TEXT("ui_failure_subline"),      71.f, 626.f,  400.f,  30.f },
		{ TEXT("ui_failure_headline"),     71.f, 562.f,  298.f,  39.f },
	};

	// 读条在 track 里的可用宽度（play 图 70 宽、起点比 track 右移 8 -> 内区约 290）
	constexpr float GSuspicionFillMaxWidth = 290.f;
	// 记忆碎片格：6 个格子的起点/间距（设计空间）
	constexpr float GMemorySlotStartX = 155.f;
	constexpr float GMemorySlotStepX = 54.f;
	constexpr float GMemorySlotY = 832.f;
	constexpr float GMemorySlotSize = 41.f;
	constexpr int32 GMemorySlotCount = 6;
}

UMetamorphosisSettlementWidget::UMetamorphosisSettlementWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		HudFont = FSlateFontInfo(RobotoFont.Object, 22, FName("Bold"));
	}

	// 六件记忆碎片（与通关所需一致；顺序 = 格子从左到右）
	FragmentItemIds = { TEXT("MedicationList"), TEXT("NoteOnFoil"), TEXT("BowHairClip"),
		TEXT("BpMedicineBottle"), TEXT("BpPill"), TEXT("HomeworkNotebook") };

	// 预加载静态图层（构造期 LoadObject + UPROPERTY 缓存 = GC 安全）
	for (const FFailureLayerRect& Layer : GFailureLayers)
	{
		const FString Path = FString::Printf(TEXT("/Game/UI/Settlement/T_%s.T_%s"), Layer.Id, Layer.Id);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path))
		{
			LayerCache.Add(Layer.Id, Texture);
		}
	}
	UE_LOG(LogMetamorphosis, Log, TEXT("[Settlement] 失败页面图层载入：%d/%d"), LayerCache.Num(), UE_ARRAY_COUNT(GFailureLayers));
}

bool UMetamorphosisSettlementWidget::Initialize()
{
	const bool bResult = Super::Initialize();
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildSettlementLayout();
		bBuiltLayout = true;
	}
	return bResult;
}

void UMetamorphosisSettlementWidget::NativeConstruct()
{
	Super::NativeConstruct();
	StopFeedback();
}

void UMetamorphosisSettlementWidget::BuildSettlementLayout()
{
	// 根：Overlay（空闲时整棵不挡输入）
	UOverlay* Root = MakeSettleWidget<UOverlay>(WidgetTree, TEXT("SettlementRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	// 设计空间等比缩放：SizeBox(屏幕) -> ScaleBox(ScaleToFit) -> SizeBox(1672x941) -> CanvasPanel
	UScaleBox* Scale = MakeSettleWidget<UScaleBox>(WidgetTree, TEXT("SettlementScale"));
	Scale->SetStretch(EStretch::ScaleToFit);
	Scale->SetStretchDirection(EStretchDirection::Both);
	if (UOverlaySlot* ScaleSlot = Root->AddChildToOverlay(Scale))
	{
		ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
		ScaleSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* DesignBox = MakeSettleWidget<USizeBox>(WidgetTree, TEXT("SettlementDesignBox"));
	DesignBox->SetWidthOverride(DesignSize.X);
	DesignBox->SetHeightOverride(DesignSize.Y);
	Scale->AddChild(DesignBox);

	DesignCanvas = MakeSettleWidget<UCanvasPanel>(WidgetTree, TEXT("SettlementCanvas"));
	DesignCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	DesignBox->AddChild(DesignCanvas);

	auto AddLayer = [this](FName Name, UTexture2D* Texture, const FVector2D& Pos, const FVector2D& Size) -> UImage*
	{
		if (Texture == nullptr)
		{
			return nullptr;
		}
		UImage* Image = MakeSettleWidget<UImage>(WidgetTree, Name);
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageType = ESlateBrushImageType::FullColor;
		Image->SetBrush(Brush);
		Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = DesignCanvas->AddChildToCanvas(Image))
		{
			Slot->SetPosition(Pos);
			Slot->SetSize(Size);
			Slot->SetAutoSize(false);
		}
		return Image;
	};

	// ① 静态图层（跳过那两张"烘死文字"的周目/时间图，改用动态文字）
	for (const FFailureLayerRect& Layer : GFailureLayers)
	{
		if (FCString::Strcmp(Layer.Id, TEXT("ui_hud_cycle")) == 0 ||
			FCString::Strcmp(Layer.Id, TEXT("ui_hud_time")) == 0)
		{
			continue;
		}
		UTexture2D* Texture = LayerCache.FindRef(FString(Layer.Id));
		AddLayer(FName(*FString::Printf(TEXT("Layer_%s"), Layer.Id)), Texture,
			FVector2D(Layer.X, Layer.Y), FVector2D(Layer.W, Layer.H));
	}

	// ② 怀疑值读条填充（动态宽度）
	// 2026-10-19：用户反馈"怀疑值读条是空的"。实测美术的 ui_hud_suspicion_fill 属于本工程那批
	// "运行时画不出来"的贴图（与暂停页按钮底板 / 拾取提示板身同族 ✗）。填充改由**代码纯色**绘制，
	// 底色取 HUD 的红——先尝试美术贴图，但要**同时叠一层纯色**保证一定可见（美术图能画出来时也只会更红一点）。
	if (UTexture2D* FillTexture = LayerCache.FindRef(TEXT("ui_hud_suspicion_fill")))
	{
		SuspicionFillImage = AddLayer(TEXT("SuspicionFill"), FillTexture, FVector2D(233.f, 162.f), FVector2D(GSuspicionFillMaxWidth, 12.f));
	}

	// 纯色填充（永远可见；放在美术填充之上，宽度在 RefreshDynamicValues 里设置）
	{
		SuspicionSolidFill = MakeSettleWidget<UBorder>(WidgetTree, TEXT("SuspicionSolidFill"));
		SuspicionSolidFill->SetBrushColor(FLinearColor(0.62f, 0.09f, 0.08f, 0.95f));
		SuspicionSolidFill->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* SolidSlot = DesignCanvas->AddChildToCanvas(SuspicionSolidFill))
		{
			SolidSlot->SetPosition(FVector2D(233.f, 162.f));
			SolidSlot->SetSize(FVector2D(2.f, 12.f));
			SolidSlot->SetAutoSize(false);
		}
	}

	// ③ 周目 / 时间（动态文字，替代烘死在图里的那两张）
	auto MakeHudText = [this](FName Name, const FVector2D& Pos, const FVector2D& Size, int32 FontSize) -> UTextBlock*
	{
		UTextBlock* Text = MakeSettleWidget<UTextBlock>(WidgetTree, Name);
		Text->SetFont(FSlateFontInfo(HudFont.FontObject, FontSize, FName("Bold")));
		Text->SetColorAndOpacity(FSlateColor(HudTextColor));
		Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = DesignCanvas->AddChildToCanvas(Text))
		{
			Slot->SetPosition(Pos);
			Slot->SetSize(Size);
			Slot->SetAutoSize(false);
		}
		return Text;
	};

	CycleText = MakeHudText(TEXT("CycleText"), FVector2D(66.f, 96.f), FVector2D(240.f, 30.f), 24);
	TimeText = MakeHudText(TEXT("TimeText"), FVector2D(268.f, 96.f), FVector2D(140.f, 26.f), 20);

	// ④ 记忆碎片格（6 个：复用 slot_01 的图，已获得则叠道具图标）
	if (UTexture2D* SlotTexture = LayerCache.FindRef(TEXT("ui_memory_slot_01")))
	{
		for (int32 Index = 0; Index < GMemorySlotCount; ++Index)
		{
			const float X = GMemorySlotStartX + GMemorySlotStepX * Index;
			UImage* SlotImage = AddLayer(FName(*FString::Printf(TEXT("MemorySlot_%d"), Index)), SlotTexture,
				FVector2D(X, GMemorySlotY), FVector2D(GMemorySlotSize, GMemorySlotSize));
			MemorySlotImages.Add(SlotImage);

			// 2026-10-19 修正：原来这里用 AddLayer(..., nullptr, ...) 建图标，而 AddLayer 对空贴图**直接返回 null**
			// ✗ -> 6 个图标控件根本没被创建 -> 碎片永远点不亮（用户："记忆碎片也不显示"）。
			// 现在直接建一个透明底的 UImage（先 Collapsed，点亮时再设贴图）。
			UImage* IconImage = MakeSettleWidget<UImage>(WidgetTree, FName(*FString::Printf(TEXT("MemoryIcon_%d"), Index)));
			{
				FSlateBrush EmptyBrush;
				EmptyBrush.ImageSize = FVector2D(GMemorySlotSize - 10.f, GMemorySlotSize - 10.f);
				EmptyBrush.DrawAs = ESlateBrushDrawType::Image;
				EmptyBrush.ImageType = ESlateBrushImageType::FullColor;
				EmptyBrush.TintColor = FSlateColor(FLinearColor::White);
				IconImage->SetBrush(EmptyBrush);
			}
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
			if (UCanvasPanelSlot* IconSlot = DesignCanvas->AddChildToCanvas(IconImage))
			{
				IconSlot->SetPosition(FVector2D(X + 5.f, GMemorySlotY + 5.f));
				IconSlot->SetSize(FVector2D(GMemorySlotSize - 10.f, GMemorySlotSize - 10.f));
				IconSlot->SetAutoSize(false);
			}
			FragmentIconImages.Add(IconImage);
		}
	}

	// ⑤ 两个按钮（透明底，盖在美术按钮区域上）
	auto MakeInvisibleButton = [this](FName Name, const FVector2D& Pos, const FVector2D& Size) -> UButton*
	{
		UButton* Button = MakeSettleWidget<UButton>(WidgetTree, Name);
		FButtonStyle Style;
		FSlateBrush Transparent;
		Transparent.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
		Transparent.DrawAs = ESlateBrushDrawType::Image;
		Style.SetNormal(Transparent);
		Style.SetHovered(Transparent);
		Style.SetPressed(Transparent);
		Button->SetStyle(Style);
		if (UCanvasPanelSlot* Slot = DesignCanvas->AddChildToCanvas(Button))
		{
			Slot->SetPosition(Pos);
			Slot->SetSize(Size);
			Slot->SetAutoSize(false);
		}
		return Button;
	};

	RestartButton = MakeInvisibleButton(TEXT("RestartButton"), FVector2D(1298.f, 416.f), FVector2D(329.f, 84.f));
	RestartButton->OnClicked.AddDynamic(this, &UMetamorphosisSettlementWidget::HandleRestartClicked);

	MenuButton = MakeInvisibleButton(TEXT("MenuButton"), FVector2D(1350.f, 524.f), FVector2D(229.f, 62.f));
	MenuButton->OnClicked.AddDynamic(this, &UMetamorphosisSettlementWidget::HandleReturnToTitleClicked);

	UE_LOG(LogMetamorphosis, Log, TEXT("[Settlement] 布局完成：静态层 %d、按钮绑定 重新开始=%d 返回主菜单=%d"),
		LayerCache.Num(), RestartButton != nullptr ? 1 : 0, MenuButton != nullptr ? 1 : 0);

	// 兜底文字（美术素材缺失时才有用；有素材就不显示）
	if (LayerCache.Num() == 0)
	{
		UTextBlock* Fallback = MakeHudText(TEXT("FallbackText"), FVector2D(71.f, 600.f), FVector2D(900.f, 80.f), 40);
		Fallback->SetColorAndOpacity(FSlateColor(FallbackTextColor));
		Fallback->SetText(FText::FromString(TEXT("FAILED")));
	}
}

void UMetamorphosisSettlementWidget::RefreshDynamicValues()
{
	const UGameInstance* GI = GetGameInstance();
	if (GI == nullptr)
	{
		return;
	}

	// 周目
	if (CycleText != nullptr)
	{
		if (const UMetamorphosisLoopSubsystem* Loop = GI->GetSubsystem<UMetamorphosisLoopSubsystem>())
		{
			CycleText->SetText(FText::FromString(FString::Printf(TEXT("第 %d 周目"), Loop->GetLoopCount() + 1)));
		}
	}

	// 时间 + 怀疑值
	if (const UMetamorphosisStatsSubsystem* Stats = GI->GetSubsystem<UMetamorphosisStatsSubsystem>())
	{
		if (TimeText != nullptr)
		{
			TimeText->SetText(Stats->GetClockText());
		}
		if (SuspicionFillImage != nullptr)
		{
			const float Ratio = FMath::Clamp(Stats->GetSuspicionRatio(), 0.f, 1.f);
			const float Width = FMath::Max(2.f, GSuspicionFillMaxWidth * Ratio);
			if (UCanvasPanelSlot* FillSlot = Cast<UCanvasPanelSlot>(SuspicionFillImage->Slot))
			{
				FillSlot->SetSize(FVector2D(Width, 12.f));
			}
		}
		// 纯色兜底填充：与美术填充同位置同宽度（美术图能画时会叠在上面，画不出来时至少这条在）
		if (SuspicionSolidFill != nullptr)
		{
			const float Ratio = FMath::Clamp(Stats->GetSuspicionRatio(), 0.f, 1.f);
			const float Width = FMath::Max(2.f, GSuspicionFillMaxWidth * Ratio);
			if (UCanvasPanelSlot* SolidSlot = Cast<UCanvasPanelSlot>(SuspicionSolidFill->Slot))
			{
				SolidSlot->SetSize(FVector2D(Width, 12.f));
			}
			UE_LOG(LogMetamorphosis, Log, TEXT("[DIAG] 结算数据：时间=%s 怀疑=%.1f（比例 %.2f）"),
				*Stats->GetClockText().ToString(), Stats->GetSuspicion(), Ratio);
		}
	}

	// 记忆碎片：已获得则画道具图标
	// 2026-10-19：用户反馈"记忆碎片也不显示" ✗。两个原因都堵上：
	//   ① 数据源不只靠"世界拾取记账"（ObtainedItemIds）—— 背包里有的也算已获得 ✓（读档/直接在背包里的情况）
	//   ② 每次刷新要**先把全部图标藏起来**再点亮 ✓（否则上一次的残留图标会留在格子里 ✗）
	{
		TSet<FString> Owned;
		if (const UMetamorphosisTaskSubsystem* Task = GI->GetSubsystem<UMetamorphosisTaskSubsystem>())
		{
			for (const FString& Id : Task->GetObtainedItemIds())
			{
				Owned.Add(Id);
			}
		}

		// 背包里现有的道具也算（结算时背包还没被清空 ✓）
		if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
		{
			if (const UMetamorphosisInventoryComponent* Inventory = PC->GetInventoryComponent())
			{
				for (int32 SlotIndex = 0; SlotIndex < UMetamorphosisInventoryComponent::NumSlots; ++SlotIndex)
				{
					const FMetamorphosisItemSlot SlotData = Inventory->GetSlot(SlotIndex);
					if (SlotData.bOccupied && !SlotData.ItemId.IsNone() && SlotData.Count > 0)
					{
						Owned.Add(SlotData.ItemId.ToString());
					}
				}
			}
		}

		int32 ObtainedCount = 0;
		FString ObtainedList;
		for (int32 Index = 0; Index < GMemorySlotCount; ++Index)
		{
			const bool bHasSlot = FragmentItemIds.IsValidIndex(Index) && FragmentIconImages.IsValidIndex(Index)
				&& FragmentIconImages[Index] != nullptr;
			if (!bHasSlot)
			{
				continue;
			}

			const FString& ItemId = FragmentItemIds[Index];
			UImage* Icon = FragmentIconImages[Index];

			// ① 先全部藏起来
			Icon->SetVisibility(ESlateVisibility::Collapsed);

			if (!Owned.Contains(ItemId))
			{
				continue;
			}

			UE_LOG(LogMetamorphosis, Log, TEXT("[Settlement] 点亮碎片 %d：%s"), Index, *ItemId);

			// ② 已获得 -> 点亮
			ObtainedCount++;
			ObtainedList += (ObtainedList.IsEmpty() ? ItemId : FString(TEXT(", ")) + ItemId);

			TObjectPtr<UTexture2D>* Cached = FragmentIconCache.Find(ItemId);
			UTexture2D* Texture = Cached != nullptr ? Cached->Get() : nullptr;
			if (Texture == nullptr)
			{
				const FString Path = FString::Printf(TEXT("/Game/UI/Items/T_Item_%s.T_Item_%s"), *ItemId, *ItemId);
				Texture = LoadObject<UTexture2D>(nullptr, *Path);
				FragmentIconCache.Add(ItemId, Texture);
			}
			if (Texture != nullptr)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(Texture);
				Brush.ImageSize = FVector2D(GMemorySlotSize - 10.f, GMemorySlotSize - 10.f);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageType = ESlateBrushImageType::FullColor;
				Icon->SetBrush(Brush);
				Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}

		UE_LOG(LogMetamorphosis, Log, TEXT("[Settlement] 记忆碎片：已获得 %d/%d（%s）｜玩家=%d 背包=%d 任务记账=%s｜配置项=%d 图标槽=%d 已建布局=%d"),
			ObtainedCount, GMemorySlotCount, ObtainedList.IsEmpty() ? TEXT("无") : *ObtainedList,
			GetOwningPlayer() != nullptr ? 1 : 0,
			(Cast<AMetamorphosisPlayerController>(GetOwningPlayer()) != nullptr
				&& Cast<AMetamorphosisPlayerController>(GetOwningPlayer())->GetInventoryComponent() != nullptr) ? 1 : 0,
			GI->GetSubsystem<UMetamorphosisTaskSubsystem>() != nullptr ? TEXT("有") : TEXT("无"),
			FragmentItemIds.Num(), FragmentIconImages.Num(), bBuiltLayout ? 1 : 0);
	}
}

void UMetamorphosisSettlementWidget::PlayFeedback(const FText& Title, const FText& Subtitle,
	float MaxOpacity, float InFadeInSeconds)
{
	// 美术素材齐全时不用传进来的文字（FAILED / 文案都烘在图里）；缺素材时兜底
	if (LayerCache.Num() == 0)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Settlement] 美术图层缺失，退回文字显示：%s"), *Title.ToString());
	}

	RefreshDynamicValues();

	// 结算中故意挡输入：切到 Visible（空闲时是 HitTestInvisible，不挡）
	SetVisibility(ESlateVisibility::Visible);

	bFeedbackVisible = true;
	TargetOpacity = FMath::Clamp(MaxOpacity, 0.f, 1.f);
	FadeSpeed = InFadeInSeconds > KINDA_SMALL_NUMBER ? (1.f / InFadeInSeconds) : 100.f;
	CurrentOpacity = 0.f;
	SetRenderOpacity(0.f);

	UE_LOG(LogMetamorphosis, Log, TEXT("[Settlement] 失败界面显示：%s（遮罩 %.2f，淡入 %.2fs）"),
		*Title.ToString(), TargetOpacity, InFadeInSeconds);
}

void UMetamorphosisSettlementWidget::StopFeedback()
{
	bFeedbackVisible = false;
	TargetOpacity = 0.f;
	CurrentOpacity = 0.f;
	SetRenderOpacity(0.f);

	// 空闲时不能挡输入（也不能 Collapsed：那样 Slate 就不再 tick 本控件了）
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMetamorphosisSettlementWidget::HandleRestartClicked()
{
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Settlement] 玩家点击「重新开始」"));
	if (OnRestartRequested.IsBound())
	{
		OnRestartRequested.Broadcast();
		return;
	}

	// 没有外部绑定 -> 自己走完整重置（复用通关界面的"重新开始"：隐藏界面 + ApplyFullLoopReset + 周目+1 + 冷却）
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMetamorphosisLoopSubsystem* Loop = GI->GetSubsystem<UMetamorphosisLoopSubsystem>())
		{
			StopFeedback();
			Loop->RestartAfterWin();
		}
	}
}

void UMetamorphosisSettlementWidget::HandleReturnToTitleClicked()
{
	UE_LOG(LogMetamorphosis, Warning, TEXT("[Settlement] 玩家点击「返回主菜单」"));
	if (OnReturnToTitleRequested.IsBound())
	{
		OnReturnToTitleRequested.Broadcast();
		return;
	}

	// 没有外部绑定 -> 自己返回主界面（先收起失败界面，再走 PC 的处理器：它会挂主界面并保持暂停）
	StopFeedback();
	if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		PC->ReturnToTitleFromSettings();
	}
}

void UMetamorphosisSettlementWidget::SimulatePress(bool bRestart)
{
	if (bRestart)
	{
		HandleRestartClicked();
	}
	else
	{
		HandleReturnToTitleClicked();
	}
}

void UMetamorphosisSettlementWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 淡入（带脏检查：到位了就不再刷）
	if (FMath::IsNearlyEqual(CurrentOpacity, TargetOpacity, 0.002f))
	{
		if (!FMath::IsNearlyEqual(GetRenderOpacity(), TargetOpacity, 0.002f))
		{
			CurrentOpacity = TargetOpacity;
			SetRenderOpacity(TargetOpacity);
		}
		return;
	}

	CurrentOpacity = FMath::FInterpConstantTo(CurrentOpacity, TargetOpacity, InDeltaTime, FadeSpeed);
	SetRenderOpacity(CurrentOpacity);
}
