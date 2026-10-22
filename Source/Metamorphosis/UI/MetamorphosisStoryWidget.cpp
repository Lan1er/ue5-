// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisStoryWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Input/Reply.h"
#include "Metamorphosis.h"
#include "Story/MetamorphosisStorySubsystem.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// ⚠️ 唯一命名：unity build 会把多个 UI cpp 拼进同一编译单元，撞名就报 already has a body。
	template <typename T>
	T* MakeStoryWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), Name);
	}

	FButtonStyle MakeStoryButtonStyle()
	{
		FButtonStyle Style;
		FSlateBrush Normal = FSlateColorBrush(FLinearColor(0.16f, 0.14f, 0.12f, 0.92f));
		FSlateBrush Hover = FSlateColorBrush(FLinearColor(0.30f, 0.26f, 0.20f, 0.96f));
		FSlateBrush Pressed = FSlateColorBrush(FLinearColor(0.42f, 0.36f, 0.24f, 1.f));
		Style.SetNormal(Normal);
		Style.SetHovered(Hover);
		Style.SetPressed(Pressed);
		Style.SetNormalPadding(FMargin(18.f, 10.f));
		Style.SetPressedPadding(FMargin(18.f, 12.f, 18.f, 8.f));
		return Style;
	}
}

UMetamorphosisStoryWidget::UMetamorphosisStoryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 构造函数里不能碰 FCoreStyle —— 只能用 ConstructorHelpers 找引擎字体
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		SpeakerFont = FSlateFontInfo(RobotoFont.Object, 15, FName("Regular"));
		BodyFont = FSlateFontInfo(RobotoFont.Object, 21, FName("Regular"));
		ChoiceFont = FSlateFontInfo(RobotoFont.Object, 18, FName("Bold"));
	}
}

bool UMetamorphosisStoryWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	// 设计器树为空 => 纯 C++ 类，自己搭布局（必须在这里，不能放 NativeConstruct）
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildStoryLayout();
		bBuiltLayout = true;
	}

	return bResult;
}

