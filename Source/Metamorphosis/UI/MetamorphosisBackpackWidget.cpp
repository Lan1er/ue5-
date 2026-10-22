// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisBackpackWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Brushes/SlateColorBrush.h"
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
#include "Components/Widget.h"
#include "Config/MetamorphosisConfigSubsystem.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisBackpack"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakeBagWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	FSlateBrush MakeBagColorBrush(const FLinearColor& Color)
	{
		return FSlateColorBrush(Color);
	}

	/** 贴图（九宫格，整数缩放不糊） */
	FSlateBrush MakeBagTextureBrush(UTexture2D* Texture, const FVector2D& Size, float Margin = 0.f)
	{
		FSlateBrush Brush;
		if (Texture == nullptr)
		{
			return FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		Brush.DrawAs = Margin > 0.f ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
		Brush.Margin = FMargin(Margin);
		Brush.TintColor = FSlateColor(FLinearColor::White);
		return Brush;
	}

	/** 读 /Game 下的贴图（不存在返回 nullptr） */
	UTexture2D* LoadBagTexture(const TCHAR* Path)
	{
		return LoadObject<UTexture2D>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UTexture2D* SlotTexture(bool bHovered)
	{
		static TWeakObjectPtr<UTexture2D> Cached[2];
		const int32 Index = bHovered ? 1 : 0;
		if (!Cached[Index].IsValid())
		{
			Cached[Index] = bHovered
				? LoadBagTexture(TEXT("/Game/UI/Inventory/Slot_Hover.Slot_Hover"))
				: LoadBagTexture(TEXT("/Game/UI/Inventory/Slot_Default.Slot_Default"));
		}
		return Cached[Index].Get();
	}

	FButtonStyle MakeBagInvisibleButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Hovered = FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.08f));
		Style.Pressed = FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}

	FString CategoryLabel(const FString& Category)
	{
		if (Category == TEXT("Tool")) { return TEXT("工具"); }
		if (Category == TEXT("Food")) { return TEXT("食物"); }
		if (Category == TEXT("Medicine")) { return TEXT("药物"); }
		if (Category == TEXT("Story")) { return TEXT("剧情道具"); }
		return Category;
	}

	/** 把配表里的效果数值拼成给人看的一行 */
	FString BuildEffectSummary(const FItemConfigEntry& Entry)
	{
		TArray<FString> Parts;
		if (!FMath::IsNearlyZero(Entry.MoodDelta))
		{
			Parts.Add(FString::Printf(TEXT("躁郁 %+.0f"), Entry.MoodDelta));
		}
		if (!FMath::IsNearlyZero(Entry.SuspicionDelta))
		{
			Parts.Add(FString::Printf(TEXT("怀疑 %+.0f"), Entry.SuspicionDelta));
		}
		if (!FMath::IsNearlyZero(Entry.SpeedBonusPercent))
		{
			Parts.Add(FString::Printf(TEXT("永久速度 %+.0f%%"), Entry.SpeedBonusPercent));
		}
		if (!FMath::IsNearlyZero(Entry.SpiderDefense))
		{
			Parts.Add(FString::Printf(TEXT("防蜘蛛 %.0f"), Entry.SpiderDefense));
		}
		if (!FMath::IsNearlyZero(Entry.DoorBlockSeconds))
		{
			Parts.Add(FString::Printf(TEXT("卡门 %.0f 秒"), Entry.DoorBlockSeconds));
		}
		if (Parts.Num() == 0)
		{
			return Entry.bConsumable ? TEXT("使用后消耗") : TEXT("查看用（不消耗）");
		}
		return FString::Join(Parts, TEXT("　"));
	}
}

UMetamorphosisBackpackWidget::UMetamorphosisBackpackWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		TitleFont = FSlateFontInfo(RobotoFont.Object, 18, FName("Bold"));
		BodyFont = FSlateFontInfo(RobotoFont.Object, 12, FName("Regular"));
		SmallFont = FSlateFontInfo(RobotoFont.Object, 11, FName("Regular"));
	}
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisBackpackWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
		bBuiltLayout = true;
	}

	CacheWidgets();
	return bResult;
}

void UMetamorphosisBackpackWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 全屏遮罩：要吃掉身后的点击（不能 HitTestInvisible）
	SetVisibility(ESlateVisibility::Visible);

	if (SlotButtons.IsValidIndex(0) && SlotButtons[0] != nullptr) { SlotButtons[0]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked0); }
	if (SlotButtons.IsValidIndex(1) && SlotButtons[1] != nullptr) { SlotButtons[1]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked1); }
	if (SlotButtons.IsValidIndex(2) && SlotButtons[2] != nullptr) { SlotButtons[2]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked2); }
	if (SlotButtons.IsValidIndex(3) && SlotButtons[3] != nullptr) { SlotButtons[3]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked3); }
	if (SlotButtons.IsValidIndex(4) && SlotButtons[4] != nullptr) { SlotButtons[4]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked4); }
	if (SlotButtons.IsValidIndex(5) && SlotButtons[5] != nullptr) { SlotButtons[5]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked5); }
	if (SlotButtons.IsValidIndex(6) && SlotButtons[6] != nullptr) { SlotButtons[6]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked6); }
	if (SlotButtons.IsValidIndex(7) && SlotButtons[7] != nullptr) { SlotButtons[7]->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked7); }
	if (CloseButton != nullptr) { CloseButton->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleCloseClicked); }

	// Esc 关闭要能收到按键
	SetKeyboardFocus();

	LastSignature.Reset();
	RefreshBackpack();
}

void UMetamorphosisBackpackWidget::NativeDestruct()
{
	if (SlotButtons.IsValidIndex(0) && SlotButtons[0] != nullptr) { SlotButtons[0]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked0); }
	if (SlotButtons.IsValidIndex(1) && SlotButtons[1] != nullptr) { SlotButtons[1]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked1); }
	if (SlotButtons.IsValidIndex(2) && SlotButtons[2] != nullptr) { SlotButtons[2]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked2); }
	if (SlotButtons.IsValidIndex(3) && SlotButtons[3] != nullptr) { SlotButtons[3]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked3); }
	if (SlotButtons.IsValidIndex(4) && SlotButtons[4] != nullptr) { SlotButtons[4]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked4); }
	if (SlotButtons.IsValidIndex(5) && SlotButtons[5] != nullptr) { SlotButtons[5]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked5); }
	if (SlotButtons.IsValidIndex(6) && SlotButtons[6] != nullptr) { SlotButtons[6]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked6); }
	if (SlotButtons.IsValidIndex(7) && SlotButtons[7] != nullptr) { SlotButtons[7]->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleSlotClicked7); }
	if (CloseButton != nullptr) { CloseButton->OnClicked.RemoveDynamic(this, &UMetamorphosisBackpackWidget::HandleCloseClicked); }

	Super::NativeDestruct();
}

void UMetamorphosisBackpackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	// 书本尺寸按视口算（屏宽 75%），视口变了才重设
	UpdateBookTargetSize();

	// 内容变了才刷（背包开着的时候玩家也可能通过调试命令塞道具）
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return;
	}

	FString Signature;
	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		const FMetamorphosisItemSlot ItemSlot = Inventory->GetSlot(Index);
		Signature += FString::Printf(TEXT("%d:%s:%d;"), ItemSlot.bOccupied ? 1 : 0, *ItemSlot.ItemId.ToString(), ItemSlot.Count);
	}
	if (Signature != LastSignature)
	{
		RefreshBackpack();
	}
}

FReply UMetamorphosisBackpackWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::B)
	{
		HandleCloseClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ---------------------------------------------------------------------------
// 布局：照美术的 808x350 书本画布摆
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 书本尺寸：屏幕上占屏宽 75%（竖屏时按高度上限反推，避免溢出）
// ---------------------------------------------------------------------------
void UMetamorphosisBackpackWidget::UpdateBookTargetSize()
{
	if (BookTargetBox == nullptr)
	{
		return;
	}

	// 同剧情面板：用控件自身的 local size（真正的 UMG 坐标空间），不要用 GetViewportSize（差 1.5 倍）
	FVector2D Viewport = GetCachedGeometry().GetLocalSize();
	if (Viewport.X < 100.f || Viewport.Y < 100.f)
	{
		Viewport = UWidgetLayoutLibrary::GetViewportSize(this);
	}
	if (Viewport.X < 100.f || Viewport.Y < 100.f)
	{
		return;
	}
	if (FMath::Abs(Viewport.X - LastBookViewportSize.X) < 1.f &&
		FMath::Abs(Viewport.Y - LastBookViewportSize.Y) < 1.f)
	{
		return;
	}
	LastBookViewportSize = Viewport;

	const float Aspect = (BookSize.X > 0.f) ? (BookSize.Y / BookSize.X) : 0.4332f;
	float Width = Viewport.X * FMath::Clamp(BookScreenWidthRatio, 0.3f, 1.f);
	float Height = Width * Aspect;
	// 高度超上限就按高度反推宽度（竖屏/超宽屏时不溢出）
	const float MaxHeight = Viewport.Y * FMath::Clamp(BookMaxScreenHeightRatio, 0.3f, 1.f);
	if (Height > MaxHeight && Aspect > 0.f)
	{
		Height = MaxHeight;
		Width = Height / Aspect;
	}

	BookTargetBox->SetWidthOverride(Width);
	BookTargetBox->SetHeightOverride(Height);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 背包书本：视口 %.0fx%.0f -> %.0fx%.0f（占屏宽 %.0f%%）"),
		Viewport.X, Viewport.Y, Width, Height, Width / Viewport.X * 100.f);
}
void UMetamorphosisBackpackWidget::BuildDefaultLayout()
{
	// 根：全屏 Overlay（遮罩 + 居中的书本）
	UOverlay* Root = MakeBagWidget<UOverlay>(WidgetTree, TEXT("BackpackRoot"));
	WidgetTree->RootWidget = Root;

	UImage* Dim = MakeBagWidget<UImage>(WidgetTree, TEXT("BackpackDim"));
	Dim->SetBrush(MakeBagColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.62f)));
	if (UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim))
	{
		DimSlot->SetHorizontalAlignment(HAlign_Fill);
		DimSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// 书本：外层 SizeBox 决定"屏幕上多大"（按视口算：屏宽 75%），内部仍是美术设计画布 808x350，
	// 靠 ScaleBox 等比放大（2026-10-17：原来固定 808x350 只占屏宽 63%，用户要求放大到 3/4 屏）。
	BookTargetBox = MakeBagWidget<USizeBox>(WidgetTree, TEXT("BackpackTargetBox"));
	BookTargetBox->SetWidthOverride(BookSize.X);
	BookTargetBox->SetHeightOverride(BookSize.Y);
	if (UOverlaySlot* TargetSlot = Root->AddChildToOverlay(BookTargetBox))
	{
		TargetSlot->SetHorizontalAlignment(HAlign_Center);
		TargetSlot->SetVerticalAlignment(VAlign_Center);
	}

	UScaleBox* BookScale = MakeBagWidget<UScaleBox>(WidgetTree, TEXT("BackpackScale"));
	BookScale->SetStretch(EStretch::ScaleToFit);
	BookScale->SetStretchDirection(EStretchDirection::Both);
	if (UOverlaySlot* ScaleSlot = Cast<UOverlaySlot>(BookTargetBox->AddChild(BookScale)))
	{
		ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
		ScaleSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* BookBox = MakeBagWidget<USizeBox>(WidgetTree, TEXT("BackpackBox"));
	BookBox->SetWidthOverride(BookSize.X);
	BookBox->SetHeightOverride(BookSize.Y);
	BookScale->AddChild(BookBox);

	BookCanvas = MakeBagWidget<UCanvasPanel>(WidgetTree, TEXT("BackpackCanvas"));
	BookBox->AddChild(BookCanvas);

	auto AddCanvasChild = [this](UWidget* Widget, const FVector2D& Pos, const FVector2D& Size)
	{
		UCanvasPanelSlot* PanelSlot = BookCanvas->AddChildToCanvas(Widget);
		if (PanelSlot != nullptr)
		{
			PanelSlot->SetAutoSize(false);
			PanelSlot->SetPosition(Pos);
			PanelSlot->SetSize(Size);
		}
		return PanelSlot;
	};

	auto AddImage = [this, &AddCanvasChild](const TCHAR* AssetPath, const TCHAR* Name,
		const FVector2D& Pos, const FVector2D& Size, int32 ZOrder)
	{
		if (UTexture2D* Texture = LoadBagTexture(AssetPath))
		{
			UImage* Image = MakeBagWidget<UImage>(WidgetTree, Name);
			Image->SetBrush(MakeBagTextureBrush(Texture, Size));
			Image->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCanvasPanelSlot* PanelSlot = AddCanvasChild(Image, Pos, Size);
			if (PanelSlot != nullptr)
			{
				PanelSlot->SetZOrder(ZOrder);
			}
			return Image;
		}
		return static_cast<UImage*>(nullptr);
	};

	// 书页（底层）
	AddImage(TEXT("/Game/UI/Backpack/Bag_LeftPage.Bag_LeftPage"), TEXT("BagLeftPage"), FVector2D(0.f, 25.f), FVector2D(470.f, 325.f), 0);
	AddImage(TEXT("/Game/UI/Backpack/Bag_RightPage.Bag_RightPage"), TEXT("BagRightPage"), FVector2D(510.f, 25.f), FVector2D(298.f, 325.f), 0);
	AddImage(TEXT("/Game/UI/Backpack/Bag_Gutter.Bag_Gutter"), TEXT("BagGutter"), FVector2D(470.f, 5.f), FVector2D(20.f, 340.f), 1);
	AddImage(TEXT("/Game/UI/Backpack/Bag_Binding.Bag_Binding"), TEXT("BagBinding"), FVector2D(0.f, 45.f), FVector2D(25.f, 281.f), 2);

	// 8 个格子（压着左页印好的 4x2 格线）
	SlotButtons.Reset();
	SlotIcons.Reset();
	SlotArts.Reset();
	SlotOutlines.Reset();

	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		const int32 Column = Index % 4;
		const int32 Row = Index / 4;
		const FVector2D CellPos(
			SlotOrigin.X + Column * (SlotCellSize.X + SlotGap),
			SlotOrigin.Y + Row * (SlotCellSize.Y + SlotGap));

		// 选中金框（底层，压在格底下面一点）
		UBorder* Outline = MakeBagWidget<UBorder>(WidgetTree, *FString::Printf(TEXT("BagSlotOutline_%d"), Index));
		Outline->SetBrush(MakeBagColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));
		Outline->SetPadding(FMargin(2.f));
		if (UCanvasPanelSlot* OutlineSlot = AddCanvasChild(Outline, CellPos - FVector2D(2.f, 2.f), SlotCellSize + FVector2D(4.f, 4.f)))
		{
			OutlineSlot->SetZOrder(2);
		}
		SlotOutlines.Add(Outline);

		UButton* Button = MakeBagWidget<UButton>(WidgetTree, *FString::Printf(TEXT("BagSlot_%d"), Index));
		Button->SetStyle(MakeBagInvisibleButtonStyle());
		if (UCanvasPanelSlot* ButtonSlot = AddCanvasChild(Button, CellPos, SlotCellSize))
		{
			ButtonSlot->SetZOrder(3);
		}

		UOverlay* CellOverlay = MakeBagWidget<UOverlay>(WidgetTree);
		Button->AddChild(CellOverlay);

		UImage* Art = MakeBagWidget<UImage>(WidgetTree, *FString::Printf(TEXT("BagSlotArt_%d"), Index));
		if (UTexture2D* SlotTex = SlotTexture(false))
		{
			Art->SetBrush(MakeBagTextureBrush(SlotTex, SlotCellSize, 10.f));
		}
		Art->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* ArtSlot = CellOverlay->AddChildToOverlay(Art))
		{
			ArtSlot->SetHorizontalAlignment(HAlign_Fill);
			ArtSlot->SetVerticalAlignment(VAlign_Fill);
		}

		USizeBox* IconBox = MakeBagWidget<USizeBox>(WidgetTree);
		// 图标占格子 0.62 -> 0.84：美术图标到位后放大，背包（3/4 屏）里一眼能认出来
		IconBox->SetWidthOverride(SlotCellSize.X * 0.84f);
		IconBox->SetHeightOverride(SlotCellSize.Y * 0.84f);
		if (UOverlaySlot* IconSlot = CellOverlay->AddChildToOverlay(IconBox))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}

		UImage* Icon = MakeBagWidget<UImage>(WidgetTree, *FString::Printf(TEXT("BagSlotIcon_%d"), Index));
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		IconBox->AddChild(Icon);

		SlotButtons.Add(Button);
		SlotIcons.Add(Icon);
		SlotArts.Add(Art);
	}

	// 右页详情：预览框 + 名字 + 分类/堆叠 + 描述
	AddImage(TEXT("/Game/UI/Backpack/Bag_PreviewPaper.Bag_PreviewPaper"), TEXT("BagPreviewPaper"), FVector2D(607.f, 46.f), FVector2D(104.f, 144.f), 2);
	AddImage(TEXT("/Game/UI/Backpack/Bag_PreviewFrame.Bag_PreviewFrame"), TEXT("BagPreviewFrame"), FVector2D(604.f, 42.f), FVector2D(108.f, 150.f), 3);

	DetailName = MakeBagWidget<UTextBlock>(WidgetTree, TEXT("BagDetailName"));
	DetailName->SetFont(TitleFont);
	DetailName->SetColorAndOpacity(FSlateColor(InkColor));
	DetailName->SetAutoWrapText(true);
	if (UCanvasPanelSlot* NameSlot = AddCanvasChild(DetailName, FVector2D(524.f, 200.f), FVector2D(272.f, 26.f)))
	{
		NameSlot->SetZOrder(4);
	}

	DetailMeta = MakeBagWidget<UTextBlock>(WidgetTree, TEXT("BagDetailMeta"));
	DetailMeta->SetFont(SmallFont);
	DetailMeta->SetColorAndOpacity(FSlateColor(InkSubColor));
	DetailMeta->SetAutoWrapText(true);
	if (UCanvasPanelSlot* MetaSlot = AddCanvasChild(DetailMeta, FVector2D(524.f, 226.f), FVector2D(272.f, 20.f)))
	{
		MetaSlot->SetZOrder(4);
	}

	DetailDesc = MakeBagWidget<UTextBlock>(WidgetTree, TEXT("BagDetailDesc"));
	DetailDesc->SetFont(BodyFont);
	DetailDesc->SetColorAndOpacity(FSlateColor(InkColor));
	DetailDesc->SetAutoWrapText(true);
	// 2026-10-17：高度 84 -> 44，给下方「使用/丢弃」两个按钮腾出位置（长描述会裁一点，可看剧情文本）
	if (UCanvasPanelSlot* DescSlot = AddCanvasChild(DetailDesc, FVector2D(524.f, 250.f), FVector2D(272.f, 44.f)))
	{
		DescSlot->SetZOrder(4);
	}

	// 详情下方的操作按钮（使用/查看、丢弃）—— 直接在这里建，复用上面的 AddCanvasChild lambda
	auto MakeInkStyle = [](const FLinearColor& NormalC, const FLinearColor& HoverC, const FLinearColor& PressedC)
	{
		FButtonStyle Style;
		Style.Normal = MakeBagColorBrush(NormalC);
		Style.Hovered = MakeBagColorBrush(HoverC);
		Style.Pressed = MakeBagColorBrush(PressedC);
		Style.Disabled = MakeBagColorBrush(FLinearColor(0.35f, 0.31f, 0.26f, 0.10f));
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	};
	const FButtonStyle ActionStyle = MakeInkStyle(
		FLinearColor(0.18f, 0.15f, 0.10f, 0.88f),
		FLinearColor(0.30f, 0.24f, 0.14f, 0.95f),
		FLinearColor(0.72f, 0.58f, 0.24f, 1.f));
	const FLinearColor ActionLabelColor(0.96f, 0.92f, 0.84f, 1.f);

	auto MakeActionButton = [&](const TCHAR* ButtonName, const TCHAR* LabelName, const FVector2D& Pos, const FText& LabelText)
	{
		UButton* ActionButton = MakeBagWidget<UButton>(WidgetTree, ButtonName);
		ActionButton->SetStyle(ActionStyle);
		if (UCanvasPanelSlot* ButtonSlot = AddCanvasChild(ActionButton, Pos, FVector2D(130.f, 28.f)))
		{
			ButtonSlot->SetZOrder(4);
		}
		UTextBlock* ActionLabel = MakeBagWidget<UTextBlock>(WidgetTree, LabelName);
		ActionLabel->SetFont(BodyFont);
		ActionLabel->SetColorAndOpacity(FSlateColor(ActionLabelColor));
		ActionLabel->SetJustification(ETextJustify::Center);
		ActionLabel->SetText(LabelText);
		ActionLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		ActionButton->AddChild(ActionLabel);
		return ActionButton;
	};

	UseButton = MakeActionButton(TEXT("BagUseButton"), TEXT("BagUseLabel"), FVector2D(524.f, 298.f), LOCTEXT("BagUse", "使用"));
	DropButton = MakeActionButton(TEXT("BagDropButton"), TEXT("BagDropLabel"), FVector2D(666.f, 298.f), LOCTEXT("BagDrop", "丢弃"));
	UseButtonLabel = Cast<UTextBlock>(UseButton != nullptr ? UseButton->GetChildAt(0) : nullptr);
	DropButtonLabel = Cast<UTextBlock>(DropButton != nullptr ? DropButton->GetChildAt(0) : nullptr);

	if (UseButton != nullptr)
	{
		UseButton->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleUseClicked);
	}
	if (DropButton != nullptr)
	{
		DropButton->OnClicked.AddDynamic(this, &UMetamorphosisBackpackWidget::HandleDropClicked);
	}

	// 外框 + 标题（盖在最上面）
	AddImage(TEXT("/Game/UI/Backpack/Bag_Frame.Bag_Frame"), TEXT("BagFrame"), FVector2D(0.f, 0.f), BookSize, 5);
	AddImage(TEXT("/Game/UI/Backpack/Bag_Title.Bag_Title"), TEXT("BagTitle"), FVector2D(44.f, 4.f), FVector2D(96.f, 36.f), 6);

	// 关闭按钮
	CloseButton = MakeBagWidget<UButton>(WidgetTree, TEXT("BagCloseButton"));
	CloseButton->SetStyle(MakeBagInvisibleButtonStyle());
	if (UCanvasPanelSlot* CloseSlot = AddCanvasChild(CloseButton, FVector2D(764.f, 10.f), FVector2D(34.f, 34.f)))
	{
		CloseSlot->SetZOrder(7);
	}
	UImage* CloseImage = MakeBagWidget<UImage>(WidgetTree, TEXT("BagCloseImage"));
	if (UTexture2D* CloseTex = LoadBagTexture(TEXT("/Game/UI/Backpack/Bag_Close.Bag_Close")))
	{
		CloseImage->SetBrush(MakeBagTextureBrush(CloseTex, FVector2D(21.f, 22.f)));
	}
	CloseImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	CloseButton->AddChild(CloseImage);
}

void UMetamorphosisBackpackWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (BookCanvas == nullptr)
	{
		BookCanvas = WidgetTree->FindWidget<UCanvasPanel>(TEXT("BackpackCanvas"));
	}
	if (CloseButton == nullptr)
	{
		CloseButton = WidgetTree->FindWidget<UButton>(TEXT("BagCloseButton"));
	}

	SlotButtons.Reset();
	SlotIcons.Reset();
	SlotArts.Reset();
	SlotOutlines.Reset();
	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		SlotButtons.Add(WidgetTree->FindWidget<UButton>(*FString::Printf(TEXT("BagSlot_%d"), Index)));
		SlotIcons.Add(WidgetTree->FindWidget<UImage>(*FString::Printf(TEXT("BagSlotIcon_%d"), Index)));
		SlotArts.Add(WidgetTree->FindWidget<UImage>(*FString::Printf(TEXT("BagSlotArt_%d"), Index)));
		SlotOutlines.Add(WidgetTree->FindWidget<UBorder>(*FString::Printf(TEXT("BagSlotOutline_%d"), Index)));
	}

	if (DetailName == nullptr)
	{
		DetailName = WidgetTree->FindWidget<UTextBlock>(TEXT("BagDetailName"));
	}
	if (DetailMeta == nullptr)
	{
		DetailMeta = WidgetTree->FindWidget<UTextBlock>(TEXT("BagDetailMeta"));
	}
	if (DetailDesc == nullptr)
	{
		DetailDesc = WidgetTree->FindWidget<UTextBlock>(TEXT("BagDetailDesc"));
	}
	if (UseButton == nullptr)
	{
		UseButton = WidgetTree->FindWidget<UButton>(TEXT("BagUseButton"));
	}
	if (DropButton == nullptr)
	{
		DropButton = WidgetTree->FindWidget<UButton>(TEXT("BagDropButton"));
	}
	if (UseButtonLabel == nullptr)
	{
		UseButtonLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("BagUseLabel"));
	}
	if (DropButtonLabel == nullptr)
	{
		DropButtonLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("BagDropLabel"));
	}
}

