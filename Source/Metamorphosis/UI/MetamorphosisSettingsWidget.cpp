// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisSettingsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ScaleBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Game/MetamorphosisSettingsSubsystem.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisSettings"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakeSettingsWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	FSlateBrush MakeSettingsBrush(const FLinearColor& Color)
	{
		return FSlateColorBrush(Color);
	}

	FButtonStyle MakeSettingsButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.10f, 0.11f, 0.14f, 1.f));
		Style.Hovered = FSlateColorBrush(FLinearColor(0.18f, 0.20f, 0.24f, 1.f));
		Style.Pressed = FSlateColorBrush(FLinearColor(0.90f, 0.72f, 0.25f, 1.f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.72f, 0.72f, 0.72f, 1.f));
		Style.NormalPadding = FMargin(18.f, 10.f);
		Style.PressedPadding = FMargin(19.f, 11.f);
		return Style;
	}

	/** 给滑条一个明显的样式（默认样式在白底上几乎看不见）
	 *
	 *  注意：Slate 是用**画刷的 ImageSize** 来给"把手"和"滑条"定尺寸的（见 SSlider::GetThumbImage/ImageSize），
	 *  所以**每个状态**（Normal / Hovered / Disabled）都必须设 ImageSize ——
	 *  只设 Normal 的话，鼠标一悬停/选中把手就变成 0 尺寸、直接看不见了（踩过）。 */
	FSliderStyle MakeVisibleSliderStyle()
	{
		const FVector2D BarSize(10.f, 10.f);
		const FVector2D ThumbSize(18.f, 26.f);

		auto MakeBrush = [](const FLinearColor& Color, const FVector2D& Size)
		{
			FSlateBrush Brush = FSlateColorBrush(Color);
			Brush.ImageSize = Size;
			return Brush;
		};

		FSliderStyle Style;
		// 2026-10-20：配色对齐美术背景（旧纸/暗绿/米金）—— 原来浅灰条 + 深蓝黑把手，压在插画上很"外来" ✗。
		// 滑条：平时暗褐（读作"旧木头上的凹槽"）、悬停略亮；把手：米金（和面板金框、金字同一色系）。
		Style.SetNormalBarImage(MakeBrush(FLinearColor(0.155f, 0.140f, 0.105f, 0.92f), BarSize));
		Style.SetHoveredBarImage(MakeBrush(FLinearColor(0.215f, 0.195f, 0.140f, 0.95f), BarSize));
		Style.SetDisabledBarImage(MakeBrush(FLinearColor(0.115f, 0.105f, 0.085f, 0.85f), BarSize));

		// 把手（那根"黑色竖杠"）：三个状态都是深色，选中/拖动时也照样看得见
		Style.SetNormalThumbImage(MakeBrush(FLinearColor(0.86f, 0.72f, 0.42f, 1.f), ThumbSize));
		Style.SetHoveredThumbImage(MakeBrush(FLinearColor(0.98f, 0.85f, 0.55f, 1.f), ThumbSize));
		Style.SetDisabledThumbImage(MakeBrush(FLinearColor(0.52f, 0.46f, 0.32f, 1.f), ThumbSize));

		Style.BarThickness = 8.f;
		return Style;
	}

	/** 勾选框样式：每个状态都要 ImageSize（否则某些状态下会变成 0 尺寸）。
	 *  另外**勾选不再整块变绿** —— 方框底色保持不变，绿色 √ 由独立的文本块叠在上面
	 *  （见 FixedJoystickCheckMark），这样"选中"看起来就是一个绿色勾。 */
	FCheckBoxStyle MakeVisibleCheckBoxStyle()
	{
		const FVector2D BoxSize(26.f, 26.f);

		auto MakeBrush = [](const FLinearColor& Color, const FVector2D& Size)
		{
			FSlateBrush Brush = FSlateColorBrush(Color);
			Brush.ImageSize = Size;
			return Brush;
		};

		const FLinearColor BoxColor(0.97f, 0.97f, 0.98f, 1.f);
		const FLinearColor BoxHoverColor(0.90f, 0.90f, 0.93f, 1.f);

		FCheckBoxStyle Style;
		Style.SetUncheckedImage(MakeBrush(BoxColor, BoxSize));
		Style.SetUncheckedHoveredImage(MakeBrush(BoxHoverColor, BoxSize));
		Style.SetCheckedImage(MakeBrush(BoxColor, BoxSize));
		Style.SetCheckedHoveredImage(MakeBrush(BoxHoverColor, BoxSize));
		Style.SetUndeterminedImage(MakeBrush(FLinearColor(0.85f, 0.85f, 0.85f, 1.f), BoxSize));
		Style.SetUndeterminedHoveredImage(MakeBrush(BoxHoverColor, BoxSize));
		Style.SetPadding(FMargin(0.f));
		Style.CheckBoxType = ESlateCheckBoxType::CheckBox;
		return Style;
	}
}


UMetamorphosisSettingsWidget::UMetamorphosisSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BackgroundBrush = FSlateColorBrush(FLinearColor::White);
	TitleText = LOCTEXT("SettingsTitle", "设置");
	BackButtonText = LOCTEXT("BackText", "返回");
	CheckMarkText = LOCTEXT("CheckMark", "√");

	// ---- 交付 2「设置页面」素材：暗绿面板 + 哥特金属外框 + 投影 + 金色标题 ----
	// 交付里没有整场景背景图（那是设置页自己的 001_无UI场景），所以底色保持半透明黑，
	// 面板三层叠起来就够"像个界面"了；滑块/勾选仍用代码画（素材里的滑块是给单个控件用的，
	// 这里行高与美术排版不同，硬套反而错位）。
	BackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);
	{
		auto LoadSettingsBrush = [](FSlateBrush& OutBrush, const TCHAR* AssetName)
		{
			const FString Path = FString::Printf(TEXT("/Game/UI/Delivery2/Settings/%s.%s"), AssetName, AssetName);
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
			if (Texture != nullptr)
			{
				OutBrush.SetResourceObject(Texture);
				OutBrush.ImageSize = FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
				OutBrush.DrawAs = ESlateBrushDrawType::Image;
				OutBrush.ImageType = ESlateBrushImageType::FullColor;
			}
		};

		LoadSettingsBrush(PanelShadowBrush, TEXT("T_PanelShadow"));
		LoadSettingsBrush(PanelBodyBrush, TEXT("T_PanelBody"));
		LoadSettingsBrush(PanelFrameBrush, TEXT("T_PanelFrame"));
		LoadSettingsBrush(TitleArtBrush, TEXT("T_Title"));

		// 有金色标题图就不再画文字标题（否则重复）
		if (TitleArtBrush.HasUObject())
		{
			TitleText = FText::GetEmpty();
		}
	}

	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		TitleFont = FSlateFontInfo(RobotoFont.Object, 46, FName("Bold"));
		BodyFont = FSlateFontInfo(RobotoFont.Object, 20, FName("Regular"));
	}
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisSettingsWidget::Initialize()
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

void UMetamorphosisSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Visible);

	if (VolumeSlider != nullptr)             { VolumeSlider->OnValueChanged.AddDynamic(this, &UMetamorphosisSettingsWidget::HandleVolumeChanged); }
	if (JoystickSizeSlider != nullptr)       { JoystickSizeSlider->OnValueChanged.AddDynamic(this, &UMetamorphosisSettingsWidget::HandleJoystickSizeChanged); }
	if (HotbarSizeSlider != nullptr)         { HotbarSizeSlider->OnValueChanged.AddDynamic(this, &UMetamorphosisSettingsWidget::HandleHotbarSizeChanged); }
	if (MobileButtonSizeSlider != nullptr)   { MobileButtonSizeSlider->OnValueChanged.AddDynamic(this, &UMetamorphosisSettingsWidget::HandleMobileButtonSizeChanged); }
	if (FixedJoystickCheck != nullptr)       { FixedJoystickCheck->OnCheckStateChanged.AddDynamic(this, &UMetamorphosisSettingsWidget::HandleFixedJoystickChanged); }
	if (BackButton != nullptr)               { BackButton->OnClicked.AddDynamic(this, &UMetamorphosisSettingsWidget::HandleBackClicked); }

	ApplyStaticStyle();
	RefreshFromSettings();

	// 2026-10-20：进视口时先按当前分辨率把背景铺满一次（之后 NativeTick 里跟随分辨率变化）
	UpdateDesignCover();
}

