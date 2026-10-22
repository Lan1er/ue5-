// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisInteractPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Config/GameConfigTypes.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Interaction/MetamorphosisInteractionComponent.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Metamorphosis.h"
#include "MetamorphosisPlayerController.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisInteractPrompt"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakePromptWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	/** 四边全透明的按钮样式：圆的外观由里面的 UImage 画，按钮自己不画底
	 *  （不这么做的话圆外面会套一个方角底板）
	 *  注意名字必须带 Interact 前缀：UE 的 unity build 会把多个 cpp 拼进同一个编译单元，
	 *  匿名命名空间里同名函数会直接撞车（MetamorphosisHotbarWidget.cpp 里也有一个同功能的）。 */
	FButtonStyle MakeInteractInvisibleButtonStyle()
	{
		FButtonStyle Style;
		const FSlateColorBrush Transparent(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Normal = Transparent;
		Style.Hovered = Transparent;
		Style.Pressed = Transparent;
		Style.Disabled = Transparent;
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}
}

UMetamorphosisInteractPromptWidget::UMetamorphosisInteractPromptWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 占位文字用中文/字母（引擎自带字体一定有字形）；别用 ⌨ 之类的符号，量出来是 0 会看不见
	KeyHintText = LOCTEXT("InteractKeyHint", "R");

	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		KeyHintFont = FSlateFontInfo(RobotoFont.Object, 30, FName("Bold"));
		PromptFont = FSlateFontInfo(RobotoFont.Object, 24, FName("Bold"));
		// 移动端手指挡视线，字比 PC 大一号
		MobilePromptFont = FSlateFontInfo(RobotoFont.Object, 34, FName("Bold"));
	}

	// ---------------- 交付 2 美术（提示徽章 + 提示底板） ----------------
	// 只有拿到资源才覆盖成美术样式；拿不到就还是纯代码的圆 + 文字（不会变空白 ✗）
	static ConstructorHelpers::FObjectFinder<UTexture2D> PromptBadgeTex(
		TEXT("/Game/UI/Delivery2/Buttons/T_PromptBadge.T_PromptBadge"));
	if (PromptBadgeTex.Succeeded())
	{
		FSlateBrush Badge;
		Badge.SetResourceObject(PromptBadgeTex.Object);
		Badge.ImageSize = FVector2D(60.f, 60.f);
		Badge.DrawAs = ESlateBrushDrawType::Image;
		Badge.TintColor = FSlateColor(FLinearColor::White);
		BadgeBrush = Badge;
	}

	static ConstructorHelpers::FObjectFinder<UTexture2D> PromptPlateTex(
		TEXT("/Game/UI/Delivery2/Buttons/T_PromptPlate.T_PromptPlate"));
	if (PromptPlateTex.Succeeded())
	{
		FSlateBrush Plate;
		Plate.SetResourceObject(PromptPlateTex.Object);
		Plate.ImageSize = FVector2D(75.f, 94.f);
		// ⚠️ 必须用 Image：实测这张贴图刷子在 DrawAs=Box（九宫格）下**整块不画** ✗，
		//    换 Image 就正常（同一张图挂徽章 UImage 上也正常）。所以底板用普通拉伸，
		//    撕边会随文字长短轻微变形 —— TODO：需要美术给一张可九宫格的版本再换回 Box。
		Plate.DrawAs = ESlateBrushDrawType::Image;
		Plate.Margin = PromptPlateMargin;
		Plate.TintColor = FSlateColor(FLinearColor::White);
		PromptPlateBrush = Plate;
	}
	// TEMP-DEBUG（定位底板不显示，验完删）：看两张图到底有没有拿到、什么画法、边距多少
	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[PromptArt] 徽章图=%d 底板图=%d 底板DrawAs=%d 边距=(%.0f,%.0f,%.0f,%.0f) ImageSize=(%.0f,%.0f)"),
		BadgeBrush.GetResourceObject() != nullptr,
		PromptPlateBrush.GetResourceObject() != nullptr,
		static_cast<int32>(PromptPlateBrush.DrawAs),
		PromptPlateMargin.Left, PromptPlateMargin.Top, PromptPlateMargin.Right, PromptPlateMargin.Bottom,
		PromptPlateBrush.ImageSize.X, PromptPlateBrush.ImageSize.Y);
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisInteractPromptWidget::Initialize()
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

void UMetamorphosisInteractPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 根节点自己不吃触摸（吃的话会吞掉屏幕上所有手势），只有圆按钮能点
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	ApplyAppearanceMode(ShouldUseMobileAppearance());

	if (BadgeButton != nullptr)
	{
		BadgeButton->OnClicked.AddDynamic(this, &UMetamorphosisInteractPromptWidget::HandleBadgeClicked);
	}
	if (MobileBadgeButton != nullptr)
	{
		MobileBadgeButton->OnClicked.AddDynamic(this, &UMetamorphosisInteractPromptWidget::HandleMobileBadgeClicked);
	}

	// 默认收起（只收内部的行，控件本身保持可见以便继续 tick）
	SetActiveRowVisible(false);
	bLastVisible = false;

	RefreshNow();
}

void UMetamorphosisInteractPromptWidget::NativeDestruct()
{
	if (BadgeButton != nullptr)
	{
		BadgeButton->OnClicked.RemoveDynamic(this, &UMetamorphosisInteractPromptWidget::HandleBadgeClicked);
	}
	if (MobileBadgeButton != nullptr)
	{
		MobileBadgeButton->OnClicked.RemoveDynamic(this, &UMetamorphosisInteractPromptWidget::HandleMobileBadgeClicked);
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------

void UMetamorphosisInteractPromptWidget::BuildDefaultLayout()
{
	// 根 = 全屏 Overlay（SelfHitTestInvisible，不挡输入）
	UOverlay* Root = MakePromptWidget<UOverlay>(WidgetTree, TEXT("InteractPromptRoot"));
	WidgetTree->RootWidget = Root;

	// CanvasPanel 负责"按锚点摆位置"。
	// 注意别把 CanvasPanel 直接当根：全屏拉伸的 Canvas 当根会被裁掉（本项目踩过）。
	PromptCanvas = MakePromptWidget<UCanvasPanel>(WidgetTree, TEXT("InteractPromptCanvas"));
	if (UOverlaySlot* CanvasSlot = Root->AddChildToOverlay(PromptCanvas))
	{
		CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
		CanvasSlot->SetVerticalAlignment(VAlign_Fill);
	}

	BuildPcRow(PromptCanvas);
	BuildMobileRow(PromptCanvas);
}

void UMetamorphosisInteractPromptWidget::BuildPcRow(UCanvasPanel* Canvas)
{
	if (Canvas == nullptr)
	{
		return;
	}

	// PC 一行：[圆徽标(带按键字符 R)] [小道具图标] [文字]
	PromptRow = MakePromptWidget<UHorizontalBox>(WidgetTree, TEXT("InteractPromptRow"));
	if (UCanvasPanelSlot* RowSlot = Canvas->AddChildToCanvas(PromptRow))
	{
		RowSlot->SetAnchors(FAnchors(ScreenAnchor.X, ScreenAnchor.Y));   // 锚点 = 这块提示的中心
		RowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		RowSlot->SetAutoSize(true);
	}

	// ---- 1) 圆形按键徽标（本身就是按钮） ----
	USizeBox* BadgeBox = MakePromptWidget<USizeBox>(WidgetTree);
	BadgeBox->SetWidthOverride(BadgeSize);
	BadgeBox->SetHeightOverride(BadgeSize);
	if (UHorizontalBoxSlot* BoxSlot = PromptRow->AddChildToHorizontalBox(BadgeBox))
	{
		BoxSlot->SetPadding(FMargin(0.f, 0.f, ElementSpacing, 0.f));
		BoxSlot->SetVerticalAlignment(VAlign_Center);
	}

	BadgeButton = MakePromptWidget<UButton>(WidgetTree, TEXT("InteractBadgeButton"));
	BadgeButton->SetStyle(MakeInteractInvisibleButtonStyle());
	BadgeBox->AddChild(BadgeButton);

	UOverlay* BadgeOverlay = MakePromptWidget<UOverlay>(WidgetTree);
	BadgeButton->AddChild(BadgeOverlay);
	// ⚠️ 要拿**按钮内容（Overlay）自己的槽**：BadgeButton->Slot 是"按钮在 SizeBox 里的槽"，
	//    对它设 HAlign_Fill 没有任何作用，圆会被压成子内容的尺寸（踩过）。
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(BadgeOverlay->Slot))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f));
	}

	BadgeImage = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractBadgeImage"));
	if (UOverlaySlot* ImageSlot = BadgeOverlay->AddChildToOverlay(BadgeImage))
	{
		ImageSlot->SetHorizontalAlignment(HAlign_Fill);
		ImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	KeyHint = MakePromptWidget<UTextBlock>(WidgetTree, TEXT("InteractKeyHint"));
	KeyHint->SetText(KeyHintText);
	KeyHint->SetJustification(ETextJustify::Center);
	if (UOverlaySlot* KeySlot = BadgeOverlay->AddChildToOverlay(KeyHint))
	{
		KeySlot->SetHorizontalAlignment(HAlign_Center);
		KeySlot->SetVerticalAlignment(VAlign_Center);
	}

	// ---- 2) 道具图标 ----
	USizeBox* IconBox = MakePromptWidget<USizeBox>(WidgetTree);
	IconBox->SetWidthOverride(IconSize);
	IconBox->SetHeightOverride(IconSize);
	if (UHorizontalBoxSlot* BoxSlot = PromptRow->AddChildToHorizontalBox(IconBox))
	{
		BoxSlot->SetPadding(FMargin(0.f, 0.f, ElementSpacing, 0.f));
		BoxSlot->SetVerticalAlignment(VAlign_Center);
	}

	ItemIcon = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractItemIcon"));
	ItemIcon->SetBrush(FSlateColorBrush(FLinearColor::White));   // 默认白块，之后按占位色/真图刷
	IconBox->AddChild(ItemIcon);
	if (USizeBoxSlot* IconSlot = Cast<USizeBoxSlot>(ItemIcon->Slot))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Fill);
		IconSlot->SetVerticalAlignment(VAlign_Fill);
		IconSlot->SetPadding(FMargin(0.f));
	}

	// ---- 3) 提示文字（交付 2：垫在美术底板上） ----
	// 结构：SizeBox(高度 = 文字高 + 内边距) -> Overlay[ UImage 底板(Fill) , UTextBlock(Center) ]
	// 为什么不用 UBorder：实测 UBorder 挂这张贴图刷子画不出来（纯色刷子却能画），换 UImage 才可靠
	// （同一张图在徽章 UImage 上是正常的 -> 结论：贴图刷子在 UImage 里才画）。
	USizeBox* TextPlateBox = MakePromptWidget<USizeBox>(WidgetTree, TEXT("InteractPromptGroup"));
	TextPlateBox->SetMinDesiredHeight(PromptPlateHeight);
	if (UHorizontalBoxSlot* GroupSlot = PromptRow->AddChildToHorizontalBox(TextPlateBox))
	{
		GroupSlot->SetVerticalAlignment(VAlign_Center);
	}

	UOverlay* TextGroup = MakePromptWidget<UOverlay>(WidgetTree);
	TextPlateBox->AddChild(TextGroup);
	if (USizeBoxSlot* OverlaySlot = Cast<USizeBoxSlot>(TextGroup->Slot))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}

	if (PromptPlateBrush.GetResourceObject() != nullptr)
	{
		// 先垫一层"同配色代码底板"（一定画得出来），再叠美术贴图（画得出来就更贴美术）。
		// 这样无论贴图渲染是否正常，文字永远有底可读；排查记录见 Docs/UI交付2接入.md 第十二节。
		PromptPlate = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractPromptPlate"));
		PromptPlate->SetBrush(FSlateRoundedBoxBrush(
			PromptPlateFillColor,
			PromptPlateCornerRadius,
			PromptPlateEdgeColor,
			PromptPlateEdgeWidth));
		PromptPlate->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* PlateSlot = TextGroup->AddChildToOverlay(PromptPlate))
		{
			PlateSlot->SetHorizontalAlignment(HAlign_Fill);
			PlateSlot->SetVerticalAlignment(VAlign_Fill);
		}

		PromptPlateArt = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractPromptPlateArt"));
		PromptPlateArt->SetBrush(PromptPlateBrush);
		PromptPlateArt->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* ArtSlot = TextGroup->AddChildToOverlay(PromptPlateArt))
		{
			ArtSlot->SetHorizontalAlignment(HAlign_Fill);
			ArtSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	PromptText = MakePromptWidget<UTextBlock>(WidgetTree, TEXT("InteractPromptText"));
	PromptText->SetJustification(ETextJustify::Left);
	if (UOverlaySlot* TextSlot = TextGroup->AddChildToOverlay(PromptText))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(PromptPlatePadding);
	}
}

