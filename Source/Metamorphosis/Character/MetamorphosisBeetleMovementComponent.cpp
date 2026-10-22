// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Character/MetamorphosisBeetleMovementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Metamorphosis.h"

bool UMetamorphosisBeetleMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (Super::IsWalkable(Hit))
	{
		return true;
	}

	if (!bTolerateInvertedFloors || !Hit.IsValidBlockingHit())
	{
		return false;
	}

	// 法线朝下但坡度在可走范围内（用 |Z| 判断）-> 也当作可走地面
	const float Flatness = FMath::Abs(Hit.ImpactNormal.Z);
	if (Flatness < GetWalkableFloorZ())
	{
		return false;
	}

	// 记一下是哪块网格（地编修资源时要用；用 mutable 缓存在 const 函数里）
	UObject* Owner = Hit.GetComponent() != nullptr ? Hit.GetComponent()->GetOwner() : nullptr;
	const_cast<UMetamorphosisBeetleMovementComponent*>(this)->LastInvertedFloorComponent =
		FString::Printf(TEXT("%s / %s"), *GetNameSafe(Owner), *GetNameSafe(Hit.GetComponent()));

	return true;
}
