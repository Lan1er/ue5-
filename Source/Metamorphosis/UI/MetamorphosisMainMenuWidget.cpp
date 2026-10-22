// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Game/MetamorphosisPreloadSubsystem.h"
#include "Save/MetamorphosisSaveSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MetamorphosisPlayerController.h"
#include "Story/MetamorphosisStorySubsystem.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/PanelWidget.h"

#define LOCTEXT_NAMESPACE "MetamorphosisMainMenu"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakeMenuWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	/** 按钮样式：设计图上按钮是"纯文字浮在背景上"（没有底板框）。
	 *  所以：常态完全透明；悬停一层很淡的暗红；按下稍亮；禁用态透明（只有文字图变灰）。 */
	FButtonStyle MakeMenuButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Hovered = FSlateColorBrush(FLinearColor(0.45f, 0.08f, 0.06f, 0.55f));
		Style.Pressed = FSlateColorBrush(FLinearColor(0.55f, 0.10f, 0.08f, 0.75f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.NormalPadding = FMargin(14.f, 6.f);
		Style.PressedPadding = FMargin(15.f, 7.f);
		return Style;
	}
}

UMetamorphosisMainMenuWidget::UMetamorphosisMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ---- 背景：设计图里的厨房插画 ----
	// 交付包里原来只有 logo + 装饰层，没有整张背景 ✗ -> 从用户给的设计图（2331x1326）里
	// 裁出【没有任何 HUD 元素】的干净区域（x 980~1700, y 0~1000，含门/长桌/椅子/花瓶/地上甲虫），
	// 存成 SourceArt/UI/Delivery2/Start/BG_MainMenu.png 再导入。换图只需替换这个资产即可。
	BackgroundBrush = FSlateColorBrush(FLinearColor(0.035f, 0.035f, 0.032f, 1.f));
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> BgTex(
			TEXT("/Game/UI/Delivery2/Start_Design/BG_Composed.BG_Composed"));
		if (BgTex.Succeeded() && BgTex.Object != nullptr)
		{
			BackgroundBrush = FSlateBrush();
			BackgroundBrush.SetResourceObject(BgTex.Object);
			BackgroundBrush.ImageSize = FVector2D(
				static_cast<float>(BgTex.Object->GetSizeX()), static_cast<float>(BgTex.Object->GetSizeY()));
			BackgroundBrush.DrawAs = ESlateBrushDrawType::Image;
			BackgroundBrush.Tiling = ESlateBrushTileType::NoTile;
		}
	}
	// 有背景图时用白色 tint（不压暗画面）；没图时退回深色底
	BackgroundColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	LogoWidth = 560.f;
	{
		auto LoadMenuBrush = [](FSlateBrush& OutBrush, const TCHAR* AssetName)
		{
			const FString Path = FString::Printf(TEXT("/Game/UI/Delivery2/Start_Design/%s.%s"), AssetName, AssetName);
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
			if (Texture != nullptr)
			{
				OutBrush.SetResourceObject(Texture);
				OutBrush.ImageSize = FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
				OutBrush.DrawAs = ESlateBrushDrawType::Image;
				OutBrush.ImageType = ESlateBrushImageType::FullColor;
			}
		};

		LoadMenuBrush(LogoTexBrush, TEXT("Logo"));
		// 划痕已烤进 BG_Composed，不再单独加载（否则会叠两层）
		// 暗角同样已烤进背景
		LoadMenuBrush(FocusLineBrush, TEXT("T_FocusLine"));

		// 有 logo 图就不再显示文字标题（否则标题重复出现两次）
		if (LogoTexBrush.HasUObject())
		{
			TitleText = FText::GetEmpty();
		}
	}

	TitleText = LOCTEXT("TitleText", "变形记");
	SubtitleText = LOCTEXT("SubtitleText", "Metamorphosis");
	StartButtonText = LOCTEXT("StartButtonText", "新游戏");
	SettingsButtonText = LOCTEXT("SettingsButtonText", "设置");

	StartButtonStyle = MakeMenuButtonStyle();

	// ---------------- 交付 2：四个按钮的文字图 ----------------
	// 美术把「继续游戏 / 开始游戏 / 设置 / 退出」切成了图片（T_Txt_*）；
	// 统一在这里用 LoadObject 取（构造期就取，取不到 BuildDefaultLayout 里还会再试一次），
	// 拿到就用图片、拿不到退回文字按钮（不会变成空按钮）。
	// 图片本身必须 HitTestInvisible，否则会把点击吃掉（之前"开始界面点不动"就是这么来的）。
	auto LoadMenuBrush2 = [](FSlateBrush& OutBrush, const TCHAR* AssetName) -> bool
	{
		const FString Path = FString::Printf(TEXT("/Game/UI/Delivery2/Start_Design/%s.%s"), AssetName, AssetName);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path))
		{
			OutBrush.SetResourceObject(Texture);
			OutBrush.ImageSize = FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
			OutBrush.DrawAs = ESlateBrushDrawType::Image;
			OutBrush.ImageType = ESlateBrushImageType::FullColor;
			return true;
		}
		return false;
	};

	{
		// 用交付包的【3x】版本（462x126 那种）：之前用的是 1x（136x31），在 342x78 的按钮里被放大 2.5 倍，
		// 笔画发虚、看着"不像美术画的"（用户反馈"文字风格不符合"）。3x 贴进 78 高的按钮是缩小显示，清晰。
		const bool bContinue = LoadMenuBrush2(ContinueArtBrush, TEXT("Txt3_Continue"));
		// 「开始游戏」美术图里烘死的是「开始游戏」，用户要显示「新游戏」-> 用同规格（462x126、宋体、同色 #D2C8A4）
		// 现做的 Txt3_NewGame.png（Tools/render_newgame_text.py），仍然是"美术文字图"，不再是系统字体文字块。
		const bool bStart = LoadMenuBrush2(StartArtBrush, TEXT("Txt3_NewGame"));
		const bool bSettings = LoadMenuBrush2(SettingsArtBrush, TEXT("Txt3_Settings"));
		const bool bQuit = LoadMenuBrush2(QuitArtBrush, TEXT("Txt3_Quit"));
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Menu] 按钮文字图载入：继续=%d 开始=%d 设置=%d 退出=%d（0 的会退回纯文字）"),
			bContinue ? 1 : 0, bStart ? 1 : 0, bSettings ? 1 : 0, bQuit ? 1 : 0);

		// 有 logo 图就不再显示文字标题（否则标题重复出现两次）
		if (LogoTexBrush.HasUObject())
		{
			TitleText = FText::GetEmpty();
		}
	}

	TitleText = LOCTEXT("TitleText", "变形记");
	SubtitleText = LOCTEXT("SubtitleText", "Metamorphosis");
	StartButtonText = LOCTEXT("StartButtonText", "新游戏");
	SettingsButtonText = LOCTEXT("SettingsButtonText", "设置");
	ContinueButtonText = LOCTEXT("ContinueButtonText", "继续游戏");
	QuitButtonText = LOCTEXT("QuitButtonText", "退出");
	// 设计图：logo 下面没有文字标题 -> 有 logo 图就把标题/副标题清空（避免和 logo 重复）
	if (LogoTexBrush.HasUObject())
	{
		TitleText = FText::GetEmpty();
		SubtitleText = FText::GetEmpty();
	}

	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		TitleFont = FSlateFontInfo(RobotoFont.Object, 64, FName("Bold"));
		BodyFont = FSlateFontInfo(RobotoFont.Object, 22, FName("Regular"));
	}
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisMainMenuWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		bApplyButtonStyle = true;
		BuildDefaultLayout();
		bBuiltLayout = true;
	}

	CacheWidgets();
	return bResult;
}

void UMetamorphosisMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Visible);

	if (StartButton != nullptr)
	{
		StartButton->OnClicked.AddDynamic(this, &UMetamorphosisMainMenuWidget::HandleStartClicked);
	}
	if (SettingsButton != nullptr)
	{
		SettingsButton->OnClicked.AddDynamic(this, &UMetamorphosisMainMenuWidget::HandleSettingsClicked);
	}
	if (ContinueButton != nullptr)
	{
		ContinueButton->OnClicked.AddDynamic(this, &UMetamorphosisMainMenuWidget::HandleContinueClicked);
	}
	if (QuitButton != nullptr)
	{
		QuitButton->OnClicked.AddDynamic(this, &UMetamorphosisMainMenuWidget::HandleQuitClicked);
	}

	ApplyStaticStyle();

	// 开始界面一出现就开始后台预加载（这是这个界面存在的意义）
	if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this))
	{
		Preload->StartPreload();
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Menu] 拿不到预加载子系统，直接放开「开始游戏」"));
		bLastReady = true;
	}

	RefreshFromPreload();

	// 「继续游戏」：根据存档是否存在决定置灰/可用（每次进主界面都刷新一次）
	RefreshContinueAvailability();
}

void UMetamorphosisMainMenuWidget::NativeDestruct()
{
	if (StartButton != nullptr)
	{
		StartButton->OnClicked.RemoveDynamic(this, &UMetamorphosisMainMenuWidget::HandleStartClicked);
	}
	if (SettingsButton != nullptr)
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &UMetamorphosisMainMenuWidget::HandleSettingsClicked);
	}
	if (ContinueButton != nullptr)
	{
		ContinueButton->OnClicked.RemoveDynamic(this, &UMetamorphosisMainMenuWidget::HandleContinueClicked);
	}
	if (QuitButton != nullptr)
	{
		QuitButton->OnClicked.RemoveDynamic(this, &UMetamorphosisMainMenuWidget::HandleQuitClicked);
	}

	Super::NativeDestruct();
}

void UMetamorphosisMainMenuWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	RefreshFromPreload();

	// 「新游戏」在资源没就绪时被点过 -> 就绪后（延时一点点让"正在加载"提示能看到）自动开局 ✓
	// 修"第一次点开始没反应、要再点一次"✗
	if (bPendingStart)
	{
		if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this))
		{
			if (Preload->IsReady())
			{
				PendingStartElapsed += DeltaTime;
				if (PendingStartElapsed >= PendingStartDelaySeconds)
				{
					bPendingStart = false;
					PendingStartElapsed = -1.f;
					UE_LOG(LogMetamorphosis, Log, TEXT("[DIAG] 资源已就绪 -> 自动开局（玩家此前已点过「新游戏」）"));
					HandleStartClicked();
				}
			}
			else
			{
				PendingStartElapsed = 0.f;
			}
		}
	}

	// 排版完成后打一次"实际绘制尺寸"，排查"按钮看着太小"这类问题时直接看日志即可
	// （构造期算的是 Slate 单位，这里量到的是真正排出来的大小；两者都受 1.5 倍 DPI 影响）
	if (!bLoggedButtonSizes)
	{
		struct FProbe { const TCHAR* Name; const UButton* Button; };
		const FProbe Probes[] =
		{
			{ TEXT("继续游戏"), ContinueButton },
			{ TEXT("开始游戏"), StartButton },
			{ TEXT("设置"),     SettingsButton },
			{ TEXT("退出"),     QuitButton },
		};

		FString Report;
		bool bAnyMeasured = false;
		for (const FProbe& Probe : Probes)
		{
			if (Probe.Button == nullptr)
			{
				continue;
			}
			const FVector2D Size = Probe.Button->GetCachedGeometry().GetLocalSize();
			if (Size.X > 1.f && Size.Y > 1.f)
			{
				bAnyMeasured = true;
			}
			Report += FString::Printf(TEXT("%s=%.0fx%.0f  "), Probe.Name, Size.X, Size.Y);
		}

		if (bAnyMeasured)
		{
			bLoggedButtonSizes = true;   // 只打一次
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Menu] 按钮实际绘制尺寸（Slate 单位）：%s｜ButtonArtHeight=%.0f MenuScale=%.2f"),
				*Report, ButtonArtHeight, MenuScale);
		}
	}
}

// ---------------------------------------------------------------------------
// 界面搭建
// ---------------------------------------------------------------------------

