// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 交付 2「三值 UI 分层包」接入版：
//   * 默认布局按美术给的设计空间 **1118 x 289** 摆位（坐标来自美术的《图层坐标清单.json》，
//     原画布 1536x1024，三值区左上角在 (88,29)，这里统一减掉这个偏移）。
//   * 整块面板用 ScaleBox 等比缩放到 PanelScreenWidth（默认 420 UMG 单位），
//     这样"改屏幕分辨率"和"改美术尺寸"互不影响 —— 踩过的坑：别拿 GetViewportSize 算尺寸，
//     工程是 1.5 倍 DPI，UMG 本地空间比物理分辨率大；纯 C++ 布局也必须放在 Initialize() 里搭。
//   * 三层结构（底部 -> 顶部）：底板 / 外框 / 内容（图标、预渲染字样、读条、圆点、数值文字）。
//
// 三态映射（对应美术的 低值_阶段I / 中值_阶段II / 高值_阶段III 三套）：
//   怀疑值：AlertLevel 0/1/2 -> 眼 闭/睁/红 + 读条填充 绿/绿/红
//   躁郁值：BodyStage 1/2/3    -> 茧 完整/裂/破 + 圆点 绿/绿/红 + 阶段字符 I/II/III
//   阈值一律读 MetamorphosisStatsSubsystem（配表驱动），本类不写死任何阈值。

#include "UI/MetamorphosisStatsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Internationalization/Text.h"
#include "Metamorphosis.h"
#include "Stats/MetamorphosisStatsSubsystem.h"
#include "Types/SlateEnums.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisStatsWidget"

namespace
{
	/** 纯色 Brush：默认外观不需要任何贴图，美术换皮时直接替换 Style 里的 Brush 即可。
	 *  注意 FSlateBrush 带参数的构造函数是 protected，纯色只能走 FSlateColorBrush。 */
	FSlateBrush MakeSolidBrush(const FLinearColor& InColor)
	{
		return FSlateColorBrush(InColor);
	}

	/** 用贴图做 Image 画刷（大小取贴图原始像素，避免被 Slate 按 0 尺寸吞掉） */
	FSlateBrush MakeImageBrush(UTexture2D* InTexture)
	{
		FSlateBrush Brush;
		if (InTexture != nullptr)
		{
			Brush.SetResourceObject(InTexture);
			Brush.ImageSize = FVector2D(InTexture->GetSurfaceWidth(), InTexture->GetSurfaceHeight());
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.ImageType = ESlateBrushImageType::FullColor;
			Brush.TintColor = FSlateColor(FLinearColor::White);
		}
		return Brush;
	}