void UMetamorphosisStoryWidget::BuildStoryLayout()
{
	UOverlay* Root = MakeStoryWidget<UOverlay>(WidgetTree, TEXT("StoryRoot"));
	WidgetTree->RootWidget = Root;

	// 1) 全屏纯黑（hit-testable：点击/触摸任意处推进靠它接事件）
	Blackout = MakeStoryWidget<UBorder>(WidgetTree, TEXT("StoryBlackout"));
	Blackout->SetBrush(FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 1.f)));
	Blackout->SetPadding(FMargin(0.f));
	if (UOverlaySlot* BlackoutSlot = Cast<UOverlaySlot>(Root->AddChildToOverlay(Blackout)))
	{
		BlackoutSlot->SetHorizontalAlignment(HAlign_Fill);
		BlackoutSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// 2) 正文区（居中偏下）
	// ⚠️ 别用 SizeBox + AutoSize 的槽：UTextBlock 开了自动换行后"期望宽度"会塌成一两个字宽，
	//    整段文字会竖着排成一条（踩过）。这里用固定尺寸的画布槽，换行宽度才稳定。
	UVerticalBox* Content = MakeStoryWidget<UVerticalBox>(WidgetTree, TEXT("StoryContent"));

	// 说话人文字：2026-10-19 改为**挂到上方浅色纸带**上（不再放进正文列），
	// 所以这里只创建控件，实际挂载在下面"设计空间画布"里。
	SpeakerText = MakeStoryWidget<UTextBlock>(WidgetTree, TEXT("StorySpeaker"));

	BodyText = MakeStoryWidget<UTextBlock>(WidgetTree, TEXT("StoryBody"));
	// 2026-10-17：显式关闭自动换行，改由 SetWrapTextAt(实际可用宽度) 控制 —— 两者同时开会互相打架
	//（踩过：只开 AutoWrapText 时 TextBlock 的"期望宽度"会把文字撑出纸面板）。
	BodyText->SetAutoWrapText(false);
	BodyText->SetWrapTextAt(600.f);
	BodyText->SetJustification(ETextJustify::Left);
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(BodyText))
	{
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 0.f));
		// ⚠️ 必须是 HAlign_Fill：用 HAlign_Center 时，开了自动换行的 TextBlock 会缩到"最小期望宽度"
		//    （一两个字宽），整段文字就竖着排成一条（踩过两次）。
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Top);
	}

	// 3) 两个选项按钮（只有分支节点显示）
	ChoiceRow = MakeStoryWidget<UHorizontalBox>(WidgetTree, TEXT("StoryChoiceRow"));
	{
		auto MakeChoiceButton = [this](const TCHAR* ButtonName, const TCHAR* TextName, TObjectPtr<UTextBlock>& OutText) -> UButton*
		{
			UButton* Button = MakeStoryWidget<UButton>(WidgetTree, ButtonName);
			Button->SetStyle(MakeStoryButtonStyle());
			OutText = MakeStoryWidget<UTextBlock>(WidgetTree, TextName);
			OutText->SetAutoWrapText(true);
			OutText->SetJustification(ETextJustify::Center);
			Button->AddChild(OutText);
			if (UButtonSlot* BS = Cast<UButtonSlot>(OutText->Slot))
			{
				BS->SetPadding(FMargin(10.f, 6.f));
				BS->SetHorizontalAlignment(HAlign_Center);
				BS->SetVerticalAlignment(VAlign_Center);
			}
			return Button;
		};

		ChoiceAButton = MakeChoiceButton(TEXT("StoryChoiceA"), TEXT("StoryChoiceAText"), ChoiceAText);
		ChoiceBButton = MakeChoiceButton(TEXT("StoryChoiceB"), TEXT("StoryChoiceBText"), ChoiceBText);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(ChoiceRow->AddChildToHorizontalBox(ChoiceAButton)))
		{
			S->SetPadding(FMargin(12.f, 0.f));
		}
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(ChoiceRow->AddChildToHorizontalBox(ChoiceBButton)))
		{
			S->SetPadding(FMargin(12.f, 0.f));
		}
	}
	// ---- 底部对话框纸面板（美术交付 1：48 帧待机动画）----
	// 2026-10-17 重做：面板尺寸改为"按视口算"（屏宽 86%），正文放进带内缩 + 裁剪的书写区，
	// 顶部对齐、字号/换行宽度跟着面板走 —— 彻底解决"文字跑到纸面外、挤在底边"。
	UCanvasPanel* Canvas = MakeStoryWidget<UCanvasPanel>(WidgetTree, TEXT("StoryCanvas"));
	if (UOverlaySlot* CanvasSlot = Cast<UOverlaySlot>(Root->AddChildToOverlay(Canvas)))
	{
		CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
		CanvasSlot->SetVerticalAlignment(VAlign_Fill);
	}

	DialogueBoxOverlay = MakeStoryWidget<UOverlay>(WidgetTree, TEXT("StoryDialogueBox"));
	DialogueBoxOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* BoxSlot = Canvas->AddChildToCanvas(DialogueBoxOverlay))
	{
		BoxSlot->SetAnchors(FAnchors(0.5f, 1.f));
		BoxSlot->SetAlignment(FVector2D(0.5f, 1.f));
		BoxSlot->SetAutoSize(false);
		BoxSlot->SetSize(DialoguePanelSize);
		BoxSlot->SetPosition(FVector2D(0.f, -28.f));
		BoxSlot->SetZOrder(1);
		DialogueBoxSlot = BoxSlot;
	}

	// ⚠️ 关键：UImage 的 brush 在本工程里不会随控件尺寸拉伸（实测 800x320 的美术帧被画成 ~711x261，
	//    文字按 1101 宽排就会跑出纸面）。所以中间垫一层 ScaleBox + 设计尺寸 SizeBox：
	//    布局/内缩/字号全部在 800x320 的**设计空间**里算，ScaleBox 再把整块等比放大到屏宽 86%。
	DialogueScale = MakeStoryWidget<UScaleBox>(WidgetTree, TEXT("StoryDialogueScale"));
	DialogueScale->SetStretch(EStretch::ScaleToFit);
	DialogueScale->SetStretchDirection(EStretchDirection::Both);
	if (UOverlaySlot* ScaleSlot = DialogueBoxOverlay->AddChildToOverlay(DialogueScale))
	{
		ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
		ScaleSlot->SetVerticalAlignment(VAlign_Fill);
	}

	DialogueDesignBox = MakeStoryWidget<USizeBox>(WidgetTree, TEXT("StoryDialogueDesignBox"));
	DialogueDesignBox->SetWidthOverride(DialogueDesignSize.X);
	DialogueDesignBox->SetHeightOverride(DialogueDesignSize.Y);
	DialogueScale->AddChild(DialogueDesignBox);

	// 设计空间内的容器（纸面帧 + 书写区 + 页码都挂在这里）
	UOverlay* DesignOverlay = MakeStoryWidget<UOverlay>(WidgetTree, TEXT("StoryDialogueDesignOverlay"));
	if (UOverlaySlot* DesignSlot = Cast<UOverlaySlot>(DialogueDesignBox->AddChild(DesignOverlay)))
	{
		DesignSlot->SetHorizontalAlignment(HAlign_Fill);
		DesignSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// 设计空间里的自由定位画布：头像（左侧人物卡片）+ 说话人文字（上方浅色纸带）
	// ⚠️ 必须**在纸面板之后**加入 DesignOverlay，否则会被面板盖住（Overlay 的子控件顺序 = z 序）。
	//    所以这一整段放在下面（纸面板 + 书写区之后、页码之前）。

	// 纸面板（48 帧动画）铺满设计空间
	DialoguePanel = MakeStoryWidget<UImage>(WidgetTree, TEXT("StoryDialoguePanel"));
	DialoguePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* PanelSlot = DesignOverlay->AddChildToOverlay(DialoguePanel))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Fill);
		PanelSlot->SetVerticalAlignment(VAlign_Fill);
		PanelSlot->SetPadding(FMargin(0.f));
	}

	DialogueTextArea = MakeStoryWidget<UBorder>(WidgetTree, TEXT("StoryTextArea"));
	DialogueTextArea->SetBrush(FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));
	DialogueTextArea->SetPadding(FMargin(0.f));
	DialogueTextArea->SetClipping(EWidgetClipping::ClipToBounds);
	DialogueTextArea->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* AreaSlot = DesignOverlay->AddChildToOverlay(DialogueTextArea))
	{
		AreaSlot->SetHorizontalAlignment(HAlign_Fill);
		AreaSlot->SetVerticalAlignment(VAlign_Fill);
		AreaSlot->SetPadding(FMargin(0.f));
		DialogueTextAreaSlot = AreaSlot;
	}

	// 正文列（顶部对齐，左对齐）
	Content->SetVisibility(ESlateVisibility::HitTestInvisible);
	DialogueTextArea->SetContent(Content);
	if (UVerticalBoxSlot* AreaContentSlot = Cast<UVerticalBoxSlot>(Content->Slot))
	{
		AreaContentSlot->SetVerticalAlignment(VAlign_Top);
	}
	{
		// 正文列自己撑满书写区顶部：VerticalBox 的子项从上往下排，天然顶部对齐
		Content->SetClipping(EWidgetClipping::ClipToBounds);
	}

	// ===== 头像 + 说话人（必须在纸面板之后加入，否则被面板盖住）=====
	DialogueCanvas = MakeStoryWidget<UCanvasPanel>(WidgetTree, TEXT("StoryDialogueCanvas"));
	DialogueCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* CanvasDesignSlot = DesignOverlay->AddChildToOverlay(DialogueCanvas))
	{
		CanvasDesignSlot->SetHorizontalAlignment(HAlign_Fill);
		CanvasDesignSlot->SetVerticalAlignment(VAlign_Fill);
		CanvasDesignSlot->SetPadding(FMargin(0.f));
	}

	// 头像层：整张 800x320 的**预合成卡片层**（头像已按透视烤进纸框内），直接铺在设计空间原点。
	// 没有对应映射时隐藏 -> 露出美术卡片本身的插图。
	PortraitImage = MakeStoryWidget<UImage>(WidgetTree, TEXT("StoryPortrait"));
	PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* PortSlot = DialogueCanvas->AddChildToCanvas(PortraitImage))
	{
		PortSlot->SetAnchors(FAnchors(0.f, 0.f));
		PortSlot->SetAlignment(FVector2D(0.f, 0.f));
		PortSlot->SetAutoSize(false);
		PortSlot->SetPosition(FVector2D(0.f, 0.f));
		PortSlot->SetSize(DialogueDesignSize);
		PortSlot->SetZOrder(2);
	}

	// 说话人文字：放到上方浅色纸带（浅底 -> 用深色字）
	if (SpeakerText != nullptr)
	{
		if (UCanvasPanelSlot* SpeakerSlot = DialogueCanvas->AddChildToCanvas(SpeakerText))
		{
			SpeakerSlot->SetAnchors(FAnchors(0.f, 0.f));
			SpeakerSlot->SetAlignment(FVector2D(0.f, 0.f));
			SpeakerSlot->SetAutoSize(true);
			SpeakerSlot->SetPosition(SpeakerOnBandPosition);
			SpeakerSlot->SetZOrder(3);
		}
	}

	// 页码（只有分页时显示）：压在**深绿纸面**的右下角（原来贴设计空间右下角，会跑到纸外/被裁）
	PageIndicator = MakeStoryWidget<UTextBlock>(WidgetTree, TEXT("StoryPageIndicator"));
	if (UOverlaySlot* PS = DesignOverlay->AddChildToOverlay(PageIndicator))
	{
		PS->SetHorizontalAlignment(HAlign_Right);
		PS->SetVerticalAlignment(VAlign_Bottom);
		PS->SetPadding(FMargin(0.f, 0.f, 64.f, 30.f));
	}
	PageIndicator->SetVisibility(ESlateVisibility::Collapsed);
	PageIndicator->SetJustification(ETextJustify::Right);

	// 选项行放到面板上方（黑底上更好读）
	ChoiceRow->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* ChoiceSlot = Canvas->AddChildToCanvas(ChoiceRow))
	{
		ChoiceSlot->SetAnchors(FAnchors(0.5f, 1.f));
		ChoiceSlot->SetAlignment(FVector2D(0.5f, 1.f));
		ChoiceSlot->SetAutoSize(true);
		ChoiceSlot->SetPosition(FVector2D(0.f, -400.f));
		ChoiceSlot->SetZOrder(4);
		ChoiceRowSlot = ChoiceSlot;
	}

	// 4) 右上角「跳过」—— 已按用户要求移除 ✗
	//    原因：点它会直接 EndStory，而开场剧情里带着 Open08（躲到床下）/ Open20（离婴儿床远一点）两道玩法门的激活，
	//    剧情一旦被跳过，门就永远不会激活 -> 玩家"一直躲着也过不去"，看起来像 bug。
	//    跳过功能仍可用调试命令 Meta.Story.Stop（仅开发期）。
	//    如果以后要做"已读剧情可跳过"，应该做成"跳过到下一个玩法门之前"，而不是直接 EndStory。
	ChoiceAButton->OnClicked.AddDynamic(this, &UMetamorphosisStoryWidget::HandleChoiceA);
	ChoiceBButton->OnClicked.AddDynamic(this, &UMetamorphosisStoryWidget::HandleChoiceB);
}

