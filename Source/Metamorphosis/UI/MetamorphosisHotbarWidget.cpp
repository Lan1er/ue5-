// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisHotbarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Metamorphosis.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MetamorphosisHotbar"

namespace
{
	template <typename TWidgetT>
	TWidgetT* MakeHotbarWidget(UWidgetTree* Tree, FName Name = NAME_None)
	{
		return Tree->ConstructWidget<TWidgetT>(TWidgetT::StaticClass(), Name);
	}

	/** 纯色画刷（不依赖任何贴图/材质，着色器没编译好也能显示） */
	FSlateBrush MakeHotbarBrush(const FLinearColor& Color)
	{
		return FSlateColorBrush(Color);
	}

	/** 九宫格贴图画刷（美术格底用） */
	FSlateBrush MakeHotbarTextureBrush(UTexture2D* Texture, float Margin, const FVector2D& Size)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FMargin(Margin);
		Brush.TintColor = FSlateColor(FLinearColor::White);
		return Brush;
	}

	/** 菜单/快捷栏用的扁平按钮样式（透明，靠我们自己画的层显示） */
	FButtonStyle MakeHotbarInvisibleButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.Hovered = FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.06f));
		Style.Pressed = FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.12f));
		Style.Disabled = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}
}

UMetamorphosisHotbarWidget::UMetamorphosisHotbarWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		CountFont = FSlateFontInfo(RobotoFont.Object, 16, FName("Bold"));
		BackpackFont = FSlateFontInfo(RobotoFont.Object, 16, FName("Bold"));
	}

	// 美术交付 2 的快捷栏格子素材（58x58 **正方形**）—— 优先用它，
	// 它天然是正方形，不需要九宫格拉伸（上一轮刚把格子修成正方形，别退回去）。
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> SlotD2Default(TEXT("/Game/UI/Delivery2/Buttons/T_Slot1_Default.T_Slot1_Default"));
		static ConstructorHelpers::FObjectFinder<UTexture2D> SlotD2Hover(TEXT("/Game/UI/Delivery2/Buttons/T_Slot1_Hover.T_Slot1_Hover"));
		if (SlotD2Default.Succeeded())
		{
			SlotDefaultTexture = SlotD2Default.Object;
			SlotTextureMargin = 0.f;
		}
		if (SlotD2Hover.Succeeded())
		{
			SlotHoverTexture = SlotD2Hover.Object;
		}
	}

	// 兜底：美术交付 1 的格子素材（104x128，需要九宫格拉伸）。拿不到就退回纯色画法。
	static ConstructorHelpers::FObjectFinder<UTexture2D> SlotDefaultFinder(TEXT("/Game/UI/Inventory/Slot_Default.Slot_Default"));
	if (SlotDefaultTexture == nullptr && SlotDefaultFinder.Succeeded())
	{
		SlotDefaultTexture = SlotDefaultFinder.Object;
		SlotTextureMargin = 12.f;
	}
	static ConstructorHelpers::FObjectFinder<UTexture2D> SlotHoverFinder(TEXT("/Game/UI/Inventory/Slot_Hover.Slot_Hover"));
	if (SlotHoverTexture == nullptr && SlotHoverFinder.Succeeded())
	{
		SlotHoverTexture = SlotHoverFinder.Object;
	}

	BackpackLabel = LOCTEXT("BackpackLabel", "背包");
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

bool UMetamorphosisHotbarWidget::Initialize()
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

void UMetamorphosisHotbarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 自己不吃触摸（转视角的手势要能穿过去），只有格子/背包按钮吃点击
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 8 个格子的点击（AddDynamic 必须字面写出函数名，不能用循环表）
	if (SlotButtons.IsValidIndex(0) && SlotButtons[0] != nullptr) { SlotButtons[0]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked0); }
	if (SlotButtons.IsValidIndex(1) && SlotButtons[1] != nullptr) { SlotButtons[1]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked1); }
	if (SlotButtons.IsValidIndex(2) && SlotButtons[2] != nullptr) { SlotButtons[2]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked2); }
	if (SlotButtons.IsValidIndex(3) && SlotButtons[3] != nullptr) { SlotButtons[3]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked3); }
	if (SlotButtons.IsValidIndex(4) && SlotButtons[4] != nullptr) { SlotButtons[4]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked4); }
	if (SlotButtons.IsValidIndex(5) && SlotButtons[5] != nullptr) { SlotButtons[5]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked5); }
	if (SlotButtons.IsValidIndex(6) && SlotButtons[6] != nullptr) { SlotButtons[6]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked6); }
	if (SlotButtons.IsValidIndex(7) && SlotButtons[7] != nullptr) { SlotButtons[7]->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked7); }

	if (BackpackButton != nullptr)
	{
		BackpackButton->OnClicked.AddDynamic(this, &UMetamorphosisHotbarWidget::HandleBackpackClicked);
	}

	ApplyStaticStyle();
	RefreshFromInventory();
}