void UMetamorphosisSettingsWidget::NativeDestruct()
{
	if (VolumeSlider != nullptr)             { VolumeSlider->OnValueChanged.RemoveDynamic(this, &UMetamorphosisSettingsWidget::HandleVolumeChanged); }
	if (JoystickSizeSlider != nullptr)       { JoystickSizeSlider->OnValueChanged.RemoveDynamic(this, &UMetamorphosisSettingsWidget::HandleJoystickSizeChanged); }
	if (HotbarSizeSlider != nullptr)         { HotbarSizeSlider->OnValueChanged.RemoveDynamic(this, &UMetamorphosisSettingsWidget::HandleHotbarSizeChanged); }
	if (MobileButtonSizeSlider != nullptr)   { MobileButtonSizeSlider->OnValueChanged.RemoveDynamic(this, &UMetamorphosisSettingsWidget::HandleMobileButtonSizeChanged); }
	if (FixedJoystickCheck != nullptr)       { FixedJoystickCheck->OnCheckStateChanged.RemoveDynamic(this, &UMetamorphosisSettingsWidget::HandleFixedJoystickChanged); }
	if (BackButton != nullptr)               { BackButton->OnClicked.RemoveDynamic(this, &UMetamorphosisSettingsWidget::HandleBackClicked); }

	Super::NativeDestruct();
}

void UMetamorphosisSettingsWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	// 设置可能在别处被改（控制台 Meta.Settings.*），这里保持同步。
	// 2026-10-19：传 false —— 每帧**不回写滑条的值**，否则玩家拖动时会被下一帧拉回旧值（表现就是"拖不动"）。
	if (!bRefreshing)
	{
		RefreshFromSettings(false);
	}

	// 进视口 1 秒后自动打一次几何日志（无头验证"点不到"靠这个取证，不需要控制台命令）
	if (!bGeometryDumped)
	{
		GeometryTimer += DeltaTime;
		if (GeometryTimer >= 1.f)
		{
			bGeometryDumped = true;
			DumpInteractiveGeometry();
		}
	}

	// 2026-10-20：背景铺满（cover）—— 只放大背景层的矩形，内容层一个像素都不动
	UpdateDesignCover();
}

// ---------------------------------------------------------------------------
// 背景铺满（cover）：零侵入做法
// ---------------------------------------------------------------------------

void UMetamorphosisSettingsWidget::UpdateDesignCover()
{
	// 2026-10-20：背景铺满（cover）—— 最终方案：**整块设计等比放大**，不是只放大背景图。
	//
	// 关键原因：这张美术背景里**烤死了面板/标题/五个标签/轨道** ✗，
	// 所以只放大背景图必然让"图里的面板"和"代码画的滑条/开关"错位（实测过，错得很明显）。
	// 反过来，只放大内容层也不行 —— 背景还是原尺寸，右下露黑带。
	// => 唯一自洽的做法：把 ScaleBox 从 ScaleToFit（contain）改成**用户指定缩放 = cover 系数**，
	//    让"图 + 控件"作为一个整体等比放大：四边铺满、面板与滑条依然严丝合缝，代价是
	//    在超宽/超长屏上会裁掉一点画边（1672x941 是 16:9，手机 20:9 两侧各裁约 5%）。
	if (DesignScaleBox == nullptr)
	{
		return;
	}

	const FVector2D ViewportPx    = UWidgetLayoutLibrary::GetViewportSize(this);   // 物理像素
	const float     ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);  // 像素/UMG
	if (ViewportPx.X <= 1.f || ViewportPx.Y <= 1.f || ViewportScale <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 换成 UMG 单位（本工程 1.5 倍 DPI；不能用控件自身尺寸 ✗）
	const FVector2D ViewportUmg = ViewportPx / ViewportScale;

	// cover：两个方向都要盖满 -> 取较大的那个比例（>= contain 的比例）
	float CoverScale = FMath::Max(ViewportUmg.X / DesignW, ViewportUmg.Y / DesignH);
	CoverScale = FMath::Max(CoverScale, 0.01f);

	if (FMath::IsNearlyEqual(CoverScale, LastCoverScale, 0.0005f))
	{
		return;   // 没变就不动，避免每帧改缩放
	}
	LastCoverScale = CoverScale;
	LastCoverViewport  = ViewportPx;
	LastCoverCanvasBox = ViewportUmg;

	DesignScaleBox->SetStretch(EStretch::UserSpecified);
	DesignScaleBox->SetUserSpecifiedScale(CoverScale);

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Settings] 背景铺满(cover)：整块设计缩放 %.3f（视口 %.0fx%.0f px = %.0fx%.0f UMG，设计 %.0fx%.0f）"),
		CoverScale, ViewportPx.X, ViewportPx.Y, ViewportUmg.X, ViewportUmg.Y, DesignW, DesignH);
}
void UMetamorphosisSettingsWidget::DumpInteractiveGeometry()
{
	struct FEntry { const TCHAR* Name; UWidget* Widget; };
	const FEntry Entries[] =
	{
		{ TEXT("VolumeSlider"),           VolumeSlider },
		{ TEXT("JoystickSizeSlider"),     JoystickSizeSlider },
		{ TEXT("HotbarSizeSlider"),       HotbarSizeSlider },
		{ TEXT("MobileButtonSizeSlider"), MobileButtonSizeSlider },
		{ TEXT("FixedJoystickCheck"),     FixedJoystickCheck },
		{ TEXT("BackButton"),             BackButton },
	};

	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] ===== 交互控件几何自检（尺寸为 0 或不在视口 = 点不到）====="));
	const FVector2D RootSize = GetCachedGeometry().GetLocalSize();
	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 设置控件自身尺寸：%.0fx%.0f"), RootSize.X, RootSize.Y);

	for (const FEntry& Entry : Entries)
	{
		if (Entry.Widget == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Settings]   控件 %s：null（根本没创建）"), Entry.Name);
			continue;
		}
		const FVector2D Size = Entry.Widget->GetCachedGeometry().GetLocalSize();
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Settings]   控件 %-24s 尺寸 %.0fx%.0f  可见性 %d  在视口 %d  可命中 %d"),
			Entry.Name, Size.X, Size.Y,
			static_cast<int32>(Entry.Widget->GetVisibility()),
			Entry.Widget->IsInViewport() ? 1 : 0,
			Entry.Widget->GetCachedGeometry().GetLocalSize().X > 0.f && Entry.Widget->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0);
	}
	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] ===== 几何自检结束 ====="));

	// 2026-10-20：背景铺满排查 —— 打出"控件自身/设计画布"的真实几何，
	// 用来判断"设计框到底占了多少屏幕"（若设计框只有屏幕的一部分，背景再怎么 cover 也铺不满）。
	{
		const FVector2D SelfSize   = GetCachedGeometry().GetAbsoluteSize();
		const FVector2D SelfPos    = GetCachedGeometry().GetAbsolutePosition();
		const FVector2D CanvasSize = DesignCanvas != nullptr ? DesignCanvas->GetCachedGeometry().GetAbsoluteSize() : FVector2D::ZeroVector;
		const FVector2D CanvasPos  = DesignCanvas != nullptr ? DesignCanvas->GetCachedGeometry().GetAbsolutePosition() : FVector2D::ZeroVector;
		const FVector2D BgSize     = BgImage != nullptr ? BgImage->GetCachedGeometry().GetAbsoluteSize() : FVector2D::ZeroVector;
		const FVector2D BgPos      = BgImage != nullptr ? BgImage->GetCachedGeometry().GetAbsolutePosition() : FVector2D::ZeroVector;
		const FVector2D ViewportSize  = UWidgetLayoutLibrary::GetViewportSize(this);
		const float     ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Settings] 几何排查：视口 %.0fx%.0f @scale %.3f（UMG %.0fx%.0f）｜控件自身 %.0fx%.0f @%.0f,%.0f｜设计画布 %.0fx%.0f @%.0f,%.0f｜背景图 %.0fx%.0f @%.0f,%.0f｜cover=%.3f"),
			ViewportSize.X, ViewportSize.Y, ViewportScale,
			ViewportSize.X / FMath::Max(ViewportScale, KINDA_SMALL_NUMBER),
			ViewportSize.Y / FMath::Max(ViewportScale, KINDA_SMALL_NUMBER),
			SelfSize.X, SelfSize.Y, SelfPos.X, SelfPos.Y,
			CanvasSize.X, CanvasSize.Y, CanvasPos.X, CanvasPos.Y,
			BgSize.X, BgSize.Y, BgPos.X, BgPos.Y,
			LastCoverScale);
	}
}

void UMetamorphosisSettingsWidget::SimulateSliderDrag(int32 Index, float Value)
{
	USlider* Slider = nullptr;
	const TCHAR* Name = TEXT("?");
	switch (Index)
	{
	case 0: Slider = VolumeSlider;           Name = TEXT("音量"); break;
	case 1: Slider = JoystickSizeSlider;     Name = TEXT("摇杆大小"); break;
	case 2: Slider = HotbarSizeSlider;       Name = TEXT("物品栏大小"); break;
	case 3: Slider = MobileButtonSizeSlider; Name = TEXT("上下按钮大小"); break;
	default: break;
	}

	if (Slider == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Settings] 模拟拖动失败：第 %d 条滑条不存在"), Index);
		return;
	}

	// 走真实链路：SetValue 会广播 OnValueChanged -> HandleXxxChanged -> 写设置子系统
	Slider->SetValue(Value);
	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 模拟拖动「%s」到 %.2f（回调链路已触发）"), Name, Value);
}