void UMetamorphosisStoryWidget::UpdateDialogueLayout()
{
	if (DialogueBoxOverlay == nullptr || !DialogueBoxSlot.IsValid())
	{
		return;
	}

	// ⚠️ 不要用 GetViewportSize：本工程 UMG 的坐标空间（root widget local size）是 1920x1080，
	//    而 GetViewportSize 返回 1280x720（ViewportScale=0.666 / DPI=1.5）——
	//    按后者算尺寸会让面板只占屏幕 ~57%（实机确认过）。控件自身的缓存几何尺寸才是真正的 Slate 空间。
	FVector2D Viewport = GetCachedGeometry().GetLocalSize();
	if (Viewport.X < 100.f || Viewport.Y < 100.f)
	{
		Viewport = UWidgetLayoutLibrary::GetViewportSize(this);
	}
	if (Viewport.X < 100.f || Viewport.Y < 100.f)
	{
		return;
	}
	// 视口没变就不重排（避免每帧设 Slot）
	if (FMath::Abs(Viewport.X - LastLayoutViewportSize.X) < 1.f &&
		FMath::Abs(Viewport.Y - LastLayoutViewportSize.Y) < 1.f)
	{
		return;
	}
	LastLayoutViewportSize = Viewport;

	// 1) 面板尺寸：屏宽比例 -> 夹在合理范围内 -> 高按宽高比
	const float PanelWidth = FMath::Clamp(Viewport.X * DialoguePanelScreenWidthRatio,
		DialoguePanelWidthRange.X, DialoguePanelWidthRange.Y);
	const float PanelHeight = PanelWidth * DialoguePanelAspect;
	DialoguePanelSize = FVector2D(PanelWidth, PanelHeight);

	const float BottomMargin = FMath::Max(10.f, Viewport.Y * DialoguePanelBottomMarginRatio);
	if (UCanvasPanelSlot* BoxSlot = DialogueBoxSlot.Get())
	{
		BoxSlot->SetSize(DialoguePanelSize);
		BoxSlot->SetPosition(FVector2D(0.f, -BottomMargin));
	}

	// 2) 书写区内缩 + 字号/换行：全部在**设计空间**（800x320）里算，
	//    ScaleBox 会把整块等比放大到屏幕上的面板尺寸 —— 这样文字和纸面永远同一个坐标系。
	const float DesignW = DialogueDesignSize.X;
	const float DesignH = DialogueDesignSize.Y;
	if (UOverlaySlot* AreaSlot = DialogueTextAreaSlot.Get())
	{
		const float Left = DesignW * DialogueTextInsets.X;
		const float Top = DesignH * DialogueTextInsets.Y;
		const float Right = DesignW * DialogueTextInsets.Z;
		const float Bottom = DesignH * DialogueTextInsets.W;
		AreaSlot->SetPadding(FMargin(Left, Top, Right, Bottom));

		// 字号固定在设计空间里（缩放交给 ScaleBox）：书写区宽 = 800*(1-0.30-0.10) = 480，字号 15 -> 约 31 字/行；
		// 书写区高 = 320*(1-0.38-0.12) = 160 -> 行高 17.25 时约 9 行（够放，不会像之前那样压到浅色纸带上）。
		BodyFont.Size = 15;
		SpeakerFont.Size = FMath::Max(8, SpeakerFontSize);
		if (BodyText != nullptr)
		{
			BodyText->SetFont(BodyFont);
			const float WrapWidth = FMath::Max(80.f, DesignW - Left - Right - 8.f);
			BodyText->SetWrapTextAt(WrapWidth);
			BodyText->SetAutoWrapText(false);
			BodyText->SetLineHeightPercentage(1.15f);
		}
		if (SpeakerText != nullptr)
		{
			SpeakerText->SetFont(SpeakerFont);
		}
	}

	// 3) 选项行跟着面板顶部上移（这一步在屏幕空间，因为选项行不在面板里）
	if (UCanvasPanelSlot* ChoiceSlot = ChoiceRowSlot.Get())
	{
		ChoiceSlot->SetPosition(FVector2D(0.f, -(BottomMargin + PanelHeight + 20.f)));
	}

	// 4) 面板动画帧
	ApplyDialogueFrame();

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[UI] 剧情排版：视口 %.0fx%.0f -> 面板 %.0fx%.0f（缩放 %.2fx），底部边距 %.0f；设计空间 %.0fx%.0f，书写区 %.0fx%.0f"),
		Viewport.X, Viewport.Y, PanelWidth, PanelHeight, PanelWidth / DesignW, BottomMargin,
		DesignW, DesignH,
		DesignW * (1.f - DialogueTextInsets.X - DialogueTextInsets.Z),
		DesignH * (1.f - DialogueTextInsets.Y - DialogueTextInsets.W));
}