void UMetamorphosisInteractPromptWidget::BuildMobileRow(UCanvasPanel* Canvas)
{
	if (Canvas == nullptr)
	{
		return;
	}

	// 移动端一行：[文字（在圆的左侧）] [大圆按钮（圆里画道具图标）]
	// 和 PC 的区别：没有按键字符、圆明显更大、文字在圆的左边、字号更大。
	MobilePromptRow = MakePromptWidget<UHorizontalBox>(WidgetTree, TEXT("InteractMobileRow"));
	if (UCanvasPanelSlot* RowSlot = Canvas->AddChildToCanvas(MobilePromptRow))
	{
		RowSlot->SetAnchors(FAnchors(MobileScreenAnchor.X, MobileScreenAnchor.Y));
		RowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		RowSlot->SetAutoSize(true);
	}

	// ---- 1) 文字（左，交付 2：垫在美术底板上，结构同 PC） ----
	USizeBox* MobileTextPlateBox = MakePromptWidget<USizeBox>(WidgetTree, TEXT("InteractMobilePromptGroup"));
	MobileTextPlateBox->SetMinDesiredHeight(PromptPlateHeight * 1.35f);   // 移动端字号更大
	if (UHorizontalBoxSlot* TextSlot = MobilePromptRow->AddChildToHorizontalBox(MobileTextPlateBox))
	{
		TextSlot->SetPadding(FMargin(0.f, 0.f, MobileTextSpacing, 0.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	UOverlay* MobileTextGroup = MakePromptWidget<UOverlay>(WidgetTree);
	MobileTextPlateBox->AddChild(MobileTextGroup);
	if (USizeBoxSlot* OverlaySlot = Cast<USizeBoxSlot>(MobileTextGroup->Slot))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}

	if (PromptPlateBrush.GetResourceObject() != nullptr)
	{
		MobilePromptPlate = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractMobilePromptPlate"));
		MobilePromptPlate->SetBrush(FSlateRoundedBoxBrush(
			PromptPlateFillColor, PromptPlateCornerRadius, PromptPlateEdgeColor, PromptPlateEdgeWidth));
		MobilePromptPlate->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* PlateSlot = MobileTextGroup->AddChildToOverlay(MobilePromptPlate))
		{
			PlateSlot->SetHorizontalAlignment(HAlign_Fill);
			PlateSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	MobilePromptText = MakePromptWidget<UTextBlock>(WidgetTree, TEXT("InteractMobilePromptText"));
	MobilePromptText->SetJustification(ETextJustify::Right);
	if (UOverlaySlot* MobileTextSlot = MobileTextGroup->AddChildToOverlay(MobilePromptText))
	{
		MobileTextSlot->SetHorizontalAlignment(HAlign_Center);
		MobileTextSlot->SetVerticalAlignment(VAlign_Center);
		MobileTextSlot->SetPadding(PromptPlatePaddingMobile);
	}

	// ---- 2) 大圆按钮（右） ----
	USizeBox* ButtonBox = MakePromptWidget<USizeBox>(WidgetTree);
	ButtonBox->SetWidthOverride(MobileButtonSize);
	ButtonBox->SetHeightOverride(MobileButtonSize);
	if (UHorizontalBoxSlot* BoxSlot = MobilePromptRow->AddChildToHorizontalBox(ButtonBox))
	{
		BoxSlot->SetVerticalAlignment(VAlign_Center);
	}

	MobileBadgeButton = MakePromptWidget<UButton>(WidgetTree, TEXT("InteractMobileButton"));
	MobileBadgeButton->SetStyle(MakeInteractInvisibleButtonStyle());
	ButtonBox->AddChild(MobileBadgeButton);

	UOverlay* ButtonOverlay = MakePromptWidget<UOverlay>(WidgetTree);
	MobileBadgeButton->AddChild(ButtonOverlay);
	// 同上：拿内容自己的槽才能真正 Fill（否则大圆会被压成方块符号的大小）
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(ButtonOverlay->Slot))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f));
	}

	// 圆底：显式给一个"和按钮一样大"的 SizeBox，保证圆一定撑满（不依赖容器的 desired size 推断）
	USizeBox* CircleBox = MakePromptWidget<USizeBox>(WidgetTree);
	CircleBox->SetWidthOverride(MobileButtonSize);
	CircleBox->SetHeightOverride(MobileButtonSize);
	if (UOverlaySlot* CircleSlot = ButtonOverlay->AddChildToOverlay(CircleBox))
	{
		CircleSlot->SetHorizontalAlignment(HAlign_Fill);
		CircleSlot->SetVerticalAlignment(VAlign_Fill);
	}

	MobileBadgeImage = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractMobileBadgeImage"));
	CircleBox->AddChild(MobileBadgeImage);
	if (USizeBoxSlot* CircleImageSlot = Cast<USizeBoxSlot>(MobileBadgeImage->Slot))
	{
		CircleImageSlot->SetHorizontalAlignment(HAlign_Fill);
		CircleImageSlot->SetVerticalAlignment(VAlign_Fill);
		CircleImageSlot->SetPadding(FMargin(0.f));
	}

	// 真图
	USizeBox* IconBox = MakePromptWidget<USizeBox>(WidgetTree);
	IconBox->SetWidthOverride(MobileIconSize);
	IconBox->SetHeightOverride(MobileIconSize);
	if (UOverlaySlot* IconSlot = ButtonOverlay->AddChildToOverlay(IconBox))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}

	MobileItemIcon = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractMobileItemIcon"));
	MobileItemIcon->SetBrush(FSlateColorBrush(FLinearColor::White));
	IconBox->AddChild(MobileItemIcon);
	if (USizeBoxSlot* MarkSlot = Cast<USizeBoxSlot>(MobileItemIcon->Slot))
	{
		MarkSlot->SetHorizontalAlignment(HAlign_Fill);
		MarkSlot->SetVerticalAlignment(VAlign_Fill);
		MarkSlot->SetPadding(FMargin(0.f));
	}

	// 没有真图时叠在圆里的方块符号（纯几何，不依赖字体字形 —— 用符号字体很容易量出 0 宽度）
	const float MarkSize = MobileButtonSize * MobileSlotMarkRatio;
	USizeBox* MarkBox = MakePromptWidget<USizeBox>(WidgetTree);
	MarkBox->SetWidthOverride(MarkSize);
	MarkBox->SetHeightOverride(MarkSize);
	if (UOverlaySlot* MarkSlot = ButtonOverlay->AddChildToOverlay(MarkBox))
	{
		MarkSlot->SetHorizontalAlignment(HAlign_Center);
		MarkSlot->SetVerticalAlignment(VAlign_Center);
	}

	MobileSlotMark = MakePromptWidget<UImage>(WidgetTree, TEXT("InteractMobileSlotMark"));
	MobileSlotMark->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.92f), MarkSize * 0.22f));
	MarkBox->AddChild(MobileSlotMark);
	if (USizeBoxSlot* MarkSlot = Cast<USizeBoxSlot>(MobileSlotMark->Slot))
	{
		MarkSlot->SetHorizontalAlignment(HAlign_Fill);
		MarkSlot->SetVerticalAlignment(VAlign_Fill);
		MarkSlot->SetPadding(FMargin(0.f));
	}
}

void UMetamorphosisInteractPromptWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	// PC 行
	if (PromptRow == nullptr)       { PromptRow = WidgetTree->FindWidget<UHorizontalBox>(TEXT("InteractPromptRow")); }
	if (BadgeButton == nullptr)     { BadgeButton = WidgetTree->FindWidget<UButton>(TEXT("InteractBadgeButton")); }
	if (BadgeImage == nullptr)      { BadgeImage = WidgetTree->FindWidget<UImage>(TEXT("InteractBadgeImage")); }
	if (KeyHint == nullptr)         { KeyHint = WidgetTree->FindWidget<UTextBlock>(TEXT("InteractKeyHint")); }
	if (ItemIcon == nullptr)        { ItemIcon = WidgetTree->FindWidget<UImage>(TEXT("InteractItemIcon")); }
	if (PromptText == nullptr)      { PromptText = WidgetTree->FindWidget<UTextBlock>(TEXT("InteractPromptText")); }

	// 移动端行
	if (MobilePromptRow == nullptr)  { MobilePromptRow = WidgetTree->FindWidget<UHorizontalBox>(TEXT("InteractMobileRow")); }
	if (MobileBadgeButton == nullptr) { MobileBadgeButton = WidgetTree->FindWidget<UButton>(TEXT("InteractMobileButton")); }
	if (MobileBadgeImage == nullptr) { MobileBadgeImage = WidgetTree->FindWidget<UImage>(TEXT("InteractMobileBadgeImage")); }
	if (MobileSlotMark == nullptr)   { MobileSlotMark = WidgetTree->FindWidget<UImage>(TEXT("InteractMobileSlotMark")); }
	if (MobileItemIcon == nullptr)   { MobileItemIcon = WidgetTree->FindWidget<UImage>(TEXT("InteractMobileItemIcon")); }
	if (MobilePromptText == nullptr) { MobilePromptText = WidgetTree->FindWidget<UTextBlock>(TEXT("InteractMobilePromptText")); }

	// 缺哪套就报哪套：WBP 只做一套也能跑，另一套会自动退回内置布局
	TArray<FString> MissingPc;
	if (PromptRow == nullptr)  { MissingPc.Add(TEXT("InteractPromptRow")); }
	if (BadgeButton == nullptr){ MissingPc.Add(TEXT("InteractBadgeButton")); }
	if (PromptText == nullptr) { MissingPc.Add(TEXT("InteractPromptText")); }

	TArray<FString> MissingMobile;
	if (MobilePromptRow == nullptr)  { MissingMobile.Add(TEXT("InteractMobileRow")); }
	if (MobileBadgeButton == nullptr){ MissingMobile.Add(TEXT("InteractMobileButton")); }
	if (MobilePromptText == nullptr) { MissingMobile.Add(TEXT("InteractMobilePromptText")); }

	if (MissingPc.Num() > 0 || MissingMobile.Num() > 0)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] %s 里找不到控件：PC 行[%s] 移动行[%s]（做 WBP 子类时按这些名字命名即可被接管）"),
			*GetName(), *FString::Join(MissingPc, TEXT(", ")), *FString::Join(MissingMobile, TEXT(", ")));
	}
}