UTextBlock* UMetamorphosisMainMenuWidget::MakeLabel(UVerticalBox* Column, const TCHAR* Name, const FText& Text,
	const FSlateFontInfo& Font, const FLinearColor& Color, float BottomPadding)
{
	UTextBlock* Label = MakeMenuWidget<UTextBlock>(WidgetTree, Name);
	Label->SetText(Text);
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(Color));
	Label->SetJustification(ETextJustify::Center);
	// 文字是装饰，不能吃掉点击（放按钮里时也要让点击穿透到按钮）
	Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (UVerticalBoxSlot* LabelSlot = Column->AddChildToVerticalBox(Label))
	{
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
		// 设计图是左对齐布局：文字统一靠左
		LabelSlot->SetHorizontalAlignment(HAlign_Left);
	}

	return Label;
}

void UMetamorphosisMainMenuWidget::BuildDefaultLayout()
{
	// 根：全屏 Overlay —— 最底下是纯色背景，上面居中放内容
	UOverlay* Root = MakeMenuWidget<UOverlay>(WidgetTree, TEXT("MenuRoot"));
	WidgetTree->RootWidget = Root;

	BackgroundImage = MakeMenuWidget<UImage>(WidgetTree, TEXT("Background"));
	BackgroundImage->SetBrush(BackgroundBrush);
	BackgroundImage->SetColorAndOpacity(BackgroundColor);
	// ★ 纯装饰：必须 HitTestInvisible（否则全屏底图会把下面所有按钮的点击吃掉）
	BackgroundImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(BackgroundImage))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// 注意：内容列（Column）在下面"装饰层之后"才加进来 ——
	// UOverlay 的 z 序 = 加入顺序，内容列放最后能保证按钮永远在装饰层之上。

	// ---- 交付 2 开始界面素材：红色划痕（右侧装饰）+ 暗角 ----
	if (ScratchesBrush.HasUObject())
	{
		UImage* Scratches = MakeMenuWidget<UImage>(WidgetTree, TEXT("DecorScratches"));
		Scratches->SetBrush(ScratchesBrush);
		// ★ 装饰层叠在按钮之上，必须 HitTestInvisible，否则点击会被它吃掉
		Scratches->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* ScratchesSlot = Root->AddChildToOverlay(Scratches))
		{
			ScratchesSlot->SetHorizontalAlignment(HAlign_Right);
			ScratchesSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	if (VignetteBrush.HasUObject())
	{
		UImage* Vignette = MakeMenuWidget<UImage>(WidgetTree, TEXT("DecorVignette"));
		Vignette->SetBrush(VignetteBrush);
		// ★ 全屏暗角：这是"按钮点不动"的元凶之一，必须 HitTestInvisible
		Vignette->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* VignetteSlot = Root->AddChildToOverlay(Vignette))
		{
			VignetteSlot->SetHorizontalAlignment(HAlign_Fill);
			VignetteSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	// ---- 内容列（放在所有装饰层之后 = 永远在最上层，按钮才点得到）----
	// 按设计图：整块内容靠【左侧】竖排（logo 在上，四个按钮在下）。
	// ★ 容器必须是 SelfHitTestInvisible（自身不挡、子可点）；写成 HitTestInvisible 会把按钮一起废掉。
	USizeBox* LeftColumnBox = MakeMenuWidget<USizeBox>(WidgetTree, TEXT("LeftColumnBox"));
	LeftColumnBox->SetWidthOverride(LeftColumnWidth);
	LeftColumnBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(LeftColumnBox))
	{
		ColumnSlot->SetHorizontalAlignment(HAlign_Left);
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
		// 底部留白 = 给左下进度条让位（否则按钮放大后「退出」会压在进度条上）
		ColumnSlot->SetPadding(FMargin(LeftPadding, 0.f, 0.f, FMath::Max(ColumnBottomPadding, 0.f)));
	}

	UVerticalBox* Column = MakeMenuWidget<UVerticalBox>(WidgetTree, TEXT("MenuColumn"));
	Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	LeftColumnBox->AddChild(Column);

	// ---- 标题用美术 logo（有图就不再显示文字标题，避免和 logo 重复）----
	if (LogoTexBrush.HasUObject())
	{
		// ★ 统一缩放：logo 宽度 x MenuScale（高度仍按素材比例算，不变形）
		const float ScaledLogoWidth = FMath::Max(LogoWidth * FMath::Max(MenuScale, 0.01f), 32.f);
		USizeBox* LogoBox = MakeMenuWidget<USizeBox>(WidgetTree, TEXT("LogoBox"));
		LogoBox->SetWidthOverride(ScaledLogoWidth);
		LogoBox->SetHeightOverride(ScaledLogoWidth * (LogoTexBrush.ImageSize.Y / FMath::Max(LogoTexBrush.ImageSize.X, 1.f)));
		LogoBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UImage* Logo = MakeMenuWidget<UImage>(WidgetTree, TEXT("LogoImage"));
		Logo->SetBrush(LogoTexBrush);
		Logo->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		LogoBox->AddChild(Logo);
		if (UVerticalBoxSlot* LogoSlot = Column->AddChildToVerticalBox(LogoBox))
		{
			LogoSlot->SetHorizontalAlignment(HAlign_Left);
			LogoSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		}

		// logo 下面一条金色聚焦线（素材里的 MENU_FocusLine）
		if (FocusLineBrush.HasUObject())
		{
			USizeBox* LineBox = MakeMenuWidget<USizeBox>(WidgetTree, TEXT("FocusLineBox"));
			LineBox->SetWidthOverride(ScaledLogoWidth * 0.58f);
			LineBox->SetHeightOverride(8.f);
			LineBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UImage* Line = MakeMenuWidget<UImage>(WidgetTree, TEXT("FocusLine"));
			Line->SetBrush(FocusLineBrush);
			Line->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			LineBox->AddChild(Line);
			if (UVerticalBoxSlot* LineSlot = Column->AddChildToVerticalBox(LineBox))
			{
				LineSlot->SetHorizontalAlignment(HAlign_Left);
				LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			}
		}
	}

	TitleLabel = MakeLabel(Column, TEXT("TitleLabel"), TitleText, TitleFont, TitleColor, 6.f);
	MakeLabel(Column, TEXT("SubtitleLabel"), SubtitleText, BodyFont, BodyColor, 48.f);

	// 进度条已移到【左下角】（见函数末尾）—— 按设计图它不在内容列里。

	// ---- 四个按钮（设计图：继续游戏 / 开始游戏 / 设置 / 退出，竖排、左对齐）----
	// 统一用这个 lambda 建：文字图拿得到就显示图、拿不到退回文字；
	// 按钮本体可命中，文字/图片一律不可命中（否则点击被吃掉 —— 本项目踩过 3 次）。
	auto BuildMenuButton = [this, Column](const TCHAR* Name, const FText& InLabel, const FSlateBrush& InBrush,
		float ArtWidth, bool bEnabled, TObjectPtr<UButton>& OutButton, TObjectPtr<UTextBlock>& OutLabel,
		TObjectPtr<UImage>& OutArt)
	{
		const FString NameStr(Name);
		// ★ 2026-10-19：统一缩放系数（MenuScale=1 时和以前一样）。所有尺寸都乘它，想整体大小改一个数即可。
		const float Scale = FMath::Max(MenuScale, 0.01f);
		// ★ 绘制高度由 ButtonArtHeight 决定（以前只给宽度 -> 图按素材原生 42 高画，怎么调都小）。
		const float ArtH = ButtonArtHeight * Scale;

		USizeBox* Row = MakeMenuWidget<USizeBox>(WidgetTree, FName(*(NameStr + TEXT("Row"))));
		Row->SetMinDesiredWidth(ButtonMinWidth * Scale);
		Row->SetHeightOverride(FMath::Max(ButtonRowHeight * Scale, ArtH));
		Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Row))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Left);
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ButtonRowSpacing * Scale));
		}

		UButton* Button = MakeMenuWidget<UButton>(WidgetTree, Name);
		Button->SetIsEnabled(bEnabled);
		if (bApplyButtonStyle)
		{
			Button->SetStyle(StartButtonStyle);
		}
		Row->AddChild(Button);
		OutButton = Button;

		UTextBlock* Label = MakeMenuWidget<UTextBlock>(WidgetTree, FName(*(NameStr + TEXT("Label"))));
		Label->SetText(InLabel);
		Label->SetFont(BodyFont);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.90f, 0.84f, 1.f)));
		Label->SetJustification(ETextJustify::Left);
		Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		OutLabel = Label;

		if (InBrush.GetResourceObject() != nullptr)
		{
			// 有美术文字图：文字收起，改用图（图不可命中，点击穿透到按钮）
			Label->SetVisibility(ESlateVisibility::Collapsed);
			// ★ 高度驱动 + 按素材长宽比算宽度：图永远不变形，且改 ButtonArtHeight 能真正放大。
			//   素材 ImageSize 无效时（极少见）回退到传入的 ArtWidth。
			USizeBox* ArtBox = MakeMenuWidget<USizeBox>(WidgetTree, FName(*(NameStr + TEXT("ArtBox"))));
			const FVector2D ImgSize = InBrush.ImageSize;
			const float ArtW = (ImgSize.X > 1.f && ImgSize.Y > 1.f)
				? (ArtH * (ImgSize.X / ImgSize.Y))
				: (ArtWidth * Scale);
			ArtBox->SetWidthOverride(ArtW);
			ArtBox->SetHeightOverride(ArtH);
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Menu] 按钮绘制尺寸：%s 素材 %.0fx%.0f -> %.0fx%.0f（高度 %.0f x 缩放 %.2f）"),
				Name, ImgSize.X, ImgSize.Y, ArtW, ArtH, ButtonArtHeight, Scale);
			ArtBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UImage* Art = MakeMenuWidget<UImage>(WidgetTree, FName(*(NameStr + TEXT("Art"))));
			Art->SetBrush(InBrush);
			Art->SetVisibility(ESlateVisibility::HitTestInvisible);
			ArtBox->AddChild(Art);
			UOverlay* Content = MakeMenuWidget<UOverlay>(WidgetTree, FName(*(NameStr + TEXT("Content"))));
			Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Content->AddChild(Label);
			if (UOverlaySlot* ArtSlot = Content->AddChildToOverlay(ArtBox))
			{
				ArtSlot->SetHorizontalAlignment(HAlign_Left);
				ArtSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->AddChild(Content);
			OutArt = Art;
		}
		else
		{
			Button->AddChild(Label);
		}
	};

	// 继续游戏：本作没有存档系统 -> 保留按钮但置灰（点击会提示"暂无存档"）
	BuildMenuButton(TEXT("ContinueButton"), ContinueButtonText, ContinueArtBrush, ContinueArtWidth,
		false, ContinueButton, ContinueLabel, ContinueArt);
	// 开始游戏：**永远可点**（用户要求）。资源没加载完时点击由 HandleStartClicked 提示"加载中"，不放行。
	// 注意：这里以前传的是 false（创建即禁用），而 RefreshFromPreload 在进度没变化时会提前 return ->
	// 从主界面回来/加载刚好 0% 时「开始游戏」会一直是灰的且永远不解禁 ✗ -> 改成创建即可用。
	BuildMenuButton(TEXT("StartButton"), StartButtonText, StartArtBrush, StartArtWidth,
		true, StartButton, StartLabel, StartArt);
	// 「开始游戏」-> 用户要求显示「新游戏」。现在直接用美术文字图 Txt3_NewGame（同规格宋体、同色），
	// 不再用"系统字体文字块"顶替 —— 之前那样做字体明显和另外三个按钮不一致（用户反馈"文字风格不符合"）。
	// 有美术图时 BuildMenuButton 已经把 Label 收起，这里什么都不用做（保留这段说明即可）。
	UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「开始游戏」按钮文字 = 新游戏（美术文字图 Txt3_NewGame）"));
	// 设置：任何时候都能点
	BuildMenuButton(TEXT("SettingsButton"), SettingsButtonText, SettingsArtBrush, SettingsArtWidth,
		true, SettingsButton, SettingsLabel, SettingsArt);
	// 退出
	BuildMenuButton(TEXT("QuitButton"), QuitButtonText, QuitArtBrush, QuitArtWidth,
		true, QuitButton, QuitLabel, QuitArt);

	// ---- 进度条：左下角（用交付包同一套素材：HUD_BarTrack 槽 / HUD_BarFill 填充 / HUD_BarFrame 金框）----
	// 之前的做法是纯色细条（深墨绿 + 暗红），太"现代 UI"、和旧纸/木地板的背景不搭（用户反馈）。
	// 现在：槽与填充都用美术图（UProgressBar 自己按 Percent 裁剪填充），再叠一层金框。
	{
		USizeBox* ProgressBox = MakeMenuWidget<USizeBox>(WidgetTree, TEXT("ProgressBox"));
		ProgressBox->SetWidthOverride(ProgressBarWidth);
		ProgressBox->SetHeightOverride(ProgressBarHeight);
		ProgressBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* PSlot = Root->AddChildToOverlay(ProgressBox))
		{
			PSlot->SetHorizontalAlignment(HAlign_Left);
			PSlot->SetVerticalAlignment(VAlign_Bottom);
			PSlot->SetPadding(FMargin(LeftPadding, 0.f, 0.f, ProgressBottomPadding));
		}

		UOverlay* BarOverlay = MakeMenuWidget<UOverlay>(WidgetTree, TEXT("ProgressOverlay"));
		BarOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		ProgressBox->AddChild(BarOverlay);

		// ---- 槽底 / 填充 / 金框：三张美术图叠出来 ----
		// ★ 2026-10-19：这里**不再用 UProgressBar** ✗。
		//   原因：本工程里 UProgressBar 的 SProgressBar Mask 裁剪实测异常 —— 文字显示 61%/99%，
		//   但条里只画得出左端约 4 像素的一小条（每次运行都复现 ✗），换了 ImageSize/BarFillStyle 都不解决。
		//   改用"一个宽度 = 百分比的裁剪容器 + 整条宽的填充图"：UImage 不会把贴图缩到控件大小（这是老坑 ✗），
		//   正好被这里利用 —— 图按整条 420x32 画、容器只开 percent 宽，配合 ClipToBounds 就是精确的"露出百分比" ✓
		auto LoadBarBrush = [this](FSlateBrush& OutBrush, const TCHAR* AssetName) -> bool
		{
			const FString Path = FString::Printf(TEXT("/Game/UI/Delivery2/Start_Design/%s.%s"), AssetName, AssetName);
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path))
			{
				OutBrush.SetResourceObject(Texture);
				OutBrush.ImageSize = FVector2D(ProgressBarWidth, ProgressBarHeight);
				OutBrush.DrawAs = ESlateBrushDrawType::Image;
				OutBrush.ImageType = ESlateBrushImageType::FullColor;
				return true;
			}
			return false;
		};

		// 1) 槽底（整条）
		FSlateBrush TrackBrush;
		const bool bTrack = LoadBarBrush(TrackBrush, TEXT("BarTrack"));
		if (!bTrack)
		{
			TrackBrush = FSlateColorBrush(FLinearColor(0.045f, 0.07f, 0.05f, 0.92f));
		}
		UImage* TrackImage = MakeMenuWidget<UImage>(WidgetTree, TEXT("ProgressTrack"));
		TrackImage->SetBrush(TrackBrush);
		TrackImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		BarOverlay->AddChildToOverlay(TrackImage);

		// 2) 填充：裁剪容器（宽度 = 百分比）+ 整条宽的填充图
		LoadingBarFillHolder = MakeMenuWidget<USizeBox>(WidgetTree, TEXT("ProgressFillHolder"));
		LoadingBarFillHolder->SetWidthOverride(0.f);
		LoadingBarFillHolder->SetHeightOverride(ProgressBarHeight);
		LoadingBarFillHolder->SetClipping(EWidgetClipping::ClipToBounds);
		LoadingBarFillHolder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* FillSlot = BarOverlay->AddChildToOverlay(LoadingBarFillHolder))
		{
			FillSlot->SetHorizontalAlignment(HAlign_Left);
			FillSlot->SetVerticalAlignment(VAlign_Center);
		}

		FSlateBrush FillBrush;
		const bool bFill = LoadBarBrush(FillBrush, TEXT("BarFill"));
		if (!bFill)
		{
			FillBrush = FSlateColorBrush(FLinearColor(0.55f, 0.11f, 0.09f, 1.f));
			FillBrush.ImageSize = FVector2D(ProgressBarWidth, ProgressBarHeight);
		}
		LoadingBarFillImage = MakeMenuWidget<UImage>(WidgetTree, TEXT("ProgressFill"));
		LoadingBarFillImage->SetBrush(FillBrush);
		LoadingBarFillImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		LoadingBarFillHolder->AddChild(LoadingBarFillImage);

		UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 进度条素材：槽=%d 填充=%d（裁剪容器 + 整条宽填充图；不用 UProgressBar）"),
			bTrack ? 1 : 0, bFill ? 1 : 0);

		// 3) 金框叠在最上层
		{
			FSlateBrush FrameBrush;
			const TCHAR* FramePath = TEXT("/Game/UI/Delivery2/Start_Design/BarFrame.BarFrame");
			if (UTexture2D* FrameTex = LoadObject<UTexture2D>(nullptr, FramePath))
			{
				FrameBrush.SetResourceObject(FrameTex);
				FrameBrush.ImageSize = FVector2D(ProgressBarWidth, ProgressBarHeight);
				FrameBrush.DrawAs = ESlateBrushDrawType::Image;
				FrameBrush.ImageType = ESlateBrushImageType::FullColor;
				UImage* FrameImage = MakeMenuWidget<UImage>(WidgetTree, TEXT("ProgressFrame"));
				FrameImage->SetBrush(FrameBrush);
				FrameImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				BarOverlay->AddChildToOverlay(FrameImage);
			}
		}

		ProgressLabel = MakeMenuWidget<UTextBlock>(WidgetTree, TEXT("ProgressLabel"));
		ProgressLabel->SetText(LOCTEXT("Loading", "正在加载资源…"));
		ProgressLabel->SetFont(BodyFont);
		ProgressLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.80f, 0.62f, 1.f)));
		ProgressLabel->SetJustification(ETextJustify::Left);
		ProgressLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* PLSlot = Root->AddChildToOverlay(ProgressLabel))
		{
			PLSlot->SetHorizontalAlignment(HAlign_Left);
			PLSlot->SetVerticalAlignment(VAlign_Bottom);
			// ★ 2026-10-19：原来这里是 ProgressBottomPadding - 28（= -12），底部内边距变负数 ->
		//   "正在加载资源…"被顶到屏幕下沿之外、只剩上半截（截图实测）。改成压在进度条**上方** ✓
		PLSlot->SetPadding(FMargin(LeftPadding, 0.f, 0.f, ProgressBottomPadding + ProgressBarHeight + 6.f));
		}
	}
}

void UMetamorphosisMainMenuWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (BackgroundImage == nullptr) { BackgroundImage = WidgetTree->FindWidget<UImage>(TEXT("Background")); }
	if (TitleLabel == nullptr)      { TitleLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("TitleLabel")); }
	if (ProgressLabel == nullptr)   { ProgressLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("ProgressLabel")); }
	if (LoadingBar == nullptr)      { LoadingBar = WidgetTree->FindWidget<UProgressBar>(TEXT("LoadingBar")); }
	if (StartButton == nullptr)     { StartButton = WidgetTree->FindWidget<UButton>(TEXT("StartButton")); }
	if (StartLabel == nullptr)      { StartLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("StartLabel")); }
	if (SettingsButton == nullptr)  { SettingsButton = WidgetTree->FindWidget<UButton>(TEXT("SettingsButton")); }
	if (SettingsLabel == nullptr)   { SettingsLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("SettingsLabel")); }
	if (ContinueButton == nullptr)  { ContinueButton = WidgetTree->FindWidget<UButton>(TEXT("ContinueButton")); }
	if (ContinueLabel == nullptr)   { ContinueLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("ContinueButtonLabel")); }
	if (QuitButton == nullptr)      { QuitButton = WidgetTree->FindWidget<UButton>(TEXT("QuitButton")); }
	if (QuitLabel == nullptr)       { QuitLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("QuitButtonLabel")); }
}