void UMetamorphosisHotbarWidget::NativeDestruct()
{
	if (SlotButtons.IsValidIndex(0) && SlotButtons[0] != nullptr) { SlotButtons[0]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked0); }
	if (SlotButtons.IsValidIndex(1) && SlotButtons[1] != nullptr) { SlotButtons[1]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked1); }
	if (SlotButtons.IsValidIndex(2) && SlotButtons[2] != nullptr) { SlotButtons[2]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked2); }
	if (SlotButtons.IsValidIndex(3) && SlotButtons[3] != nullptr) { SlotButtons[3]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked3); }
	if (SlotButtons.IsValidIndex(4) && SlotButtons[4] != nullptr) { SlotButtons[4]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked4); }
	if (SlotButtons.IsValidIndex(5) && SlotButtons[5] != nullptr) { SlotButtons[5]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked5); }
	if (SlotButtons.IsValidIndex(6) && SlotButtons[6] != nullptr) { SlotButtons[6]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked6); }
	if (SlotButtons.IsValidIndex(7) && SlotButtons[7] != nullptr) { SlotButtons[7]->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleSlotClicked7); }

	if (BackpackButton != nullptr)
	{
		BackpackButton->OnClicked.RemoveDynamic(this, &UMetamorphosisHotbarWidget::HandleBackpackClicked);
	}

	Super::NativeDestruct();
}

void UMetamorphosisHotbarWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	RefreshFromInventory();
}

// ---------------------------------------------------------------------------
// 布局（美术格底九宫格；素材缺失时退回 MC 风格纯色凹槽）
// ---------------------------------------------------------------------------

