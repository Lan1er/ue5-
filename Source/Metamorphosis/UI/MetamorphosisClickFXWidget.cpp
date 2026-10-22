// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisClickFXWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/Texture2D.h"
#include "Metamorphosis.h"
#include "Styling/SlateBrush.h"

namespace
{
	/** 本控件专用的画刷（缩放模式：不做九宫格，保持美术原比例、中心对齐） */
	void MakeClickBurstBrush(FSlateBrush& OutBrush, UTexture2D* Texture, const FLinearColor& Tint)
	{
		OutBrush = FSlateBrush();
		OutBrush.DrawAs = ESlateBrushDrawType::Image;
		OutBrush.SetResourceObject(Texture);
		OutBrush.TintColor = FSlateColor(Tint);
		OutBrush.ImageSize = FVector2D(Texture != nullptr ? Texture->GetSizeX() : 320.f,
			Texture != nullptr ? Texture->GetSizeY() : 320.f);
	}
}

UMetamorphosisClickFXWidget::UMetamorphosisClickFXWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 覆盖层不参与点击命中：本控件是纯视觉反馈，绝不能挡输入
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

bool UMetamorphosisClickFXWidget::Initialize()
{
	const bool bResult = Super::Initialize();

	// 设计器树为空 => 纯 C++ 类，自己搭一棵默认布局（必须在这里搭，放 NativeConstruct 会尺寸恒 0）
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildClickFXLayout();
	}

	LoadFrames();
	return bResult;
}

void UMetamorphosisClickFXWidget::BuildClickFXLayout()
{
	// 根：全屏 Overlay（SelfHitTestInvisible），里面再放一个 CanvasPanel 用来按绝对坐标摆 burst
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ClickFXRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	BurstCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ClickFXCanvas"));
	BurstCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UOverlaySlot* OverlaySlot = Root->AddChildToOverlay(BurstCanvas))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 点击反馈控件已搭好布局（全屏 Overlay + Canvas，SelfHitTestInvisible 不挡输入）"));
}

void UMetamorphosisClickFXWidget::LoadFrames()
{
	if (bTriedLoadFrames || FrameCount <= 0)
	{
		return;
	}
	bTriedLoadFrames = true;

	Frames.Reset();
	for (int32 Index = 0; Index < FrameCount; ++Index)
	{
		const FString AssetPath = FString::Printf(TEXT("%s%03d"), *FrameFolder, Index);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath))
		{
			Frames.Add(Texture);
		}
	}

	if (Frames.Num() == 0)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] 点击反馈素材没找到（%s000..%03d）：点击不会有点击特效。请检查 /Game/UI/Click 是否已导入。"),
			*FrameFolder, FMath::Max(0, FrameCount - 1));
	}
	else
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 点击反馈：载入 %d/%d 帧（尺寸 %g，时长 %.2f 秒）"),
			Frames.Num(), FrameCount, BurstSize, Duration);
	}
}

void UMetamorphosisClickFXWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	LoadFrames();
}

void UMetamorphosisClickFXWidget::NativeDestruct()
{
	StopAll();
	Super::NativeDestruct();
}

void UMetamorphosisClickFXWidget::ApplyConfig(float InBurstSize, float InDurationSeconds, int32 InMaxAlive,
	float InMinIntervalSeconds, float InMinDistance, const FString& InFrameFolder, int32 InFrameCount)
{
	BurstSize = FMath::Max(8.f, InBurstSize);
	Duration = FMath::Max(0.05f, InDurationSeconds);
	MaxAlive = FMath::Max(1, InMaxAlive);
	MinInterval = FMath::Max(0.f, InMinIntervalSeconds);
	MinDistance = FMath::Max(0.f, InMinDistance);

	if (!InFrameFolder.IsEmpty() && InFrameFolder != FrameFolder)
	{
		FrameFolder = InFrameFolder;
		bTriedLoadFrames = false;
	}
	if (InFrameCount > 0 && InFrameCount != FrameCount)
	{
		FrameCount = InFrameCount;
		bTriedLoadFrames = false;
	}

	LoadFrames();
}

