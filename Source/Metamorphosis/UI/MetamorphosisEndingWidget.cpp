// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisEndingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// ⚠️ 唯一命名：UE 的 unity build 会把多个 cpp 拼进同一编译单元，撞名就报 already has a body。
	template <typename T>
	T* MakeEndingWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), Name);
	}

	/** 通关界面的按钮样式：只用 FSlateColorBrush（手搓 FSlateBrush(Image) 在本工程会画成白膜） */
	FButtonStyle MakeEndingButtonStyle()
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateColorBrush(FLinearColor(0.16f, 0.14f, 0.12f, 0.92f)));
		Style.SetHovered(FSlateColorBrush(FLinearColor(0.30f, 0.26f, 0.20f, 0.96f)));
		Style.SetPressed(FSlateColorBrush(FLinearColor(0.42f, 0.36f, 0.24f, 1.f)));
		Style.SetNormalPadding(FMargin(22.f, 11.f));
		Style.SetPressedPadding(FMargin(22.f, 13.f, 22.f, 9.f));
		return Style;
	}
}

UMetamorphosisEndingWidget::UMetamorphosisEndingWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 构造函数里不能碰 FCoreStyle，只能用 ConstructorHelpers 找引擎字体
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		TitleFont = FSlateFontInfo(RobotoFont.Object, 52, FName("Bold"));
		SubtitleFont = FSlateFontInfo(RobotoFont.Object, 20, FName("Regular"));
		StatsFont = FSlateFontInfo(RobotoFont.Object, 15, FName("Regular"));
		ButtonFont = FSlateFontInfo(RobotoFont.Object, 19, FName("Bold"));
	}
}

bool UMetamorphosisEndingWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	// 设计器树为空 => 纯 C++ 类，自己搭布局（必须在这里，不能放 NativeConstruct）
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildEndingLayout();
		bBuiltLayout = true;
	}

	return bResult;
}