	template <typename TWidgetT>
	TWidgetT* MakeWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	/** 按设计空间坐标把控件摆到 Canvas 上（左上角锚点 + 绝对位置/尺寸） */
	void PlaceOnCanvas(UCanvasPanel* Canvas, UWidget* Widget, float X, float Y, float Width, float Height)
	{
		if (Canvas == nullptr || Widget == nullptr)
		{
			return;
		}

		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetAutoSize(false);
			Slot->SetAnchors(FAnchors(0.f, 0.f));
			Slot->SetAlignment(FVector2D(0.f, 0.f));
			Slot->SetPosition(FVector2D(X, Y));
			Slot->SetSize(FVector2D(Width, Height));
		}
	}

	UImage* MakeCanvasImage(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name,
		const FSlateBrush& Brush, float X, float Y, float Width, float Height)
	{
		UImage* Image = MakeWidget<UImage>(Tree, Name);
		Image->SetBrush(Brush);
		PlaceOnCanvas(Canvas, Image, X, Y, Width, Height);
		return Image;
	}

	/** 从 Delivery2/Stats 里载入一张贴图（构造函数里只能走 ConstructorHelpers） */
	void LoadStatBrush(FSlateBrush& OutBrush, const TCHAR* AssetName)
	{
		// 注意：static 的 Finder 必须放在独立作用域里，否则多个调用点会共用一个 static 变量。
		const FString Path = FString::Printf(TEXT("/Game/UI/Delivery2/Stats/%s.%s"), AssetName, AssetName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
		OutBrush = MakeImageBrush(Texture);
	}

	/** 刷新一条数值（进度条 + 右侧文字 + 档位配色） */
	void UpdateStatVisual(
		const TObjectPtr<UProgressBar>& Bar,
		const TObjectPtr<UTextBlock>& Value,
		float Current,
		float Max,
		float Ratio,
		int32 TierIndex,
		const TArray<FText>& TierTexts,
		const TArray<FLinearColor>& TierColors,
		const FText& Format,
		bool bTintByTier)
	{
		if (Bar != nullptr)
		{
			Bar->SetPercent(Ratio);

			if (bTintByTier && TierColors.IsValidIndex(TierIndex))
			{
				Bar->SetFillColorAndOpacity(TierColors[TierIndex]);
			}
		}

		if (Value != nullptr)
		{
			// 用户要求：去掉「0/100 忽略」「2/100 虫子」这类数值+档位文字（放大会溢出外框、还压住圆点）。
			// 数值本身仍由读条 / 圆点 / 阶段牌表达，所以这里直接把文本块收起；将来想恢复只需改回 SetVisibility(Visible)。
			Value->SetText(FText::GetEmpty());
			Value->SetVisibility(ESlateVisibility::Collapsed);
			const FText TierText = TierTexts.IsValidIndex(TierIndex) ? TierTexts[TierIndex] : FText::GetEmpty();
			(void)TierText;
		}
	}
}

// ---------------------------------------------------------------------------
// 构造 / 生命周期
// ---------------------------------------------------------------------------

