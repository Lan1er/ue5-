// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisPauseMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisPauseMenu"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakePauseWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	FSlateBrush MakePauseBrush(const FLinearColor& Color)
	{
		return FSlateColorBrush(Color);
	}

	/** 九宫格贴图画刷 */
	FSlateBrush MakePauseTextureBrush(UTexture2D* Texture, const FVector2D& Size, float Margin = 0.f)
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

	/** 隐形按钮样式（底板由子层 UImage 画，避免按钮样式里的贴图在某些平台不显示） */
	FButtonStyle MakePauseInvisibleButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Hovered = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Pressed = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}

	FButtonStyle MakePauseFallbackButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.13f, 0.14f, 0.18f, 1.f));
		Style.Hovered = FSlateColorBrush(FLinearColor(0.22f, 0.24f, 0.30f, 1.f));
		Style.Pressed = FSlateColorBrush(FLinearColor(0.90f, 0.72f, 0.25f, 1.f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.70f, 0.70f, 0.70f, 1.f));
		Style.NormalPadding = FMargin(16.f, 10.f);
		Style.PressedPadding = FMargin(17.f, 11.f);
		return Style;
	}

	/** 美术底板按钮样式（九宫格，左右留 ~30px 破损边）。
	 *  注意：美术给的「默认底板」是很暗的深绿（平均 RGB 约 38,44,30），放进这张几乎是黑的房间背景里
	 *  会**完全看不见** —— 而「悬停底板」是亮的（约 204,172,141）。所以三个状态都用亮底板，
	 *  靠 **透明度/染色**区分：默认 62% 透明、悬停 100%、按下加暖色。 */
	FButtonStyle MakePauseArtButtonStyle(UTexture2D* Normal, UTexture2D* Hover, const FVector2D& Size)
	{
		// 2026-10-17 修：原来两块都用了 Hover 底板（Normal 也画 Hover）✗，且默认透明度 0.62 叠在
		// 同样很暗的纯色底上 -> 看起来像"底板没渲染" ✗。
		// 现在：Normal 用 Normal 底板、Hover 用 Hover 底板，默认完全不透明并**提亮**（tint>1），
		// 配合 RebuildOptionButtons 里更深的纯色底，暗色底板在暗背景上也能看出形状。
		UTexture2D* PlateNormal = Normal != nullptr ? Normal : Hover;
		UTexture2D* PlateHover = Hover != nullptr ? Hover : Normal;
		if (PlateNormal == nullptr)
		{
			return MakePauseFallbackButtonStyle();
		}

		FSlateBrush NormalBrush = MakePauseTextureBrush(PlateNormal, Size, 30.f);
		NormalBrush.TintColor = FSlateColor(FLinearColor(1.30f, 1.30f, 1.30f, 1.f));

		FSlateBrush HoverBrush = MakePauseTextureBrush(PlateHover != nullptr ? PlateHover : PlateNormal, Size, 30.f);
		HoverBrush.TintColor = FSlateColor(FLinearColor(1.85f, 1.85f, 1.80f, 1.f));

		FSlateBrush PressedBrush = MakePauseTextureBrush(PlateHover != nullptr ? PlateHover : PlateNormal, Size, 30.f);
		PressedBrush.TintColor = FSlateColor(FLinearColor(1.55f, 1.36f, 0.92f, 1.f));

		FButtonStyle Style;
		Style.Normal = NormalBrush;
		Style.Hovered = HoverBrush;
		Style.Pressed = PressedBrush;
		Style.Disabled = MakePauseTextureBrush(PlateNormal, Size, 30.f);
		Style.NormalPadding = FMargin(18.f, 8.f);
		Style.PressedPadding = FMargin(18.f, 10.f);
		return Style;
	}
}

