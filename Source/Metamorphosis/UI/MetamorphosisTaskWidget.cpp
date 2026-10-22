// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisTaskWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Config/GameConfigTypes.h"
#include "Engine/Font.h"
#include "Metamorphosis.h"
#include "Tasks/MetamorphosisTaskSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// ⚠️ 唯一命名：本工程 UI 的 cpp 里已经有同名的 MakeWidget/MakeSolidBrush 辅助函数，
	//    UE 的 unity build 会把多个 cpp 拼进同一编译单元，撞名就报 already has a body。
	template <typename T>
	T* MakeTaskWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), Name);
	}

	FSlateBrush MakeTaskSolidBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.TintColor = FSlateColor(Color);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		return Brush;
	}
}

UMetamorphosisTaskWidget::UMetamorphosisTaskWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	HeaderText = FText::FromString(TEXT("当前任务"));
	AllDoneText = FText::FromString(TEXT("（任务都完成了）"));

	// 默认字体：和 UTextBlock 一样取引擎 Roboto。
	// 构造函数里不能碰 Slate 样式集（FCoreStyle），只能用 ConstructorHelpers 找资源。
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		LabelFont = FSlateFontInfo(RobotoFont.Object, 13, FName("Regular"));
		TitleFont = FSlateFontInfo(RobotoFont.Object, 17, FName("Bold"));
		DetailFont = FSlateFontInfo(RobotoFont.Object, 12, FName("Regular"));
	}
}

bool UMetamorphosisTaskWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	// 设计器树为空 => 这是纯 C++ 类，自己搭一棵默认的布局
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTaskLayout();
		bBuiltLayout = true;
	}

	return bResult;
}

void UMetamorphosisTaskWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 纯显示控件：不参与命中测试（否则会挡住摇杆/按钮的点击）
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
	{
		Tasks->OnTaskChanged.AddDynamic(this, &UMetamorphosisTaskWidget::HandleTaskChanged);
	}

	ApplyTaskStaticStyle();
	RefreshTask();
}

void UMetamorphosisTaskWidget::NativeDestruct()
{
	if (UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this))
	{
		Tasks->OnTaskChanged.RemoveDynamic(this, &UMetamorphosisTaskWidget::HandleTaskChanged);
	}

	Super::NativeDestruct();
}

void UMetamorphosisTaskWidget::HandleTaskChanged(FName /*TaskId*/, int32 /*TaskIndex*/, const FText& /*Title*/)
{
	// 任务系统只在真的变化时广播，这里直接重新取一遍最省事
	RefreshTask();
}

void UMetamorphosisTaskWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 屏宽保护：放大到 1.5 倍后，510 宽在竖屏（720）上会占 70% -> 只在第一帧钳一次。
	// ⚠️ 不能对本控件用 GetCachedGeometry()：那是"控件自己的尺寸"（= 510），不是屏宽 ✗。
	//    本项目 1.5 倍 DPI：GetViewportSize 返回物理像素，UMG 空间 = 物理 / ViewportScale。
	if (!bAppliedScreenClamp && RootSizeBox != nullptr)
	{
		const FVector2D PhysSize = UWidgetLayoutLibrary::GetViewportSize(this);
		const float ViewScale = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
		const float ScreenWidthUmg = PhysSize.X / ViewScale;
		if (ScreenWidthUmg > 1.0f)
		{
			bAppliedScreenClamp = true;
			const float MaxWidth = ScreenWidthUmg * FMath::Clamp(MaxPanelScreenRatio, 0.2f, 0.95f);
			if (CurrentPanelWidth > MaxWidth)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务 HUD 宽度按屏宽钳制：%.0f -> %.0f（屏宽 %.0f UMG 单位）"),
					CurrentPanelWidth, MaxWidth, ScreenWidthUmg);
				CurrentPanelWidth = MaxWidth;
				RootSizeBox->SetWidthOverride(CurrentPanelWidth);
			}
			else
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务 HUD 宽度 %.0f 未超屏宽上限 %.0f（屏宽 %.0f UMG 单位）"),
					CurrentPanelWidth, MaxWidth, ScreenWidthUmg);
			}
		}
	}

	// 玩法门的「躲好，别出声（N 秒）」是随时间变化的文字，而门激活/倒计时都不会触发 OnTaskChanged
	// -> 必须自己定时刷（否则门目标与倒计时会整段不出现，玩家不知道要躲、也不知道还剩几秒）。
	// 限频 + RefreshTask 内的脏检查：内容没变时只是几次字符串比较，开销可忽略。
	RefreshAccumulator += InDeltaTime;
	const float Interval = FMath::Max(0.05f, RefreshIntervalSeconds);
	if (RefreshAccumulator < Interval)
	{
		return;
	}
	RefreshAccumulator = 0.f;
	RefreshTask();
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------

