// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 虫子专用的移动组件：只加一件事 —— **容忍"法线朝下"的地板**。
//
// 背景：地编给的新场景里，部分地板网格的三角形绕序/法线是反的（材质是双面所以看着正常，
// 但碰撞面的法线朝下）。引擎的 UCharacterMovementComponent::IsWalkable 要求法线朝上
// （ImpactNormal.Z >= WalkableFloorZ），于是角色站在这种地板上时 CurrentFloor 不算
// "可走地面"，CharacterMovement 会**拒绝一切水平移动** —— 玩家看到的就是"走不动 / 只能单向滑"。
//
// 这里把判定放宽成 |Z|：法线朝下的地板也当作可走。地板平面基本水平时，
// 沿面移动的水平分量不受影响，所以走起来是正常的。
//
// ⚠️ 这是**兜底**，不是正解：正解是让地编把那些网格的法线/绕序修对。
//    修好后可以把 bTolerateInvertedFloors 关掉（配置项，默认开是为了不卡住开发）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MetamorphosisBeetleMovementComponent.generated.h"

UCLASS()
class METAMORPHOSIS_API UMetamorphosisBeetleMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** 容忍「法线朝下」的地板（应急开关，**默认关**）。
	 *  2026-10-13 那次"站在地上却完全走不动"的真凶是新地图的天空球带了碰撞（已在地图侧改成 NoCollision），
	 *  所以这里默认关，避免掩盖以后的地图问题。万一下一张图又出现同样症状，可以在 ini 里临时打开。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Movement")
	bool bTolerateInvertedFloors = false;

	/** 上一次被判定为"反法线地板"的组件名（调试用） */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Metamorphosis|Movement")
	FString LastInvertedFloorComponent;

	virtual bool IsWalkable(const FHitResult& Hit) const override;
};
