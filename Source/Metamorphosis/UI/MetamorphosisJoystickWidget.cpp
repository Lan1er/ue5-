// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisJoystickWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Metamorphosis.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FSlateBrush MakeJoystickBrush(const FLinearColor& Color)
	{
		return FSlateColorBrush(Color);
	}
}

UMetamorphosisJoystickWidget::UMetamorphosisJoystickWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ---- 交付 2「实机界面」的摇杆素材 ----
	// 美术底圈本身画了四个方向箭头 + 中心凹陷 + 「移动」字样，所以：
	//   * 底盘直接用美术图（不透明度拉满，否则金线看不清）
	//   * 摇杆头美术没单独给，仍用纯色圆，颜色取美术的深墨绿，压在中心凹陷上刚好
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> BaseFinder(
			TEXT("/Game/UI/Delivery2/Buttons/T_Joystick_Default.T_Joystick_Default"));
		if (BaseFinder.Succeeded())
		{
			BaseBrush.SetResourceObject(BaseFinder.Object);
			BaseBrush.ImageSize = FVector2D(BaseFinder.Object->GetSurfaceWidth(), BaseFinder.Object->GetSurfaceHeight());
			BaseBrush.DrawAs = ESlateBrushDrawType::Image;
			BaseBrush.ImageType = ESlateBrushImageType::FullColor;
			BaseOpacity = 1.f;
			BaseColor = FLinearColor::White;
			// 美术底圈 306x303，可操作区域约等于整图：半径给到 132 让触摸范围与画面一致
			BaseRadius = 132.f;
			KnobRadius = 46.f;
			KnobColor = FLinearColor(0.10f, 0.14f, 0.10f, 1.f);
			KnobOpacity = 0.92f;
		}
	}
}

bool UMetamorphosisJoystickWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultLayout();
		bBuiltLayout = true;
	}

	return bResult;
}

void UMetamorphosisJoystickWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 纯显示层：不吃任何触摸（触摸由控制器统一处理）。
	// 注意：控件本身**不能**设成 Collapsed —— 那会把整棵子树一起藏掉；
	// 显示/隐藏只由 UpdateVisual 控制"底盘/摇杆头"这两个子控件。
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMetamorphosisJoystickWidget::BuildDefaultLayout()
{
	// 根必须是铺满屏幕的 Overlay：
	// UUserWidget 加进视口时槽位是按"期望尺寸"排的，如果根直接是 CanvasPanel，
	// 画布就只有子控件那么大，绝对定位到屏幕中下部的圆会被裁掉（踩过）。
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("JoystickRoot"));
	WidgetTree->RootWidget = Root;

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("JoystickCanvas"));
	if (UOverlaySlot* CanvasSlot = Root->AddChildToOverlay(Canvas))
	{
		CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
		CanvasSlot->SetVerticalAlignment(VAlign_Fill);
	}

	BaseSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("JoystickBaseBox"));
	BaseSizeBox->SetWidthOverride(BaseRadius * 2.f);
	BaseSizeBox->SetHeightOverride(BaseRadius * 2.f);
	if (UCanvasPanelSlot* BaseSlotAdded = Canvas->AddChildToCanvas(BaseSizeBox))
	{
		BaseSlotAdded->SetAnchors(FAnchors(0.f, 0.f));
		BaseSlotAdded->SetAutoSize(false);
		BaseSlotAdded->SetSize(FVector2D(BaseRadius * 2.f, BaseRadius * 2.f));
		BaseSlotAdded->SetAlignment(FVector2D(0.5f, 0.5f));
	}

	BaseImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("JoystickBase"));
	BaseImage->SetBrush(BaseBrush.GetResourceObject() != nullptr ? BaseBrush : MakeJoystickBrush(FLinearColor(BaseColor.R, BaseColor.G, BaseColor.B, BaseOpacity)));
	BaseSizeBox->AddChild(BaseImage);

	KnobSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("JoystickKnobBox"));
	KnobSizeBox->SetWidthOverride(KnobRadius * 2.f);
	KnobSizeBox->SetHeightOverride(KnobRadius * 2.f);
	if (UCanvasPanelSlot* KnobSlotAdded = Canvas->AddChildToCanvas(KnobSizeBox))
	{
		KnobSlotAdded->SetAnchors(FAnchors(0.f, 0.f));
		KnobSlotAdded->SetAutoSize(false);
		KnobSlotAdded->SetSize(FVector2D(KnobRadius * 2.f, KnobRadius * 2.f));
		KnobSlotAdded->SetAlignment(FVector2D(0.5f, 0.5f));
	}

	KnobImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("JoystickKnob"));
	KnobImage->SetBrush(KnobBrush.GetResourceObject() != nullptr ? KnobBrush : MakeJoystickBrush(FLinearColor(KnobColor.R, KnobColor.G, KnobColor.B, KnobOpacity)));
	KnobSizeBox->AddChild(KnobImage);
}