// ---------------------------------------------------------------------------
// 数据
// ---------------------------------------------------------------------------

UMetamorphosisInventoryComponent* UMetamorphosisBackpackWidget::GetInventory() const
{
	const APlayerController* Controller = GetOwningPlayer();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	return Pawn != nullptr ? Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>() : nullptr;
}

void UMetamorphosisBackpackWidget::RefreshBackpack()
{
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return;
	}

	LastSignature.Reset();
	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		const FMetamorphosisItemSlot ItemSlot = Inventory->GetSlot(Index);
		LastSignature += FString::Printf(TEXT("%d:%s:%d;"), ItemSlot.bOccupied ? 1 : 0, *ItemSlot.ItemId.ToString(), ItemSlot.Count);

		if (!SlotIcons.IsValidIndex(Index) || SlotIcons[Index] == nullptr)
		{
			continue;
		}

		UTexture2D* IconTexture = ItemSlot.bOccupied ? GetInventory() ? GetInventory()->GetItemIconResolved(ItemSlot.ItemId) : nullptr : nullptr;
		if (IconTexture != nullptr)
		{
			SlotIcons[Index]->SetBrush(MakeBagTextureBrush(IconTexture, SlotCellSize));
			SlotIcons[Index]->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			SlotIcons[Index]->SetBrush(MakeBagColorBrush(FLinearColor::White));
			SlotIcons[Index]->SetColorAndOpacity(ItemSlot.bOccupied ? ItemSlot.PlaceholderColor : FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
	}

	// 金框标出正在查看的格子
	for (int32 Index = 0; Index < SlotOutlines.Num(); ++Index)
	{
		if (SlotOutlines[Index] == nullptr)
		{
			continue;
		}
		SlotOutlines[Index]->SetBrush(MakeBagColorBrush(Index == InspectIndex
			? HighlightColor
			: FLinearColor(0.f, 0.f, 0.f, 0.f)));
	}

	UpdateDetail();
}