void UMetamorphosisEndingWidget::BuildEndingLayout()
{
	// 根 = 全屏 Overlay
	UOverlay* Root = MakeEndingWidget<UOverlay>(WidgetTree, TEXT("EndingRoot"));
	WidgetTree->RootWidget = Root;

	// ① 全屏黑幕（用 FSlateColorBrush 才是真黑；DrawAs=Image+Tint 会画成白膜）
	MaskBorder = MakeEndingWidget<UBorder>(WidgetTree, TEXT("EndingMask"));
	MaskBorder->SetBrush(FSlateColorBrush(FLinearColor::Black));
	MaskBorder->SetBrushColor(FLinearColor::Black);
	if (UOverlaySlot* MaskSlot = Root->AddChildToOverlay(MaskBorder))
	{
		MaskSlot->SetHorizontalAlignment(HAlign_Fill);
		MaskSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// ② 居中内容
	ContentBox = MakeEndingWidget<UVerticalBox>(WidgetTree, TEXT("EndingContent"));

	TitleText = MakeEndingWidget<UTextBlock>(WidgetTree, TEXT("EndingTitle"));
	TitleText->SetFont(TitleFont);
	TitleText->SetColorAndOpacity(FSlateColor(TitleColor));
	TitleText->SetJustification(ETextJustify::Center);
	// 文字块一律不参与命中测试：否则它们会盖在按钮的命中区上抢点击（本项目踩过同类坑）
	TitleText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UVerticalBoxSlot* TitleSlot = ContentBox->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	}

	SubtitleText = MakeEndingWidget<UTextBlock>(WidgetTree, TEXT("EndingSubtitle"));
	SubtitleText->SetFont(SubtitleFont);
	SubtitleText->SetColorAndOpacity(FSlateColor(SubtitleColor));
	SubtitleText->SetJustification(ETextJustify::Center);
	SubtitleText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	// ⚠️ 文本必须显式给换行宽度：只开 AutoWrap 会让 TextBlock 的期望宽度撑爆父容器（本工程踩过）
	SubtitleText->SetAutoWrapText(false);
	SubtitleText->SetWrapTextAt(720.f);
	if (UVerticalBoxSlot* SubtitleSlot = ContentBox->AddChildToVerticalBox(SubtitleText))
	{
		SubtitleSlot->SetHorizontalAlignment(HAlign_Center);
		SubtitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	StatsText = MakeEndingWidget<UTextBlock>(WidgetTree, TEXT("EndingStats"));
	StatsText->SetFont(StatsFont);
	StatsText->SetColorAndOpacity(FSlateColor(StatsColor));
	StatsText->SetJustification(ETextJustify::Center);
	StatsText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UVerticalBoxSlot* StatsSlot = ContentBox->AddChildToVerticalBox(StatsText))
	{
		StatsSlot->SetHorizontalAlignment(HAlign_Center);
		StatsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 26.f));
	}

	// ③ 两个按钮：重新开始 / 返回标题
	UHorizontalBox* ButtonRow = MakeEndingWidget<UHorizontalBox>(WidgetTree, TEXT("EndingButtons"));
	if (UVerticalBoxSlot* RowSlot = ContentBox->AddChildToVerticalBox(ButtonRow))
	{
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	RestartButton = MakeEndingWidget<UButton>(WidgetTree, TEXT("EndingRestartButton"));
	RestartButton->SetStyle(MakeEndingButtonStyle());
	UTextBlock* RestartLabel = MakeEndingWidget<UTextBlock>(WidgetTree, TEXT("EndingRestartLabel"));
	RestartLabel->SetText(RestartButtonText);
	RestartLabel->SetFont(ButtonFont);
	RestartLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	RestartLabel->SetJustification(ETextJustify::Center);
	// 按钮里的文字必须不参与命中：否则点在文字上收不到，只有点按钮边缘才行（用户会觉得"点不动"）
	RestartLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RestartButton->AddChild(RestartLabel);
	if (UHorizontalBoxSlot* BSlot = ButtonRow->AddChildToHorizontalBox(RestartButton))
	{
		BSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
		BSlot->SetVerticalAlignment(VAlign_Center);
	}

	ReturnButton = MakeEndingWidget<UButton>(WidgetTree, TEXT("EndingReturnButton"));
	ReturnButton->SetStyle(MakeEndingButtonStyle());
	UTextBlock* ReturnLabel = MakeEndingWidget<UTextBlock>(WidgetTree, TEXT("EndingReturnLabel"));
	ReturnLabel->SetText(ReturnButtonText);
	ReturnLabel->SetFont(ButtonFont);
	ReturnLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ReturnLabel->SetJustification(ETextJustify::Center);
	ReturnLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ReturnButton->AddChild(ReturnLabel);
	if (UHorizontalBoxSlot* RSlot = ButtonRow->AddChildToHorizontalBox(ReturnButton))
	{
		RSlot->SetVerticalAlignment(VAlign_Center);
	}

	if (UOverlaySlot* ContentSlot = Root->AddChildToOverlay(ContentBox))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Center);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
	}

	ContentBox->SetVisibility(ESlateVisibility::Collapsed);

	// 空闲态：不挡输入，但保持 Visible（Collapsed 的控件 Slate 不再 tick —— 本工程踩过）
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMetamorphosisEndingWidget::ShowEnding(const FText& Title, const FText& Subtitle, const FText& StatsLine)
{
	if (TitleText != nullptr)
	{
		TitleText->SetText(Title);
	}
	if (SubtitleText != nullptr)
	{
		SubtitleText->SetText(Subtitle);
	}
	if (StatsText != nullptr)
	{
		StatsText->SetText(StatsLine);
	}
	if (ContentBox != nullptr)
	{
		// ⚠️ 必须是 Visible，不能是 HitTestInvisible：
		//   HitTestInvisible = "自己**和所有子控件**都不接受点击" -> 里面的「重新开始/返回标题」永远收不到点击（用户踩到）。
		//   这里用 Visible，但 ContentBox 自己是个无画刷的 VerticalBox，空白处不会吃点击，只有按钮命中 ✓
		ContentBox->SetVisibility(ESlateVisibility::Visible);
	}

	// 通关界面**故意挡输入**（终局界面，背后的游戏不该再被点到）
	SetVisibility(ESlateVisibility::Visible);
	SetRenderOpacity(FMath::Clamp(MaskOpacity, 0.f, 1.f));

	// 按钮回调（重复 Show 时先摘再挂，避免重复绑定）
	if (RestartButton != nullptr)
	{
		RestartButton->OnClicked.RemoveDynamic(this, &UMetamorphosisEndingWidget::HandleRestartClicked);
		RestartButton->OnClicked.AddDynamic(this, &UMetamorphosisEndingWidget::HandleRestartClicked);
	}
	if (ReturnButton != nullptr)
	{
		ReturnButton->OnClicked.RemoveDynamic(this, &UMetamorphosisEndingWidget::HandleReturnToTitleClicked);
		ReturnButton->OnClicked.AddDynamic(this, &UMetamorphosisEndingWidget::HandleReturnToTitleClicked);
	}

	bEndingVisible = true;

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 通关界面显示：%s｜%s｜%s"),
		*Title.ToString(), *Subtitle.ToString(), *StatsLine.ToString());
}