void UMetamorphosisStoryWidget::ApplyStaticStyle()
{
	if (SpeakerText != nullptr)
	{
		SpeakerText->SetFont(SpeakerFont);
		SpeakerText->SetColorAndOpacity(FSlateColor(SpeakerOnBandColor));
	}
	if (BodyText != nullptr)
	{
		BodyText->SetFont(BodyFont);
		BodyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.92f, 0.88f, 1.f)));
	}
	if (PageIndicator != nullptr)
	{
		// 页码用小一号的说话人字体（金色），压在纸面板右下角
		FSlateFontInfo PageFont = SpeakerFont.HasValidFont() ? SpeakerFont : BodyFont;
		PageFont.Size = FMath::Max(10, PageFont.Size - 4);
		PageIndicator->SetFont(PageFont);
		PageIndicator->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.78f, 0.60f, 0.85f)));
	}
	if (ChoiceAText != nullptr)
	{
		ChoiceAText->SetFont(ChoiceFont);
		ChoiceAText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.90f, 0.78f, 1.f)));
	}
	if (ChoiceBText != nullptr)
	{
		ChoiceBText->SetFont(ChoiceFont);
		ChoiceBText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.90f, 0.78f, 1.f)));
	}
}

void UMetamorphosisStoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 自己整块要能接点击（黑色遮罩推进），但不吃按钮的点击：按钮在它上层，Slate 会先给按钮
	SetVisibility(ESlateVisibility::Visible);

	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->OnStoryChanged.AddDynamic(this, &UMetamorphosisStoryWidget::HandleStoryChanged);
	}

	ApplyStaticStyle();
	RefreshFromStory();

	// 首帧就把面板排好（不然第一帧是 800x320 的旧尺寸，闪一下）
	LastLayoutViewportSize = FVector2D::ZeroVector;
	UpdateDialogueLayout();

	// 让空格/回车/Esc 能直接作用在本控件上（剧情期间 PC 不再处理游戏输入）
	SetKeyboardFocus();
}

