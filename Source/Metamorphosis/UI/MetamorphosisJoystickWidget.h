// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 移动端移动摇杆（纯显示层，触摸逻辑在 MetamorphosisPlayerController 里）。
//
// 为什么自己做：引擎自带的虚拟摇杆（UTouchInterface + SVirtualJoystick）只能在资源里写死位置和大小，
// 而且**不支持"浮动"**（手指按哪里摇杆就出现在哪里）。设置里要"摇杆是否固定 + 摇杆大小"，
// 所以这里自己画：底盘 + 摇杆头两个圆，位置与偏移每帧由控制器推过来。
//
// 设置项读取 UMetamorphosisSettingsSubsystem（bFixedJoystick / JoystickScale）。
// 美术换图：BaseBrush / KnobBrush（留空则用下面两个颜色画圆）。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "MetamorphosisJoystickWidget.generated.h"

class UImage;
class USizeBox;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisJoystickWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisJoystickWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	/**
	 *  由控制器每帧推送状态。
	 *  @param bActive    当前有没有手指在操控摇杆
	 *  @param bFloating  是否是浮动模式（底盘跟着手指走）
	 *  @param Center     底盘中心（屏幕像素坐标，左上角为原点）
	 *  @param KnobOffset 摇杆头相对底盘中心的偏移（像素，已按半径夹紧）
	 *  @param Scale      大小倍率（来自设置）
	 */
	void UpdateVisual(bool bActive, bool bFloating, const FVector2D& Center, const FVector2D& KnobOffset, float Scale);

	/** 底盘基准半径（像素，乘设置里的倍率） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Layout")
	float BaseRadius = 120.f;

	/** 摇杆头基准半径（像素） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Layout")
	float KnobRadius = 52.f;

	/** 底盘不透明度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Art")
	float BaseOpacity = 0.28f;

	/** 摇杆头不透明度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Art")
	float KnobOpacity = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Art")
	FLinearColor BaseColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Art")
	FLinearColor KnobColor = FLinearColor::White;

	/** 美术换图用（留空则用上面的颜色画实心圆/方块） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Art")
	FSlateBrush BaseBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Art")
	FSlateBrush KnobBrush;

	/** 不操控时是否隐藏（浮动模式建议隐藏；**固定模式建议常显**，否则玩家看不到摇杆在哪） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Layout")
	bool bHideWhenInactive = true;

	/** 固定模式下即使没在操控也把底盘画出来（默认开：让玩家看得见摇杆在哪） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Joystick|Layout")
	bool bShowBaseWhenIdle = true;

	/** 当前底盘半径（含倍率），控制器夹紧偏移时要用同一个值 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Joystick")
	float GetEffectiveRadius() const { return BaseRadius * CurrentScale; }

protected:
	void BuildDefaultLayout();

	UPROPERTY(Transient) TObjectPtr<USizeBox> BaseSizeBox;
	UPROPERTY(Transient) TObjectPtr<UImage> BaseImage;
	UPROPERTY(Transient) TObjectPtr<USizeBox> KnobSizeBox;
	UPROPERTY(Transient) TObjectPtr<UImage> KnobImage;

	float CurrentScale = 1.f;

	bool bBuiltLayout = false;

	/** 上一帧的显示状态（只在变化时打日志） */
	bool bLastLoggedActive = false;
};

