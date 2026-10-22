// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisMobileControlsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Metamorphosis.h"
#include "MetamorphosisPlayerController.h"
#include "Styling/CoreStyle.h"
#include "Types/SlateEnums.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisMobileControls"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakeMobileWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	/** 默认按钮样式：纯色、高对比（深色半透明底），不依赖任何贴图/材质。
	 *  用引擎默认按钮样式的话，材质着色器还没编译完时按钮会是"看不见"的。 */
	FButtonStyle MakeSolidButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.03f, 0.03f, 0.04f, 0.55f));
		Style.Hovered = FSlateColorBrush(FLinearColor(0.10f, 0.10f, 0.12f, 0.70f));
		Style.Pressed = FSlateColorBrush(FLinearColor(0.95f, 0.78f, 0.30f, 0.90f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.03f, 0.03f, 0.04f, 0.20f));
		Style.NormalPadding = FMargin(2.f);
		Style.PressedPadding = FMargin(3.f);
		return Style;
	}
}

UMetamorphosisMobileControlsWidget::UMetamorphosisMobileControlsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 占位文字：用中文字（引擎自带 CJK 回退字体一定能显示）；
	// 不要用 ↑ ↓ 这类符号，默认字体里没有字形，量出来的尺寸是 0，按钮会变成 0x0 看不见。
	// 美术换成图标时把这两个文字清掉、在 WBP 里放图片即可
	AscendText = LOCTEXT("AscendText", "上");
	DescendText = LOCTEXT("DescendText", "下");

	AscendButtonStyle = MakeSolidButtonStyle();
	DescendButtonStyle = AscendButtonStyle;

	// ---- 交付 2「实机界面」的方向按钮素材（上 121x128 / 下 125x122，自带荆棘底框 + 箭头）----
	// 美术图里有箭头了，所以把占位文字"上/下"清空（否则文字叠在箭头上），
	// 并显式打开 bApplyButtonStyles，让本类用自己的按钮样式覆盖引擎默认样式。
	{
		struct FButtonArt
		{
			const TCHAR* Normal;
			const TCHAR* Hover;
			FButtonStyle* Style;
		};

		static ConstructorHelpers::FObjectFinder<UTexture2D> UpNormal(
			TEXT("/Game/UI/Delivery2/Buttons/T_Up_Default.T_Up_Default"));
		static ConstructorHelpers::FObjectFinder<UTexture2D> UpHover(
			TEXT("/Game/UI/Delivery2/Buttons/T_Up_Hover.T_Up_Hover"));
		static ConstructorHelpers::FObjectFinder<UTexture2D> DownNormal(
			TEXT("/Game/UI/Delivery2/Buttons/T_Down_Default.T_Down_Default"));
		static ConstructorHelpers::FObjectFinder<UTexture2D> DownHover(
			TEXT("/Game/UI/Delivery2/Buttons/T_Down_Hover.T_Down_Hover"));

		auto MakeArtBrush = [](UTexture2D* Texture) -> FSlateBrush
		{
			FSlateBrush Brush;
			if (Texture != nullptr)
			{
				Brush.SetResourceObject(Texture);
				Brush.ImageSize = FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageType = ESlateBrushImageType::FullColor;
			}
			else
			{
				Brush = FSlateColorBrush(FLinearColor(0.03f, 0.03f, 0.04f, 0.55f));
			}
			return Brush;
		};

		if (UpNormal.Succeeded() && DownNormal.Succeeded())
		{
			AscendButtonStyle.Normal = MakeArtBrush(UpNormal.Object);
			AscendButtonStyle.Hovered = MakeArtBrush(UpHover.Succeeded() ? UpHover.Object : UpNormal.Object);
			AscendButtonStyle.Pressed = MakeArtBrush(UpHover.Succeeded() ? UpHover.Object : UpNormal.Object);
			AscendButtonStyle.Disabled = AscendButtonStyle.Normal;

			DescendButtonStyle.Normal = MakeArtBrush(DownNormal.Object);
			DescendButtonStyle.Hovered = MakeArtBrush(DownHover.Succeeded() ? DownHover.Object : DownNormal.Object);
			DescendButtonStyle.Pressed = MakeArtBrush(DownHover.Succeeded() ? DownHover.Object : DownNormal.Object);
			DescendButtonStyle.Disabled = DescendButtonStyle.Normal;

			AscendButtonStyle.NormalPadding = FMargin(0.f);
			AscendButtonStyle.PressedPadding = FMargin(0.f);
			DescendButtonStyle.NormalPadding = FMargin(0.f);
			DescendButtonStyle.PressedPadding = FMargin(0.f);

			bApplyButtonStyles = true;
			ButtonOpacity = 1.f;
			ButtonSize = 118.f;

			// 美术图自带箭头 -> 占位文字清空
			AscendText = FText::GetEmpty();
			DescendText = FText::GetEmpty();
		}
	}

	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		LabelFont = FSlateFontInfo(RobotoFont.Object, 56, FName("Bold"));
	}
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisMobileControlsWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		// 纯代码搭的默认布局由本类负责按钮样式（做 WBP 子类时保持 False，用设计器里的样式）
		// 注意必须在 BuildDefaultLayout 之前置位
		bApplyButtonStyles = true;
		BuildDefaultLayout();
		bBuiltLayout = true;
	}

	CacheWidgets();
	return bResult;
}

void UMetamorphosisMobileControlsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 自己不吃触摸（否则全屏的控件会把屏幕上所有手势都吞掉），但子控件（按钮）照常接收
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ApplyPanelMargin();

	if (AscendButton != nullptr)
	{
		AscendButton->OnPressed.AddDynamic(this, &UMetamorphosisMobileControlsWidget::HandleAscendPressed);
		AscendButton->OnReleased.AddDynamic(this, &UMetamorphosisMobileControlsWidget::HandleAscendReleased);
	}
	if (DescendButton != nullptr)
	{
		DescendButton->OnPressed.AddDynamic(this, &UMetamorphosisMobileControlsWidget::HandleDescendPressed);
		DescendButton->OnReleased.AddDynamic(this, &UMetamorphosisMobileControlsWidget::HandleDescendReleased);
	}

	ApplyStaticStyle();
}

void UMetamorphosisMobileControlsWidget::NativeDestruct()
{
	if (AscendButton != nullptr)
	{
		AscendButton->OnPressed.RemoveDynamic(this, &UMetamorphosisMobileControlsWidget::HandleAscendPressed);
		AscendButton->OnReleased.RemoveDynamic(this, &UMetamorphosisMobileControlsWidget::HandleAscendReleased);
	}
	if (DescendButton != nullptr)
	{
		DescendButton->OnPressed.RemoveDynamic(this, &UMetamorphosisMobileControlsWidget::HandleDescendPressed);
		DescendButton->OnReleased.RemoveDynamic(this, &UMetamorphosisMobileControlsWidget::HandleDescendReleased);
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------

UButton* UMetamorphosisMobileControlsWidget::MakeButton(
	UVerticalBox* Column, const TCHAR* ButtonName, const TCHAR* LabelName,
	const FText& Label, bool bUseCustomStyle, const FButtonStyle& Style)
{
	USizeBox* Box = MakeMobileWidget<USizeBox>(WidgetTree);
	Box->SetWidthOverride(ButtonSize);
	Box->SetHeightOverride(ButtonSize);
	ButtonBoxes.Add(Box);
	if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Box))
	{
		BoxSlot->SetPadding(FMargin(0.f, ButtonSpacing * 0.5f));
		BoxSlot->SetHorizontalAlignment(HAlign_Right);
	}

	UButton* Button = MakeMobileWidget<UButton>(WidgetTree, ButtonName);
	Button->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(ButtonOpacity, 0.f, 1.f)));
	if (bUseCustomStyle)
	{
		Button->SetStyle(Style);
	}
	Box->AddChild(Button);

	// 关键：把 SizeBox 槽位显式设成拉伸，否则按钮会缩成它内容的尺寸
	// （占位文字如果量不出宽高，按钮就成了 0x0，屏幕上什么都看不到）
	if (USizeBoxSlot* ButtonSlot = Cast<USizeBoxSlot>(Button->Slot))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f));
	}

	UTextBlock* LabelWidget = MakeMobileWidget<UTextBlock>(WidgetTree, LabelName);
	LabelWidget->SetText(Label);
	LabelWidget->SetJustification(ETextJustify::Center);
	Button->AddChild(LabelWidget);

	return Button;
}