void UMetamorphosisMainMenuWidget::ApplyStaticStyle()
{
	if (TitleLabel != nullptr)
	{
		TitleLabel->SetText(TitleText);
		TitleLabel->SetFont(TitleFont);
		TitleLabel->SetColorAndOpacity(FSlateColor(TitleColor));
	}

	if (ProgressLabel != nullptr)
	{
		ProgressLabel->SetFont(BodyFont);
		ProgressLabel->SetColorAndOpacity(FSlateColor(BodyColor));
	}

	if (StartLabel != nullptr)
	{
		StartLabel->SetText(StartButtonText);
		// ★「新游戏」是用**文字块顶替美术图**（美术图里烘死的是"开始游戏"✗ 不能直接用），
		//   所以字号必须跟着按钮高度走 —— BodyFont 只有 22，在 96 高的按钮里会显得像小字 ✗（用户反馈过）
		FSlateFontInfo BigFont = BodyFont;
		BigFont.Size = FMath::Clamp(ButtonArtHeight * FMath::Max(MenuScale, 0.01f) * 0.56f, 16.f, 120.f);
		BigFont.OutlineSettings.OutlineSize = 2;
		BigFont.OutlineSettings.OutlineColor = FLinearColor(0.05f, 0.04f, 0.03f, 0.85f);
		StartLabel->SetFont(BigFont);
		StartLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.93f, 0.82f, 1.f)));
		UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「新游戏」文字字号 = %.0f（跟随 ButtonArtHeight %.0f）"), BigFont.Size, ButtonArtHeight);
	}
	if (ContinueLabel != nullptr)
	{
		ContinueLabel->SetText(ContinueButtonText);
		ContinueLabel->SetFont(BodyFont);
	}
	if (SettingsLabel != nullptr)
	{
		SettingsLabel->SetText(SettingsButtonText);
		SettingsLabel->SetFont(BodyFont);
	}
	if (QuitLabel != nullptr)
	{
		QuitLabel->SetText(QuitButtonText);
		QuitLabel->SetFont(BodyFont);
	}
	// 继续游戏：没有存档系统 -> 永远置灰（设计图里保留这个位置）
	if (ContinueButton != nullptr)
	{
		ContinueButton->SetIsEnabled(false);
	}

	if (BackgroundImage != nullptr)
	{
		BackgroundImage->SetColorAndOpacity(BackgroundColor);
	}

	if (StartButton != nullptr && bApplyButtonStyle)
	{
		StartButton->SetStyle(StartButtonStyle);
	}

	// 菜单里要把鼠标放出来（PC 上才点得到按钮）
	if (APlayerController* Controller = GetOwningPlayer())
	{
		if (bShowCursorInMenu)
		{
			Controller->bShowMouseCursor = true;
		}
	}
}