void UMetamorphosisSettingsWidget::SimulateToggleSwitch()
{
	if (FixedJoystickCheck == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Settings] 模拟切换失败：摇杆开关不存在"));
		return;
	}
	const bool bNewState = !FixedJoystickCheck->IsChecked();
	FixedJoystickCheck->SetIsChecked(bNewState);
	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 模拟点击「摇杆固定」-> %s（回调链路已触发）"),
		bNewState ? TEXT("开") : TEXT("关"));
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------

class UHorizontalBox* UMetamorphosisSettingsWidget::MakeRow(UVerticalBox* Column, const TCHAR* RowName,
	const FText& Label, const FSlateFontInfo& Font, const FLinearColor& Color, TObjectPtr<UTextBlock>& OutValueLabel)
{
	USizeBox* RowBox = MakeSettingsWidget<USizeBox>(WidgetTree);
	RowBox->SetWidthOverride(RowWidth);
	if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(RowBox))
	{
		RowSlot->SetPadding(FMargin(0.f, RowSpacing * 0.5f));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UHorizontalBox* Row = MakeSettingsWidget<UHorizontalBox>(WidgetTree, RowName);
	RowBox->AddChild(Row);

	// 左：标题（占满剩余空间）
	UTextBlock* LabelBlock = MakeSettingsWidget<UTextBlock>(WidgetTree);
	LabelBlock->SetText(Label);
	LabelBlock->SetFont(Font);
	LabelBlock->SetColorAndOpacity(FSlateColor(Color));
	LabelBlock->SetJustification(ETextJustify::Left);
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelBlock))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	// 右：数值文本
	OutValueLabel = MakeSettingsWidget<UTextBlock>(WidgetTree);
	OutValueLabel->SetFont(Font);
	OutValueLabel->SetColorAndOpacity(FSlateColor(Color));
	OutValueLabel->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(OutValueLabel))
	{
		ValueSlot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
		ValueSlot->SetVerticalAlignment(VAlign_Center);
	}

	return Row;
}

// ---------------------------------------------------------------------------
// 2026-10-19：按交付包复刻的设置界面（设计空间 1672x941，见 BuildDesignLayout 声明处注释）
// ---------------------------------------------------------------------------

namespace
{
	UTexture2D* LoadSettingsTexture(const TCHAR* Path)
	{
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Path);
		if (Tex == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Settings] 素材载入失败：%s"), Path);
		}
		return Tex;
	}

	/** 在 Canvas 上放一张图（坐标/尺寸都是设计空间的绝对量） */
	UImage* MakeDesignImage(UWidgetTree* Tree, UCanvasPanel* Canvas, UTexture2D* Tex,
		FVector2D Pos, FVector2D Size, const TCHAR* Name = nullptr)
	{
		if (Tex == nullptr || Canvas == nullptr)
		{
			return nullptr;
		}
		UImage* Img = Tree->ConstructWidget<UImage>(UImage::StaticClass(), Name ? FName(Name) : NAME_None);
		Img->SetBrushFromTexture(Tex, false);
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Img))
		{
			S->SetPosition(Pos);
			S->SetSize(Size);
			S->SetAutoSize(false);
		}
		return Img;
	}

	/**
	 * 在 Canvas 上放一块"代码画的板子"（金边 + 深色填充）。
	 * 2026-10-19：用户反馈"字体消失、背景透明"——根因是 BG_Settings 这张贴图在本工程画不出来
	 * （第 6 次同类，见 Docs/UI素材与替换进度.md），烤进图里的面板/金框/标题/标签全看不见。
	 * 所以面板、标题、五行标签、按钮底板一律改成【代码画】，BG_Settings 只作可选的装饰叠加。
	 */
	void MakeDesignPlate(UWidgetTree* Tree, UCanvasPanel* Canvas,
		FVector2D Pos, FVector2D Size,
		FLinearColor Fill, FLinearColor Outline, float OutlineThickness = 2.f)
	{
		if (Canvas == nullptr)
		{
			return;
		}
		// 外层 = 金边（比内容外扩 OutlineThickness）
		if (OutlineThickness > 0.f && Outline.A > 0.f)
		{
			UBorder* Edge = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Edge->SetBrushColor(Outline);
			Edge->SetPadding(FMargin(0.f));
			Edge->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Edge))
			{
				S->SetPosition(Pos - FVector2D(OutlineThickness, OutlineThickness));
				S->SetSize(Size + FVector2D(OutlineThickness * 2.f, OutlineThickness * 2.f));
				S->SetAutoSize(false);
			}
		}
		// 内层 = 面板填充
		UBorder* FillBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		FillBox->SetBrushColor(Fill);
		FillBox->SetPadding(FMargin(0.f));
		FillBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(FillBox))
		{
			S->SetPosition(Pos);
			S->SetSize(Size);
			S->SetAutoSize(false);
		}
	}

	/** 透明热区按钮 */
	UButton* MakeDesignHitBox(UWidgetTree* Tree, UCanvasPanel* Canvas, FVector2D Pos, FVector2D Size, const TCHAR* Name)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		FButtonStyle Style;
		Style.Normal = Style.Hovered = Style.Pressed = FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.f));
		Btn->SetStyle(Style);
		Btn->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Btn))
		{
			S->SetPosition(Pos);
			S->SetSize(Size);
			S->SetAutoSize(false);
			// 2026-10-19：按钮热区必须压在所有东西之上 —— 否则美术背景（Z=-5）盖住后点不到
			S->SetZOrder(20);
		}
		return Btn;
	}
}