UMetamorphosisStatsWidget::UMetamorphosisStatsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ---- 默认文案 ----
	SuspicionLabelText = LOCTEXT("SuspicionLabel", "怀疑值");
	MoodLabelText = LOCTEXT("MoodLabel", "躁郁值");
	ValueFormatText = LOCTEXT("ValueFormat", "{0} / {1}   {2}");

	AlertLevelTexts =
	{
		LOCTEXT("AlertIgnore", "忽略"),
		LOCTEXT("AlertSearch", "搜寻"),
		LOCTEXT("AlertExposed", "暴露")
	};

	BodyStageTexts =
	{
		LOCTEXT("StageBug", "虫子"),
		LOCTEXT("StageDouble", "2 倍"),
		LOCTEXT("StageQuad", "4 倍")
	};

	// ---- 默认字体：和 UTextBlock 一样取引擎 Roboto。
	//      构造函数里不能碰 Slate 样式集（FCoreStyle），只能用 ConstructorHelpers 找资源。
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		LabelFont = FSlateFontInfo(RobotoFont.Object, 22, FName("Regular"));
		ValueFont = FSlateFontInfo(RobotoFont.Object, 26, FName("Bold"));
	}

	// ---- 数值颜色：深墨绿纸面上的亮金字 ----
	LabelColor = FLinearColor(0.93f, 0.87f, 0.74f, 1.f);
	ValueColor = FLinearColor(0.98f, 0.94f, 0.86f, 1.f);

	// 档位配色：只用来给进度条/圆点做轻微统一（贴图本身已经带色）
	AlertLevelColors =
	{
		FLinearColor::White,
		FLinearColor(1.f, 0.92f, 0.78f, 1.f),
		FLinearColor(1.f, 0.72f, 0.70f, 1.f)
	};
	BodyStageColors =
	{
		FLinearColor::White,
		FLinearColor(1.f, 0.92f, 0.78f, 1.f),
		FLinearColor(1.f, 0.78f, 0.72f, 1.f)
	};

	// ---- 交付 2 的三值 UI 贴图 ----
	// 三态贴图用 TArray 装（蓝图可见），索引 0/1/2 = 低/中/高
	EyeBrushes.SetNum(3);
	CocoonBrushes.SetNum(3);
	StageCharBrushes.SetNum(3);

	LoadStatBrush(TitleFrameBrush,        TEXT("T_TitleFrame"));
	LoadStatBrush(TitlePlateBrush,        TEXT("T_TitlePlate"));
	LoadStatBrush(TitleTextBrush,         TEXT("T_TitleText"));
	LoadStatBrush(SuspicionFrameBrush,    TEXT("T_SuspFrame"));
	LoadStatBrush(SuspicionPlateBrush,    TEXT("T_SuspPlate"));
	LoadStatBrush(MoodFrameBrush,         TEXT("T_MoodFrame"));
	LoadStatBrush(MoodPlateBrush,         TEXT("T_MoodPlate"));
	LoadStatBrush(BarTrackBrush,          TEXT("T_BarTrack"));
	LoadStatBrush(BarOutlineBrush,        TEXT("T_BarOutline"));
	LoadStatBrush(BarFillGreenBrush,      TEXT("T_Fill_Green"));
	LoadStatBrush(BarFillRedBrush,        TEXT("T_Fill_Red"));
	LoadStatBrush(LabelSuspicionBrush,    TEXT("T_Label_Suspicion"));
	LoadStatBrush(LabelMoodBrush,         TEXT("T_Label_Mood"));
	LoadStatBrush(LabelBodyBrush,         TEXT("T_Label_Body"));
	LoadStatBrush(PipEmptyBrush,          TEXT("T_Pip_Empty"));
	LoadStatBrush(PipGreenBrush,          TEXT("T_Pip_Green"));
	LoadStatBrush(PipRedBrush,            TEXT("T_Pip_Red"));
	LoadStatBrush(StagePlateBrush,        TEXT("T_StagePlate"));
	LoadStatBrush(BodyLineBrush,          TEXT("T_BodyLine"));

	LoadStatBrush(EyeBrushes[0],          TEXT("T_Eye_0"));
	LoadStatBrush(EyeBrushes[1],          TEXT("T_Eye_1"));
	LoadStatBrush(EyeBrushes[2],          TEXT("T_Eye_2"));
	LoadStatBrush(CocoonBrushes[0],       TEXT("T_Cocoon_0"));
	LoadStatBrush(CocoonBrushes[1],       TEXT("T_Cocoon_1"));
	LoadStatBrush(CocoonBrushes[2],       TEXT("T_Cocoon_2"));
	LoadStatBrush(StageCharBrushes[0],    TEXT("T_StageChar_1"));
	LoadStatBrush(StageCharBrushes[1],    TEXT("T_StageChar_2"));
	LoadStatBrush(StageCharBrushes[2],    TEXT("T_StageChar_3"));

	// ---- 进度条样式：槽底 + 填充都用美术给的贴图（填充绿/红在刷新时切换） ----
	SuspicionBarStyle = FProgressBarStyle::GetDefault();
	SuspicionBarStyle.BackgroundImage = BarTrackBrush.HasUObject() ? BarTrackBrush : MakeSolidBrush(FLinearColor(0.06f, 0.05f, 0.04f, 0.85f));
	SuspicionBarStyle.FillImage = BarFillGreenBrush.HasUObject() ? BarFillGreenBrush : MakeSolidBrush(FLinearColor::White);

	MoodBarStyle = SuspicionBarStyle;

	// 换皮后不再用"着色档位"，颜色由贴图本身决定
	bTintBarByTier = false;
}

bool UMetamorphosisStatsWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	// 设计器树为空 => 这是纯 C++ 类，自己搭一棵默认的布局
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
		bBuiltLayout = true;
		bApplyBarStyle = true;
	}

	CacheWidgets();
	return bResult;
}

float UMetamorphosisStatsWidget::GetPreferredHUDHeight() const
{
	// 设计空间 1118x289 等比缩放到 PanelScreenWidth；除零保护兜一下
	if (StatsDesignWidth <= KINDA_SMALL_NUMBER)
	{
		return PanelScreenWidth;
	}
	return PanelScreenWidth * (StatsDesignHeight / StatsDesignWidth);
}

void UMetamorphosisStatsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// HUD 不参与命中测试，否则会挡住「点击移动」
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		Stats->OnStatsChanged.AddDynamic(this, &UMetamorphosisStatsWidget::HandleStatsChanged);
	}

	ApplyStaticStyle();
	RefreshStats();
}

