// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisLoopCountWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Font.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "UObject/ConstructorHelpers.h"

UMetamorphosisLoopCountWidget::UMetamorphosisLoopCountWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ini 值在构造函数里读不到（UE 先跑构造函数再套配置），所以这里只给"代码默认值"，
	// 真正的样式在 ApplyLoopCountStaticStyle 里应用。
	FormatText = FText::FromString(TEXT("第 {0} 周目"));

	// 默认字体：和 UTextBlock 一样取引擎 Roboto。
	// 构造函数里不能碰 Slate 样式集（FCoreStyle 之类），只能用 ConstructorHelpers 找资源
	// —— 这是本工程 UI 控件的统一写法（见 MetamorphosisTaskWidget）。
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		LoopFont = FSlateFontInfo(RobotoFont.Object, 16, FName("Bold"));
	}
}

bool UMetamorphosisLoopCountWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	// 设计器树为空 => 这是纯 C++ 类，自己搭一棵默认布局。
	// ⚠️ 必须在 Initialize 里搭：放到 NativeConstruct 就太晚了，控件画不出来（实测过：
	//    控件挂上了、但尺寸恒为 0、屏幕上什么都没有）。
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildLoopCountLayout();
		bBuiltLayout = true;
	}

	ApplyLoopCountStaticStyle();
	return bResult;
}

void UMetamorphosisLoopCountWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 纯显示，不参与命中测试。
	// 注意：这里**不能**用 Collapsed —— Slate 不会 tick 被整体折叠的控件（本工程踩过这个坑），
	// 那样 NativeTick 里的周目刷新就永远不会跑了。要藏只藏内部面板。
	SetVisibility(ESlateVisibility::HitTestInvisible);

	ApplyLoopCountStaticStyle();
	RefreshLoopCount();
}

void UMetamorphosisLoopCountWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 轮询 + 脏检查（只有周目号变化时才改文本，不是每帧刷）
	RefreshLoopCount();
}

void UMetamorphosisLoopCountWidget::RefreshLoopCount()
{
	// 周目号 = 已结算次数 + 1：开局第 1 周目，被捉/入夜结算一次后变第 2 周目。
	int32 LoopNumber = 1;
	UMetamorphosisStatsSubsystem* Stats = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMetamorphosisLoopSubsystem* LoopSubsystem = GameInstance->GetSubsystem<UMetamorphosisLoopSubsystem>())
		{
			LoopNumber = LoopSubsystem->GetLoopCount() + 1;
		}

		Stats = GameInstance->GetSubsystem<UMetamorphosisStatsSubsystem>();
	}

	if (LoopNumber != DisplayedLoopNumber)
	{
		DisplayedLoopNumber = LoopNumber;

		if (LoopText != nullptr)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("0"), FText::AsNumber(LoopNumber));
			LoopText->SetText(FText::Format(FormatText, Args));
		}
	}

	// 时间（2026-10-19）：放在周目旁边，格式「7:40」。按分钟做脏检查，不是每帧改文本。
	const int32 ClockMinute = (Stats != nullptr) ? FMath::FloorToInt(Stats->GetDayMinutes()) : -1;
	if (ClockMinute != DisplayedClockMinute)
	{
		DisplayedClockMinute = ClockMinute;

		if (TimeText != nullptr)
		{
			TimeText->SetText(Stats != nullptr ? Stats->GetClockText() : FText::GetEmpty());
		}
	}
}

void UMetamorphosisLoopCountWidget::BuildLoopCountLayout()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LoopCountRow"));
	PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LoopCountPanel"));
	AccentBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LoopCountAccent"));
	LoopText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LoopCountText"));
	// 2026-10-19：当天时间「7:40」，和周目并排（用户要求"时间放到周目旁边"）
	TimeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LoopCountTimeText"));

	// 根 = 面板（整块半透明深褐），内容 = 一行[描金竖条 + 文字]
	if (PanelBorder != nullptr)
	{
		PanelBorder->SetContent(Row);
		PanelBorder->SetPadding(FMargin(PanelPadding, PanelPadding * 0.6f));
		WidgetTree->RootWidget = PanelBorder;
	}

	if (Row != nullptr && AccentBar != nullptr)
	{
		// 竖条本体没内容会算成 0 尺寸，所以套一个只定宽度的 SizeBox
		if (USizeBox* AccentSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LoopCountAccentSize")))
		{
			AccentSize->SetWidthOverride(FMath::Max(1.f, AccentWidth));
			AccentSize->SetMinDesiredHeight(16.f);
			AccentBar->SetContent(AccentSize);
		}

		if (UHorizontalBoxSlot* AccentSlot = Row->AddChildToHorizontalBox(AccentBar))
		{
			AccentSlot->SetVerticalAlignment(VAlign_Fill);
			AccentSlot->SetPadding(FMargin(0.f, 0.f, AccentToTextGap, 0.f));
		}
	}

	if (Row != nullptr && LoopText != nullptr)
	{
		if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(LoopText))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	// 时间文字：紧跟在周目文字右边（同一个面板里，视觉上属于同一行信息）
	if (Row != nullptr && TimeText != nullptr)
	{
		if (UHorizontalBoxSlot* TimeSlot = Row->AddChildToHorizontalBox(TimeText))
		{
			TimeSlot->SetVerticalAlignment(VAlign_Center);
			TimeSlot->SetPadding(FMargin(FMath::Max(0.f, TimeTextGap), 0.f, 0.f, 0.f));
		}
		TimeText->SetText(FText::GetEmpty());
	}
}

void UMetamorphosisLoopCountWidget::ApplyLoopCountStaticStyle()
{
	if (PanelBorder != nullptr)
	{
		PanelBorder->SetBrushColor(PanelBackgroundColor);
	}
	if (AccentBar != nullptr)
	{
		AccentBar->SetBrushColor(AccentColor);
	}
	if (LoopText != nullptr)
	{
		LoopText->SetFont(LoopFont);
		LoopText->SetColorAndOpacity(FSlateColor(TextColor));
		// 半透明深色底上给一点描边感，浅色壁纸场景也看得清
		LoopText->SetShadowOffset(FVector2D(1.f, 1.f));
		LoopText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	}
	if (TimeText != nullptr)
	{
		TimeText->SetFont(LoopFont);
		TimeText->SetColorAndOpacity(FSlateColor(TimeTextColor));
		TimeText->SetShadowOffset(FVector2D(1.f, 1.f));
		TimeText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	}
}