void UMetamorphosisSettingsWidget::BuildDesignLayout()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 按交付包构建设置界面（设计空间 %.0fx%.0f）"), DesignW, DesignH);

	// ---- 素材 ----
	UTexture2D* BgTex     = LoadSettingsTexture(TEXT("/Game/UI/Settings/BG_Settings.BG_Settings"));
	UTexture2D* FillTex   = LoadSettingsTexture(TEXT("/Game/UI/Settings/Fill.Fill"));
	UTexture2D* HandleTex = LoadSettingsTexture(TEXT("/Game/UI/Settings/Handle.Handle"));
	// 2026-10-19：Fill / Handle 这两张自绘图标在本工程画不出来（且滑条现在用引擎可见样式），
	// 保留载入仅为将来"贴图能画出来"时可直接复用；(void) 避免被当成未使用变量。
	(void)FillTex;
	(void)HandleTex;
	UTexture2D* KnobTex   = LoadSettingsTexture(TEXT("/Game/UI/Settings/SwitchKnob.SwitchKnob"));
	// UTexture2D* PlateTex = ...BtnPlate... —— 2026-10-19：不再使用（贴图在本工程画不出来，且与「返回」样式不统一，已改用代码画的底板）

	// ---- Overlay -> ScaleBox -> SizeBox(设计尺寸) -> Canvas ----
	UOverlay* Root = MakeSettingsWidget<UOverlay>(WidgetTree, TEXT("SettingsRoot"));
	WidgetTree->RootWidget = Root;

	UScaleBox* Scale = MakeSettingsWidget<UScaleBox>(WidgetTree, TEXT("DesignScale"));
	// UE5.6：缩放规则枚举是 EStretch::Type（不是 EScaleBoxStretch）
	Scale->SetStretch(EStretch::ScaleToFit);
	DesignScaleBox = Scale;   // 2026-10-20：cover 时把它切成 UserSpecified（见 UpdateDesignCover）
	Root->AddChildToOverlay(Scale);

	USizeBox* Box = MakeSettingsWidget<USizeBox>(WidgetTree, TEXT("DesignSize"));
	Box->SetWidthOverride(DesignW);
	Box->SetHeightOverride(DesignH);
	Scale->AddChild(Box);

	UCanvasPanel* Canvas = MakeSettingsWidget<UCanvasPanel>(WidgetTree, TEXT("DesignCanvas"));
	DesignCanvas = Canvas;
	Box->AddChild(Canvas);

	// ---- 第一层：不透明底（兜底）----
	// 2026-10-19：用户反馈"背景是透明的"——烤好的 BG_Settings 本身 alpha 全 255 ✓，但贴图在本工程偶尔画不出来
	// （与"暂停页底板/拾取提示板身"同族的渲染问题）。这里先铺一层**纯色不透明底**，保证设置界面永远不透明；
	// BG_Settings 若能画出来就叠在它上面，画不出来也只是少一张插画、绝不会有"透出游戏画面"的观感。
	{
		// 2026-10-19：用户反馈"背板是一整块平的灰绿、太死板"——改成【上浅下深的渐变 + 四周暗角】，
		// 全部用代码叠半透明 Border 画（不依赖贴图，保证一定可见）。
		// ① 底色：略偏暖的墨绿，避免纯黑显脏
		UBorder* Backdrop = MakeSettingsWidget<UBorder>(WidgetTree, TEXT("SettingsBackdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.070f, 0.078f, 0.068f, 1.f));
		Backdrop->SetPadding(FMargin(0.f));
		Backdrop->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop))
		{
			BackdropSlot->SetPosition(FVector2D(0.f, 0.f));
			BackdropSlot->SetSize(FVector2D(DesignW, DesignH));
			BackdropSlot->SetAutoSize(false);
			// 2026-10-20（用户反馈"背景还是不符合美术稿"）：真因是这一层**不透明**兜底底原本 Z=0，
			// 而美术背景 BG_Settings 在 Z=-5 —— 于是不透明底把美术图整个盖住了 ✗。
			// 层级从现在起严格按数值排：兜底底 -100 < 渐变/暗角 -95 < 美术背景 -50 < 装饰 5 < 交互 10 < 按钮热区 20。
			BackdropSlot->SetZOrder(-100);
		}

		// ② 竖向渐变：三层半透明黑（上 35% / 中 55% / 下 80%），越往下越暗 —— 模拟旧房间的光衰
		auto AddShade = [this, Canvas](FVector2D Pos, FVector2D Size, FLinearColor Color, int32 Z)
		{
			UBorder* Shade = MakeSettingsWidget<UBorder>(WidgetTree);
			Shade->SetBrushColor(Color);
			Shade->SetPadding(FMargin(0.f));
			Shade->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Shade))
			{
				S->SetPosition(Pos);
				S->SetSize(Size);
				S->SetAutoSize(false);
				// 2026-10-19：兜底层（底/渐变/暗角）统一压到最底下，绝不允许盖住交互件
				// 2026-10-20：但必须压在不透明兜底底（-100）**之上**，否则渐变/暗角自己也被盖住 ✗
				S->SetZOrder(Z - 95);
			}
		};
		const float ThirdH = DesignH / 3.f;
		AddShade(FVector2D(0.f, 0.f),               FVector2D(DesignW, ThirdH), FLinearColor(0.f, 0.f, 0.f, 0.35f), 0);
		AddShade(FVector2D(0.f, ThirdH),            FVector2D(DesignW, ThirdH), FLinearColor(0.f, 0.f, 0.f, 0.55f), 0);
		AddShade(FVector2D(0.f, ThirdH * 2.f),      FVector2D(DesignW, DesignH - ThirdH * 2.f), FLinearColor(0.f, 0.f, 0.f, 0.80f), 0);

		// ③ 四周暗角：四条半透明黑边（左右厚、上下薄），把视线压到中间的面板上
		const float VignetteX = DesignW * 0.16f;   // 左右各 16% 宽
		const float VignetteY = DesignH * 0.10f;   // 上下各 10% 高
		AddShade(FVector2D(0.f, 0.f),                 FVector2D(VignetteX, DesignH), FLinearColor(0.f, 0.f, 0.f, 0.55f), 0);
		AddShade(FVector2D(DesignW - VignetteX, 0.f), FVector2D(VignetteX, DesignH), FLinearColor(0.f, 0.f, 0.f, 0.55f), 0);
		AddShade(FVector2D(0.f, 0.f),                 FVector2D(DesignW, VignetteY), FLinearColor(0.f, 0.f, 0.f, 0.45f), 0);
		AddShade(FVector2D(0.f, DesignH - VignetteY), FVector2D(DesignW, VignetteY), FLinearColor(0.f, 0.f, 0.f, 0.45f), 0);
	}

	// ---- 美术背景 BG_Settings（2026-10-19 重新挂回）----
	// 用户要求"复刻美术给的画面"。上一轮因为这张贴图在本工程疑似画不出来，把美术背景整块撤掉了，
	// 现在按 TC_Default + MaxTextureSize=2048 重导后挂回最底层（纯色兜底底之上、所有代码画的控件之下）。
	// 若它这次能画出来（bArtBackgroundDrawn=true 的日志/截图可验证），就把面板/标题/标签交给美术图，
	// 代码只保留滑条/开关/数值这些"活的"控件。
	if (BgTex != nullptr)
	{
		BgImage = MakeDesignImage(WidgetTree, Canvas, BgTex, FVector2D(0.f, 0.f), FVector2D(DesignW, DesignH));
		if (BgImage != nullptr)
		{
			// 2026-10-19（用户反馈"除了返回按钮，其它控件全部失效"）：
			// 根因是这张不透明大图被设成 ZOrder=1，而滑条/开关/数值全是默认 ZOrder=0 —— 背景盖在它们上面，
			// 既看不见也点不到（只有最后加入的按钮在同一 Z 下压在最上层）。
			// 现在：背景压到 ZOrder=-5（兜底层 -10 之上、所有交互件之下），并且 SelfHitTestInvisible 不拦点击。
			// 2026-10-20（用户反馈"背景还是不符合美术稿"）：真正的原因是上面那层**不透明兜底底**
			// 原本在 Z=0、盖住了这里；现在兜底底压到 -100，美术背景排 -50 ——
			// 层级：兜底底 -100 < 渐变/暗角 -95 < 美术背景 -50 < 装饰 5 < 交互 10 < 按钮热区 20。
			BgImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(BgImage->Slot))
			{
				S->SetZOrder(-50);
			}
			UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 美术背景 BG_Settings 已挂载（1672x941，ZOrder=-50，SelfHitTestInvisible）"));
		}
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Settings] 美术背景 BG_Settings 载入失败，改用代码画面板兜底"));
	}

	// 2026-10-19：美术背景里的面板/标题/五行标签是"烤死"的，代码再画一套会把它盖住（两套文字重叠）。
	// 做法：先按"美术背景可用"来构建（不画代码面板/标题/标签），用截图验证；若美术背景仍画不出来，
	// 把 bUseArtBackground 改回 false 即可自动退回代码兜底（其余逻辑不动）。
	// 2026-10-19：改成「有图就用美术图、没图就自动退回代码文字」，避免贴图载入失败时「字体全消失」。
	const bool bUseArtBackground = (BgTex != nullptr);

	// ---- 面板 / 标题 / 五行标签 / 按钮底板：全部【代码画】----
	// 2026-10-19：用户反馈"字体消失、背景透明"。根因：BG_Settings 这张贴图在本工程画不出来（第 6 次同类），
	// 烤进图里的面板、哥特金框、金色"设置"标题、五行标签全看不见，只剩代码画的滑条和数字。
	// 所以下面这些都改成代码绘制 —— 不依赖 BG_Settings 就一定可见；BG_Settings 仍然叠在上面，
	// 若哪天真能画出来，就是更漂亮的装饰（画不出来也不影响功能与可读性）。
	const FLinearColor PanelFill(0.098f, 0.125f, 0.098f, 0.97f);   // 暗绿面板
	const FLinearColor GoldLine(0.62f, 0.52f, 0.30f, 0.95f);        // 金线
	const FLinearColor GoldText(0.83f, 0.72f, 0.42f, 1.f);          // 金字
	const FLinearColor CreamText(0.82f, 0.76f, 0.60f, 1.f);         // 米金标签

	// 设计空间是 1672x941，BodyFont 只有 20——放到这个画布上太小，这里单独放大（不影响旧布局）
	// 注意：TitleFont 是 Roboto Bold，粗体字型没有中文回退（"设置"二字会不显示），所以标题用 BodyFont（Regular）放大
	FSlateFontInfo PanelTitleFont = BodyFont;
	PanelTitleFont.Size = 50;
	FSlateFontInfo PanelLabelFont = BodyFont;
	PanelLabelFont.Size = 30;

	// 面板（2026-10-19：用户反馈"面板偏左、视觉重心偏左"——原来是 x280..1007 的窄板贴在左边。
	// 因为底图插画在本工程画不出来，整屏是纯暗底，窄板就会显得"挂在左边"。
	// 现在改成【居中加宽】：宽 1300 居中于 1672 画布（x=186），里面的标签/滑条/开关/按钮仍按原坐标摆，
	// 于是它们自然落在面板的中偏左区域，整体视觉平衡。）
	const float PanelX = (DesignW - 1300.f) * 0.5f;   // = 186
	const float PanelY = 170.f;
	const float PanelW = 1300.f;
	const float PanelH = 610.f;
	if (!bUseArtBackground)
	{
		MakeDesignPlate(WidgetTree, Canvas, FVector2D(PanelX, PanelY), FVector2D(PanelW, PanelH), PanelFill, GoldLine, 2.f);
	}

	// 标题「设置」（2026-10-19：之前不可见的根因是——BodyFont 是 Roboto、没有中文字形，且字号 64 放进了只有 60px 高的框里，
	// 行高超过框高被裁掉。现在：框高给到 96、字号降到 50，确保中文字形完整显示。）
	// 美术背景可用时，标题由美术图负责，这里不再画（避免两套文字重叠）。
	if (!bUseArtBackground)
	{
		UTextBlock* Title = MakeSettingsWidget<UTextBlock>(WidgetTree, TEXT("SettingsTitle"));
		Title->SetText(FText::FromString(TEXT("设置")));
		Title->SetFont(PanelTitleFont);
		Title->SetColorAndOpacity(FSlateColor(GoldText));
		Title->SetJustification(ETextJustify::Center);
		Title->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Title))
		{
			S->SetPosition(FVector2D(PanelX, PanelY + 20.f));
			S->SetSize(FVector2D(PanelW, 96.f));
			S->SetAutoSize(false);
		}
	}

	// 五行标签（左对齐在面板里，数值在右侧 ValueX 处）
	// 美术背景可用时，标签由美术图负责，这里不再画（避免两套文字重叠）。
	if (!bUseArtBackground)
	{
		struct FRowLabel { const TCHAR* Text; float Y; };
		const FRowLabel RowLabels[5] =
		{
			{ TEXT("音量"),         320.f },   // 对应 VolumeSlider
			{ TEXT("摇杆固定"),     391.f },   // 对应 FixedJoystickCheck
			{ TEXT("摇杆大小"),     489.f },   // 对应 JoystickSizeSlider
			{ TEXT("物品栏大小"),   574.f },   // 对应 HotbarSizeSlider
			{ TEXT("上下按钮大小"), 660.f },   // 对应 MobileButtonSizeSlider
		};
		for (int32 i = 0; i < 5; ++i)
		{
			UTextBlock* Label = MakeSettingsWidget<UTextBlock>(WidgetTree, FName(*FString::Printf(TEXT("Label_%d"), i)));
			Label->SetText(FText::FromString(RowLabels[i].Text));
			Label->SetFont(PanelLabelFont);
			Label->SetColorAndOpacity(FSlateColor(CreamText));
			Label->SetJustification(ETextJustify::Left);
			Label->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Label))
			{
				S->SetPosition(FVector2D(310.f, RowLabels[i].Y - 4.f));
				S->SetSize(FVector2D(250.f, 32.f));
				S->SetAutoSize(false);
			}
		}
	}

	// 摇杆固定的开关底槽（代码画的 UBorder，**胶囊/椭圆**形状）
	// 2026-10-20（用户反馈"绿底应该是椭圆，现在是个矩形绿块盖在美术的椭圆槽上"）：
	//   几何改用交付包图层坐标：底板 x=867 y=391 w=88 h=43（取自 图层坐标清单.json）；
	//   画刷换成 RoundedBox + RoundingType=HalfHeightRadius -> **胶囊/椭圆** ✓，与美术图那个椭圆槽重合。
	JoystickSwitchTrack = MakeSettingsWidget<UBorder>(WidgetTree, TEXT("JoystickSwitchTrack"));
	if (JoystickSwitchTrack != nullptr)
	{
		FSlateBrush CapsuleBrush;
		CapsuleBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		CapsuleBrush.TintColor = FSlateColor(FLinearColor(0.06f, 0.07f, 0.06f, 1.f));
		CapsuleBrush.ImageSize = FVector2D(SwitchW, SwitchH);
		CapsuleBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		CapsuleBrush.OutlineSettings.CornerRadii = FVector4(0.f, 0.f, 0.f, 0.f);
		CapsuleBrush.OutlineSettings.Width = 0.f;
		JoystickSwitchTrack->SetBrush(CapsuleBrush);
		JoystickSwitchTrack->SetPadding(FMargin(0.f));
		JoystickSwitchTrack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(JoystickSwitchTrack))
		{
			S->SetPosition(FVector2D(SwitchX, SwitchY));
			S->SetSize(FVector2D(SwitchW, SwitchH));
			S->SetAutoSize(false);
			S->SetZOrder(12);   // 画在透明热区（Z=10）之上：看得见、但不拦点击
		}
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Settings] 摇杆固定开关底槽：胶囊 x=%.0f y=%.0f w=%.0f h=%.0f（HalfHeightRadius）"),
			SwitchX, SwitchY, SwitchW, SwitchH);
	}

	// （「返回」的底板与文字在后面统一用代码画，见本函数后半段的 MakeDesignPlate 段）

	// ---- 第二层：烤好的静态背景（叠在不透明底之上）----
	// 2026-10-19：BG_Settings 在本工程只渲染出零碎边角、还会盖住代码画的面板/标题/标签，
	// 所以不再挂载它（美术图仍然导入在 /Game/UI/Settings/BG_Settings，随时可回退）。

	// ---- 四个滑条：透明热区 + 自己画的填充/滑块 + 数值 ----
	UTextBlock* ValueLabels[4] = { nullptr, nullptr, nullptr, nullptr };
	const TCHAR* SliderNames[4] = { TEXT("VolumeSlider"), TEXT("JoystickSizeSlider"), TEXT("HotbarSizeSlider"), TEXT("MobileButtonSizeSlider") };
	// 注意：顺序必须与 TrackY 一致 —— 声音大小 / 摇杆大小 / 物品栏大小 / 上下按钮大小
	for (int32 i = 0; i < 4; ++i)
	{
		const float Y = TrackY[i];

		// 2026-10-19（用户反馈"除了返回全失效 / 滑条看不见、拖不动"）：
		// 之前是「全透明 USlider + 自己画的 Fill/Knob 图」——Fill/Handle 那两张图在本工程画不出来，
		// 于是滑条**看不见**；再叠加"每帧回写值"，就变成"拖不动"。
		// 现在换成【引擎可见样式】的滑条（条 + 把手都画得出来），坐标仍压在美术图的轨道上。
		// Fill / Knob 两张自绘图标不再创建（SliderFills/SliderKnobs 保持 null，刷新处都有判空）。
		USlider* Slider = MakeSettingsWidget<USlider>(WidgetTree, FName(SliderNames[i]));
		Slider->SetWidgetStyle(MakeVisibleSliderStyle());
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Slider))
		{
			S->SetPosition(FVector2D(TrackX - 8.f, Y - 10.f));
			S->SetSize(FVector2D(TrackW + 16.f, 32.f));
			S->SetAutoSize(false);
			S->SetZOrder(10);   // 交互件压在所有装饰/背景之上，否则看不见也点不到
		}
		if (i == 0) { VolumeSlider = Slider; }
		else if (i == 1) { JoystickSizeSlider = Slider; }
		else if (i == 2) { HotbarSizeSlider = Slider; }
		else { MobileButtonSizeSlider = Slider; }

		// 2026-10-19：用户反馈"最大值怎么只有 100 了"——设计布局里的滑条忘了设范围（默认 0~1）。
		// 现在：音量 0~1（显示 0%~100%）；三个尺寸 0.6~2.0（显示 60%~200%）。
		if (i == 0)
		{
			Slider->SetMinValue(0.f);
			Slider->SetMaxValue(1.f);
		}
		else
		{
			Slider->SetMinValue(UMetamorphosisSettingsSubsystem::MinScale);
			Slider->SetMaxValue(UMetamorphosisSettingsSubsystem::MaxScale);
			Slider->SetStepSize(0.05f);
		}

		// 自绘滑块不再创建（贴图在本工程画不出来），滑条自身的把手已经可见

		UTextBlock* Value = MakeSettingsWidget<UTextBlock>(WidgetTree, FName(*FString::Printf(TEXT("Value_%d"), i)));
		Value->SetFont(BodyFont);
		Value->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.76f, 0.60f, 1.f)));
		Value->SetJustification(ETextJustify::Right);
		Value->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Value))
		{
			S->SetPosition(FVector2D(ValueX, Y - 2.f));
			S->SetSize(FVector2D(ValueW, 28.f));
			S->SetAutoSize(false);
			S->SetZOrder(10);   // 数值文字必须在美术背景之上
		}
		ValueLabels[i] = Value;
	}
	VolumeValue = ValueLabels[0];
	JoystickSizeValue = ValueLabels[1];
	HotbarSizeValue = ValueLabels[2];
	MobileButtonSizeValue = ValueLabels[3];

	// ---- 摇杆固定：**透明的点击热区** + 上面画的胶囊底槽 + 滑块 ----
	// 2026-10-20（用户反馈"绿底应该是椭圆"）：形状交给 JoystickSwitchTrack（胶囊）+ JoystickKnobImage（圆钮），
	// UCheckBox 只当 88x43 的透明热区（画刷全透明），这样既有椭圆外观、又保留原来的点击/回调链路 ✓。
	FixedJoystickCheck = MakeSettingsWidget<UCheckBox>(WidgetTree, TEXT("FixedJoystickCheck"));
	{
		auto MakeTransparentBrush = [](float W, float H)
		{
			FSlateBrush Brush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
			Brush.ImageSize = FVector2D(W, H);
			return Brush;
		};

		FCheckBoxStyle Style;
		Style.SetCheckBoxType(ESlateCheckBoxType::ToggleButton);
		// 全部透明：外观由胶囊 + 圆钮负责（否则会再叠一个矩形色块上去 ✗）
		Style.SetUncheckedImage(MakeTransparentBrush(SwitchW, SwitchH));
		Style.SetUncheckedHoveredImage(MakeTransparentBrush(SwitchW, SwitchH));
		Style.SetCheckedImage(MakeTransparentBrush(SwitchW, SwitchH));
		Style.SetCheckedHoveredImage(MakeTransparentBrush(SwitchW, SwitchH));
		Style.SetUndeterminedImage(MakeTransparentBrush(SwitchW, SwitchH));
		Style.SetUndeterminedHoveredImage(MakeTransparentBrush(SwitchW, SwitchH));
		Style.SetPadding(FMargin(0.f));
		FixedJoystickCheck->SetWidgetStyle(Style);
	}
	if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(FixedJoystickCheck))
	{
		S->SetPosition(FVector2D(SwitchX, SwitchY));
		S->SetSize(FVector2D(SwitchW, SwitchH));
		S->SetAutoSize(false);
		S->SetZOrder(10);   // 交互件压最上层
	}
	JoystickKnobImage = MakeDesignImage(WidgetTree, Canvas, KnobTex,
		FVector2D(SwitchKnobOnX, SwitchKnobY), FVector2D(SwitchKnobSize, SwitchKnobSize));
	if (JoystickKnobImage != nullptr)
	{
		if (UCanvasPanelSlot* KS = Cast<UCanvasPanelSlot>(JoystickKnobImage->Slot)) { KS->SetZOrder(13); }
	}

	// ---- 「返回」：代码画的底板 + 文字（原来只有透明热区，靠背景里画好的字，但背景画不出来 -> 看不见）----
	// 2026-10-20：美术背景可用时它自带按钮底板，代码底板会叠两层 ✗ -> 只在兜底模式画。
	if (!bUseArtBackground)
	{
		MakeDesignPlate(WidgetTree, Canvas, FVector2D(467.f, 738.f), FVector2D(326.f, 86.f),
			FLinearColor(0.12f, 0.10f, 0.07f, 0.96f), GoldLine, 2.f);
	}
	{
		UTextBlock* BackLabel = MakeSettingsWidget<UTextBlock>(WidgetTree, TEXT("BackLabel"));
		BackLabel->SetText(FText::FromString(TEXT("返回")));
		BackLabel->SetFont(PanelLabelFont);
		BackLabel->SetColorAndOpacity(FSlateColor(CreamText));
		BackLabel->SetJustification(ETextJustify::Center);
		BackLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(BackLabel))
		{
			S->SetPosition(FVector2D(467.f, 760.f));
			S->SetSize(FVector2D(326.f, 44.f));
			S->SetAutoSize(false);
		}
	}
	BackButton = MakeDesignHitBox(WidgetTree, Canvas, FVector2D(467.f, 738.f), FVector2D(326.f, 86.f), TEXT("BackButton"));

	// 2026-10-20：用户要求设置界面**不要再有**「返回主界面」按钮（该入口在暂停菜单里）-> 已删除。
	// 原来这里画了它的底板 + 文字 + 透明热区，现在整块去掉，底部只剩「返回」一个按钮 ✓

	// ---- 绑定 ----
	// 2026-10-19：这里**不再**绑定回调 —— NativeConstruct 里已经统一绑过一次，
	// 两处都绑会导致每次改动触发两次回调（数值翻倍/抖动）。
	// 若控件是懒创建（NativeConstruct 之后才建），NativeConstruct 仍会覆盖到（创建后第一次 Construct 才绑）。
}