void UMetamorphosisStoryWidget::NativeDestruct()
{
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->OnStoryChanged.RemoveDynamic(this, &UMetamorphosisStoryWidget::HandleStoryChanged);
	}

	Super::NativeDestruct();
}

void UMetamorphosisStoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 暂停时冻结打字机与纸面板逐帧动画（2026-10-17：暂停/设置的"时停"必须覆盖所有 UI 表现）
	if (const UWorld* W = GetWorld())
	{
		if (UGameplayStatics::IsGamePaused(W) || W->IsPaused())
		{
			return;
		}
	}

	EnsureDialogueFrames();
	AdvanceDialogueFrame(InDeltaTime);

	// 面板尺寸/字号/换行宽度按视口算（视口变了才重排）
	UpdateDialogueLayout();

	// 一次性诊断：打一次实际排版尺寸（排查布局问题时看这一行就够）
	static bool bLoggedLayoutOnce = false;
	if (!bLoggedLayoutOnce && (GFrameCounter % 60) == 0)
	{
		const FVector2D BoxSize = DialogueBoxOverlay != nullptr ? DialogueBoxOverlay->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		const FVector2D ImgSize = DialoguePanel != nullptr ? DialoguePanel->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		const FVector2D AreaSize = DialogueTextArea != nullptr ? DialogueTextArea->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		const FVector2D BodySize = BodyText != nullptr ? BodyText->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		const FVector2D ScaleSize = DialogueScale != nullptr ? DialogueScale->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[UI] 排版实测：Box=%.0fx%.0f Scale=%.0fx%.0f Image=%.0fx%.0f TextArea=%.0fx%.0f Body=%.0fx%.0f 目标=%.0fx%.0f（UMG 空间）"),
			BoxSize.X, BoxSize.Y, ScaleSize.X, ScaleSize.Y, ImgSize.X, ImgSize.Y,
			AreaSize.X, AreaSize.Y, BodySize.X, BodySize.Y, DialoguePanelSize.X, DialoguePanelSize.Y);
		bLoggedLayoutOnce = true;
	}

	// 打字机：按配置速度逐字显示
	const float Speed = GetTypewriterSpeed();
	if (Speed > 0.f && IsTypewriterRunning())
	{
		TypewriterAccum += InDeltaTime * Speed;
		const int32 Step = FMath::FloorToInt(TypewriterAccum);
		if (Step > 0)
		{
			TypewriterAccum -= static_cast<float>(Step);
			TypewriterVisibleChars = FMath::Min(TypewriterVisibleChars + Step, TypewriterFullText.Len());
			ApplyTypewriterPrefix();
			if (!IsTypewriterRunning())
			{
				UE_LOG(LogMetamorphosis, Verbose, TEXT("[UI] 打字机：本页显示完毕（%d 字）"), TypewriterFullText.Len());
			}
		}
	}

	// 带脏检查的轮询：节点或分支状态变了才刷
	if (const UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		const FName NodeId = Story->GetCurrentNodeId();
		const bool bChoice = Story->HasChoice();
		if (NodeId != LastNodeId || bChoice != bLastHasChoice)
		{
			RefreshFromStory();
		}
	}
}