void UMetamorphosisHotbarWidget::BuildDefaultLayout()
{
	// 根：铺满屏幕的 Overlay，把快捷栏顶到屏幕底部居中
	UOverlay* Root = MakeHotbarWidget<UOverlay>(WidgetTree, TEXT("HotbarRoot"));
	WidgetTree->RootWidget = Root;

	// 底板（MC 那条半透明黑底）
	UBorder* RowBackground = MakeHotbarWidget<UBorder>(WidgetTree, TEXT("HotbarBackground"));
	RowBackground->SetBrush(MakeHotbarBrush(RowBackgroundColor));
	RowBackground->SetPadding(RowPadding);
	if (UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(RowBackground))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Center);
		BackgroundSlot->SetVerticalAlignment(VAlign_Bottom);
		BackgroundSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomMargin));
	}

	// 底板里：左边 8 个格子 + 右边「背包」按钮
	UHorizontalBox* OuterRow = MakeHotbarWidget<UHorizontalBox>(WidgetTree, TEXT("HotbarOuterRow"));
	RowBackground->SetContent(OuterRow);

	HotbarRow = MakeHotbarWidget<UHorizontalBox>(WidgetTree, TEXT("HotbarRow"));
	if (UHorizontalBoxSlot* RowSlot = OuterRow->AddChildToHorizontalBox(HotbarRow))
	{
		RowSlot->SetVerticalAlignment(VAlign_Center);
	}

	SlotButtons.Reset();
	SlotIcons.Reset();
	SlotCounts.Reset();
	SlotWidgets.Reset();

	UTexture2D* SlotArtTexture = GetSlotArtTexture(false);
	const float SlotHeight = SlotSize * SlotHeightRatio;

	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		// 每格：SizeBox 固定尺寸 -> 隐形按钮 -> Overlay（选中框 / 美术格底或凹槽 / 图标 / 数量）
		USizeBox* SlotBox = MakeHotbarWidget<USizeBox>(WidgetTree);
		SlotBox->SetWidthOverride(SlotSize);
		SlotBox->SetHeightOverride(SlotHeight);
		if (UHorizontalBoxSlot* BoxSlot = HotbarRow->AddChildToHorizontalBox(SlotBox))
		{
			BoxSlot->SetPadding(FMargin(SlotSpacing * 0.5f, 0.f));
			BoxSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* SlotButton = MakeHotbarWidget<UButton>(WidgetTree,
			*FString::Printf(TEXT("Slot_%d"), Index));
		SlotButton->SetStyle(MakeHotbarInvisibleButtonStyle());
		SlotBox->AddChild(SlotButton);

		// 选中框：白色描边；没选中时透明（不占视觉）
		UBorder* EdgeBorder = MakeHotbarWidget<UBorder>(WidgetTree,
			*FString::Printf(TEXT("SlotEdge_%d"), Index));
		EdgeBorder->SetBrush(MakeHotbarBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));
		EdgeBorder->SetPadding(FMargin(SelectionFrameWidth));
		SlotButton->AddChild(EdgeBorder);

		// 格底：有美术素材就用九宫格贴图，否则一层层套纯色；两者最终都把 ContentOverlay 放在里面
		UImage* SlotArt = nullptr;
		UOverlay* ContentOverlay = MakeHotbarWidget<UOverlay>(WidgetTree);
		if (SlotArtTexture != nullptr)
		{
			EdgeBorder->SetContent(ContentOverlay);

			SlotArt = MakeHotbarWidget<UImage>(WidgetTree, *FString::Printf(TEXT("SlotArt_%d"), Index));
			SlotArt->SetBrush(MakeHotbarTextureBrush(SlotArtTexture, SlotTextureMargin, FVector2D(SlotSize, SlotHeight)));
			SlotArt->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UOverlaySlot* ArtSlot = ContentOverlay->AddChildToOverlay(SlotArt))
			{
				ArtSlot->SetHorizontalAlignment(HAlign_Fill);
				ArtSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
		else
		{
			UBorder* BevelBorder = MakeHotbarWidget<UBorder>(WidgetTree);
			BevelBorder->SetBrush(MakeHotbarBrush(SlotBevelColor));
			BevelBorder->SetPadding(FMargin(2.f));
			EdgeBorder->SetContent(BevelBorder);

			UBorder* FillBorder = MakeHotbarWidget<UBorder>(WidgetTree);
			FillBorder->SetBrush(MakeHotbarBrush(SlotFillColor));
			BevelBorder->SetContent(FillBorder);
			FillBorder->SetContent(ContentOverlay);
		}

		// 物品图标（居中）
		USizeBox* IconBox = MakeHotbarWidget<USizeBox>(WidgetTree);
		IconBox->SetWidthOverride(SlotSize * IconSizeRatio);
		IconBox->SetHeightOverride(SlotSize * IconSizeRatio * SlotHeightRatio);
		if (UOverlaySlot* IconSlot = ContentOverlay->AddChildToOverlay(IconBox))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}

		UImage* Icon = MakeHotbarWidget<UImage>(WidgetTree, *FString::Printf(TEXT("SlotIcon_%d"), Index));
		Icon->SetBrush(MakeHotbarBrush(FLinearColor::White));
		IconBox->AddChild(Icon);

		// 数量角标（右下）
		UTextBlock* Count = MakeHotbarWidget<UTextBlock>(WidgetTree, *FString::Printf(TEXT("SlotCount_%d"), Index));
		Count->SetFont(CountFont);
		Count->SetColorAndOpacity(FSlateColor(CountColor));
		Count->SetJustification(ETextJustify::Right);
		Count->SetShadowOffset(FVector2D(1.f, 1.f));
		Count->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
		if (UOverlaySlot* CountSlot = ContentOverlay->AddChildToOverlay(Count))
		{
			CountSlot->SetHorizontalAlignment(HAlign_Right);
			CountSlot->SetVerticalAlignment(VAlign_Bottom);
			CountSlot->SetPadding(FMargin(0.f, 0.f, 2.f, 0.f));
		}

		FMetamorphosisHotbarSlotWidgets Widgets;
		Widgets.SlotBox = SlotBox;
		Widgets.IconBox = IconBox;
		Widgets.Button = SlotButton;
		Widgets.EdgeBorder = EdgeBorder;
		Widgets.SlotArt = SlotArt;
		Widgets.Icon = Icon;
		Widgets.Count = Count;
		SlotWidgets.Add(Widgets);

		SlotButtons.Add(SlotButton);
		SlotIcons.Add(Icon);
		SlotCounts.Add(Count);
	}

	// 「背包」按钮：**右上角、紧挨设置按钮（齿轮）左边**（2026-10-17 从快捷栏右侧挪过来）。
	// 用同一个全屏 Overlay 顶到右上角：右边距 = 齿轮右边距 + 齿轮尺寸 + 间距（默认 26+56+12=94），
	// 观感与齿轮一致（圆形半透明底 + 三态反馈），移动端也能点。
	{
		USizeBox* BagBox = MakeHotbarWidget<USizeBox>(WidgetTree, TEXT("BackpackButtonBox"));
		BagBox->SetWidthOverride(BackpackButtonSize);
		BagBox->SetHeightOverride(BackpackButtonSize);
		if (UOverlaySlot* BagBoxSlot = Root->AddChildToOverlay(BagBox))
		{
			BagBoxSlot->SetHorizontalAlignment(HAlign_Right);
			BagBoxSlot->SetVerticalAlignment(VAlign_Top);
			BagBoxSlot->SetPadding(FMargin(0.f, BackpackButtonTopMargin, BackpackButtonRightMargin, 0.f));
		}

		BackpackButton = MakeHotbarWidget<UButton>(WidgetTree, TEXT("BackpackButton"));
		FButtonStyle BagStyle;
		BagStyle.Normal = FSlateRoundedBoxBrush(FLinearColor(0.06f, 0.06f, 0.08f, 0.42f), BackpackButtonSize * 0.5f);
		BagStyle.Hovered = FSlateRoundedBoxBrush(FLinearColor(0.16f, 0.17f, 0.22f, 0.62f), BackpackButtonSize * 0.5f);
		BagStyle.Pressed = FSlateRoundedBoxBrush(FLinearColor(0.90f, 0.72f, 0.25f, 0.85f), BackpackButtonSize * 0.5f);
		BagStyle.Disabled = BagStyle.Normal;
		BagStyle.NormalPadding = FMargin(0.f);
		BagStyle.PressedPadding = FMargin(0.f);
		BackpackButton->SetStyle(BagStyle);
		BagBox->AddChild(BackpackButton);

		// 图标：优先用交付包里的背包图标（/Game/UI/HUD/T_Icon_Backpack），
		// 拿不到退化成引擎自带图标，再不行才是「包」字 —— 任何情况下都有可点的东西。
		UTexture2D* BagTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/HUD/T_Icon_Backpack.T_Icon_Backpack"));
		FSlateBrush BagIconBrush;
		if (BagTex != nullptr)
		{
			BagIconBrush.SetResourceObject(BagTex);
			BagIconBrush.ImageSize = FVector2D(128.f, 128.f);
			BagIconBrush.DrawAs = ESlateBrushDrawType::Image;
			BagIconBrush.TintColor = FSlateColor(FLinearColor::White);
			UE_LOG(LogMetamorphosis, Log, TEXT("[HUD] 背包图标已载入：T_Icon_Backpack"));
		}
		else
		{
			BagIconBrush = *FCoreStyle::Get().GetBrush(TEXT("Icons.Inventory"));
			UE_LOG(LogMetamorphosis, Warning, TEXT("[HUD] 背包图标素材缺失，退化成引擎图标"));
		}
		if (BagIconBrush.GetResourceObject() != nullptr)
		{
			USizeBox* BagIconBox = MakeHotbarWidget<USizeBox>(WidgetTree);
			BagIconBox->SetWidthOverride(BackpackButtonSize * 0.56f);
			BagIconBox->SetHeightOverride(BackpackButtonSize * 0.56f);
			UImage* BagIcon = MakeHotbarWidget<UImage>(WidgetTree, TEXT("BackpackButtonIcon"));
			BagIcon->SetBrush(BagIconBrush);
			// 2026-10-19：用户反馈"不透明度有点低"——原来压到 0.93/0.87/0.74（偏灰），这里提到 1.18 略提亮
			// （不过曝：图标本身是米金描线，1.18 只是让它从背景里跳出来一点）。
			BagIcon->SetColorAndOpacity(FLinearColor(1.18f, 1.18f, 1.18f, 1.f));
			BagIcon->SetVisibility(ESlateVisibility::HitTestInvisible);

			// 2026-10-19：用户反馈"没有金色边框"——HUD 其它按钮都有金框，这里用代码画一圈细金框
			// （UBorder 的 Border 型画刷 + 全透明填充 = 只画边，不依赖任何贴图，绝不会"画不出来"）。
			UOverlay* BagOverlay = MakeHotbarWidget<UOverlay>(WidgetTree);
			BagOverlay->AddChildToOverlay(BagIcon);

			UBorder* BagFrame = MakeHotbarWidget<UBorder>(WidgetTree, TEXT("BackpackButtonFrame"));
			{
				FSlateBrush FrameBrush;
				FrameBrush.DrawAs = ESlateBrushDrawType::Border;
				FrameBrush.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f)); // 填充全透明，只留描边
				FrameBrush.OutlineSettings.Color = FSlateColor(FLinearColor(0.86f, 0.72f, 0.42f, 0.95f)); // 米金色，和 HUD 一致
				FrameBrush.OutlineSettings.Width = 2.f;
				FrameBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
				FrameBrush.OutlineSettings.CornerRadii = FVector4(6.f, 6.f, 6.f, 6.f);
				BagFrame->SetBrush(FrameBrush);
				BagFrame->SetPadding(FMargin(0.f));
				BagFrame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			BagOverlay->AddChildToOverlay(BagFrame);

			BagIconBox->AddChild(BagOverlay);
			BackpackButton->AddChild(BagIconBox);
		}
		else
		{
			BackpackButtonLabel = MakeHotbarWidget<UTextBlock>(WidgetTree, TEXT("BackpackButtonLabel"));
			BackpackButtonLabel->SetText(FText::FromString(TEXT("包")));
			BackpackButtonLabel->SetFont(BackpackFont);
			BackpackButtonLabel->SetJustification(ETextJustify::Center);
			BackpackButtonLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.87f, 0.74f, 1.f)));
			BackpackButton->AddChild(BackpackButtonLabel);
		}
	}
}

