// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 可交互物件：虫子走近 -> 提示 -> 按交互键/点按钮 -> 拾取进背包 或 就地吃掉（仿《原神》）。
//
// 两种放置方式（UMetamorphosisInteractionComponent 两条路都认）：
//   1) 正式路：往关卡里摆一个 AMetamorphosisInteractable（Mesh 可换模型，留空就是纯逻辑交互物）
//   2) 标签路：任意 Actor 加标签 "Meta.Item:<ItemId>" 或 "Meta.Item:<ItemId>:<Count>"
//      —— 地编/美术不用换类，直接在关卡里打标签即可；交互后该 Actor 被销毁
//
// 行为由道具配表（Content/Config/game_config.json 的 Items）决定：
//   Mode = Auto 时：bCarryable ? 拾取进背包 : 就地吃掉
//   ⚠️ 这条规则只写在 UMetamorphosisInteractionComponent::ResolveMode() 一处。
//
// 调试：Meta.Interact.Scan / .Info / .Do（见 MetamorphosisInteractionComponent.cpp）

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetamorphosisInteractable.generated.h"

class UStaticMeshComponent;

/** 交互方式 */
UENUM(BlueprintType)
enum class EMetamorphosisInteractMode : uint8
{
	/** 按配表判定：bCarryable ? 拾取进背包 : 就地吃掉（推荐，规则只有一处） */
	Auto UMETA(DisplayName = "自动（按配表）"),

	/** 一定要进背包（配表里 bCarryable=false 时会被拒绝，物件保留） */
	PickUp UMETA(DisplayName = "拾取进背包"),

	/** 一定要就地吃掉：不进背包，直接结算配表效果后销毁 */
	EatInPlace UMETA(DisplayName = "就地吃掉")
};

/**
 *  一个可交互的道具物件。
 *  交互点 = 本 Actor 的位置（距离判定用），所以待机动画只动 Mesh 不动根，避免"看着近却交互不到"。
 */
UCLASS(Blueprintable)
class METAMORPHOSIS_API AMetamorphosisInteractable : public AActor
{
	GENERATED_BODY()

public:
	AMetamorphosisInteractable();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---------------------------------------------------------------
	// 内容（策划/关卡填写）
	// ---------------------------------------------------------------

	/** 道具 ID：必须与配表 Items 里的 ItemId 一致（如 BrownSugar / Cereal） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	FName ItemId;

	/** 一次给几个（进背包时受配表 MaxStack 限制；就地吃固定按 1 份结算） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** 交互方式；Auto = 按配表（不可携带 -> 就地吃） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	EMetamorphosisInteractMode Mode = EMetamorphosisInteractMode::Auto;

	/** 自定义提示文字；留空 = 自动「拾取 XX」/「吃掉 XX」 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	FText PromptTextOverride;

	/** 覆盖全局交互距离（厘米）；0 = 用交互组件的 InteractRangeCm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact", meta = (ClampMin = "0"))
	float InteractRangeOverrideCm = 0.f;

	// ---------------------------------------------------------------
	// 待机表现（可选；纯逻辑交互物可以关掉）
	// -------------------------------------------------------

	/** 缓慢自转：让玩家一眼看出"这个能交互"。
	 *  ⚠️ 2026-10-17 用户要求：**可交互物不要上下浮动**（看起来像"卡在地里一直弹"），
	 *  所以垂直 bob 已整个移除；只保留这种不改变高度的自转。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Idle")
	bool bIdleSpin = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Idle")
	float IdleSpinSpeedDeg = 45.f;

	// ---------------------------------------------------------------
	// 组件
	// ---------------------------------------------------------------

	/** 根（位置 = 交互点；待机动画不动它） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 物件网格：美术换模型用；留空 = 看不见的纯逻辑交互物（可以只挂特效/文字） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 取交互点位置（目前就是 Actor 位置；以后要挪交互点改这里一处） */
	FVector GetInteractLocation() const;

private:
	/** Mesh 的初始相对变换（浮动/自转在它基础上叠加） */
	FVector MeshBaseLocation = FVector::ZeroVector;
	FRotator MeshBaseRotation = FRotator::ZeroRotator;

	/** 已经跑了多久（待机动画相位） */
	float IdleTime = 0.f;
};