void UMetamorphosisMobileControlsWidget::BuildDefaultLayout()
{
	// 整个控件铺满屏幕（视口默认就是全屏拉伸），用 Overlay 把按钮组顶到右下角。
	// 控件本身设成 SelfHitTestInvisible：自己不挡触摸，只有按钮吃输入。
	UOverlay* Root = MakeMobileWidget<UOverlay>(WidgetTree, TEXT("MobileControlsRoot"));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = MakeMobileWidget<UVerticalBox>(WidgetTree, TEXT("MobileControlsColumn"));
	if (UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column))
	{
		ColumnSlot->SetHorizontalAlignment(HAlign_Right);
		ColumnSlot->SetVerticalAlignment(VAlign_Bottom);
	}

	AscendButton = MakeButton(Column, TEXT("AscendButton"), TEXT("AscendLabel"),
		AscendText, bApplyButtonStyles, AscendButtonStyle);

	DescendButton = MakeButton(Column, TEXT("DescendButton"), TEXT("DescendLabel"),
		DescendText, bApplyButtonStyles, DescendButtonStyle);

	ApplyPanelMargin();
}

void UMetamorphosisMobileControlsWidget::ApplyPanelMargin()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (UVerticalBox* Column = Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("MobileControlsColumn"))))
	{
		if (UOverlaySlot* ColumnSlot = Cast<UOverlaySlot>(Column->Slot))
		{
			ColumnSlot->SetPadding(PanelMargin);
		}
	}
}

void UMetamorphosisMobileControlsWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (AscendButton == nullptr)  { AscendButton = WidgetTree->FindWidget<UButton>(TEXT("AscendButton")); }
	if (DescendButton == nullptr) { DescendButton = WidgetTree->FindWidget<UButton>(TEXT("DescendButton")); }
	if (AscendLabel == nullptr)   { AscendLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("AscendLabel")); }
	if (DescendLabel == nullptr)  { DescendLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("DescendLabel")); }

	TArray<FString> Missing;
	if (AscendButton == nullptr)  { Missing.Add(TEXT("AscendButton")); }
	if (DescendButton == nullptr) { Missing.Add(TEXT("DescendButton")); }
	if (Missing.Num() > 0)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] %s 里找不到控件：%s（做 WBP 子类时按这个名字命名按钮即可被接管）"),
			*GetName(), *FString::Join(Missing, TEXT(", ")));
	}
}

void UMetamorphosisMobileControlsWidget::ApplyStaticStyle()
{
	if (LabelFont.HasValidFont())
	{
		if (AscendLabel != nullptr)  { AscendLabel->SetFont(LabelFont); }
		if (DescendLabel != nullptr) { DescendLabel->SetFont(LabelFont); }
	}
	if (AscendLabel != nullptr)  { AscendLabel->SetColorAndOpacity(FSlateColor(LabelColor)); }
	if (DescendLabel != nullptr) { DescendLabel->SetColorAndOpacity(FSlateColor(LabelColor)); }
}

// ---------------------------------------------------------------------------
// 交互 -> 转发给控制器
// ---------------------------------------------------------------------------

void UMetamorphosisMobileControlsWidget::HandleAscendPressed()
{
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->SetAscendButtonHeld(true);
	}
}

void UMetamorphosisMobileControlsWidget::HandleAscendReleased()
{
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->SetAscendButtonHeld(false);
	}
}

void UMetamorphosisMobileControlsWidget::HandleDescendPressed()
{
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->SetDescendButtonHeld(true);
	}
}

void UMetamorphosisMobileControlsWidget::HandleDescendReleased()
{
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->SetDescendButtonHeld(false);
	}
}

#undef LOCTEXT_NAMESPACE

void UMetamorphosisMobileControlsWidget::ApplyScale(float Scale)
{
	CurrentScale = FMath::Clamp(Scale, 0.3f, 3.f);
	const float Size = ButtonSize * CurrentScale;

	for (TObjectPtr<USizeBox>& Box : ButtonBoxes)
	{
		if (Box != nullptr)
		{
			Box->SetWidthOverride(Size);
			Box->SetHeightOverride(Size);
		}
	}

	if (LabelFont.HasValidFont())
	{
		FSlateFontInfo ScaledFont = LabelFont;
		ScaledFont.Size = FMath::Max(10, FMath::RoundToInt(56.f * CurrentScale));
		if (AscendLabel != nullptr)  { AscendLabel->SetFont(ScaledFont); }
		if (DescendLabel != nullptr) { DescendLabel->SetFont(ScaledFont); }
	}
}