// ---------------------------------------------------------------------------
// 进度刷新
// ---------------------------------------------------------------------------

void UMetamorphosisMainMenuWidget::RefreshFromPreload()
{
	UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this);
	if (Preload == nullptr)
	{
		if (StartButton != nullptr) { StartButton->SetIsEnabled(true); }
		return;
	}

	// ---- 1) 取真实进度（每帧都取，不再"没变化就 early return"）----
	const float Progress = FMath::Clamp(Preload->GetProgress(), 0.f, 1.f);
	const bool bReady = Preload->IsReady();

	if (!bLoggedProgressSource)
	{
		bLoggedProgressSource = true;
		int32 Loaded = 0;
		int32 Total = 0;
		Preload->GetCounts(Loaded, Total);
		UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 加载进度源：%d/%d 项（GetProgress 为逐句柄真实进度均值）"),
			Loaded, Total);
	}

	// ---- 2) 时间地板 + 缓动：保证"无论加载多快，进度条都 visibly 在涨" ----
	const float DeltaSeconds = GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.016f;
	ProgressElapsed += FMath::Clamp(DeltaSeconds, 0.f, 0.1f);

	const float RampFloor = ProgressMinAnimateSeconds > KINDA_SMALL_NUMBER
		? FMath::Clamp(ProgressElapsed / ProgressMinAnimateSeconds, 0.f, 1.f)
		: 1.f;

	// 目标值 = max(真实进度, 时间地板)；就绪之后直接冲 1
	float Target = bReady ? 1.f : FMath::Max(Progress, RampFloor * 0.98f);
	Target = FMath::Clamp(Target, 0.f, 1.f);

	ProgressDisplay = FMath::FInterpTo(ProgressDisplay, Target, DeltaSeconds, FMath::Max(0.01f, ProgressInterpSpeed));
	if (bReady && ProgressDisplay >= 0.995f)
	{
		ProgressDisplay = 1.f;
	}

	// 每帧都设（UProgressBar::SetPercent 只是给 Slate 属性赋值，开销可忽略；之前"变了才设"正是看不到变化的原因之一 ✗）
	if (LoadingBarFillHolder != nullptr)
	{
		// 裁剪容器宽度 = 百分比 × 整条宽（取整，避免亚像素抖动）
		const float W = FMath::Max(0.f, ProgressBarWidth * FMath::Clamp(ProgressDisplay, 0.f, 1.f));
		LoadingBarFillHolder->SetWidthOverride(FMath::RoundToFloat(W));
	}
	else if (LoadingBar != nullptr)
	{
		// 兼容老路径（设计器树里若还有 UProgressBar）
		LoadingBar->SetPercent(ProgressDisplay);
	}

	// ---- 3) 文字：只在整数百分比变化时重建（避免每帧重建 Slate 文本）----
	const int32 PercentInt = FMath::Clamp(FMath::RoundToInt(ProgressDisplay * 100.f), 0, 100);
	if (ProgressLabel != nullptr)
	{
		if (bReady && ProgressDisplay >= 1.f)
		{
			// 设计图上进度条那块没有"已就绪"文字 -> 加载完成后直接收起（进度条保持满格）
			if (LastPercentShown != 101)
			{
				LastPercentShown = 101;
				ProgressLabel->SetText(FText::GetEmpty());
				ProgressLabel->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else if (PercentInt != LastPercentShown)
		{
			LastPercentShown = PercentInt;

			int32 Loaded = 0;
			int32 Total = 0;
			Preload->GetCounts(Loaded, Total);

			const FText Text = FText::Format(
				LOCTEXT("LoadingFormat", "正在加载资源… {0}%（已完成 {1}/{2} 项）"),
				FText::AsNumber(PercentInt),
				FText::AsNumber(Loaded),
				FText::AsNumber(Total));
			ProgressLabel->SetText(Text);
			ProgressLabel->SetColorAndOpacity(FSlateColor(BodyColor));
			ProgressLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			// 每 10% 打一条，便于无头验证"进度确实在涨"
			if (PercentInt % 10 == 0 || PercentInt >= 99)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 加载进度：%d%%（真实 %.0f%%，目标 %.0f%%，已完成 %d/%d 项）"),
					PercentInt, Progress * 100.f, Target * 100.f, Loaded, Total);
			}
		}
	}

	// ---- 4) 状态变化时才动按钮（这部分沿用"变了才处理"的旧逻辑）----
	if (bReady != bLastReady || !FMath::IsNearlyEqual(Progress, LastProgress))
	{
		LastProgress = Progress;
		bLastReady = bReady;
	}

	if (StartButton != nullptr)
	{
		// 「开始游戏」永远可用（用户要求：有存档时也不该是灰的；只有「继续游戏」在无存档时才置灰）。
		// 之前这里是 SetIsEnabled(bReady) -> 资源未加载完/从主界面回来时按钮会一直灰着 ✗。
		// 资源没加载完点击时由 HandleStartClicked 给出"加载中"提示，不会放行。
		StartButton->SetIsEnabled(true);
	}
}