UMetamorphosisPauseMenuWidget::UMetamorphosisPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleText = LOCTEXT("PauseTitle", "暂停");
	OptionButtonStyle = MakePauseFallbackButtonStyle();

	// 默认选项；以后要加就在 PlayerController::HandlePauseMenuOption 里加一个 Tag 分支
	Options.Add({ TEXT("Resume"), LOCTEXT("Resume", "继续游戏") });
	Options.Add({ TEXT("OpenSettings"), LOCTEXT("OpenSettings", "打开设置") });
	// 2026-10-19：用户要求「下一周目」做在暂停界面（原来在设置面板里，已移除）。
	// 放在「返回主界面」之前 —— 它和"返回主界面"都是会离开当前局的操作，摆在一起更容易被注意到。
	Options.Add({ TEXT("NextLoop"), LOCTEXT("NextLoop", "下一周目") });
	// 用户反馈"点暂停后没有返回主界面按钮" —— 加在暂停菜单里
	Options.Add({ TEXT("ReturnToTitle"), LOCTEXT("ReturnToTitle", "返回主界面") });

	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		TitleFont = FSlateFontInfo(RobotoFont.Object, 34, FName("Bold"));
		ButtonFont = FSlateFontInfo(RobotoFont.Object, 24, FName("Bold"));
	}

	// 美术交付 1 的暂停页素材（拿不到就走纯色兜底）
	if (static ConstructorHelpers::FObjectFinder<UTexture2D> Finder(TEXT("/Game/UI/Pause/Pause_BG.Pause_BG")); Finder.Succeeded())
	{
		BackgroundTexture = Finder.Object;
	}
	if (static ConstructorHelpers::FObjectFinder<UTexture2D> Finder(TEXT("/Game/UI/Pause/Pause_Title.Pause_Title")); Finder.Succeeded())
	{
		TitleTexture = Finder.Object;
	}
	if (static ConstructorHelpers::FObjectFinder<UTexture2D> Finder(TEXT("/Game/UI/Pause/Pause_BtnPlate_Normal.Pause_BtnPlate_Normal")); Finder.Succeeded())
	{
		ButtonPlateNormalTexture = Finder.Object;
	}
	if (static ConstructorHelpers::FObjectFinder<UTexture2D> Finder(TEXT("/Game/UI/Pause/Pause_BtnPlate_Hover.Pause_BtnPlate_Hover")); Finder.Succeeded())
	{
		ButtonPlateHoverTexture = Finder.Object;
	}
	if (static ConstructorHelpers::FObjectFinder<UTexture2D> Finder(TEXT("/Game/UI/Pause/Pause_Beetle.Pause_Beetle")); Finder.Succeeded())
	{
		BeetleTexture = Finder.Object;
	}
	if (static ConstructorHelpers::FObjectFinder<UTexture2D> Finder(TEXT("/Game/UI/Pause/Pause_Shadow.Pause_Shadow")); Finder.Succeeded())
	{
		ShadowTexture = Finder.Object;
	}

}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisPauseMenuWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
		bBuiltLayout = true;
	}

	RebuildOptionButtons();
	return bResult;
}

void UMetamorphosisPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 自己必须是可见且能接收点击的：全屏遮罩要吃掉身后的点击（不能 HitTestInvisible）
	SetVisibility(ESlateVisibility::Visible);

	RebuildOptionButtons();
	FocusFirstOption();
}

void UMetamorphosisPauseMenuWidget::NativeDestruct()
{
	for (int32 Index = 0; Index < OptionButtons.Num(); ++Index)
	{
		if (OptionButtons[Index] == nullptr)
		{
			continue;
		}

		switch (Index)
		{
		case 0: OptionButtons[Index]->OnClicked.RemoveDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption0); break;
		case 1: OptionButtons[Index]->OnClicked.RemoveDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption1); break;
		case 2: OptionButtons[Index]->OnClicked.RemoveDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption2); break;
		case 3: OptionButtons[Index]->OnClicked.RemoveDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption3); break;
		case 4: OptionButtons[Index]->OnClicked.RemoveDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption4); break;
		case 5: OptionButtons[Index]->OnClicked.RemoveDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption5); break;
		default: break;
		}
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 布局（美术暂停页：房间插画背景 + 毛笔标题 + 破损纸底板按钮 + 虫子装饰）
// ---------------------------------------------------------------------------