// ---------------------------------------------------------------------------
// 打字机（2026-10-17）
//   语义：正在打字时点击/空格 -> 立即显示本页全文（不翻页/不推进节点）；
//         本页已显示完 -> 再点 -> 翻下一页 / 推进节点（分页逻辑在 StorySubsystem 里）。
// ---------------------------------------------------------------------------

float UMetamorphosisStoryWidget::GetTypewriterSpeed() const
{
	if (const UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		return Story->GetDialogueTypewriterCharsPerSecond();
	}
	return 0.f;
}

bool UMetamorphosisStoryWidget::IsTypewriterRunning() const
{
	return TypewriterVisibleChars < TypewriterFullText.Len();
}

void UMetamorphosisStoryWidget::CompleteTypewriter()
{
	TypewriterVisibleChars = TypewriterFullText.Len();
	TypewriterAccum = 0.f;
	ApplyTypewriterPrefix();
}

void UMetamorphosisStoryWidget::ApplyTypewriterPrefix()
{
	if (BodyText == nullptr)
	{
		return;
	}
	FString Shown = TypewriterFullText.Left(FMath::Clamp(TypewriterVisibleChars, 0, TypewriterFullText.Len()));
	// 别把代理对（emoji 等）劈成半个字符
	if (!Shown.IsEmpty() && Shown.Len() < TypewriterFullText.Len())
	{
		const TCHAR Last = Shown[Shown.Len() - 1];
		if (Last >= 0xD800 && Last <= 0xDBFF)
		{
			Shown.LeftChopInline(1);
		}
	}
	BodyText->SetText(FText::FromString(Shown));
}

void UMetamorphosisStoryWidget::EnsureDialogueFrames()
{
	if (bDialogueFramesLoaded)
	{
		return;
	}
	bDialogueFramesLoaded = true;

	if (DialoguePanel == nullptr)
	{
		return;
	}

	// 48 帧（美术给的是 288 帧，导入时每 6 帧取 1 帧并缩到 800x320）
	DialogueFrames.Reset();
	for (int32 Index = 0; Index < 48; ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Game/UI/Dialogue/Seq/DLG_Idle_%02d.DLG_Idle_%02d"), Index, Index);
		if (UTexture2D* Frame = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			DialogueFrames.Add(Frame);
		}
	}

	// 一帧都没有 -> 退回静态层，连静态层也没有就保持黑底
	if (DialogueFrames.Num() == 0)
	{
		if (UTexture2D* Static = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/UI/Dialogue/DLG_BackPaper_A.DLG_BackPaper_A"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			DialogueFrames.Add(Static);
		}
	}

	DialogueFrameIndex = 0;
	ApplyDialogueFrame();

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 对话框纸面板：载入 %d 帧（动画=%d，%.0f fps）"),
		DialogueFrames.Num(), bDialogueBoxAnimated ? 1 : 0, DialogueBoxFps);
}

void UMetamorphosisStoryWidget::AdvanceDialogueFrame(float DeltaTime)
{
	if (!bDialogueBoxAnimated || DialogueFrames.Num() <= 1 || DialogueBoxFps <= 0.f)
	{
		return;
	}

	DialogueFrameAccum += DeltaTime;
	const float FrameDuration = 1.f / DialogueBoxFps;
	if (DialogueFrameAccum < FrameDuration)
	{
		return;
	}

	DialogueFrameAccum = 0.f;
	DialogueFrameIndex = (DialogueFrameIndex + 1) % DialogueFrames.Num();
	ApplyDialogueFrame();
}