void UMetamorphosisHotbarWidget::CacheWidgets()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (HotbarRow == nullptr)
	{
		HotbarRow = WidgetTree->FindWidget<UHorizontalBox>(TEXT("HotbarRow"));
	}
	if (BackpackButton == nullptr)
	{
		BackpackButton = WidgetTree->FindWidget<UButton>(TEXT("BackpackButton"));
	}
	if (BackpackButtonLabel == nullptr)
	{
		BackpackButtonLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("BackpackButtonLabel"));
	}

	// 没有设计器树时上面已经建好了；这个分支只服务 WBP 子类
	SlotButtons.Reset();
	SlotIcons.Reset();
	SlotCounts.Reset();
	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		UButton* Button = WidgetTree->FindWidget<UButton>(*FString::Printf(TEXT("Slot_%d"), Index));
		UImage* Icon = WidgetTree->FindWidget<UImage>(*FString::Printf(TEXT("SlotIcon_%d"), Index));
		UTextBlock* Count = WidgetTree->FindWidget<UTextBlock>(*FString::Printf(TEXT("SlotCount_%d"), Index));
		SlotButtons.Add(Button);
		SlotIcons.Add(Icon);
		SlotCounts.Add(Count);
	}

	if (bBuiltLayout)
	{
		return;
	}

	// WBP 子类：把每格的"外框/格底"找出来（按 SlotEdge_i / SlotArt_i 命名），没有就退化成只用图标/数量
	SlotWidgets.Reset();
	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		FMetamorphosisHotbarSlotWidgets Widgets;
		Widgets.Button = SlotButtons.IsValidIndex(Index) ? SlotButtons[Index] : nullptr;
		Widgets.Icon = SlotIcons.IsValidIndex(Index) ? SlotIcons[Index] : nullptr;
		Widgets.Count = SlotCounts.IsValidIndex(Index) ? SlotCounts[Index] : nullptr;
		Widgets.EdgeBorder = WidgetTree->FindWidget<UBorder>(*FString::Printf(TEXT("SlotEdge_%d"), Index));
		Widgets.SlotArt = WidgetTree->FindWidget<UImage>(*FString::Printf(TEXT("SlotArt_%d"), Index));
		SlotWidgets.Add(Widgets);
	}
}