void UMetamorphosisMainMenuWidget::HandleStartClicked()
{
	// 「开始游戏」= 新游戏：先删掉旧存档（否则下次「继续游戏」会把旧进度读回来）
	if (UMetamorphosisSaveSubsystem* Save = GetSaveSubsystem())
	{
		Save->StartNewGame();
	}

	if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this))
	{
		if (!Preload->IsReady())
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[DIAG] 点击「新游戏」：资源未就绪 -> 等待就绪后自动开局（不用再点一次）"));
			// 记下"玩家已经点过开始" ✓ 就绪后自动进入 ✗ 否则第一次点只在等加载（看起来像按钮坏了 ✗）
			bPendingStart = true;
			PendingStartElapsed = 0.f;
			// 给玩家一点反馈（以前是静默 return，看起来像"按钮坏了"✗）
			if (ProgressLabel != nullptr)
			{
				ProgressLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				ProgressLabel->SetText(LOCTEXT("StartNotReady", "资源还在加载，加载完成后会自动开始…"));
				ProgressLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.72f, 0.35f, 1.f)));
			}
			return;
		}

		// 新一局：清掉上一局残留的剧情状态（否则从主界面重新进来时剧情会卡在上一句、点不动）
		if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
		{
			Story->ResetForNewRun();
		}

		// 解除"菜单背后的暂停"+ 输入回 GameOnly + 收起开始界面（成对恢复）
		if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
		{
			Controller->ExitFrontendMode(TEXT("开始游戏"));
		}

		Preload->EnterGame();
	}
}

void UMetamorphosisMainMenuWidget::HandleSettingsClicked()
{
	// 交给控制器开设置界面（UI 的挂载与切换统一在控制器里管）
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->ShowSettings();
	}
}

void UMetamorphosisMainMenuWidget::HandleContinueClicked()
{
	UMetamorphosisSaveSubsystem* Save = GetSaveSubsystem();
	if (Save == nullptr || !Save->HasSave())
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「继续游戏」：暂无存档"));
		if (ProgressLabel != nullptr)
		{
			ProgressLabel->SetText(LOCTEXT("NoSave", "暂无存档 —— 请选择「开始游戏」"));
			ProgressLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.72f, 0.35f, 1.f)));
		}
		return;
	}

	// 有存档：读档 -> 打开游戏关卡（玩家位置由 PC BeginPlay 里的 ApplyPendingPlayerTransform 应用）
	UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「继续游戏」：读取存档 %s"), *Save->Summary());
	if (ProgressLabel != nullptr)
	{
		ProgressLabel->SetText(LOCTEXT("LoadingSave", "正在读取存档…"));
		ProgressLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.72f, 0.35f, 1.f)));
	}

	// 读档前先停掉上一局残留的剧情（门/临时文本一起清），读档本身会 StartStory 到存下来的节点
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->StopStory();
	}

	// 解除"菜单背后的暂停"+ 输入回 GameOnly + 收起开始界面（成对恢复）
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->ExitFrontendMode(TEXT("继续游戏"));
	}

	Save->RequestContinue(this);
}

