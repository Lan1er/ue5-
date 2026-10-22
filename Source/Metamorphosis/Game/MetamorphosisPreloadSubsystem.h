// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 开始界面的"提前加载"：在菜单里就把游戏关卡 + 相关资源异步加载好，
// 这样点「开始游戏」进关时不会再出现资源没加载完/闪烁/卡顿的情况。
//
// 用法（C++ 或蓝图）：
//   UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this);
//   Preload->StartPreload();                       // 菜单出来时调
//   Preload->GetProgress() / IsReady()             // UI 每帧查
//   Preload->EnterGame();                          // 点开始游戏时调
//
// 要加载哪些东西全部在 Config/DefaultGame.ini 的
// [/Script/Metamorphosis.MetamorphosisPreloadSubsystem] 段里配，不用重编译。

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/StreamableManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MetamorphosisPreloadSubsystem.generated.h"

class UMetamorphosisPreloadSubsystem;

/** 进度/状态变化（UI 也可以不订阅，直接每帧查 GetProgress/IsReady） */
DECLARE_MULTICAST_DELEGATE(FMetamorphosisPreloadStateChanged);

UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisPreloadSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 方便取用（拿不到就返回 nullptr） */
	static UMetamorphosisPreloadSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 开始预加载；重复调用无效。菜单出现时调它 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Preload")
	void StartPreload();

	/** 加载进度 0~1 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Preload")
	float GetProgress() const;

	/** 真正加载完了吗（不含最短停留时间） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Preload")
	bool IsLoadComplete() const { return bLoadComplete; }

	/** 可以进游戏了吗（加载完 + 满足最短停留时间） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Preload")
	bool IsReady() const { return bReady; }

	/** 一共要加载几项 / 已经完成几项 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Preload")
	void GetCounts(int32& OutLoaded, int32& OutTotal) const;

	/** 当前在加载哪个资源（给 UI 显示） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Preload")
	FString GetCurrentAssetName() const { return CurrentAssetName; }

	/** 状态变化广播 */
	FMetamorphosisPreloadStateChanged OnStateChanged;

	/** 打开游戏关卡（内部也是「开始游戏」按钮调的） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Preload")
	void EnterGame();

	/** 清掉"已经进过游戏"的一次性标志。
	 *  用途：通关界面点「返回标题」后，玩家再点「开始游戏」必须还能真的重进关卡
	 *  （否则 EnterGame 会被 bEntered 挡住，看起来像按钮没反应）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Preload")
	void ResetEntered() { bEntered = false; }

protected:
	/** 游戏关卡包路径（相对 /Game） */
	UPROPERTY(config, EditAnywhere, Category = "Preload")
	FString GameMap = TEXT("/Game/mymap/map/Untitled");

	/** 额外要预加载的资源（贴图/材质/蓝图…）。关卡依赖的资源会自动查出来，一般不用手写 */
	UPROPERTY(config, EditAnywhere, Category = "Preload")
	TArray<FSoftObjectPath> PreloadAssets;

	/** 是否用 AssetRegistry 把关卡依赖的资源也提前加载好（推荐开）
	 *  注意：关卡包本身**不会**被预加载 —— 直接预加载 World 包会让引擎正式跳转时
	 *  LoadMap 失败（"Failed to load package"）。这里只预加载它依赖的资源。 */
	UPROPERTY(config, EditAnywhere, Category = "Preload")
	bool bPreloadGameMapDependencies = true;

	/** 开始界面最少停留多少秒：太快闪一下体验很差，顺便给材质着色器编译留时间 */
	UPROPERTY(config, EditAnywhere, Category = "Preload")
	float MinimumLoadingSeconds = 2.5f;

	/** 最长等多久（秒）就放行进游戏，避免某一项卡死永远进不去 */
	UPROPERTY(config, EditAnywhere, Category = "Preload")
	float MaxLoadingSeconds = 90.f;

	/** 加载完成后自动进游戏（默认关：让玩家自己点「开始游戏」） */
	UPROPERTY(config, EditAnywhere, Category = "Preload")
	bool bAutoEnterWhenReady = false;

private:
	/** 每帧驱动：刷新进度、判定就绪、到点自动进关 */
	bool Tick(float DeltaSeconds);
	void BuildRequestList();
	void OnAssetLoaded(FSoftObjectPath Path);
	void OnLoadFinished();
	void EnterGameInternal();
	void BroadcastStateChanged();

	/** /Game/mymap/map.map -> /Game/mymap/map */
	static FString NormalizeMapPackage(const FString& InMapPath);

	/** 强引用住句柄，别让加载中途被回收 */
	TSharedPtr<FStreamableHandle> LoadHandle;

	/** 每一项一个句柄（并行加载，同时方便统计完成数） */
	TArray<TSharedPtr<FStreamableHandle>> ItemHandles;

	TArray<FSoftObjectPath> RequestedPaths;

	bool bStarted = false;
	bool bLoadComplete = false;
	bool bReady = false;
	bool bEntered = false;
	double StartRealTime = 0.0;
	int32 LoadedCount = 0;
	int32 TotalCount = 0;
	int32 SkippedMissingCount = 0;
	float LastBroadcastProgress = -1.f;
	FString CurrentAssetName;

	FTSTicker::FDelegateHandle TickerHandle;
};