UTexture2D* UMetamorphosisHotbarWidget::GetSlotArtTexture(bool bHovered) const
{
	const TSoftObjectPtr<UTexture2D>& Ref = bHovered ? SlotHoverTexture : SlotDefaultTexture;
	return Ref.IsNull() ? nullptr : Ref.LoadSynchronous();
}

void UMetamorphosisHotbarWidget::ApplyStaticStyle()
{
	for (UTextBlock* Count : SlotCounts)
	{
		if (Count != nullptr)
		{
			Count->SetFont(CountFont);
			Count->SetColorAndOpacity(FSlateColor(CountColor));
		}
	}

	if (BackpackButtonLabel != nullptr)
	{
		// 图标拿不到时的兜底文字（一个「包」字，56px 圆里放得下）
		BackpackButtonLabel->SetText(FText::FromString(TEXT("包")));
		if (BackpackFont.HasValidFont())
		{
			BackpackButtonLabel->SetFont(BackpackFont);
		}
	}
}

// ---------------------------------------------------------------------------
// 数据刷新
// ---------------------------------------------------------------------------

void UMetamorphosisHotbarWidget::ApplyScale(float Scale)
{
	CurrentScale = FMath::Clamp(Scale, 0.3f, 3.f);

	const float ScaledSlotWidth = SlotSize * CurrentScale;
	const float ScaledSlotHeight = SlotSize * SlotHeightRatio * CurrentScale;
	const float ScaledIconSize = FMath::Min(ScaledSlotWidth, ScaledSlotHeight) * FMath::Clamp(IconSizeRatio, 0.1f, 1.f);

	for (FMetamorphosisHotbarSlotWidgets& Widgets : SlotWidgets)
	{
		if (Widgets.SlotBox != nullptr)
		{
			Widgets.SlotBox->SetWidthOverride(ScaledSlotWidth);
			Widgets.SlotBox->SetHeightOverride(ScaledSlotHeight);
		}
		if (Widgets.IconBox != nullptr)
		{
			Widgets.IconBox->SetWidthOverride(ScaledIconSize);
			Widgets.IconBox->SetHeightOverride(ScaledIconSize);
		}
	}

	// 数量角标字号跟着缩放（找不到字体就保持原样）
	if (CountFont.HasValidFont())
	{
		FSlateFontInfo ScaledFont = CountFont;
		ScaledFont.Size = FMath::Max(8, FMath::RoundToInt(16.f * CurrentScale));
		for (TObjectPtr<UTextBlock>& Count : SlotCounts)
		{
			if (Count != nullptr)
			{
				Count->SetFont(ScaledFont);
			}
		}
	}
	if (BackpackFont.HasValidFont())
	{
		FSlateFontInfo ScaledBagFont = BackpackFont;
		ScaledBagFont.Size = FMath::Max(8, FMath::RoundToInt(16.f * CurrentScale));
		if (BackpackButtonLabel != nullptr)
		{
			BackpackButtonLabel->SetFont(ScaledBagFont);
		}
	}
}

