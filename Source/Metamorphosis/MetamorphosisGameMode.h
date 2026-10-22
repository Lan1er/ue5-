// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetamorphosisGameMode.generated.h"

/**
 *  主 GameMode（C++）：全项目的默认 Pawn / PlayerController 在这里定死，
 *  不再依赖蓝图资源，免得出现「BP 里指错类」的问题。
 *
 *  2026-10-09：默认 Pawn 换成 AMetamorphosisBeetleCharacter
 *  （2D 精灵虫形主角 + 锁定视角相机）。模板自带的 BP_TopDownGameMode /
 *  BP_TopDownCharacter 已不再被使用。
 *
 *  2026-10-10：加「开始界面」分支。开始界面自己是一张空关卡（默认 MainMenu），
 *  在这个关卡里：不生成虫形主角，只把开始界面挂上去（界面上会提前把游戏资源加载完）。
 *  判断依据是关卡名，见 MenuLevelName。
 */
UCLASS(config = Game)
class AMetamorphosisGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	AMetamorphosisGameMode();

	/** 开始界面所在关卡的关卡名（只比较名字，不带路径）。改成别的名字即可换菜单关 */
	UPROPERTY(EditAnywhere, Config, Category = "Metamorphosis|Menu")
	FString MenuLevelName = TEXT("MainMenu");

	/** 当前是不是「开始界面」关卡 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Menu")
	bool IsMenuLevel() const;

	/** 开始界面关卡里不生成 Pawn（菜单是纯 UI） */
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
};