void UMetamorphosisSettingsWidget::RefreshDesignWidgets()
{
	UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this);
	if (Settings == nullptr || DesignCanvas == nullptr)
	{
		return;
	}
	const FMetamorphosisGameSettings& Current = Settings->GetSettings();

	const float Values[4] = { Current.MasterVolume, Current.JoystickScale, Current.HotbarScale, Current.MobileButtonScale };
	// 2026-10-19：尺寸类滑条的范围是 MinScale~MaxScale（0.6~2.0），填充比例必须按范围归一化，
	// 否则 1.0 会显示成"满格"、而 0.6 会显示成"一半"。
	const float ScaleRange = FMath::Max(0.001f, UMetamorphosisSettingsSubsystem::MaxScale - UMetamorphosisSettingsSubsystem::MinScale);
	for (int32 i = 0; i < 4; ++i)
	{
		const float Percent = (i == 0)
			? FMath::Clamp(Values[i], 0.f, 1.f)
			: FMath::Clamp((Values[i] - UMetamorphosisSettingsSubsystem::MinScale) / ScaleRange, 0.f, 1.f);
		if (SliderFills[i] != nullptr)
		{
			if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(SliderFills[i]->Slot))
			{
				S->SetSize(FVector2D(TrackW * Percent, 10.f));
			}
		}
		if (SliderKnobs[i] != nullptr)
		{
			if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(SliderKnobs[i]->Slot))
			{
				const float KnobX = TrackX + TrackW * Percent - HandleSize * 0.5f;
				S->SetPosition(FVector2D(KnobX, TrackY[i] + 6.f - HandleSize * 0.5f));
			}
		}
	}

	// 开关钮：固定=true -> 右；false -> 左（坐标取自交付包：开 x=916 / 关 x=870 / y=395 / 36x36）
	// 2026-10-19：用户反馈"看不出开没开"——除了左右位移，再加明显的颜色差别：
	//   开 = 米金色（和 HUD 强调色一致）；关 = 灰暗色。这样即使底板/轨道画不出来，也能一眼看出状态。
	if (JoystickKnobImage != nullptr)
	{
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(JoystickKnobImage->Slot))
		{
			S->SetPosition(FVector2D(Current.bFixedJoystick ? SwitchKnobOnX : SwitchKnobOffX, SwitchKnobY));
		}
		JoystickKnobImage->SetColorAndOpacity(Current.bFixedJoystick
			? FLinearColor(1.00f, 0.86f, 0.52f, 1.f)   // 开：米金
			: FLinearColor(0.45f, 0.45f, 0.42f, 1.f)); // 关：灰暗
	}

	// 开关底槽（**胶囊/椭圆**）：开 = 墨绿（用户要求"打开的时候背景变成绿色"）；关 = 暗色
	if (JoystickSwitchTrack != nullptr)
	{
		JoystickSwitchTrack->SetBrushColor(Current.bFixedJoystick
			? FLinearColor(0.18f, 0.46f, 0.22f, 1.f)    // 开：墨绿
			: FLinearColor(0.06f, 0.07f, 0.06f, 1.f));  // 关：暗
	}
}