void UMetamorphosisStatsWidget::NativeDestruct()
{
	if (UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this))
	{
		Stats->OnStatsChanged.RemoveDynamic(this, &UMetamorphosisStatsWidget::HandleStatsChanged);
	}

	Super::NativeDestruct();
}

void UMetamorphosisStatsWidget::HandleStatsChanged(float /*Suspicion*/, float /*Mood*/, int32 /*AlertLevel*/, int32 /*BodyStage*/)
{
	// 数值系统只在真的变化时才广播，这里直接重新取一遍最省事
	RefreshStats();
}

// ---------------------------------------------------------------------------
// 布局（设计空间 1118 x 289）
// ---------------------------------------------------------------------------

void UMetamorphosisStatsWidget::BuildDefaultLayout()
{
	// 外层：固定屏幕宽度 -> 等比缩放到设计空间 -> 1118x289 的画布
	USizeBox* Root = MakeWidget<USizeBox>(WidgetTree, TEXT("StatsRoot"));
	Root->SetWidthOverride(PanelScreenWidth);
	Root->SetHeightOverride(PanelScreenWidth * (StatsDesignHeight / StatsDesignWidth));
	WidgetTree->RootWidget = Root;

	UScaleBox* Scaler = MakeWidget<UScaleBox>(WidgetTree, TEXT("StatsScaler"));
	Scaler->SetStretch(EStretch::ScaleToFit);
	Scaler->SetStretchDirection(EStretchDirection::Both);
	Root->AddChild(Scaler);

	USizeBox* DesignBox = MakeWidget<USizeBox>(WidgetTree, TEXT("StatsDesign"));
	DesignBox->SetWidthOverride(StatsDesignWidth);
	DesignBox->SetHeightOverride(StatsDesignHeight);
	Scaler->AddChild(DesignBox);

	UCanvasPanel* Canvas = MakeWidget<UCanvasPanel>(WidgetTree, TEXT("StatsCanvas"));
	DesignBox->AddChild(Canvas);

	// ---- 左上角标题牌（「三值」字样） ----
	MakeCanvasImage(WidgetTree, Canvas, TEXT("TitlePlate"), TitlePlateBrush, 19.f, 32.f, 253.f, 62.f);
	MakeCanvasImage(WidgetTree, Canvas, TEXT("TitleText"), TitleTextBrush, 42.f, 37.f, 219.f, 50.f);
	MakeCanvasImage(WidgetTree, Canvas, TEXT("TitleFrame"), TitleFrameBrush, 0.f, 19.f, 297.f, 84.f);

	// ---- 第 1 行：怀疑值 ----
	MakeCanvasImage(WidgetTree, Canvas, TEXT("SuspicionPlate"), SuspicionPlateBrush, 317.f, 6.f, 787.f, 94.f);
	MakeCanvasImage(WidgetTree, Canvas, TEXT("SuspicionFrame"), SuspicionFrameBrush, 306.f, 0.f, 812.f, 101.f);

	SuspicionIcon = MakeCanvasImage(WidgetTree, Canvas, TEXT("SuspicionIcon"),
		(EyeBrushes.IsValidIndex(0) && EyeBrushes[0].HasUObject()) ? EyeBrushes[0] : SuspicionIconBrush, 339.f, 3.f, 122.f, 95.f);

	MakeCanvasImage(WidgetTree, Canvas, TEXT("SuspicionLabelArt"), LabelSuspicionBrush, 487.f, 20.f, 140.f, 65.f);

	// 读条：ProgressBar（槽底=Track，填充=绿/红），再叠一层外框
	{
		USizeBox* BarBox = MakeWidget<USizeBox>(WidgetTree, TEXT("SuspicionBarBox"));
		BarBox->SetWidthOverride(383.f);
		BarBox->SetHeightOverride(18.f);
		PlaceOnCanvas(Canvas, BarBox, 656.f, 43.f, 383.f, 18.f);

		UProgressBar* Bar = MakeWidget<UProgressBar>(WidgetTree, TEXT("SuspicionBar"));
		Bar->SetPercent(0.f);
		Bar->SetWidgetStyle(SuspicionBarStyle);
		Bar->SetFillColorAndOpacity(FLinearColor::White);
		BarBox->AddChild(Bar);
		SuspicionBar = Bar;
	}

	MakeCanvasImage(WidgetTree, Canvas, TEXT("SuspicionBarOutline"), BarOutlineBrush, 649.f, 35.f, 400.f, 35.f);

	SuspicionValue = MakeWidget<UTextBlock>(WidgetTree, TEXT("SuspicionValue"));
	SuspicionValue->SetJustification(ETextJustify::Right);
	PlaceOnCanvas(Canvas, SuspicionValue, 800.f, 72.f, 300.f, 26.f);

	// ---- 第 2 行：躁郁值（美术这一行是 8 个刻度圆点，不是读条） ----
	MakeCanvasImage(WidgetTree, Canvas, TEXT("MoodPlate"), MoodPlateBrush, 317.f, 104.f, 787.f, 94.f);
	MakeCanvasImage(WidgetTree, Canvas, TEXT("MoodFrame"), MoodFrameBrush, 306.f, 97.f, 812.f, 113.f);

	MoodIcon = MakeCanvasImage(WidgetTree, Canvas, TEXT("MoodIcon"),
		(CocoonBrushes.IsValidIndex(0) && CocoonBrushes[0].HasUObject()) ? CocoonBrushes[0] : MoodIconBrush, 339.f, 98.f, 122.f, 105.f);

	MakeCanvasImage(WidgetTree, Canvas, TEXT("MoodLabelArt"), LabelMoodBrush, 487.f, 114.f, 140.f, 66.f);

	// 8 个圆点（美术给的是 49x49，间距 51.5）
	MoodPips.Reset();
	{
		static const float PipX[8] = { 651.f, 702.f, 753.f, 805.f, 857.f, 908.f, 959.f, 1010.f };
		for (int32 Index = 0; Index < 8; ++Index)
		{
			UImage* Pip = MakeCanvasImage(WidgetTree, Canvas, *FString::Printf(TEXT("MoodPip%d"), Index + 1),
				PipEmptyBrush, PipX[Index], 122.f, 49.f, 49.f);
			MoodPips.Add(Pip);
		}
	}

	// 躁郁值仍然保留一个 ProgressBar（WBP 子类/调试按名字找得到），但美术这一行是圆点，所以默认隐藏。
	{
		UProgressBar* Bar = MakeWidget<UProgressBar>(WidgetTree, TEXT("MoodBar"));
		Bar->SetPercent(0.f);
		Bar->SetWidgetStyle(MoodBarStyle);
		Bar->SetVisibility(ESlateVisibility::Collapsed);
		PlaceOnCanvas(Canvas, Bar, 656.f, 43.f, 383.f, 18.f);
		MoodBar = Bar;
	}

	MoodValue = MakeWidget<UTextBlock>(WidgetTree, TEXT("MoodValue"));
	MoodValue->SetJustification(ETextJustify::Right);
	PlaceOnCanvas(Canvas, MoodValue, 800.f, 172.f, 300.f, 24.f);

	// ---- 第 3 行：体型阶段 ----
	MakeCanvasImage(WidgetTree, Canvas, TEXT("BodyLine"), BodyLineBrush, 362.f, 230.f, 335.f, 23.f);
	MakeCanvasImage(WidgetTree, Canvas, TEXT("BodyLabelArt"), LabelBodyBrush, 812.f, 217.f, 171.f, 57.f);
	MakeCanvasImage(WidgetTree, Canvas, TEXT("StagePlate"), StagePlateBrush, 994.f, 206.f, 86.f, 83.f);

	BodyStageCharImage = MakeCanvasImage(WidgetTree, Canvas, TEXT("StageChar"),
		StageCharBrushes.Num() > 0 ? StageCharBrushes[0] : FSlateBrush(), 1014.f, 223.f, 46.f, 46.f);
}

void UMetamorphosisStatsWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (SuspicionBar == nullptr)   { SuspicionBar = WidgetTree->FindWidget<UProgressBar>(TEXT("SuspicionBar")); }
	if (MoodBar == nullptr)        { MoodBar = WidgetTree->FindWidget<UProgressBar>(TEXT("MoodBar")); }
	if (SuspicionLabel == nullptr) { SuspicionLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("SuspicionLabel")); }
	if (MoodLabel == nullptr)      { MoodLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("MoodLabel")); }
	if (SuspicionValue == nullptr) { SuspicionValue = WidgetTree->FindWidget<UTextBlock>(TEXT("SuspicionValue")); }
	if (MoodValue == nullptr)      { MoodValue = WidgetTree->FindWidget<UTextBlock>(TEXT("MoodValue")); }
	if (SuspicionIcon == nullptr)  { SuspicionIcon = WidgetTree->FindWidget<UImage>(TEXT("SuspicionIcon")); }
	if (MoodIcon == nullptr)       { MoodIcon = WidgetTree->FindWidget<UImage>(TEXT("MoodIcon")); }
	if (BodyStageCharImage == nullptr) { BodyStageCharImage = WidgetTree->FindWidget<UImage>(TEXT("StageChar")); }

	// WBP 子类最容易踩的坑：控件名字没对上，界面就永远不刷新。这里直接点名。
	TArray<FString> Missing;
	if (SuspicionBar == nullptr)   { Missing.Add(TEXT("SuspicionBar")); }
	if (SuspicionValue == nullptr) { Missing.Add(TEXT("SuspicionValue")); }
	if (MoodValue == nullptr)      { Missing.Add(TEXT("MoodValue")); }

	if (Missing.Num() > 0)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] %s 里找不到控件：%s。做 WBP 子类时请用这些名字命名控件（进度条用 ProgressBar，数值文字用 TextBlock）。"),
			*GetName(), *FString::Join(Missing, TEXT(", ")));
	}
}

