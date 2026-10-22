// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisSettingsButtonWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Metamorphosis.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FSlateBrush MakeSettingsButtonBrush(const FLinearColor& Color, float Radius)
	{
		// 圆形底：用 RoundedBox 画（不需要美术资源）
		return FSlateRoundedBoxBrush(Color, Radius);
	}
}

UMetamorphosisSettingsButtonWidget::UMetamorphosisSettingsButtonWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ---- 交付 2「实机界面」的齿轮按钮素材（65x62，自带暗底 + 金线）----
	// 拿到美术图后：图标铺满、代码不再画圆底（否则会有两层底），尺寸按美术比例放大一点。
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> IconFinder(
			TEXT("/Game/UI/Delivery2/Buttons/T_Settings_Default.T_Settings_Default"));
		if (IconFinder.Succeeded())
		{
			IconBrush.SetResourceObject(IconFinder.Object);
			IconBrush.ImageSize = FVector2D(IconFinder.Object->GetSurfaceWidth(), IconFinder.Object->GetSurfaceHeight());
			IconBrush.DrawAs = ESlateBrushDrawType::Image;
			IconBrush.ImageType = ESlateBrushImageType::FullColor;
			IconRatio = 1.f;
			ButtonSize = 62.f;
			// 美术图自带底 → 代码的圆底全透明，只留 hover/pressed 的轻微提亮
			BackgroundColor = FLinearColor(1.f, 1.f, 1.f, 0.f);
			HoveredColor = FLinearColor(1.f, 0.96f, 0.82f, 0.22f);
			PressedColor = FLinearColor(0.95f, 0.80f, 0.35f, 0.35f);
			IconColor = FLinearColor::White;
		}
	}
}

bool UMetamorphosisSettingsButtonWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
		bBuiltLayout = true;
	}

	return bResult;
}

void UMetamorphosisSettingsButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// **千万别设 Visible**：根是铺满屏幕的 Overlay，设 Visible 会把整屏点击全吃掉，
	// 导致快捷栏、上下按钮都点不到（踩过）。SelfHitTestInvisible = 自己不吃输入，只有按钮吃。
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (IconButton != nullptr)
	{
		IconButton->OnClicked.AddDynamic(this, &UMetamorphosisSettingsButtonWidget::HandleClicked);
	}

	ApplyCornerMargin();
}

void UMetamorphosisSettingsButtonWidget::NativeDestruct()
{
	if (IconButton != nullptr)
	{
		IconButton->OnClicked.RemoveDynamic(this, &UMetamorphosisSettingsButtonWidget::HandleClicked);
	}

	Super::NativeDestruct();
}

void UMetamorphosisSettingsButtonWidget::BuildDefaultLayout()
{
	// 根铺满屏幕，按钮用 Overlay 顶到右上角（真正的边距由 PlayerController 给控件位置）
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SettingsButtonRoot"));
	// 根 Overlay 本身不吃输入（只有里面的按钮吃）
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	ButtonBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SettingsButtonBox"));
	ButtonBox->SetWidthOverride(ButtonSize);
	ButtonBox->SetHeightOverride(ButtonSize);
	if (UOverlaySlot* BoxSlot = Root->AddChildToOverlay(ButtonBox))
	{
		BoxSlot->SetHorizontalAlignment(HAlign_Right);
		BoxSlot->SetVerticalAlignment(VAlign_Top);
	}

	IconButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SettingsButton"));
	IconButton->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(ButtonOpacity, 0.f, 1.f)));

	// 三种状态的圆底
	FButtonStyle Style;
	Style.Normal = MakeSettingsButtonBrush(BackgroundColor, ButtonSize * 0.5f);
	Style.Hovered = MakeSettingsButtonBrush(HoveredColor, ButtonSize * 0.5f);
	Style.Pressed = MakeSettingsButtonBrush(PressedColor, ButtonSize * 0.5f);
	Style.Disabled = MakeSettingsButtonBrush(BackgroundColor, ButtonSize * 0.5f);
	Style.NormalPadding = FMargin(0.f);
	Style.PressedPadding = FMargin(0.f);
	IconButton->SetStyle(Style);
	ButtonBox->AddChild(IconButton);

	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	IconBox->SetWidthOverride(ButtonSize * IconRatio);
	IconBox->SetHeightOverride(ButtonSize * IconRatio);
	UOverlay* IconOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	IconBox->AddChild(IconOverlay);
	IconButton->AddChild(IconBox);

	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SettingsIcon"));
	// 默认用引擎自带矢量图标 Icons.Settings（SlateCore / Starship 图标集）
	if (IconBrush.GetResourceObject() == nullptr)
	{
		IconBrush = *FCoreStyle::Get().GetBrush(TEXT("Icons.Settings"));
	}
	IconImage->SetBrush(IconBrush);
	IconImage->SetColorAndOpacity(IconColor);
	// 图标不吃点击，点击落在按钮上
	IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* IconSlot = IconOverlay->AddChildToOverlay(IconImage))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}

	if (IconBrush.GetResourceObject() == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] 设置按钮没拿到引擎图标 Icons.Settings（打包时记得把 Slate 图标一起 cook，或者换成美术图）"));
	}
}

void UMetamorphosisSettingsButtonWidget::HandleClicked()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 点了右上角设置按钮"));
	OnClicked.Broadcast();
}

void UMetamorphosisSettingsButtonWidget::ApplyCornerMargin()
{
	if (ButtonBox == nullptr)
	{
		return;
	}

	if (UOverlaySlot* BoxSlot = Cast<UOverlaySlot>(ButtonBox->Slot))
	{
		BoxSlot->SetPadding(CornerMargin);
	}
}