void UMetamorphosisBackpackWidget::InspectSlot(int32 Index)
{
	InspectIndex = Index;
	UpdateDetail();
	for (int32 SlotIndex = 0; SlotIndex < SlotOutlines.Num(); ++SlotIndex)
	{
		if (SlotOutlines[SlotIndex] != nullptr)
		{
			SlotOutlines[SlotIndex]->SetBrush(MakeBagColorBrush(SlotIndex == InspectIndex
				? HighlightColor
				: FLinearColor(0.f, 0.f, 0.f, 0.f)));
		}
	}
}

void UMetamorphosisBackpackWidget::UpdateDetail()
{
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return;
	}

	const FMetamorphosisItemSlot ItemSlot = (InspectIndex >= 0 && InspectIndex < UMetamorphosisInventoryComponent::NumSlots)
		? Inventory->GetSlot(InspectIndex)
		: FMetamorphosisItemSlot();

	FString Name = TEXT("—");
	FString Meta;
	FString Desc = TEXT("点一个格子看看。");
	const FItemConfigEntry* DetailEntry = nullptr;

	if (ItemSlot.bOccupied)
	{
		Name = ItemSlot.DisplayName.ToString();
		// 有道具时不要保留占位提示（否则会出现「点一个格子看看。」和真实描述同时显示）
		Desc.Reset();

		const UMetamorphosisConfigSubsystem* Config = nullptr;
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			Config = GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>();
		}
		const FItemConfigEntry* Entry = Config != nullptr ? Config->GetItem(ItemSlot.ItemId) : nullptr;
		DetailEntry = Entry;
		if (Entry != nullptr)
		{
			if (Name.IsEmpty())
			{
				Name = Entry->DisplayName;
			}
			Meta = FString::Printf(TEXT("%s · 堆叠 %d/%d"), *CategoryLabel(Entry->Category),
				ItemSlot.Count, FMath::Max(1, Entry->MaxStack));
			if (!Entry->Description.IsEmpty())
			{
				Desc = Entry->Description;
			}
			const FString Effects = BuildEffectSummary(*Entry);
			if (!Effects.IsEmpty())
			{
				Desc += TEXT("\n") + Effects;
			}
		}
		else
		{
			Meta = FString::Printf(TEXT("数量 %d"), ItemSlot.Count);
		}
	}

	if (DetailName != nullptr)
	{
		DetailName->SetText(FText::FromString(Name));
	}
	if (DetailMeta != nullptr)
	{
		DetailMeta->SetText(FText::FromString(Meta));
	}
	if (DetailDesc != nullptr)
	{
		DetailDesc->SetText(FText::FromString(Desc));
	}

	UpdateActionButtons(ItemSlot, DetailEntry);
}