void UMetamorphosisEndingWidget::HideEnding()
{
	bEndingVisible = false;

	if (ContentBox != nullptr)
	{
		ContentBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetRenderOpacity(0.f);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (RestartButton != nullptr)
	{
		RestartButton->OnClicked.RemoveDynamic(this, &UMetamorphosisEndingWidget::HandleRestartClicked);
	}
	if (ReturnButton != nullptr)
	{
		ReturnButton->OnClicked.RemoveDynamic(this, &UMetamorphosisEndingWidget::HandleReturnToTitleClicked);
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 通关界面已收起"));
}

void UMetamorphosisEndingWidget::HandleRestartClicked()
{
	if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
	{
		Loop->RestartAfterWin();
	}
}

void UMetamorphosisEndingWidget::HandleReturnToTitleClicked()
{
	if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
	{
		Loop->ReturnToTitleAfterWin();
	}
}

bool UMetamorphosisEndingWidget::SimulatePress(const FString& Which)
{
	const bool bRestart = Which.Equals(TEXT("Restart"), ESearchCase::IgnoreCase);
	const bool bTitle = Which.Equals(TEXT("Title"), ESearchCase::IgnoreCase);
	if (!bRestart && !bTitle)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] Meta.Ending.Press 参数无效：%s（应为 Restart / Title）"), *Which);
		return false;
	}
	if (!bEndingVisible)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] Meta.Ending.Press：通关界面当前不可见，忽略"));
		return false;
	}

	UButton* Btn = bRestart ? RestartButton : ReturnButton;
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 模拟点击「%s」（按钮=%s 可见性=%s）-> 走真实回调"),
		*Which, *GetNameSafe(Btn), Btn != nullptr ? *UEnum::GetValueAsString(Btn->GetVisibility()) : TEXT("null"));

	// 直接调事件（和真实点击走同一条委托链路）
	if (bRestart)
	{
		HandleRestartClicked();
	}
	else
	{
		HandleReturnToTitleClicked();
	}
	return true;
}

void UMetamorphosisEndingWidget::LogControlTree() const
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[EndingTree] 没有控件树"));
		return;
	}

	int32 Count = 0;
	UE_LOG(LogMetamorphosis, Log, TEXT("[EndingTree] 根：%s  Visibility=%s"),
		*WidgetTree->RootWidget->GetName(),
		*UEnum::GetValueAsString(WidgetTree->RootWidget->GetVisibility()));

	WidgetTree->ForEachWidget([&Count](UWidget* W)
	{
		if (W == nullptr)
		{
			return;
		}
		++Count;
		UE_LOG(LogMetamorphosis, Log, TEXT("[EndingTree]   %-28s %-22s Visibility=%s"),
			*W->GetName(), *GetNameSafe(W->GetClass()),
			*UEnum::GetValueAsString(W->GetVisibility()));
	});
	UE_LOG(LogMetamorphosis, Log, TEXT("[EndingTree] ---- 共 %d 个控件 ----"), Count);
}