// ---------------------------------------------------------------------------
// 外观（PC / 移动端两套）
// ---------------------------------------------------------------------------

bool UMetamorphosisInteractPromptWidget::ShouldUseMobileAppearance() const
{
	// "移动端模式"的唯一判据在 PlayerController 上（真机恒真；PC 上 Meta.UI.Mobile 可强制）
	if (const AMetamorphosisPlayerController* Controller =
		Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		return Controller->IsMobileUI();
	}
	return false;
}

FSlateBrush UMetamorphosisInteractPromptWidget::MakeBadgeBrush(float Diameter) const
{
	if (BadgeBrush.GetResourceObject() != nullptr)
	{
		return BadgeBrush;
	}
	return FSlateRoundedBoxBrush(BadgeFillColor, Diameter * 0.5f, BadgeEdgeColor, BadgeEdgeWidth);
}

void UMetamorphosisInteractPromptWidget::ApplyAppearanceMode(bool bMobile)
{
	// 1) 锚点
	if (PromptRow != nullptr)
	{
		if (UCanvasPanelSlot* RowSlot = Cast<UCanvasPanelSlot>(PromptRow->Slot))
		{
			RowSlot->SetAnchors(FAnchors(ScreenAnchor.X, ScreenAnchor.Y));
			RowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			RowSlot->SetAutoSize(true);
		}
	}
	if (MobilePromptRow != nullptr)
	{
		if (UCanvasPanelSlot* RowSlot = Cast<UCanvasPanelSlot>(MobilePromptRow->Slot))
		{
			RowSlot->SetAnchors(FAnchors(MobileScreenAnchor.X, MobileScreenAnchor.Y));
			RowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			RowSlot->SetAutoSize(true);
		}
	}

	// 2) 圆底（尺寸在 BuildPcRow/BuildMobileRow 里给；这里只刷"画成什么样"）
	if (BadgeImage != nullptr)
	{
		BadgeImage->SetBrush(MakeBadgeBrush(BadgeSize));
	}
	if (MobileBadgeImage != nullptr)
	{
		MobileBadgeImage->SetBrush(MakeBadgeBrush(MobileButtonSize));
	}

	// 3) 按键字符：PC 显示 R，移动端不显示（圆按钮本身就是操作方式）
	if (KeyHint != nullptr)
	{
		if (KeyHintFont.HasValidFont())
		{
			KeyHint->SetFont(KeyHintFont);
		}
		KeyHint->SetColorAndOpacity(FSlateColor(KeyHintColor));
		KeyHint->SetText(KeyHintText);
		KeyHint->SetVisibility((!bMobile && bShowKeyHint)
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	// 4) 文字：移动端字号更大
	if (PromptText != nullptr)
	{
		if (PromptFont.HasValidFont())
		{
			PromptText->SetFont(PromptFont);
		}
		PromptText->SetColorAndOpacity(FSlateColor(PromptTextColor));
		PromptText->SetShadowColorAndOpacity(PromptShadowColor);
		PromptText->SetShadowOffset(FVector2D(1.5f, 1.5f));
		PromptText->SetJustification(ETextJustify::Left);
	}
	if (MobilePromptText != nullptr)
	{
		if (MobilePromptFont.HasValidFont())
		{
			MobilePromptText->SetFont(MobilePromptFont);
		}
		else if (PromptFont.HasValidFont())
		{
			MobilePromptText->SetFont(PromptFont);
		}
		MobilePromptText->SetColorAndOpacity(FSlateColor(PromptTextColor));
		MobilePromptText->SetShadowColorAndOpacity(PromptShadowColor);
		MobilePromptText->SetShadowOffset(FVector2D(2.f, 2.f));
		MobilePromptText->SetJustification(ETextJustify::Right);
	}

	bAppearanceApplied = true;

	// 换外观会把圆底重刷成默认样式（圆 = 深色底），所以要让"道具外观"重新刷一次：
	// 清掉脏检查键，下一次 Refresh 就会重刷图标/占位色（否则切到移动端后圆一直是深色，
	// 直到玩家换一个道具才恢复 —— 踩过）。
	LastItemId.Reset();
	LastCount = -1;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[UI] 交互提示外观：%s（PC = R 键小徽标 + 图标 + 文字；移动端 = 大圆按钮 + 圆内图标 + 左侧文字）"),
		bMobile ? TEXT("移动端") : TEXT("PC"));
}

void UMetamorphosisInteractPromptWidget::SetActiveRowVisible(bool bVisible)
{
	const bool bMobile = ShouldUseMobileAppearance();

	// 非当前外观的那一行永远收起（避免两套同时出现）
	if (PromptRow != nullptr)
	{
		const bool bShow = bVisible && !bMobile;
		PromptRow->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (MobilePromptRow != nullptr)
	{
		const bool bShow = bVisible && bMobile;
		MobilePromptRow->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	bLastVisible = bVisible;
}

// ---------------------------------------------------------------------------
// 取交互组件
// ---------------------------------------------------------------------------

UMetamorphosisInteractionComponent* UMetamorphosisInteractPromptWidget::GetInteraction() const
{
	const APlayerController* Controller = GetOwningPlayer();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	return Pawn != nullptr ? Pawn->FindComponentByClass<UMetamorphosisInteractionComponent>() : nullptr;
}

// ---------------------------------------------------------------------------
// 刷新（带脏检查）
// ---------------------------------------------------------------------------

void UMetamorphosisInteractPromptWidget::SetPromptText(const FString& Text)
{
	if (PromptText != nullptr && LastPrompt != Text)
	{
		PromptText->SetText(FText::FromString(Text));
	}
	if (MobilePromptText != nullptr && LastPrompt != Text)
	{
		MobilePromptText->SetText(FText::FromString(Text));
	}
	LastPrompt = Text;
}

void UMetamorphosisInteractPromptWidget::ApplyItemVisuals(const FMetamorphosisInteractTarget& Target)
{
	// 图标：换了道具才重新加载（LoadSynchronous 有开销，别每帧调）
	UTexture2D* LoadedIcon = nullptr;
	if (const UMetamorphosisInteractionComponent* Interaction = GetInteraction())
	{
		if (const AActor* Owner = Interaction->GetOwner())
		{
			if (const UMetamorphosisInventoryComponent* Inventory =
				Owner->FindComponentByClass<UMetamorphosisInventoryComponent>())
			{
				LoadedIcon = Inventory->GetItemIcon(FName(*Target.ItemId)).LoadSynchronous();
			}
		}
	}

	// ⚠️ 通关点没有道具配表（Config == nullptr）-> 这里必须判空，否则一走近缝隙就空指针崩溃
	const FLinearColor PlaceholderColor = Target.Config != nullptr
		? UMetamorphosisInventoryComponent::ParsePlaceholderColor(Target.Config->PlaceholderColor)
		: FLinearColor(0.85f, 0.78f, 0.55f, 1.f);

	// ---- PC：小色块 / 真图 ----
	// 不是"道具"的目标（通关点「从缝隙出去」等 Config == nullptr）不画图标：
	// 那里挂一个占位色方块会显得像"要捡的东西"，反而误导。（美术底板本身已经很完整了。）
	const bool bHasItemVisual = (Target.Config != nullptr) || (LoadedIcon != nullptr);
	if (ItemIcon != nullptr)
	{
		if (LoadedIcon != nullptr)
		{
			ItemIcon->SetBrushFromTexture(LoadedIcon, false);
			ItemIcon->SetColorAndOpacity(FLinearColor::White);
			ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else if (Target.Config != nullptr)
		{
			// 美术图没到：用配表的占位色染一个色块（和快捷栏同一套解析）
			ItemIcon->SetBrush(FSlateColorBrush(FLinearColor::White));
			ItemIcon->SetColorAndOpacity(PlaceholderColor);
			ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			ItemIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ---- 移动端：圆里画图标；没图就用占位色填充整个圆 + 一个方块符号 ----
	if (MobileBadgeImage != nullptr)
	{
		if (LoadedIcon != nullptr)
		{
			MobileBadgeImage->SetBrush(MakeBadgeBrush(MobileButtonSize));
		}
		else
		{
			// 占位色填充圆（描边弱一点，让颜色占主导）
			MobileBadgeImage->SetBrush(FSlateRoundedBoxBrush(
				FLinearColor(PlaceholderColor.R, PlaceholderColor.G, PlaceholderColor.B, 0.95f),
				MobileButtonSize * 0.5f, BadgeEdgeColor, BadgeEdgeWidth));
		}
	}
	if (MobileItemIcon != nullptr)
	{
		if (LoadedIcon != nullptr)
		{
			MobileItemIcon->SetBrushFromTexture(LoadedIcon, false);
			MobileItemIcon->SetColorAndOpacity(FLinearColor::White);
			MobileItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			MobileItemIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (MobileSlotMark != nullptr)
	{
		// 只有"没有真图"时才叠方块符号
		MobileSlotMark->SetVisibility(LoadedIcon != nullptr
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	UE_LOG(LogMetamorphosis, Verbose, TEXT("[UI] 交互提示图标：%s -> %s"),
		*Target.ItemId, LoadedIcon != nullptr ? *LoadedIcon->GetName() : TEXT("占位色/符号"));
}

void UMetamorphosisInteractPromptWidget::Refresh(const FMetamorphosisInteractTarget& Target)
{
	// 提示文案统一问交互组件：它负责通关点（「从缝隙出去」）、剧情道具（「查看 xx」）和普通道具（拾取/吃掉）。
	// ⚠️ 绝对不能直接取 Target.Config->DisplayName：**通关点没有道具配表**（Config == nullptr），
	//    那样会空指针崩溃（2026-10-18 实测：走到缝隙旁边、提示一出现就崩，崩溃地址 0x18 = Config 偏移后的 DisplayName）。
	const UMetamorphosisInteractionComponent* Interaction = GetInteraction();
	const FString Prompt = Interaction != nullptr
		? Interaction->GetPromptText()
		: UMetamorphosisInteractionComponent::MakePromptText(
			Target.Mode,
			Target.Config != nullptr ? Target.Config->DisplayName : FString(),
			Target.PromptOverride);
	// 数量 > 1 才带 xN（和快捷栏的角标一致）
	const FString FullPrompt = Target.Count > 1
		? FString::Printf(TEXT("%s x%d"), *Prompt, Target.Count)
		: Prompt;

	SetPromptText(FullPrompt);

	if (LastItemId != Target.ItemId || LastCount < 0)
	{
		ApplyItemVisuals(Target);
	}

	LastItemId = Target.ItemId;
	LastCount = Target.Count;
	LastMode = static_cast<uint8>(Target.Mode);
}

void UMetamorphosisInteractPromptWidget::RefreshNow()
{
	// 外观跟着"移动端模式"走（PC 上 Meta.UI.Mobile 切换后这里会立刻换）
	const bool bMobile = ShouldUseMobileAppearance();
	if (!bAppearanceApplied || bMobile != bLastMobileUI)
	{
		bLastMobileUI = bMobile;
		ApplyAppearanceMode(bMobile);
	}

	// 显示/隐藏只动内部那一行：整个控件保持可见，Slate 才会继续 tick 它（见头文件说明）
	// 暂停时不显示、不响应（对齐触摸/输入的早退逻辑）
	if (UGameplayStatics::IsGamePaused(this))
	{
		SetActiveRowVisible(false);
		return;
	}

	const UMetamorphosisInteractionComponent* Interaction = GetInteraction();
	if (Interaction == nullptr || !Interaction->HasTarget())
	{
		SetActiveRowVisible(false);
		return;
	}

	SetActiveRowVisible(true);
	Refresh(Interaction->GetTarget());
}

void UMetamorphosisInteractPromptWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	RefreshNow();
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------

void UMetamorphosisInteractPromptWidget::HandleBadgeClicked()
{
	// 点圆徽标 = 按 R（PC 上也能点）
	if (UMetamorphosisInteractionComponent* Interaction = GetInteraction())
	{
		Interaction->TryInteract();
		RefreshNow();
	}
}

void UMetamorphosisInteractPromptWidget::HandleMobileBadgeClicked()
{
	// 移动端的主要交互方式
	HandleBadgeClicked();
}

#undef LOCTEXT_NAMESPACE
