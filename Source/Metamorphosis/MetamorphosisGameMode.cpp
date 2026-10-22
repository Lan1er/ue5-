// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetamorphosisGameMode.h"

#include "Character/MetamorphosisBeetleCharacter.h"
#include "Engine/World.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Metamorphosis.h"
#include "MetamorphosisPlayerController.h"

AMetamorphosisGameMode::AMetamorphosisGameMode()
{
	// 全 C++ 指定默认框架类，不依赖蓝图资源
	DefaultPawnClass = AMetamorphosisBeetleCharacter::StaticClass();
	PlayerControllerClass = AMetamorphosisPlayerController::StaticClass();
}

bool AMetamorphosisGameMode::IsMenuLevel() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FString LevelName = UGameplayStatics::GetCurrentLevelName(World, true);
	return LevelName.Equals(MenuLevelName, ESearchCase::IgnoreCase);
}

UClass* AMetamorphosisGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	// 开始界面是纯 UI 关卡：不给虫形主角，改给一个旁观者 Pawn
	//（返回 nullptr 会让引擎在生成时刷 "no class specified" 报错，用旁观者最干净；
	//  以后菜单要放 3D 背景/转台，这个旁观者相机也能直接用）
	if (IsMenuLevel())
	{
		return ASpectatorPawn::StaticClass();
	}

	return Super::GetDefaultPawnClassForController_Implementation(InController);
}