void UMetamorphosisPauseMenuWidget::BuildDefaultLayout()
{
	UOverlay* Root = MakePauseWidget<UOverlay>(WidgetTree, TEXT("PauseRoot"));
	WidgetTree->RootWidget = Root;

	// 背景插画（铺满），拿不到就只剩下面的压暗层
	if (UTexture2D* Background = BackgroundTexture.LoadSynchronous())
	{
		UImage* BackgroundImage = MakePauseWidget<UImage>(WidgetTree, TEXT("PauseBackground"));
		BackgroundImage->SetBrush(MakePauseTextureBrush(Background, FVector2D(1672.f, 941.f)));
		BackgroundImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(BackgroundImage))
		{
			BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
			BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	// 压暗层：同时吃掉身后的点击（这样弹窗打开时点不到快捷栏/按钮）
	BackdropImage = MakePauseWidget<UImage>(WidgetTree, TEXT("Backdrop"));
	BackdropImage->SetBrush(MakePauseBrush(BackdropColor));
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(BackdropImage))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// 中间一列：标题 + 两个按钮
	PanelBorder = MakePauseWidget<UBorder>(WidgetTree, TEXT("Panel"));
	PanelBorder->SetBrush(MakePauseBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));
	PanelBorder->SetPadding(FMargin(0.f));
	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelBorder))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	OptionColumn = MakePauseWidget<UVerticalBox>(WidgetTree, TEXT("PauseOptionColumn"));
	PanelBorder->SetContent(OptionColumn);

	// 标题：有美术图就用毛笔字，否则用文字
	if (UTexture2D* TitleArt = TitleTexture.LoadSynchronous())
	{
		USizeBox* TitleBox = MakePauseWidget<USizeBox>(WidgetTree);
		TitleBox->SetWidthOverride(240.f);
		TitleBox->SetHeightOverride(128.f);
		TitleBottomPadding = 26.f;
		TitleWidget = TitleBox;
		if (UVerticalBoxSlot* TitleSlot = OptionColumn->AddChildToVerticalBox(TitleBox))
		{
			TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, TitleBottomPadding));
			TitleSlot->SetHorizontalAlignment(HAlign_Center);
		}

		TitleImage = MakePauseWidget<UImage>(WidgetTree, TEXT("PauseTitleArt"));
		TitleImage->SetBrush(MakePauseTextureBrush(TitleArt, FVector2D(305.f, 163.f)));
		TitleImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		TitleBox->AddChild(TitleImage);
	}
	else
	{
		TitleLabel = MakePauseWidget<UTextBlock>(WidgetTree, TEXT("PauseTitle"));
		TitleLabel->SetText(TitleText);
		TitleLabel->SetFont(TitleFont);
		TitleLabel->SetColorAndOpacity(FSlateColor(TitleColor));
		TitleLabel->SetJustification(ETextJustify::Center);
		TitleBottomPadding = 22.f;
		TitleWidget = TitleLabel;
		if (UVerticalBoxSlot* TitleSlot = OptionColumn->AddChildToVerticalBox(TitleLabel))
		{
			TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, TitleBottomPadding));
			TitleSlot->SetHorizontalAlignment(HAlign_Center);
		}
	}

	// 右下角的虫子装饰（纯装饰，不吃点击）
	if (UTexture2D* Beetle = BeetleTexture.LoadSynchronous())
	{
		UOverlay* BeetleOverlay = MakePauseWidget<UOverlay>(WidgetTree, TEXT("PauseBeetleOverlay"));
		if (UOverlaySlot* BeetleSlot = Root->AddChildToOverlay(BeetleOverlay))
		{
			BeetleSlot->SetHorizontalAlignment(HAlign_Right);
			BeetleSlot->SetVerticalAlignment(VAlign_Bottom);
			BeetleSlot->SetPadding(FMargin(0.f, 0.f, 48.f, 18.f));
		}

		USizeBox* BeetleBox = MakePauseWidget<USizeBox>(WidgetTree);
		BeetleBox->SetWidthOverride(220.f);
		BeetleBox->SetHeightOverride(118.f);
		if (UOverlaySlot* BeetleBoxSlot = BeetleOverlay->AddChildToOverlay(BeetleBox))
		{
			BeetleBoxSlot->SetHorizontalAlignment(HAlign_Right);
			BeetleBoxSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		// 影子在下，虫子在上的感觉用一张图即可（素材本身自带）
		if (UTexture2D* Shadow = ShadowTexture.LoadSynchronous())
		{
			UImage* ShadowImage = MakePauseWidget<UImage>(WidgetTree, TEXT("PauseBeetleShadow"));
			ShadowImage->SetBrush(MakePauseTextureBrush(Shadow, FVector2D(230.f, 123.f)));
			ShadowImage->SetVisibility(ESlateVisibility::HitTestInvisible);
			BeetleBox->AddChild(ShadowImage);
		}

		UImage* BeetleImage = MakePauseWidget<UImage>(WidgetTree, TEXT("PauseBeetleArt"));
		BeetleImage->SetBrush(MakePauseTextureBrush(Beetle, FVector2D(140.f, 74.f)));
		BeetleImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		BeetleBox->AddChild(BeetleImage);

		BeetleOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UMetamorphosisPauseMenuWidget::RebuildArtButtonStyle()
{
	// 运行时加载（LoadObject 一定走磁盘资源，CDO 阶段用 LoadSynchronous 可能拿不到）
	UTexture2D* PlateNormal = ButtonPlateNormalTexture.IsNull()
		? LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Pause/Pause_BtnPlate_Normal.Pause_BtnPlate_Normal"), nullptr, LOAD_NoWarn | LOAD_Quiet)
		: ButtonPlateNormalTexture.LoadSynchronous();
	UTexture2D* PlateHover = ButtonPlateHoverTexture.IsNull()
		? LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Pause/Pause_BtnPlate_Hover.Pause_BtnPlate_Hover"), nullptr, LOAD_NoWarn | LOAD_Quiet)
		: ButtonPlateHoverTexture.LoadSynchronous();

	if (PlateNormal == nullptr)
	{
		bArtPlateLoaded = false;
		UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] 暂停页：没找到按钮底板贴图，退回纯色按钮"));
		return;
	}

	bArtPlateLoaded = true;
	OptionButtonStyle = MakePauseArtButtonStyle(PlateNormal, PlateHover, FVector2D(407.f, 81.f));
}

void UMetamorphosisPauseMenuWidget::RebuildOptionButtons()
{
	RebuildArtButtonStyle();

	if (WidgetTree == nullptr || OptionColumn == nullptr)
	{
		return;
	}

	// 2026-10-20 修「幽灵按钮」✗：
	// 选项的层级是 OptionColumn -> USizeBox(ButtonBox) -> Overlay[底板 UImage, UButton]，
	// 原来只对 UButton 调 RemoveFromParent()，SizeBox / Overlay / 底板 全留在 OptionColumn 里
	// -> 每调用一次重建（Initialize + NativeConstruct 共两次）就多出 Count 个"没有文字的空底板"，
	//    还把真正的选项整体挤到下面（用户看到的 4 个空框 + "四项偏下"）。
	// 现在：整列清空 -> 把标题重新挂回去 -> 再建选项，彻底幂等。
	const int32 OldChildCount = OptionColumn->GetChildrenCount();
	OptionColumn->ClearChildren();
	OptionButtons.Reset();
	OptionLabels.Reset();
	OptionBoxes.Reset();

	// 标题重新挂回（ClearChildren 把它一起清掉了）
	if (TitleWidget != nullptr)
	{
		TitleWidget->RemoveFromParent();
		if (UVerticalBoxSlot* TitleSlot = OptionColumn->AddChildToVerticalBox(TitleWidget))
		{
			TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, TitleBottomPadding));
			TitleSlot->SetHorizontalAlignment(HAlign_Center);
		}
	}

	// 整块上移：居中布局下给底板加"下内边距"= 内容整体上移
	if (PanelBorder != nullptr)
	{
		PanelBorder->SetPadding(FMargin(0.f, 0.f, 0.f, FMath::Max(0.f, ContentVerticalOffset)));
	}

	const int32 Count = FMath::Min(Options.Num(), MaxOptions);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		USizeBox* ButtonBox = MakePauseWidget<USizeBox>(WidgetTree);
		ButtonBox->SetWidthOverride(ButtonWidth);
		ButtonBox->SetHeightOverride(ButtonHeight);
		if (UVerticalBoxSlot* BoxSlot = OptionColumn->AddChildToVerticalBox(ButtonBox))
		{
			BoxSlot->SetPadding(FMargin(0.f, OptionSpacing * 0.5f));
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
		}

		UButton* Button = MakePauseWidget<UButton>(WidgetTree, *FString::Printf(TEXT("PauseOption_%d"), Index));
		Button->SetStyle(MakePauseInvisibleButtonStyle());

		// 2026-10-17：底板放到**按钮外面**（SizeBox -> Overlay[底板, 按钮]）。
		// 实测把 UImage 放进按钮内容层时看不到（同一张亮色 Hover 底板也不显示），
		// 放到兄弟层就正常渲染 —— 和背包书页一样走普通 UOverlay 布局。
		UOverlay* ButtonLayers = MakePauseWidget<UOverlay>(WidgetTree);
		ButtonBox->AddChild(ButtonLayers);

		UTexture2D* PlateTexture = ButtonPlateHoverTexture.IsNull()
			? LoadObject<UTexture2D>(nullptr,
				TEXT("/Game/UI/Pause/Pause_BtnPlate_Hover.Pause_BtnPlate_Hover"), nullptr, LOAD_NoWarn | LOAD_Quiet)
			: ButtonPlateHoverTexture.LoadSynchronous();
		// 实测：Normal 底板平均 RGB 仅 (38,44,30)，与背景 (23,26,21) 几乎一样黑 -> 用它永远"看不见"；
		// Hover 底板是亮的 (204,171,140)，所以默认态直接用 Hover 底板，悬停靠文字变暖金区分。
		UImage* Plate = MakePauseWidget<UImage>(WidgetTree, *FString::Printf(TEXT("PauseOptionPlate_%d"), Index));
		if (PlateTexture != nullptr)
		{
			Plate->SetBrush(MakePauseTextureBrush(PlateTexture, FVector2D(ButtonWidth, ButtonHeight), 30.f));
			Plate->SetColorAndOpacity(FLinearColor(0.78f, 0.76f, 0.72f, 1.f));
		}
		else
		{
			Plate->SetBrush(MakePauseBrush(FLinearColor(0.34f, 0.32f, 0.26f, 0.92f)));
		}
		Plate->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* PlateSlot = ButtonLayers->AddChildToOverlay(Plate))
		{
			PlateSlot->SetHorizontalAlignment(HAlign_Fill);
			PlateSlot->SetVerticalAlignment(VAlign_Fill);
		}
		if (UOverlaySlot* ButtonSlot = ButtonLayers->AddChildToOverlay(Button))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
			ButtonSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UOverlay* ButtonOverlay = MakePauseWidget<UOverlay>(WidgetTree);
		Button->AddChild(ButtonOverlay);

		UTextBlock* Label = MakePauseWidget<UTextBlock>(WidgetTree, *FString::Printf(TEXT("PauseOptionLabel_%d"), Index));
		Label->SetText(Options[Index].Label);
		Label->SetFont(ButtonFont);
		Label->SetColorAndOpacity(FSlateColor(ButtonTextColor));
		Label->SetJustification(ETextJustify::Center);
		Label->SetShadowOffset(FVector2D(1.f, 1.f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.75f));
		if (UOverlaySlot* LabelSlot = ButtonOverlay->AddChildToOverlay(Label))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
		// 点击 / 悬停绑定（AddDynamic 要字面写函数名，所以这里逐个分支）
		switch (Index)
		{
		case 0:
			Button->OnClicked.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption0);
			Button->OnHovered.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption0Hover);
			Button->OnUnhovered.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption0Unhover);
			break;
		case 1:
			Button->OnClicked.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption1);
			Button->OnHovered.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption1Hover);
			Button->OnUnhovered.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption1Unhover);
			break;
		case 2: Button->OnClicked.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption2); break;
		case 3: Button->OnClicked.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption3); break;
		case 4: Button->OnClicked.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption4); break;
		case 5: Button->OnClicked.AddDynamic(this, &UMetamorphosisPauseMenuWidget::HandleOption5); break;
		default: break;
		}

		OptionButtons.Add(Button);
		OptionLabels.Add(Label);
		OptionBoxes.Add(ButtonBox);
	}

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Pause] 重建选项：%d 项（清掉旧子控件 %d 个；底板=%s）"),
		Count, OldChildCount, bArtPlateLoaded ? TEXT("美术") : TEXT("纯色兜底"));
}

