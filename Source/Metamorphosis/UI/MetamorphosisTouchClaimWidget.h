// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 可交互 UI 的基类：**告诉控制器"这根手指是点在我身上的"**。
//
// 为什么需要它：
//   移动摇杆是在 PlayerController 里轮询触摸状态的，而"屏幕左半边 = 摇杆"这种粗分区
//   会把点在快捷栏/按钮上的手指也当成摇杆（表现：按钮点不到，人物还自己跑了）。
//   与其在控制器里猜"这个位置有没有 UI"（要处理 DPI/窗口坐标换算，很容易错），
//   不如让 UI 自己认领：谁被点到谁负责。
//
// 注意：只**观察**触摸，不吃掉事件 —— 返回值仍是 Super 的结果，按钮的点击照常工作。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MetamorphosisTouchClaimWidget.generated.h"

UCLASS(Abstract)
class METAMORPHOSIS_API UMetamorphosisTouchClaimWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
};