void UMetamorphosisSettingsWidget::BuildDefaultLayout()
{
	// 2026-10-19：设置界面已改为"按交付包复刻"的设计空间布局，旧的代码搭建布局不再使用。
	BuildDesignLayout();
	return;
}

void UMetamorphosisSettingsWidget::BuildDefaultLayoutLegacy_Unused()
{
	UOverlay* Root = MakeSettingsWidget<UOverlay>(WidgetTree, TEXT("SettingsRoot"));
	WidgetTree->RootWidget = Root;

	BackgroundImage = MakeSettingsWidget<UImage>(WidgetTree, TEXT("Background"));
	BackgroundImage->SetBrush(BackgroundBrush);
	BackgroundImage->SetColorAndOpacity(BackgroundColor);
	if (UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(BackgroundImage))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// ---- 交付 2「设置页面」的面板：投影 + 暗绿面板 + 哥特金属外框（三层叠在内容后面）----
	// 素材按 810x690 设计，这里等比摆到屏幕中央：内容列再压在上面。
	if (PanelFrameBrush.HasUObject() || PanelBodyBrush.HasUObject())
	{
		USizeBox* PanelBox = MakeSettingsWidget<USizeBox>(WidgetTree, TEXT("SettingsPanelBox"));
		PanelBox->SetWidthOverride(PanelWidth);
		PanelBox->SetHeightOverride(PanelWidth * (690.f / 810.f));

		UOverlay* PanelOverlay = MakeSettingsWidget<UOverlay>(WidgetTree, TEXT("SettingsPanelOverlay"));
		PanelBox->AddChild(PanelOverlay);

		auto AddPanelLayer = [&](const FSlateBrush& Brush, const TCHAR* Name)
		{
			if (!Brush.HasUObject())
			{
				return;
			}
			UImage* Layer = MakeSettingsWidget<UImage>(WidgetTree, FName(Name));
			Layer->SetBrush(Brush);
			if (UOverlaySlot* LayerSlot = PanelOverlay->AddChildToOverlay(Layer))
			{
				LayerSlot->SetHorizontalAlignment(HAlign_Fill);
				LayerSlot->SetVerticalAlignment(VAlign_Fill);
			}
		};

		AddPanelLayer(PanelShadowBrush, TEXT("PanelShadow"));
		AddPanelLayer(PanelBodyBrush, TEXT("PanelBody"));
		AddPanelLayer(PanelFrameBrush, TEXT("PanelFrame"));

		if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelBox))
		{
			PanelSlot->SetHorizontalAlignment(HAlign_Center);
			PanelSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	UVerticalBox* Column = MakeSettingsWidget<UVerticalBox>(WidgetTree, TEXT("SettingsColumn"));
	if (UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column))
	{
		ColumnSlot->SetHorizontalAlignment(HAlign_Center);
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}

	// 标题（有美术的金色「设置」字样就用图，否则退回文字）
	if (TitleArtBrush.HasUObject())
	{
		USizeBox* TitleBox = MakeSettingsWidget<USizeBox>(WidgetTree, TEXT("SettingsTitleBox"));
		TitleBox->SetWidthOverride(TitleArtWidth);
		TitleBox->SetHeightOverride(TitleArtWidth * (72.f / 160.f));
		UImage* TitleArt = MakeSettingsWidget<UImage>(WidgetTree, TEXT("SettingsTitleArt"));
		TitleArt->SetBrush(TitleArtBrush);
		TitleBox->AddChild(TitleArt);
		if (UVerticalBoxSlot* ArtSlot = Column->AddChildToVerticalBox(TitleBox))
		{
			ArtSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 26.f));
			ArtSlot->SetHorizontalAlignment(HAlign_Center);
		}
	}

	// 标题
	TitleLabel = MakeSettingsWidget<UTextBlock>(WidgetTree, TEXT("SettingsTitle"));
	TitleLabel->SetText(TitleText);
	TitleLabel->SetFont(TitleFont);
	TitleLabel->SetColorAndOpacity(FSlateColor(TitleColor));
	TitleLabel->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleLabel))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 30.f));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	// 音量
	{
		UHorizontalBox* Row = MakeRow(Column, TEXT("VolumeRow"), LOCTEXT("VolumeLabel", "声音大小"),
			BodyFont, BodyColor, VolumeValue);
		USizeBox* SliderBox = MakeSettingsWidget<USizeBox>(WidgetTree);
		SliderBox->SetWidthOverride(SliderWidth);
		SliderBox->SetHeightOverride(SliderHeight);
		if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(SliderBox))
		{
			SliderSlot->SetPadding(FMargin(12.f, 0.f));
			SliderSlot->SetVerticalAlignment(VAlign_Center);
		}

		VolumeSlider = MakeSettingsWidget<USlider>(WidgetTree, TEXT("VolumeSlider"));
		VolumeSlider->SetWidgetStyle(MakeVisibleSliderStyle());
		VolumeSlider->SetMinValue(0.f);
		VolumeSlider->SetMaxValue(1.f);
		SliderBox->AddChild(VolumeSlider);
	}

	// 摇杆是否固定
	{
		// 注意：这里用临时变量接收"数值文本"，别覆盖上面音量行的 VolumeValue
		TObjectPtr<UTextBlock> FixedRowTrailing = nullptr;
		UHorizontalBox* Row = MakeRow(Column, TEXT("FixedJoystickRow"), LOCTEXT("FixedJoystickLabel", "摇杆固定"),
			BodyFont, BodyColor, FixedRowTrailing);

		FixedJoystickCheck = MakeSettingsWidget<UCheckBox>(WidgetTree, TEXT("FixedJoystickCheck"));
		FixedJoystickCheck->SetWidgetStyle(MakeVisibleCheckBoxStyle());

		// 勾选标记：方框底色不变，只在方框**上面**叠一个绿色 √。
		// 不能用 UCheckBox 的 Content —— 那会把内容排在方框**旁边**（变成带文字的复选框），
		// 所以这里用 Overlay 把 √ 叠上去。√ 设成 HitTestInvisible，点击照样落在复选框上。
		UOverlay* CheckOverlay = MakeSettingsWidget<UOverlay>(WidgetTree);
		if (UOverlaySlot* CheckInnerSlot = CheckOverlay->AddChildToOverlay(FixedJoystickCheck))
		{
			CheckInnerSlot->SetHorizontalAlignment(HAlign_Fill);
			CheckInnerSlot->SetVerticalAlignment(VAlign_Fill);
		}

		FixedJoystickCheckMark = MakeSettingsWidget<UTextBlock>(WidgetTree, TEXT("FixedJoystickCheckMark"));
		FixedJoystickCheckMark->SetText(CheckMarkText);
		FixedJoystickCheckMark->SetColorAndOpacity(FSlateColor(CheckMarkColor));
		FixedJoystickCheckMark->SetJustification(ETextJustify::Center);
		{
			FSlateFontInfo MarkFont = BodyFont;
			MarkFont.Size = 28;
			MarkFont.TypefaceFontName = FName("Bold");
			FixedJoystickCheckMark->SetFont(MarkFont);
		}
		if (UOverlaySlot* MarkSlot = CheckOverlay->AddChildToOverlay(FixedJoystickCheckMark))
		{
			MarkSlot->SetHorizontalAlignment(HAlign_Center);
			MarkSlot->SetVerticalAlignment(VAlign_Center);
		}

		USizeBox* CheckBox26 = MakeSettingsWidget<USizeBox>(WidgetTree);
		CheckBox26->SetWidthOverride(30.f);
		CheckBox26->SetHeightOverride(30.f);
		CheckBox26->AddChild(CheckOverlay);
		if (UHorizontalBoxSlot* CheckSlot = Row->AddChildToHorizontalBox(CheckBox26))
		{
			CheckSlot->SetPadding(FMargin(12.f, 0.f));
			CheckSlot->SetHorizontalAlignment(HAlign_Left);
			CheckSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	// 摇杆大小
	{
		UHorizontalBox* Row = MakeRow(Column, TEXT("JoystickSizeRow"), LOCTEXT("JoystickSizeLabel", "摇杆大小"),
			BodyFont, BodyColor, JoystickSizeValue);
		USizeBox* SliderBox = MakeSettingsWidget<USizeBox>(WidgetTree);
		SliderBox->SetWidthOverride(SliderWidth);
		SliderBox->SetHeightOverride(SliderHeight);
		if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(SliderBox))
		{
			SliderSlot->SetPadding(FMargin(12.f, 0.f));
			SliderSlot->SetVerticalAlignment(VAlign_Center);
		}

		JoystickSizeSlider = MakeSettingsWidget<USlider>(WidgetTree, TEXT("JoystickSizeSlider"));
		JoystickSizeSlider->SetWidgetStyle(MakeVisibleSliderStyle());
		JoystickSizeSlider->SetMinValue(UMetamorphosisSettingsSubsystem::MinScale);
		JoystickSizeSlider->SetMaxValue(UMetamorphosisSettingsSubsystem::MaxScale);
		JoystickSizeSlider->SetStepSize(0.05f);
		SliderBox->AddChild(JoystickSizeSlider);
	}

	// 物品栏大小
	{
		UHorizontalBox* Row = MakeRow(Column, TEXT("HotbarSizeRow"), LOCTEXT("HotbarSizeLabel", "物品栏大小"),
			BodyFont, BodyColor, HotbarSizeValue);
		USizeBox* SliderBox = MakeSettingsWidget<USizeBox>(WidgetTree);
		SliderBox->SetWidthOverride(SliderWidth);
		SliderBox->SetHeightOverride(SliderHeight);
		if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(SliderBox))
		{
			SliderSlot->SetPadding(FMargin(12.f, 0.f));
			SliderSlot->SetVerticalAlignment(VAlign_Center);
		}

		HotbarSizeSlider = MakeSettingsWidget<USlider>(WidgetTree, TEXT("HotbarSizeSlider"));
		HotbarSizeSlider->SetWidgetStyle(MakeVisibleSliderStyle());
		HotbarSizeSlider->SetMinValue(UMetamorphosisSettingsSubsystem::MinScale);
		HotbarSizeSlider->SetMaxValue(UMetamorphosisSettingsSubsystem::MaxScale);
		HotbarSizeSlider->SetStepSize(0.05f);
		SliderBox->AddChild(HotbarSizeSlider);
	}

	// 上下按钮大小
	{
		UHorizontalBox* Row = MakeRow(Column, TEXT("MobileButtonSizeRow"), LOCTEXT("MobileButtonSizeLabel", "上下按钮大小"),
			BodyFont, BodyColor, MobileButtonSizeValue);
		USizeBox* SliderBox = MakeSettingsWidget<USizeBox>(WidgetTree);
		SliderBox->SetWidthOverride(SliderWidth);
		SliderBox->SetHeightOverride(SliderHeight);
		if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(SliderBox))
		{
			SliderSlot->SetPadding(FMargin(12.f, 0.f));
			SliderSlot->SetVerticalAlignment(VAlign_Center);
		}

		MobileButtonSizeSlider = MakeSettingsWidget<USlider>(WidgetTree, TEXT("MobileButtonSizeSlider"));
		MobileButtonSizeSlider->SetWidgetStyle(MakeVisibleSliderStyle());
		MobileButtonSizeSlider->SetMinValue(UMetamorphosisSettingsSubsystem::MinScale);
		MobileButtonSizeSlider->SetMaxValue(UMetamorphosisSettingsSubsystem::MaxScale);
		MobileButtonSizeSlider->SetStepSize(0.05f);
		SliderBox->AddChild(MobileButtonSizeSlider);
	}

	// 返回
	{
		BackButton = MakeSettingsWidget<UButton>(WidgetTree, TEXT("BackButton"));
		BackButton->SetStyle(MakeSettingsButtonStyle());
		if (UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(BackButton))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 36.f, 0.f, 0.f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		}

		UTextBlock* BackLabel = MakeSettingsWidget<UTextBlock>(WidgetTree);
		BackLabel->SetText(BackButtonText);
		BackLabel->SetFont(BodyFont);
		BackLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		BackLabel->SetJustification(ETextJustify::Center);
		// 按钮内文字必须不可命中，否则点文字不会传到按钮（本项目踩过：开始界面/通关界面）
		BackLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		BackButton->AddChild(BackLabel);
	}

	// 2026-10-19：用户要求「下一周目」做在暂停界面 -> 设置面板里这个按钮已移除
	// （入口在 UMetamorphosisPauseMenuWidget 的 Options，Tag = NextLoop）。
	// 2026-10-20：用户要求设置界面不再有「返回主界面」-> 旧布局里的那个按钮也一并删除。
}

void UMetamorphosisSettingsWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (BackgroundImage == nullptr)        { BackgroundImage = WidgetTree->FindWidget<UImage>(TEXT("Background")); }
	if (TitleLabel == nullptr)             { TitleLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("SettingsTitle")); }
	if (VolumeSlider == nullptr)           { VolumeSlider = WidgetTree->FindWidget<USlider>(TEXT("VolumeSlider")); }
	if (JoystickSizeSlider == nullptr)     { JoystickSizeSlider = WidgetTree->FindWidget<USlider>(TEXT("JoystickSizeSlider")); }
	if (HotbarSizeSlider == nullptr)       { HotbarSizeSlider = WidgetTree->FindWidget<USlider>(TEXT("HotbarSizeSlider")); }
	if (MobileButtonSizeSlider == nullptr) { MobileButtonSizeSlider = WidgetTree->FindWidget<USlider>(TEXT("MobileButtonSizeSlider")); }
	if (FixedJoystickCheck == nullptr)     { FixedJoystickCheck = WidgetTree->FindWidget<UCheckBox>(TEXT("FixedJoystickCheck")); }
	if (FixedJoystickCheckMark == nullptr)  { FixedJoystickCheckMark = WidgetTree->FindWidget<UTextBlock>(TEXT("FixedJoystickCheckMark")); }
	if (BackButton == nullptr)             { BackButton = WidgetTree->FindWidget<UButton>(TEXT("BackButton")); }
	// 2026-10-19：「下一周目」按钮已移除（改到暂停菜单）
	// 2026-10-20：「返回主界面」按钮已移除（暂停菜单第二项已有该入口）
}