void UMetamorphosisPauseMenuWidget::SetOptionPlateHighlighted(int32 Index, bool bHighlighted)
{
	// 2026-10-17：底板改由 UButton 的 style 绘制（Normal/Hover 两块不同贴图，Slate 自己会切换），
	// 所以悬停高亮改成给**文字**加暖色 + 提亮，不再去改 UImage 的透明度。
	if (OptionLabels.IsValidIndex(Index) && OptionLabels[Index] != nullptr)
	{
		const FLinearColor Highlight(1.f, 0.90f, 0.62f, 1.f);
		OptionLabels[Index]->SetColorAndOpacity(FSlateColor(bHighlighted ? Highlight : ButtonTextColor));
	}
}

void UMetamorphosisPauseMenuWidget::HandleOption0Hover() { SetOptionPlateHighlighted(0, true); }
void UMetamorphosisPauseMenuWidget::HandleOption0Unhover() { SetOptionPlateHighlighted(0, false); }
void UMetamorphosisPauseMenuWidget::HandleOption1Hover() { SetOptionPlateHighlighted(1, true); }
void UMetamorphosisPauseMenuWidget::HandleOption1Unhover() { SetOptionPlateHighlighted(1, false); }

void UMetamorphosisPauseMenuWidget::AddOption(FName Tag, const FText& Label)
{
	FMetamorphosisPauseMenuOption Option;
	Option.Tag = Tag;
	Option.Label = Label;
	Options.Add(Option);

	RebuildOptionButtons();
}

