// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 移动 / 视角 / 升空 的代码内输入定义（不依赖任何二进制输入资源）
//
// PC 调试键位：
//   W A S D   移动（S 在地面 = 后退；在空中 = 下降）
//   空格      上升 / 起飞    双击空格 = 立刻落地
//   Q / 鼠标左右 / 手柄右摇杆   水平转视角（左转）
//   E         水平转视角（右转）
//   R         交互 / 拾取（2026-10-14 起：交互从 E 改成 R，E 还给"右转视角"；
//             更早一版 E 曾是交互、右转挪到 → 键，那一版已废弃）
//   1..9、0   直选快捷栏第 N 格；滚轮翻页
// 移动端：
//   左虚拟摇杆 -> Gamepad_LeftX/LeftY（移动）
//   右虚拟摇杆 -> Gamepad_RightX（水平转视角，备用）
//   手指在屏幕上左右搓 -> 控制器直接处理触摸（InputTouch），见 MetamorphosisPlayerController
//   交互 -> 点交互提示上的**大圆按钮**（MetamorphosisInteractPromptWidget 的移动端外观），不需要按键
// 手柄：左摇杆移动、右摇杆转视角、A 上升、B 下降。
//
// 注意：UObject 的 CDO 构造函数里不能 NewObject（会崩），所以映射在 Get() 里惰性构建。

#pragma once

#include "CoreMinimal.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "MetamorphosisMoveInput.generated.h"

/** 移动输入动作：二维轴（X 左右 / Y 前后） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Move : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Move()
	{
		ValueType = EInputActionValueType::Axis2D;
	}
};

/** 视角水平旋转：一维轴（正 = 右转） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Look : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Look()
	{
		ValueType = EInputActionValueType::Axis1D;
	}
};

/** 上升 / 起飞（按住持续上升；双击 = 落地） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Ascend : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Ascend()
	{
		ValueType = EInputActionValueType::Boolean;
	}
};

/** 下降（空中按住持续下降，贴地即落地） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Descend : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Descend()
	{
		ValueType = EInputActionValueType::Boolean;
	}
};

/** 触摸按住（移动端手指搓屏幕转视角的判定；位置由控制器轮询） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_TouchLook : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_TouchLook()
	{
		ValueType = EInputActionValueType::Boolean;
	}
};

/** 快捷栏切换（滚轮 / 手柄左右方向键）：值 > 0 往右一格，< 0 往左一格 */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_CycleSlot : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_CycleSlot()
	{
		ValueType = EInputActionValueType::Axis1D;
	}
};

/** 快捷栏直选（数字键 1..8 各一个实例；移动端点击格子由 UI 直接调组件，不走这里） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Slot : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Slot()
	{
		ValueType = EInputActionValueType::Boolean;
	}
};

/** 交互 / 拾取（PC = R；移动端点交互提示上的圆徽标，不走这里） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Interact : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Interact()
	{
		ValueType = EInputActionValueType::Boolean;
	}
};

/** 打开 / 关闭背包（PC = B；移动端点 HUD 上的背包按钮，不走这里） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputAction_Backpack : public UInputAction
{
	GENERATED_BODY()

public:
	UMetamorphosisInputAction_Backpack()
	{
		ValueType = EInputActionValueType::Boolean;
	}
};

/** 输入映射上下文（代码内定义，通过 Get() 获取） */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisInputMappingContext_Move : public UInputMappingContext
{
	GENERATED_BODY()

public:
	/** 快捷栏格子数（要与 UMetamorphosisInventoryComponent::NumSlots 一致） */
	static constexpr int32 NumSlotKeys = 8;

	/** 获取映射上下文（惰性构建，进程内唯一，不会被 GC） */
	static UInputMappingContext* Get();

	/** 与映射上下文配对的各动作实例（绑定输入事件时必须用同一个对象） */
	static UInputAction* GetMoveAction();
	static UInputAction* GetLookAction();
	static UInputAction* GetAscendAction();
	static UInputAction* GetDescendAction();
	static UInputAction* GetTouchLookAction();

	/** 快捷栏滚轮切换动作 */
	static UInputAction* GetCycleSlotAction();

	/** 快捷栏第 SlotIndex 格（0 起）对应的按键动作；越界返回 nullptr */
	static UInputAction* GetSlotAction(int32 SlotIndex);

	/** 交互 / 拾取（PC = R） */
	static UInputAction* GetInteractAction();

	/** 打开 / 关闭背包（PC = B） */
	static UInputAction* GetBackpackAction();
};