void UMetamorphosisStoryWidget::ApplyDialogueFrame()
{
	if (DialoguePanel == nullptr || !DialogueFrames.IsValidIndex(DialogueFrameIndex) || DialogueFrames[DialogueFrameIndex] == nullptr)
	{
		return;
	}

	FSlateBrush Brush;
	Brush.SetResourceObject(DialogueFrames[DialogueFrameIndex]);
	// ⚠️ 不要把 ImageSize 设成面板尺寸：SImage 会按 ImageSize 画（实测面板只画到 ~0.6 倍大小、文字跑到纸外），
	//    留 0 才会铺满控件（控件尺寸由画布槽决定 = 屏宽 86%）。
	Brush.ImageSize = FVector2D::ZeroVector;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	DialoguePanel->SetBrush(Brush);
}

UTexture2D* UMetamorphosisStoryWidget::ResolvePortrait(const FString& Speaker)
{
	if (Speaker.IsEmpty())
	{
		return nullptr;
	}

	// 1) 从 ini 映射里找 "说话人:资产名"
	FString AssetName;
	for (const FString& Entry : PortraitMap)
	{
		FString Key, Value;
		if (Entry.Split(TEXT(":"), &Key, &Value))
		{
			if (Key.TrimStartAndEnd().Equals(Speaker.TrimStartAndEnd(), ESearchCase::IgnoreCase))
			{
				AssetName = Value.TrimStartAndEnd();
				break;
			}
		}
	}
	if (AssetName.IsEmpty())
	{
		return nullptr;
	}

	// 2) 命中缓存且有效就用（UPROPERTY 缓存 -> GC 不会收走）
	if (TObjectPtr<UTexture2D>* Cached = PortraitCache.Find(AssetName))
	{
		if (IsValid(*Cached))
		{
			return *Cached;
		}
	}

	// 3) 失效/未加载 -> 重新加载（换关卡后资源包会卸载）
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), *PortraitCardFolder, *AssetName, *AssetName);
	UTexture2D* Loaded = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	PortraitCache.Add(AssetName, Loaded);
	UE_LOG(LogMetamorphosis, Log, TEXT("[Story] 头像层 %s -> %s（%s）"),
		*Speaker, *AssetName, Loaded != nullptr ? TEXT("已加载") : TEXT("缺失"));
	return Loaded;
}

FText UMetamorphosisStoryWidget::ResolveSpeakerDisplayName(const FText& Speaker) const
{
	const FString Raw = Speaker.ToString();
	if (Raw.IsEmpty() || SpeakerDisplayName.Num() == 0)
	{
		return Speaker;
	}

	// ini 里写 "原名:显示名"（多条用 +SpeakerDisplayName= 重复添加）
	for (const FString& Entry : SpeakerDisplayName)
	{
		FString Key, Value;
		if (Entry.Split(TEXT(":"), &Key, &Value))
		{
			if (Key.TrimStartAndEnd().Equals(Raw.TrimStartAndEnd(), ESearchCase::IgnoreCase))
			{
				return FText::FromString(Value.TrimStartAndEnd());
			}
		}
	}
	return Speaker;
}