void UMetamorphosisStatsWidget::ApplyStaticStyle()
{
	if (SuspicionLabel != nullptr)
	{
		if (LabelFont.HasValidFont()) { SuspicionLabel->SetFont(LabelFont); }
		SuspicionLabel->SetColorAndOpacity(FSlateColor(LabelColor));
	}

	if (MoodLabel != nullptr)
	{
		if (LabelFont.HasValidFont()) { MoodLabel->SetFont(LabelFont); }
		MoodLabel->SetColorAndOpacity(FSlateColor(LabelColor));
	}

	// 数值文字画在深墨绿纸面上：暖白 + 轻微描边感
	if (SuspicionValue != nullptr)
	{
		if (ValueFont.HasValidFont()) { SuspicionValue->SetFont(ValueFont); }
		SuspicionValue->SetColorAndOpacity(FSlateColor(ValueColor));
		SuspicionValue->SetShadowOffset(FVector2D(1.f, 2.f));
		SuspicionValue->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	}

	if (MoodValue != nullptr)
	{
		if (ValueFont.HasValidFont())
		{
			FSlateFontInfo SmallFont = ValueFont;
			SmallFont.Size = 24;
			MoodValue->SetFont(SmallFont);
		}
		MoodValue->SetColorAndOpacity(FSlateColor(ValueColor));
		MoodValue->SetShadowOffset(FVector2D(1.f, 2.f));
		MoodValue->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	}

	// 只有默认布局（或美术显式勾选）才由本类接管进度条样式，避免覆盖 WBP 里自己调的样式
	if (bBuiltLayout || bApplyBarStyle)
	{
		if (SuspicionBar != nullptr) { SuspicionBar->SetWidgetStyle(SuspicionBarStyle); }
		if (MoodBar != nullptr)      { MoodBar->SetWidgetStyle(MoodBarStyle); }
	}
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------

void UMetamorphosisStatsWidget::UpdateTierArt(int32 AlertLevel, int32 BodyStage)
{
	const int32 LevelIndex = FMath::Clamp(AlertLevel, 0, 2);
	const int32 StageIndex = FMath::Clamp(BodyStage - 1, 0, 2);

	// 怀疑值：眼 闭/睁/红
	if (SuspicionIcon != nullptr && EyeBrushes.IsValidIndex(LevelIndex) && EyeBrushes[LevelIndex].HasUObject())
	{
		SuspicionIcon->SetBrush(EyeBrushes[LevelIndex]);
	}

	// 躁郁值：茧 完整/裂/破
	if (MoodIcon != nullptr && CocoonBrushes.IsValidIndex(StageIndex) && CocoonBrushes[StageIndex].HasUObject())
	{
		MoodIcon->SetBrush(CocoonBrushes[StageIndex]);
	}

	// 体型阶段：阶段字符 I/II/III
	if (BodyStageCharImage != nullptr && StageCharBrushes.IsValidIndex(StageIndex) && StageCharBrushes[StageIndex].HasUObject())
	{
		BodyStageCharImage->SetBrush(StageCharBrushes[StageIndex]);
	}

	// 读条填充：低值/中值 绿，高值（暴露 / 第三阶段）红
	const bool bHighState = (LevelIndex >= 2) || (StageIndex >= 2);
	if (SuspicionBar != nullptr && (BarFillGreenBrush.HasUObject() || BarFillRedBrush.HasUObject()))
	{
		FProgressBarStyle Style = SuspicionBarStyle;
		Style.FillImage = bHighState ? BarFillRedBrush : BarFillGreenBrush;
		SuspicionBar->SetWidgetStyle(Style);
	}

	// 圆点：空心 / 绿 / 红
	FSlateBrush PipFilled = bHighState ? PipRedBrush : PipGreenBrush;
	if (!PipFilled.HasUObject())
	{
		PipFilled = PipEmptyBrush;
	}
	for (TObjectPtr<UImage>& Pip : MoodPips)
	{
		if (Pip != nullptr)
		{
			Pip->SetBrush(PipEmptyBrush);
		}
	}
	(void)PipFilled;
}

void UMetamorphosisStatsWidget::RefreshStats()
{
	UMetamorphosisStatsSubsystem* Stats = UMetamorphosisStatsSubsystem::Get(this);
	if (Stats == nullptr)
	{
		return;
	}

	const float Suspicion = Stats->GetSuspicion();
	const float Mood = Stats->GetMood();
	const int32 AlertLevel = Stats->GetAlertLevel();
	const int32 BodyStage = Stats->GetBodyStage();

	UpdateStatVisual(
		SuspicionBar, SuspicionValue,
		Suspicion, Stats->GetMaxSuspicion(), Stats->GetSuspicionRatio(),
		AlertLevel, AlertLevelTexts, AlertLevelColors, ValueFormatText, bTintBarByTier);

	UpdateStatVisual(
		MoodBar, MoodValue,
		Mood, Stats->GetMaxMood(), Stats->GetMoodRatio(),
		BodyStage - 1, BodyStageTexts, BodyStageColors, ValueFormatText, bTintBarByTier);

	UpdateTierArt(AlertLevel, BodyStage);

	// 躁郁值画成 8 段圆点：比例 -> 点亮个数（美术的 pips 就是当刻度用的）
	if (MoodPips.Num() > 0)
	{
		const float MoodRatio = FMath::Clamp(Stats->GetMoodRatio(), 0.f, 1.f);
		const int32 LitCount = FMath::RoundToInt(MoodRatio * MoodPips.Num());
		const bool bHighState = (FMath::Clamp(BodyStage - 1, 0, 2) >= 2) || (FMath::Clamp(AlertLevel, 0, 2) >= 2);
		const FSlateBrush& LitBrush = bHighState ? PipRedBrush : PipGreenBrush;

		for (int32 Index = 0; Index < MoodPips.Num(); ++Index)
		{
			if (UImage* Pip = MoodPips[Index])
			{
				Pip->SetBrush(Index < LitCount && LitBrush.HasUObject() ? LitBrush : PipEmptyBrush);
			}
		}
	}

	OnStatsUpdated(Suspicion, Mood, AlertLevel, BodyStage);
}

#undef LOCTEXT_NAMESPACE
