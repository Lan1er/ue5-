// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Input/MetamorphosisMoveInput.h"
#include "Metamorphosis.h"
#include "UObject/StrongObjectPtr.h"

namespace MetamorphosisMoveInput
{
	// 强引用静态持有，避免被 GC 回收（SubSystem 只在使用期间引用上下文/动作）
	static TStrongObjectPtr<UInputMappingContext> ContextInstance;
	static TStrongObjectPtr<UInputAction> MoveActionInstance;
	static TStrongObjectPtr<UInputAction> LookActionInstance;
	static TStrongObjectPtr<UInputAction> AscendActionInstance;
	static TStrongObjectPtr<UInputAction> DescendActionInstance;
	static TStrongObjectPtr<UInputAction> TouchLookActionInstance;
	static TStrongObjectPtr<UInputAction> CycleSlotActionInstance;
	static TStrongObjectPtr<UInputAction> InteractActionInstance;
	static TStrongObjectPtr<UInputAction> BackpackActionInstance;

	/** 8 个数字键各一个动作实例（顺序 = 格子顺序：1..8） */
	static TStrongObjectPtr<UInputAction> SlotActionInstances[UMetamorphosisInputMappingContext_Move::NumSlotKeys];
}

UInputMappingContext* UMetamorphosisInputMappingContext_Move::Get()
{
	using namespace MetamorphosisMoveInput;

	if (!ContextInstance.IsValid())
	{
		ContextInstance = TStrongObjectPtr<UInputMappingContext>(NewObject<UMetamorphosisInputMappingContext_Move>(GetTransientPackage()));
		MoveActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_Move>(GetTransientPackage()));
		LookActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_Look>(GetTransientPackage()));
		AscendActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_Ascend>(GetTransientPackage()));
		DescendActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_Descend>(GetTransientPackage()));
		TouchLookActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_TouchLook>(GetTransientPackage()));
		CycleSlotActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_CycleSlot>(GetTransientPackage()));
		InteractActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_Interact>(GetTransientPackage()));
		BackpackActionInstance = TStrongObjectPtr<UInputAction>(NewObject<UMetamorphosisInputAction_Backpack>(GetTransientPackage()));
		for (int32 SlotIndex = 0; SlotIndex < UMetamorphosisInputMappingContext_Move::NumSlotKeys; ++SlotIndex)
		{
			SlotActionInstances[SlotIndex] = TStrongObjectPtr<UInputAction>(
				NewObject<UMetamorphosisInputAction_Slot>(GetTransientPackage()));
		}

		UInputMappingContext* Ctx = ContextInstance.Get();
		UInputAction* Move = MoveActionInstance.Get();
		UInputAction* Look = LookActionInstance.Get();
		UInputAction* Ascend = AscendActionInstance.Get();
		UInputAction* Descend = DescendActionInstance.Get();
		UInputAction* TouchLook = TouchLookActionInstance.Get();
		UInputAction* CycleSlot = CycleSlotActionInstance.Get();
		UInputAction* Interact = InteractActionInstance.Get();
		UInputAction* Backpack = BackpackActionInstance.Get();

		// ---- 移动：键盘 WASD（PC 调试） ----
		FEnhancedActionKeyMapping& KeyD = Ctx->MapKey(Move, EKeys::D);
		KeyD.Modifiers.Add(NewObject<UInputModifierDeadZone>(Ctx));

		FEnhancedActionKeyMapping& KeyA = Ctx->MapKey(Move, EKeys::A);
		KeyA.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));

		FEnhancedActionKeyMapping& KeyW = Ctx->MapKey(Move, EKeys::W);
		KeyW.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));

		FEnhancedActionKeyMapping& KeyS = Ctx->MapKey(Move, EKeys::S);
		KeyS.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));
		KeyS.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));

		// ---- 移动：手柄左摇杆 / 移动端左虚拟摇杆（输出 LeftX / LeftY 模拟键） ----
		FEnhancedActionKeyMapping& StickX = Ctx->MapKey(Move, EKeys::Gamepad_LeftX);
		StickX.Modifiers.Add(NewObject<UInputModifierDeadZone>(Ctx));

		FEnhancedActionKeyMapping& StickY = Ctx->MapKey(Move, EKeys::Gamepad_LeftY);
		StickY.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));
		StickY.Modifiers.Add(NewObject<UInputModifierDeadZone>(Ctx));

		// ---- 视角水平旋转（值当"速率"用，配合相机 CameraYawSpeed） ----
		FEnhancedActionKeyMapping& StickLook = Ctx->MapKey(Look, EKeys::Gamepad_RightX);
		StickLook.Modifiers.Add(NewObject<UInputModifierDeadZone>(Ctx));

		// 键盘转视角：Q（左）+ E（右）。
		// ⚠️ 2026-10-14：交互从 E 改成 R（见下），E 还给「右转视角」。
		FEnhancedActionKeyMapping& KeyQ = Ctx->MapKey(Look, EKeys::Q);
		KeyQ.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
		Ctx->MapKey(Look, EKeys::E);

		// ---- 交互 / 拾取（PC = R；移动端走交互提示上的大圆按钮） ----
		Ctx->MapKey(Interact, EKeys::R);

		// ---- 背包（PC = B；移动端走 HUD 上的背包按钮） ----
		Ctx->MapKey(Backpack, EKeys::B);

		// 打印一次键位，换键后方便自证（尤其是"E 到底归谁"这种反复改过的键）
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Input] 键位就绪：移动 WASD / 视角 Q左 E右 + 鼠标左右 + 手柄右摇杆 / 交互 R / 背包 B / 上升 Space / 下降 X / 快捷栏 1-8 + 滚轮"));

		// 鼠标左右：原始值是像素增量，压小当速率用（想更灵敏就调这个 Scalar）
		FEnhancedActionKeyMapping& MouseLook = Ctx->MapKey(Look, EKeys::MouseX);
		UInputModifierScalar* MouseScale = NewObject<UInputModifierScalar>(Ctx);
		MouseScale->Scalar = FVector(0.05f, 0.f, 0.f);
		MouseLook.Modifiers.Add(MouseScale);

		// ---- 上升 / 下降 ----
		// 空格：上升、起飞；双击由控制器判定为"立刻落地"
		Ctx->MapKey(Ascend, EKeys::SpaceBar);
		Ctx->MapKey(Ascend, EKeys::Gamepad_FaceButton_Bottom);
		// X：下降（2026-10-09 从"S/摇杆下拉"改成独立按键 —— 摇杆下拉和 S 都还给"后退"）
		Ctx->MapKey(Descend, EKeys::X);
		Ctx->MapKey(Descend, EKeys::Gamepad_FaceButton_Right);
		// 移动端没有键盘，下降用右下角的「下」按钮（见 MetamorphosisMobileControlsWidget）

		// ---- 移动端：手指按住屏幕（位置由控制器轮询，用来搓屏幕转视角） ----
		// 注意：这一版引擎里触摸键只有数组形式，没有 EKeys::Touch1
		Ctx->MapKey(TouchLook, EKeys::TouchKeys[ETouchIndex::Touch1]);

		// ---- 快捷栏（8 格物品栏）切换 ----
		// 滚轮：一格一个脉冲，值 > 0 往右
		Ctx->MapKey(CycleSlot, EKeys::MouseWheelAxis);
		// 手柄左右方向键也能翻
		Ctx->MapKey(CycleSlot, EKeys::Gamepad_DPad_Right);
		FEnhancedActionKeyMapping& DPadLeft = Ctx->MapKey(CycleSlot, EKeys::Gamepad_DPad_Left);
		DPadLeft.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));

		// 数字键 1..8 直选（顺序 = 格子顺序）
		static const FKey SlotKeys[UMetamorphosisInputMappingContext_Move::NumSlotKeys] =
		{
			EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
			EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight
		};
		for (int32 SlotIndex = 0; SlotIndex < UMetamorphosisInputMappingContext_Move::NumSlotKeys; ++SlotIndex)
		{
			Ctx->MapKey(SlotActionInstances[SlotIndex].Get(), SlotKeys[SlotIndex]);
		}
	}

	return ContextInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetMoveAction()
{
	using namespace MetamorphosisMoveInput;
	Get(); // 确保动作与上下文一起构建
	return MoveActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetLookAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return LookActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetAscendAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return AscendActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetDescendAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return DescendActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetTouchLookAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return TouchLookActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetCycleSlotAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return CycleSlotActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetSlotAction(int32 SlotIndex)
{
	using namespace MetamorphosisMoveInput;
	Get();

	if (SlotIndex < 0 || SlotIndex >= NumSlotKeys)
	{
		return nullptr;
	}

	return SlotActionInstances[SlotIndex].Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetInteractAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return InteractActionInstance.Get();
}

UInputAction* UMetamorphosisInputMappingContext_Move::GetBackpackAction()
{
	using namespace MetamorphosisMoveInput;
	Get();
	return BackpackActionInstance.Get();
}