void UMetamorphosisStoryWidget::RefreshPortrait(const FText& Speaker)
{
	UTexture2D* Portrait = ResolvePortrait(Speaker.ToString());
	if (PortraitImage == nullptr)
	{
		return;
	}
	if (Portrait != nullptr)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Portrait);
		// ⚠️ 本工程的 UImage 不会被控件尺寸拉伸（踩过）：必须显式给 brush 的 ImageSize，
		//    否则 800x320 的卡片层会按原生尺寸铺出去 / 或只画一小块。
		//    预合成层是 800x320（= 设计空间），所以 ImageSize 必须等于设计空间尺寸。
		Brush.ImageSize = DialogueDesignSize;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.TintColor = FSlateColor(FLinearColor::White);
		PortraitImage->SetBrush(Brush);
		// 层里已经含了卡片的透视与倾斜，这里**不要**再旋转（再转就歪了 ✗）
		PortraitImage->SetRenderTransformAngle(0.f);
		PortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		// 旁白/系统/没配头像的说话人 -> 隐藏头像，露出美术卡片本身的插图
		PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMetamorphosisStoryWidget::RefreshFromStory()
{
	const UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this);
	if (Story == nullptr)
	{
		return;
	}

	LastNodeId = Story->GetCurrentNodeId();
	bLastHasChoice = Story->HasChoice();

	const FText Speaker = Story->GetSpeakerText();
	if (SpeakerText != nullptr)
	{
		SpeakerText->SetText(ResolveSpeakerDisplayName(Speaker));
		// 位置已移到上方浅色纸带 -> 用深色字（浅底浅字看不清）
		SpeakerText->SetColorAndOpacity(FSlateColor(SpeakerOnBandColor));
		SpeakerText->SetVisibility(Speaker.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	// 头像按说话人切换（没有映射的说话人 -> 隐藏头像）
	RefreshPortrait(Speaker);
	if (BodyText != nullptr)
	{
		// 分页状态在 StorySubsystem 里：GetBodyText() 已经返回"当前页"，这里只负责显示
		const FString PageText = Story->GetBodyText().ToString();
		// 换页/换节点 -> 打字机从头开始；同页刷新（如重开控件）则直接显示全文
		if (PageText != TypewriterFullText)
		{
			TypewriterFullText = PageText;
			TypewriterVisibleChars = 0;
			TypewriterAccum = 0.f;
			if (GetTypewriterSpeed() <= 0.f)
			{
				// 关掉打字机 -> 直接整页显示
				TypewriterVisibleChars = TypewriterFullText.Len();
			}
			ApplyTypewriterPrefix();
		}
		else if (!IsTypewriterRunning())
		{
			ApplyTypewriterPrefix();
		}
	}

	// 页码（只有分页时显示）：右下角「1/2」
	if (PageIndicator != nullptr)
	{
		const int32 PageCount = Story->GetPageCount();
		const int32 PageIndex = Story->GetPageIndex();
		const bool bShow = PageCount > 1;
		PageIndicator->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShow)
		{
			PageIndicator->SetText(FText::FromString(
				FString::Printf(TEXT("%d / %d"), PageIndex + 1, PageCount)));
		}
	}

	// 分支行：只有分支节点显示（只折叠内部行，不折叠整个控件）
	if (ChoiceRow != nullptr)
	{
		ChoiceRow->SetVisibility(bLastHasChoice ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (bLastHasChoice)
	{
		if (ChoiceAText != nullptr) { ChoiceAText->SetText(Story->GetChoiceText(0)); }
		if (ChoiceBText != nullptr) { ChoiceBText->SetText(Story->GetChoiceText(1)); }
	}
}

FReply UMetamorphosisStoryWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	// 先交给基类认领手指（正确手指号），再推进
	const FReply BaseReply = Super::NativeOnTouchStarted(InGeometry, InGestureEvent);

	// 2026-10-19 诊断：确认"点屏幕"到底有没有进到过场控件（进不来 = 输入模式/z 序问题；进来了但没换句 = 子系统问题）
	UE_LOG(LogMetamorphosis, Log, TEXT("[DIAG] 过场收到点击（touch）"));

	// 打字中：这一下只"立刻显示本页全文"，不翻页、不推进节点
	if (IsTypewriterRunning())
	{
		CompleteTypewriter();
		return BaseReply.Handled();
	}

	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		if (Story->IsPlaying() && !Story->HasChoice())
		{
			// 翻页与节点推进都由子系统负责（Advance 会自动判断"还有下一页"）
			Story->Advance();
		}
	}
	return BaseReply.Handled();
}

FReply UMetamorphosisStoryWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& MouseEvent)
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[DIAG] 过场收到点击（mouse）"));

	// 打字中：只补全本页
	if (IsTypewriterRunning())
	{
		CompleteTypewriter();
		return FReply::Handled();
	}

	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		if (Story->IsPlaying() && !Story->HasChoice())
		{
			Story->Advance();
			return FReply::Handled();
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, MouseEvent);
}

FReply UMetamorphosisStoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this);
	if (Story != nullptr && Story->IsPlaying())
	{
		if (Key == EKeys::SpaceBar || Key == EKeys::Enter)
		{
			// 打字中：空格/回车先补全本页（与点击一致）
			if (IsTypewriterRunning())
			{
				CompleteTypewriter();
				return FReply::Handled();
			}
			if (!Story->HasChoice())
			{
				Story->Advance();
			}
			return FReply::Handled();
		}
		if (Key == EKeys::Escape || Key == EKeys::Q)
		{
			Story->StopStory();
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMetamorphosisStoryWidget::HandleChoiceA()
{
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->ChooseOption(0);
	}
}

void UMetamorphosisStoryWidget::HandleChoiceB()
{
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->ChooseOption(1);
	}
}

void UMetamorphosisStoryWidget::HandleSkip()
{
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->StopStory();
	}
}
void UMetamorphosisStoryWidget::HandleStoryChanged(bool bPlaying, FName NodeId, const FText& Speaker, const FText& Text, bool bHasChoice)
{
	RefreshFromStory();

	// 结束时由 PlayerController 负责隐藏本控件；这里只保证焦点还在，方便继续用键盘
	if (bPlaying)
	{
		SetKeyboardFocus();
	}
}