void UMetamorphosisTaskWidget::BuildTaskLayout()
{
	// 整体缩放：所有布局尺寸与字号统一乘这个值（改 HudScale 就能整体调大小，不用逐个改数字）
	const float S = FMath::Max(0.1f, HudScale);
	CurrentPanelWidth = PanelWidth * S;
	const float Pad = PanelPadding * S;
	const float Spacing = RowSpacing * S;

	USizeBox* Root = MakeTaskWidget<USizeBox>(WidgetTree, TEXT("TaskRoot"));
	RootSizeBox = Root;
	Root->SetWidthOverride(CurrentPanelWidth);
	// 整棵子树都不参与命中测试（纯显示）
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = Root;

	PanelBorder = MakeTaskWidget<UBorder>(WidgetTree, TEXT("TaskPanel"));
	PanelBorder->SetBrush(MakeTaskSolidBrush(PanelBackgroundColor));
	// 上下留白比左右多一点：三行文字（当前任务 / 标题 / 详情）贴到上下边缘会显得很挤。
	PanelBorder->SetPadding(FMargin(Pad + 2.f * S, Pad + 7.f * S, Pad + 2.f * S, Pad + 7.f * S));
	Root->AddChild(PanelBorder);

	UHorizontalBox* Row = MakeTaskWidget<UHorizontalBox>(WidgetTree, TEXT("TaskRow"));
	PanelBorder->SetContent(Row);

	// 左侧强调竖条（描金）：用 SizeBox 卡一个固定宽度
	USizeBox* AccentBox = MakeTaskWidget<USizeBox>(WidgetTree, TEXT("TaskAccentBox"));
	AccentBox->SetWidthOverride(AccentWidth * S);
	if (UHorizontalBoxSlot* AccentSlot = Row->AddChildToHorizontalBox(AccentBox))
	{
		AccentSlot->SetPadding(FMargin(0.f, 1.f, Pad, 1.f));
		AccentSlot->SetVerticalAlignment(VAlign_Fill);
	}
	AccentBar = MakeTaskWidget<UBorder>(WidgetTree, TEXT("TaskAccent"));
	AccentBar->SetBrush(MakeTaskSolidBrush(AccentColor));
	AccentBox->AddChild(AccentBar);

	// 右侧文字列：小标题「当前任务」/ 任务标题 / 补充说明
	ContentColumn = MakeTaskWidget<UVerticalBox>(WidgetTree, TEXT("TaskColumn"));
	if (UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(ContentColumn))
	{
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}

	HeaderLabel = MakeTaskWidget<UTextBlock>(WidgetTree, TEXT("TaskHeader"));
	HeaderLabel->SetText(HeaderText);
	if (UVerticalBoxSlot* HeaderSlot = ContentColumn->AddChildToVerticalBox(HeaderLabel))
	{
		HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f * S));
	}

	TitleText = MakeTaskWidget<UTextBlock>(WidgetTree, TEXT("TaskTitle"));
	TitleText->SetText(FText::GetEmpty());
	TitleText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* TitleSlot = ContentColumn->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, Spacing));
	}

	DetailText = MakeTaskWidget<UTextBlock>(WidgetTree, TEXT("TaskDetail"));
	DetailText->SetText(FText::GetEmpty());
	DetailText->SetAutoWrapText(true);
	ContentColumn->AddChildToVerticalBox(DetailText);

	ApplyTaskStaticStyle();
}