UMetamorphosisInventoryComponent* UMetamorphosisHotbarWidget::GetInventory() const
{
	const APlayerController* Controller = GetOwningPlayer();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	return Pawn != nullptr ? Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>() : nullptr;
}

void UMetamorphosisHotbarWidget::RefreshFromInventory()
{
	UMetamorphosisInventoryComponent* Inventory = GetInventory();
	const bool bHasInventory = Inventory != nullptr;

	if (bHasInventory == bLastHadInventory
		&& (!bHasInventory || Inventory->GetSelectedIndex() == LastSelectedIndex))
	{
		// 内容变化也要刷（道具/数量/图标都会变）
		bool bContentChanged = LastOccupied.Num() != UMetamorphosisInventoryComponent::NumSlots;
		if (!bContentChanged && bHasInventory)
		{
			for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
			{
				const FMetamorphosisItemSlot ItemSlot = Inventory->GetSlot(Index);
				if (ItemSlot.bOccupied != LastOccupied[Index]
					|| ItemSlot.Count != LastCounts[Index]
					|| ItemSlot.ItemId != LastItemIds[Index]
					|| ItemSlot.Icon != LastIcons[Index])
				{
					bContentChanged = true;
					break;
				}
			}
		}

		if (!bContentChanged)
		{
			return;
		}
	}

	bLastHadInventory = bHasInventory;
	LastSelectedIndex = bHasInventory ? Inventory->GetSelectedIndex() : -1;
	LastOccupied.SetNum(UMetamorphosisInventoryComponent::NumSlots);
	LastCounts.SetNum(UMetamorphosisInventoryComponent::NumSlots);
	LastItemIds.SetNum(UMetamorphosisInventoryComponent::NumSlots);
	LastIcons.SetNum(UMetamorphosisInventoryComponent::NumSlots);

	for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
	{
		FMetamorphosisHotbarSlotWidgets* Widgets = SlotWidgets.IsValidIndex(Index) ? &SlotWidgets[Index] : nullptr;
		const FMetamorphosisItemSlot ItemSlot = bHasInventory
			? Inventory->GetSlot(Index)
			: FMetamorphosisItemSlot();

		LastOccupied[Index] = ItemSlot.bOccupied;
		LastCounts[Index] = ItemSlot.Count;
		LastItemIds[Index] = ItemSlot.ItemId;
		LastIcons[Index] = ItemSlot.Icon;

		const bool bSelected = bHasInventory && (Index == LastSelectedIndex);

		if (Widgets != nullptr)
		{
			// 选中框：选中变白（MC 的高亮框），未选中透明
			if (Widgets->EdgeBorder != nullptr)
			{
				Widgets->EdgeBorder->SetBrush(MakeHotbarBrush(
					bSelected ? SelectionColor : FLinearColor(0.f, 0.f, 0.f, 0.f)));
			}

			// 美术格底：选中换成悬停版（浅纸底）
			if (Widgets->SlotArt != nullptr)
			{
				if (UTexture2D* ArtTexture = GetSlotArtTexture(bSelected))
				{
					Widgets->SlotArt->SetBrush(MakeHotbarTextureBrush(
						ArtTexture, SlotTextureMargin,
						FVector2D(SlotSize * CurrentScale, SlotSize * SlotHeightRatio * CurrentScale)));
				}
			}

			if (Widgets->Icon != nullptr)
			{
				Widgets->Icon->SetVisibility(ItemSlot.bOccupied ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);

				// 图标：优先美术图（/Game/UI/Items/T_Item_<ItemId>）。
				// 拿不到就退回配表里的 PlaceholderColor 纯色块 —— 美术图没到之前每个道具也是可区分的。
				UTexture2D* IconTexture = ItemSlot.bOccupied ? GetInventory() ? GetInventory()->GetItemIconResolved(ItemSlot.ItemId) : nullptr : nullptr;
				if (IconTexture != nullptr)
				{
					Widgets->Icon->SetBrushFromTexture(IconTexture, false);
					Widgets->Icon->SetColorAndOpacity(FLinearColor::White);
				}
				else
				{
					Widgets->Icon->SetBrush(MakeHotbarBrush(FLinearColor::White));
					Widgets->Icon->SetColorAndOpacity(ItemSlot.PlaceholderColor);
				}
			}

			if (Widgets->Count != nullptr)
			{
				const bool bShowCount = ItemSlot.bOccupied && ItemSlot.Count > 1;
				Widgets->Count->SetVisibility(bShowCount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
				if (bShowCount)
				{
					Widgets->Count->SetText(FText::AsNumber(ItemSlot.Count));
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// 点击
// ---------------------------------------------------------------------------

void UMetamorphosisHotbarWidget::HandleSlotClicked(int32 Index)
{
	if (UMetamorphosisInventoryComponent* Inventory = GetInventory())
	{
		Inventory->SelectSlot(Index);
		LastSelectedIndex = -1;   // 强制下一帧刷新
	}
}

void UMetamorphosisHotbarWidget::HandleSlotClicked0() { HandleSlotClicked(0); }
void UMetamorphosisHotbarWidget::HandleSlotClicked1() { HandleSlotClicked(1); }
void UMetamorphosisHotbarWidget::HandleSlotClicked2() { HandleSlotClicked(2); }
void UMetamorphosisHotbarWidget::HandleSlotClicked3() { HandleSlotClicked(3); }
void UMetamorphosisHotbarWidget::HandleSlotClicked4() { HandleSlotClicked(4); }
void UMetamorphosisHotbarWidget::HandleSlotClicked5() { HandleSlotClicked(5); }
void UMetamorphosisHotbarWidget::HandleSlotClicked6() { HandleSlotClicked(6); }
void UMetamorphosisHotbarWidget::HandleSlotClicked7() { HandleSlotClicked(7); }

void UMetamorphosisHotbarWidget::HandleBackpackClicked()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 点了背包按钮"));
	OnBackpackRequested.Broadcast();
}

#undef LOCTEXT_NAMESPACE