// ---------------------------------------------------------------------------
// 详情下方的操作按钮（使用 / 丢弃）
// ---------------------------------------------------------------------------

void UMetamorphosisBackpackWidget::UpdateActionButtons(const FMetamorphosisItemSlot& ItemSlot, const FItemConfigEntry* Entry)
{
	const bool bHasItem = ItemSlot.bOccupied && Entry != nullptr;

	// 可消耗 -> 「使用」；剧情/工具（bConsumable=false）-> 「查看」，点了只打日志、不消耗
	const bool bConsumable = Entry != nullptr && Entry->bConsumable;

	if (UseButton != nullptr)
	{
		UseButton->SetIsEnabled(bHasItem);
		UseButton->SetRenderOpacity(bHasItem ? 1.f : 0.45f);
	}
	if (DropButton != nullptr)
	{
		DropButton->SetIsEnabled(bHasItem);
		DropButton->SetRenderOpacity(bHasItem ? 1.f : 0.45f);
	}
	if (UseButtonLabel != nullptr)
	{
		UseButtonLabel->SetText(bConsumable ? LOCTEXT("BagUseConsume", "使用") : LOCTEXT("BagUseInspect", "查看"));
		UseButtonLabel->SetColorAndOpacity(FSlateColor(bHasItem
			? FLinearColor(0.96f, 0.92f, 0.84f, 1.f)
			: FLinearColor(0.45f, 0.41f, 0.35f, 1.f)));
	}
	if (DropButtonLabel != nullptr)
	{
		DropButtonLabel->SetColorAndOpacity(FSlateColor(bHasItem
			? FLinearColor(0.96f, 0.92f, 0.84f, 1.f)
			: FLinearColor(0.45f, 0.41f, 0.35f, 1.f)));
	}
}