void UMetamorphosisTaskWidget::ApplyTaskStaticStyle()
{
	if (PanelBorder != nullptr)
	{
		PanelBorder->SetBrush(MakeTaskSolidBrush(PanelBackgroundColor));
	}
	if (AccentBar != nullptr)
	{
		AccentBar->SetBrush(MakeTaskSolidBrush(AccentColor));
	}
	if (HeaderLabel != nullptr)
	{
		if (LabelFont.HasValidFont())
		{
			FSlateFontInfo Font = LabelFont;
			Font.Size = FMath::RoundToInt(LabelFont.Size * FMath::Max(0.1f, HudScale));
			HeaderLabel->SetFont(Font);
		}
		HeaderLabel->SetColorAndOpacity(FSlateColor(LabelColor));
	}
	if (TitleText != nullptr)
	{
		if (TitleFont.HasValidFont())
		{
			FSlateFontInfo Font = TitleFont;
			Font.Size = FMath::RoundToInt(TitleFont.Size * FMath::Max(0.1f, HudScale));
			TitleText->SetFont(Font);
		}
		TitleText->SetColorAndOpacity(FSlateColor(TitleColor));
	}
	if (DetailText != nullptr)
	{
		if (DetailFont.HasValidFont()) { DetailText->SetFont(DetailFont); }
		DetailText->SetColorAndOpacity(FSlateColor(DetailColor));
	}
}

void UMetamorphosisTaskWidget::RefreshTask()
{
	UMetamorphosisTaskSubsystem* Tasks = UMetamorphosisTaskSubsystem::Get(this);
	if (Tasks == nullptr || TitleText == nullptr)
	{
		return;
	}

	const FTaskConfigEntry* Task = Tasks->GetCurrentTask();
	const bool bAllDone = Tasks->IsAllCompleted();

	// 没有任务（配置没载入 / 还没开工）就整块收起来；有一条以上都显示
	FString Title;
	if (Task != nullptr)
	{
		Title = Task->Title;
	}
	else if (bAllDone)
	{
		Title = AllDoneText.ToString();
	}
	FString Detail = Task != nullptr ? Task->Detail : FString();

	// 剧情玩法门期间：用剧情给的目标文字顶掉标题（门结束由剧情系统清除）。
	// 保留原任务不丢线：把它挪到小字行显示「（当前任务：XXX）」。
	// 选这个方案的理由：玩法门往往只是主线里的一小步，玩家不该因此忘记当前主线目标。
	if (Tasks->HasObjectiveOverride())
	{
		Detail = Task != nullptr
			? FString::Printf(TEXT("（当前任务：%s）"), *Task->Title)
			: FString();
		Title = Tasks->GetObjectiveOverride().ToString();

		// 限时门：在标题后带上剩余秒数，让玩家知道有时间压力
		const float Left = Tasks->GetObjectiveSecondsLeft();
		if (Left > 0.f)
		{
			Title += FString::Printf(TEXT("（%d 秒）"), FMath::CeilToInt(Left));
		}
	}

	// 脏检查：内容没变就什么都不做（不每帧刷文本/重排布局）
	if (Title == LastTitle && Detail == LastDetail && bAllDone == bLastAllCompleted)
	{
		return;
	}
	LastTitle = Title;
	LastDetail = Detail;
	bLastAllCompleted = bAllDone;

	// 只折叠内部面板，控件本身保持可见 —— Slate 不会 tick 被整体 Collapsed 的控件
	if (PanelBorder != nullptr)
	{
		PanelBorder->SetVisibility(Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	TitleText->SetText(FText::FromString(Title));
	DetailText->SetText(FText::FromString(Detail));
	if (DetailText->GetParent() != nullptr)
	{
		DetailText->SetVisibility(Detail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	OnTaskUpdated(FText::FromString(Title), FText::FromString(Detail), bAllDone);
}
