// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Interaction/MetamorphosisInteractable.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Metamorphosis.h"

AMetamorphosisInteractable::AMetamorphosisInteractable()
{
	PrimaryActorTick.bCanEverTick = true;
	// 只有开了待机表现才需要 tick（纯逻辑交互物别浪费）
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Mesh 默认不挂任何模型：美术在编辑器里换，程序测试时也可以直接用它摆个球
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // 交互物不挡路（挡路会让虫子卡住）
	Mesh->SetGenerateOverlapEvents(false);
}

void AMetamorphosisInteractable::BeginPlay()
{
	Super::BeginPlay();

	if (Mesh != nullptr)
	{
		MeshBaseLocation = Mesh->GetRelativeLocation();
		MeshBaseRotation = Mesh->GetRelativeRotation();
	}

	// 待机表现：不开自转就彻底不 tick
	SetActorTickEnabled(bIdleSpin);

	// ItemId 检查放到"下一帧"：运行时丢弃/生成的交互物是 SpawnActor 之后才填 ItemId 的
	// （BeginPlay 在 SpawnActor 内部就跑完了，直接在这里检查会误报"没有填 ItemId"）。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (IsValid(this) && ItemId.IsNone())
			{
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Interact] %s 没有填 ItemId，交互时会被跳过（可用 Meta.Inv.List 看有哪些道具）"), *GetName());
			}
		}));
	}
}

FVector AMetamorphosisInteractable::GetInteractLocation() const
{
	return GetActorLocation();
}

void AMetamorphosisInteractable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bIdleSpin || Mesh == nullptr)
	{
		return;
	}

	IdleTime += DeltaSeconds;

	// ⚠️ 2026-10-17 用户要求：可交互物**不要上下浮动**（看起来像"卡在地里一直弹"）。
	// 这里只做"绕自身竖直轴缓慢自转"，**完全不动高度**（MeshBaseLocation 不再被修改）。
	Mesh->SetRelativeLocation(MeshBaseLocation);
	Mesh->SetRelativeRotation(MeshBaseRotation + FRotator(0.f, IdleTime * IdleSpinSpeedDeg, 0.f));
}