bool UMetamorphosisClickFXWidget::PlayAt(const FVector2D& LocalPosition)
{
	if (BurstCanvas == nullptr)
	{
		return false;
	}
	if (Frames.Num() == 0)
	{
		// 素材没导入：只记一次日志，不刷屏
		return false;
	}

	// 同位置限频：避免一次按下被鼠标/触摸两条链路各播一次、或连点刷屏
	if ((Clock - LastSpawnTime) < MinInterval
		&& FVector2D::Distance(LocalPosition, LastSpawnCenter) <= MinDistance)
	{
		return false;
	}

	// 并发上限：先清掉最老的
	while (Bursts.Num() >= MaxAlive)
	{
		RemoveBurstAt(0);
	}

	UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	Image->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f));

	FSlateBrush Brush;
	MakeClickBurstBrush(Brush, Frames[0], FLinearColor::White);
	Image->SetBrush(Brush);

	if (UCanvasPanelSlot* PanelSlot = BurstCanvas->AddChildToCanvas(Image))
	{
		// 美术规定"对齐点是画布正中心"：所以位置 = 中心点 - 半尺寸
		const float Half = BurstSize * 0.5f;
		PanelSlot->SetAutoSize(false);
		PanelSlot->SetSize(FVector2D(BurstSize, BurstSize));
		PanelSlot->SetPosition(FVector2D(LocalPosition.X - Half, LocalPosition.Y - Half));
		// 画在其它 UI 之上（本控件自身 ZOrder 由 PlayerController 决定）
		PanelSlot->SetZOrder(Bursts.Num());
	}

	FBurst Burst;
	Burst.Image = Image;
	Burst.Center = LocalPosition;
	Burst.Elapsed = 0.f;
	Bursts.Add(Burst);

	LastSpawnTime = Clock;
	LastSpawnCenter = LocalPosition;

	UE_LOG(LogMetamorphosis, Verbose, TEXT("[UI] 点击反馈：在 (%.0f, %.0f) 播放（存活 %d/%d）"),
		LocalPosition.X, LocalPosition.Y, Bursts.Num(), MaxAlive);

	return true;
}

void UMetamorphosisClickFXWidget::StopAll()
{
	for (int32 Index = Bursts.Num() - 1; Index >= 0; --Index)
	{
		RemoveBurstAt(Index);
	}
	Bursts.Reset();
}

void UMetamorphosisClickFXWidget::RemoveBurstAt(int32 Index)
{
	if (!Bursts.IsValidIndex(Index))
	{
		return;
	}
	if (UImage* Image = Bursts[Index].Image.Get())
	{
		Image->RemoveFromParent();
	}
	Bursts.RemoveAtSwap(Index);
}

void UMetamorphosisClickFXWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	Clock += InDeltaTime;

	if (Bursts.Num() == 0 || Frames.Num() == 0)
	{
		return;
	}

	const float SafeDuration = FMath::Max(0.05f, Duration);
	const int32 TotalFrames = Frames.Num();

	for (int32 Index = Bursts.Num() - 1; Index >= 0; --Index)
	{
		FBurst& Burst = Bursts[Index];
		Burst.Elapsed += InDeltaTime;

		if (Burst.Elapsed >= SafeDuration)
		{
			RemoveBurstAt(Index);
			continue;
		}

		UImage* Image = Burst.Image.Get();
		if (Image == nullptr)
		{
			Bursts.RemoveAtSwap(Index);
			continue;
		}

		// 逐帧推进：按"总时长 / 帧数"均匀取帧（美术规格 15 帧 / 500ms / 30fps）
		const float Progress = FMath::Clamp(Burst.Elapsed / SafeDuration, 0.f, 0.999f);
		const int32 FrameIndex = FMath::Clamp(FMath::FloorToInt(Progress * TotalFrames), 0, TotalFrames - 1);
		if (UTexture2D* Texture = Frames[FrameIndex])
		{
			FSlateBrush Brush;
			MakeClickBurstBrush(Brush, Texture, FLinearColor::White);
			Image->SetBrush(Brush);
		}

		// 末段淡出（美术末几帧本来就是全透明，这里再补一个整体淡出保证消失干净）
		const float FadeStart = 0.72f;
		const float Alpha = Progress <= FadeStart ? 1.f : FMath::Clamp(1.f - (Progress - FadeStart) / (1.f - FadeStart), 0.f, 1.f);
		Image->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Alpha));
	}
}