void UMetamorphosisSettingsWidget::ApplyStaticStyle()
{
	if (TitleLabel != nullptr)
	{
		TitleLabel->SetText(TitleText);
		TitleLabel->SetFont(TitleFont);
		TitleLabel->SetColorAndOpacity(FSlateColor(TitleColor));
	}

	if (BackgroundImage != nullptr)
	{
		BackgroundImage->SetColorAndOpacity(BackgroundColor);
	}
}

// ---------------------------------------------------------------------------
// 与设置子系统同步
// ---------------------------------------------------------------------------

void UMetamorphosisSettingsWidget::RefreshFromSettings(bool bSyncSliderValues)
{
	UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this);
	if (Settings == nullptr)
	{
		return;
	}

	const FMetamorphosisGameSettings& Current = Settings->GetSettings();

	bRefreshing = true;

	// 2026-10-19：只有"打开界面 / 外部改动"时才把值回写到控件。
	// 每帧回写会把玩家正在拖的滑条拉回旧值（= 拖不动），这是本界面之前"除了返回全失效"的主因之一。
	if (bSyncSliderValues)
	{
		if (VolumeSlider != nullptr)           { VolumeSlider->SetValue(Current.MasterVolume); }
		if (JoystickSizeSlider != nullptr)     { JoystickSizeSlider->SetValue(Current.JoystickScale); }
		if (HotbarSizeSlider != nullptr)       { HotbarSizeSlider->SetValue(Current.HotbarScale); }
		if (MobileButtonSizeSlider != nullptr) { MobileButtonSizeSlider->SetValue(Current.MobileButtonScale); }
		if (FixedJoystickCheck != nullptr)     { FixedJoystickCheck->SetIsChecked(Current.bFixedJoystick); }
	}
	if (FixedJoystickCheckMark != nullptr)
	{
		FixedJoystickCheckMark->SetVisibility(Current.bFixedJoystick
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (VolumeValue != nullptr)
	{
		VolumeValue->SetText(FText::Format(LOCTEXT("PercentFormat", "{0}%"),
			FText::AsNumber(FMath::RoundToInt(Current.MasterVolume * 100.f))));
	}
	if (JoystickSizeValue != nullptr)
	{
		JoystickSizeValue->SetText(FText::Format(LOCTEXT("ScaleFormat", "{0}%"),
			FText::AsNumber(FMath::RoundToInt(Current.JoystickScale * 100.f))));
	}
	if (HotbarSizeValue != nullptr)
	{
		HotbarSizeValue->SetText(FText::Format(LOCTEXT("ScaleFormat2", "{0}%"),
			FText::AsNumber(FMath::RoundToInt(Current.HotbarScale * 100.f))));
	}
	if (MobileButtonSizeValue != nullptr)
	{
		MobileButtonSizeValue->SetText(FText::Format(LOCTEXT("ScaleFormat3", "{0}%"),
			FText::AsNumber(FMath::RoundToInt(Current.MobileButtonScale * 100.f))));
	}

	bRefreshing = false;

	// 复刻布局：填充宽度 / 滑块位置 / 开关钮 / 数值文字
	RefreshDesignWidgets();
}

// ---------------------------------------------------------------------------
// 控件回调
// ---------------------------------------------------------------------------

void UMetamorphosisSettingsWidget::HandleVolumeChanged(float Value)
{
	if (!bRefreshing)
	{
		if (UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Settings->SetMasterVolume(Value);
		}
	}
}

void UMetamorphosisSettingsWidget::HandleJoystickSizeChanged(float Value)
{
	if (!bRefreshing)
	{
		if (UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Settings->SetJoystickScale(Value);
		}
	}
}

void UMetamorphosisSettingsWidget::HandleHotbarSizeChanged(float Value)
{
	if (!bRefreshing)
	{
		if (UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Settings->SetHotbarScale(Value);
		}
	}
}

void UMetamorphosisSettingsWidget::HandleMobileButtonSizeChanged(float Value)
{
	if (!bRefreshing)
	{
		if (UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Settings->SetMobileButtonScale(Value);
		}
	}
}

void UMetamorphosisSettingsWidget::HandleFixedJoystickChanged(bool bChecked)
{
	if (!bRefreshing)
	{
		if (UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Settings->SetFixedJoystick(bChecked);
		}
	}
}

void UMetamorphosisSettingsWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

// 2026-10-20：HandleReturnToTitleClicked / SetShowReturnToTitle 已删除
// （用户要求设置界面不再有「返回主界面」按钮；该入口在暂停菜单里）。

#undef LOCTEXT_NAMESPACE







