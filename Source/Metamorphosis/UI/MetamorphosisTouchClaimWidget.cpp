// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "UI/MetamorphosisTouchClaimWidget.h"

#include "MetamorphosisPlayerController.h"
#include "Metamorphosis.h"

FReply UMetamorphosisTouchClaimWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->NotifyUITouchClaimed(InGestureEvent.GetPointerIndex());
	}

	// 不吃事件：按钮、滑条该响还得响
	return Super::NativeOnTouchStarted(InGeometry, InGestureEvent);
}

FReply UMetamorphosisTouchClaimWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	// 手指在 UI 上滑动时继续保持认领（避免中途被摇杆抢走）
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->NotifyUITouchClaimed(InGestureEvent.GetPointerIndex());
	}

	return Super::NativeOnTouchMoved(InGeometry, InGestureEvent);
}

FReply UMetamorphosisTouchClaimWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (AMetamorphosisPlayerController* Controller = Cast<AMetamorphosisPlayerController>(GetOwningPlayer()))
	{
		Controller->NotifyUITouchReleased(InGestureEvent.GetPointerIndex());
	}

	return Super::NativeOnTouchEnded(InGeometry, InGestureEvent);
}