void UMetamorphosisJoystickWidget::UpdateVisual(bool bActive, bool bFloating, const FVector2D& Center,
	const FVector2D& KnobOffset, float Scale)
{
	if (BaseSizeBox == nullptr || KnobSizeBox == nullptr || BaseImage == nullptr || KnobImage == nullptr)
	{
		return;
	}

	CurrentScale = FMath::Max(0.1f, Scale);

	// 显示状态变化时打一条日志，方便确认"摇杆到底画没画"
	if (bActive != bLastLoggedActive)
	{
		bLastLoggedActive = bActive;
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[UI] 摇杆%s：中心=(%.0f, %.0f)px 偏移=(%.0f, %.0f)px 半径=%.0f Slate单位"),
			bActive ? TEXT("显示") : TEXT("隐藏"),
			Center.X, Center.Y, KnobOffset.X, KnobOffset.Y, BaseRadius * CurrentScale);

		if (bActive)
		{
			const FVector2D MySize = GetCachedGeometry().GetLocalSize();
			const FVector2D BaseSize = BaseSizeBox->GetCachedGeometry().GetLocalSize();
			const FVector2D BasePos = BaseSizeBox->GetCachedGeometry().GetAbsolutePosition();
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[UI] 摇杆诊断：在视口=%d 可见性=%d 控件尺寸=(%.0f, %.0f) 底盘尺寸=(%.0f, %.0f) 底盘绝对位置=(%.0f, %.0f) 底盘可见性=%d"),
				IsInViewport() ? 1 : 0, static_cast<int32>(GetVisibility()),
				MySize.X, MySize.Y, BaseSize.X, BaseSize.Y, BasePos.X, BasePos.Y,
				static_cast<int32>(BaseSizeBox->GetVisibility()));
		}
	}

	// 显示/隐藏：
	//  固定模式（bFloating=false）默认**常显底盘** —— 否则玩家看不到摇杆在哪、不知道该按哪儿
	//  （触发区是围绕那个圆的，看不见就容易觉得"摇杆没出来"）。
	//  浮动模式只在按住时显示。摇杆头只在操控时显示。
	const bool bShowBase = bActive || (!bFloating && bShowBaseWhenIdle);
	const bool bShowKnob = bActive;

	BaseSizeBox->SetVisibility(bShowBase ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	KnobSizeBox->SetVisibility(bShowKnob ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (!bShowBase)
	{
		return;
	}

	// 尺寸随设置缩放
	const float Radius = BaseRadius * CurrentScale;
	const float Knob = KnobRadius * CurrentScale;
	BaseSizeBox->SetWidthOverride(Radius * 2.f);
	BaseSizeBox->SetHeightOverride(Radius * 2.f);
	KnobSizeBox->SetWidthOverride(Knob * 2.f);
	KnobSizeBox->SetHeightOverride(Knob * 2.f);

	// 画法：用 RoundedBox 画圆（纯代码，不需要美术资源）。
	// 注意别用"半透明白色方块"——叠在浅色地板上几乎看不见（踩过）。
	//   底盘 = 半透明深色圆 + 白色描边；摇杆头 = 高不透明度的浅色圆
	if (BaseBrush.GetResourceObject() != nullptr)
	{
		BaseImage->SetBrush(BaseBrush);
	}
	else
	{
		BaseImage->SetBrush(FSlateRoundedBoxBrush(
			FLinearColor(0.03f, 0.03f, 0.05f, 0.30f * BaseOpacity / FMath::Max(BaseOpacity, 0.01f)),
			Radius,
			FLinearColor(BaseColor.R, BaseColor.G, BaseColor.B, 0.85f),
			3.f));
	}

	if (KnobBrush.GetResourceObject() != nullptr)
	{
		KnobImage->SetBrush(KnobBrush);
	}
	else
	{
		KnobImage->SetBrush(FSlateRoundedBoxBrush(
			FLinearColor(KnobColor.R, KnobColor.G, KnobColor.B, 0.75f),
			Knob));
	}

	if (UCanvasPanelSlot* BaseSlot = Cast<UCanvasPanelSlot>(BaseSizeBox->Slot))
	{
		BaseSlot->SetSize(FVector2D(Radius * 2.f, Radius * 2.f));
		// 用 DPI 反缩放，保证屏幕像素尺寸与控制器算的一致
		const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
		const float SafeScale = ViewportScale > 0.f ? ViewportScale : 1.f;
		BaseSlot->SetPosition(Center / SafeScale);
	}

	if (UCanvasPanelSlot* KnobSlot = Cast<UCanvasPanelSlot>(KnobSizeBox->Slot))
	{
		KnobSlot->SetSize(FVector2D(Knob * 2.f, Knob * 2.f));
		const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
		const float SafeScale = ViewportScale > 0.f ? ViewportScale : 1.f;
		KnobSlot->SetPosition((Center + KnobOffset) / SafeScale);
	}

}