UMetamorphosisSaveSubsystem* UMetamorphosisMainMenuWidget::GetSaveSubsystem() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UMetamorphosisSaveSubsystem>();
	}
	return nullptr;
}

void UMetamorphosisMainMenuWidget::RefreshContinueAvailability()
{
	UMetamorphosisSaveSubsystem* Save = GetSaveSubsystem();
	const bool bHas = Save != nullptr && Save->HasSave();
	if (ContinueButton != nullptr)
	{
		// 有存档：正常可点；无存档：仍然可点（点了要能提示"暂无存档"），但半透明表示不可用
		ContinueButton->SetRenderOpacity(bHas ? 1.0f : 0.62f);
		ContinueButton->SetIsEnabled(true);
	}
	UE_LOG(LogMetamorphosis, Log, TEXT("[DIAG] 主界面：HasSave=%d 存档=%s Continue.opacity=%.2f Start.enabled=%d"),
		bHas ? 1 : 0, Save != nullptr ? *Save->GetSavePath() : TEXT("(无SaveSubsystem)"),
		bHas ? 1.0f : 0.62f, (StartButton != nullptr && StartButton->GetIsEnabled()) ? 1 : 0);
	if (ContinueArt != nullptr)
	{
		ContinueArt->SetRenderOpacity(bHas ? 1.0f : 0.62f);
	}
	if (ContinueLabel != nullptr)
	{
		ContinueLabel->SetRenderOpacity(bHas ? 1.0f : 0.62f);
	}
	UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「继续游戏」状态：%s"), bHas ? TEXT("有存档（可用）") : TEXT("无存档（置灰）"));

	// 「开始游戏」永远可用（用户要求：灰的只能是"无存档时的继续游戏"）
	if (StartButton != nullptr)
	{
		StartButton->SetIsEnabled(true);
		StartButton->SetRenderOpacity(1.0f);
	}
	if (StartArt != nullptr) { StartArt->SetRenderOpacity(1.0f); }
	if (StartLabel != nullptr) { StartLabel->SetRenderOpacity(1.0f); }
	UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「开始游戏」状态：%s"), TEXT("始终可用"));
}

void UMetamorphosisMainMenuWidget::HandleQuitClicked()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[Menu] 「退出」：结束游戏"));
	if (bQuitEnabled)
	{
		if (APlayerController* Controller = GetOwningPlayer())
		{
			UKismetSystemLibrary::QuitGame(this, Controller, EQuitPreference::Quit, false);
		}
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Menu] 退出被 bQuitEnabled=false 关掉了"));
	}
}

#undef LOCTEXT_NAMESPACE

// ---------------------------------------------------------------------------
// 调试：模拟点按钮 / 打印控件树（排查"按钮点不动"）
// ---------------------------------------------------------------------------

namespace
{
	const TCHAR* MenuVisToText(ESlateVisibility V)
	{
		switch (V)
		{
		case ESlateVisibility::Visible:              return TEXT("Visible");
		case ESlateVisibility::Collapsed:            return TEXT("Collapsed");
		case ESlateVisibility::Hidden:               return TEXT("Hidden");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		case ESlateVisibility::HitTestInvisible:     return TEXT("HitTestInvisible");
		default:                                     return TEXT("?");
		}
	}

	void DumpMenuTree(const UWidget* W, int32 Depth, int32& OutCount)
	{
		if (W == nullptr || Depth > 12)
		{
			return;
		}
		++OutCount;
		FString Indent;
		for (int32 i = 0; i < Depth; ++i)
		{
			Indent += TEXT("  ");
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[MenuTree] %s%s（%s）Visibility=%s"),
			*Indent, *W->GetName(), *W->GetClass()->GetName(),
			MenuVisToText(W->GetVisibility()));

		if (const UPanelWidget* Panel = Cast<UPanelWidget>(W))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				DumpMenuTree(Panel->GetChildAt(i), Depth + 1, OutCount);
			}
		}
	}
}

bool UMetamorphosisMainMenuWidget::SimulatePress(const FString& Which)
{
	const FString Key = Which.TrimStartAndEnd().ToLower();
	if (Key == TEXT("start") || Key == TEXT("开始") || Key == TEXT("开始游戏"))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟点击「开始游戏」（StartButton 启用=%d 可见=%s）"),
			(StartButton != nullptr && StartButton->GetIsEnabled()) ? 1 : 0,
			StartButton != nullptr ? MenuVisToText(StartButton->GetVisibility()) : TEXT("null"));
		HandleStartClicked();
		return StartButton != nullptr;
	}
	if (Key == TEXT("settings") || Key == TEXT("设置"))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟点击「设置」（SettingsButton 可见=%s）"),
			SettingsButton != nullptr ? MenuVisToText(SettingsButton->GetVisibility()) : TEXT("null"));
		HandleSettingsClicked();
		return SettingsButton != nullptr;
	}

	if (Key == TEXT("continue") || Key == TEXT("继续") || Key == TEXT("继续游戏"))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟点击「继续游戏」（ContinueButton 启用=%d）"),
			(ContinueButton != nullptr && ContinueButton->GetIsEnabled()) ? 1 : 0);
		HandleContinueClicked();
		return ContinueButton != nullptr;
	}
	if (Key == TEXT("quit") || Key == TEXT("退出"))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟点击「退出」（QuitButton 可见=%s）"),
			QuitButton != nullptr ? MenuVisToText(QuitButton->GetVisibility()) : TEXT("null"));
		// 无头/编辑器里不要真的退出进程（默认只打日志）；要真退出改 ini MenuQuitReally=true
		if (bQuitReally)
		{
			HandleQuitClicked();
		}
		return QuitButton != nullptr;
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Menu.Press <Start|Continue|Settings|Quit>"));
	return false;
}

void UMetamorphosisMainMenuWidget::LogControlTree() const
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[MenuTree] 控件树为空"));
		return;
	}
	int32 Count = 0;
	UE_LOG(LogMetamorphosis, Log, TEXT("[MenuTree] ---- 开始界面控件树（根=%s）----"),
		*WidgetTree->RootWidget->GetName());
	DumpMenuTree(WidgetTree->RootWidget, 0, Count);
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[MenuTree] ---- 共 %d 个控件；规则：装饰层应 HitTestInvisible/SelfHitTestInvisible，按钮应 Visible ----"),
		Count);
}