void UMetamorphosisPauseMenuWidget::FocusFirstOption()
{
	if (OptionButtons.IsValidIndex(0) && OptionButtons[0] != nullptr)
	{
		OptionButtons[0]->SetKeyboardFocus();
	}
}

// ---------------------------------------------------------------------------
// 点击
// ---------------------------------------------------------------------------

void UMetamorphosisPauseMenuWidget::HandleOptionClicked(int32 Index)
{
	if (!Options.IsValidIndex(Index))
	{
		return;
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 弹窗选项：%s"), *Options[Index].Tag.ToString());
	OnOptionChosen.Broadcast(Options[Index].Tag);
}

void UMetamorphosisPauseMenuWidget::HandleOption0() { HandleOptionClicked(0); }
void UMetamorphosisPauseMenuWidget::HandleOption1() { HandleOptionClicked(1); }
void UMetamorphosisPauseMenuWidget::HandleOption2() { HandleOptionClicked(2); }
void UMetamorphosisPauseMenuWidget::HandleOption3() { HandleOptionClicked(3); }
void UMetamorphosisPauseMenuWidget::HandleOption4() { HandleOptionClicked(4); }
void UMetamorphosisPauseMenuWidget::HandleOption5() { HandleOptionClicked(5); }

#undef LOCTEXT_NAMESPACE