void UMetamorphosisBackpackWidget::HandleUseClicked()
{
	UseInspectedSlot();
}

void UMetamorphosisBackpackWidget::HandleDropClicked()
{
	DropInspectedSlot();
}

bool UMetamorphosisBackpackWidget::UseInspectedSlot()
{
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return false;
	}
	if (InspectIndex < 0 || InspectIndex >= UMetamorphosisInventoryComponent::NumSlots)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Bag] 还没选格子，无法「使用」"));
		return false;
	}
	const bool bDone = Inventory->UseSlot(InspectIndex);
	RefreshBackpack();
	UE_LOG(LogMetamorphosis, Log, TEXT("[Bag] 对第 %d 格执行「使用/查看」 -> %s"),
		InspectIndex + 1, bDone ? TEXT("成功") : TEXT("未生效（空格/不可消耗/不在配表）"));
	return bDone;
}

bool UMetamorphosisBackpackWidget::DropInspectedSlot()
{
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return false;
	}
	if (InspectIndex < 0 || InspectIndex >= UMetamorphosisInventoryComponent::NumSlots)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Bag] 还没选格子，无法「丢弃」"));
		return false;
	}
	const bool bDone = Inventory->DropSlot(InspectIndex);
	RefreshBackpack();
	UE_LOG(LogMetamorphosis, Log, TEXT("[Bag] 对第 %d 格执行「丢弃」 -> %s"),
		InspectIndex + 1, bDone ? TEXT("成功（已生成世界物件）") : TEXT("失败（空格/生成失败）"));
	return bDone;
}

// ---------------------------------------------------------------------------
// 点击
// ---------------------------------------------------------------------------

void UMetamorphosisBackpackWidget::HandleSlotClicked(int32 Index)
{
	InspectSlot(Index);
}

void UMetamorphosisBackpackWidget::HandleSlotClicked0() { HandleSlotClicked(0); }
void UMetamorphosisBackpackWidget::HandleSlotClicked1() { HandleSlotClicked(1); }
void UMetamorphosisBackpackWidget::HandleSlotClicked2() { HandleSlotClicked(2); }
void UMetamorphosisBackpackWidget::HandleSlotClicked3() { HandleSlotClicked(3); }
void UMetamorphosisBackpackWidget::HandleSlotClicked4() { HandleSlotClicked(4); }
void UMetamorphosisBackpackWidget::HandleSlotClicked5() { HandleSlotClicked(5); }
void UMetamorphosisBackpackWidget::HandleSlotClicked6() { HandleSlotClicked(6); }
void UMetamorphosisBackpackWidget::HandleSlotClicked7() { HandleSlotClicked(7); }

void UMetamorphosisBackpackWidget::HandleCloseClicked()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 背包：点了关闭"));
	OnBackpackClosed.Broadcast();
}

#undef LOCTEXT_NAMESPACE



