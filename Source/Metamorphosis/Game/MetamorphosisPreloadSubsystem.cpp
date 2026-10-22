// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Game/MetamorphosisPreloadSubsystem.h"

#include "Containers/Ticker.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Metamorphosis.h"
#include "Misc/PackageName.h"

UMetamorphosisPreloadSubsystem* UMetamorphosisPreloadSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UMetamorphosisPreloadSubsystem>() : nullptr;
}

void UMetamorphosisPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMetamorphosisPreloadSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	LoadHandle.Reset();
	ItemHandles.Reset();

	Super::Deinitialize();
}

void UMetamorphosisPreloadSubsystem::BuildRequestList()
{
	RequestedPaths.Reset();
	SkippedMissingCount = 0;

	for (const FSoftObjectPath& Path : PreloadAssets)
	{
		if (Path.IsValid())
		{
			RequestedPaths.AddUnique(Path);
		}
	}

	if (!bPreloadGameMapDependencies || GameMap.IsEmpty())
	{
		return;
	}

	// 关键：**不预加载关卡包本身**。
	// 直接把 World 包拉进内存后，引擎正式跳转时的 LoadMap 会报
	// "Failed to load package" 而失败（关卡里只要有一个缺失的硬引用就会这样）。
	// 所以这里只把关卡依赖的资源（贴图/材质/网格…）提前加载好，
	// 关卡本身留到点「开始游戏」时由引擎正常加载 —— 依赖都在内存里，那一步就很快。
	const FString PackageName = NormalizeMapPackage(GameMap);

	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (AssetRegistry == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Preload] AssetRegistry 不可用，跳过关卡依赖预加载"));
		return;
	}

	TArray<FName> Dependencies;
	AssetRegistry->GetDependencies(FName(*PackageName), Dependencies,
		UE::AssetRegistry::EDependencyCategory::Package);

	UE_LOG(LogMetamorphosis, Log, TEXT("[Preload] 关卡 %s 的直接依赖包：%d 个"), *PackageName, Dependencies.Num());

	for (const FName& Dependency : Dependencies)
	{
		const FString DependencyName = Dependency.ToString();

		if (DependencyName == PackageName || DependencyName.StartsWith(TEXT("/Script/")))
		{
			continue;
		}

		// 缺失的引用直接跳过（本工程已知有几条指向 /Game/大嘴吉2/... 的失效引用）
		if (!FPackageName::DoesPackageExist(DependencyName))
		{
			SkippedMissingCount++;
			continue;
		}

		RequestedPaths.AddUnique(FSoftObjectPath(DependencyName));
	}

	if (SkippedMissingCount > 0)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Preload] 跳过 %d 个磁盘上不存在的引用（缺失资源，进游戏后也不会加载到）"),
			SkippedMissingCount);
	}
}

FString UMetamorphosisPreloadSubsystem::NormalizeMapPackage(const FString& InMapPath)
{
	FString PackageName = InMapPath;
	PackageName.TrimStartAndEndInline();

	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindLastChar(TEXT('.'), DotIndex))
	{
		// /Game/mymap/map.map -> /Game/mymap/map
		PackageName.LeftInline(DotIndex);
	}

	return PackageName;
}

void UMetamorphosisPreloadSubsystem::StartPreload()
{
	if (bStarted)
	{
		return;
	}

	bStarted = true;
	StartRealTime = FPlatformTime::Seconds();
	TotalCount = 0;
	LoadedCount = 0;

	BuildRequestList();

	UE_LOG(LogMetamorphosis, Log, TEXT("[Preload] 开始预加载 %d 项（游戏关卡 %s，最短停留 %.1f 秒）"),
		RequestedPaths.Num(), *GameMap, MinimumLoadingSeconds);

	if (RequestedPaths.Num() == 0)
	{
		OnLoadFinished();
	}
	else
	{
		// 逐项异步加载：既能并行，又方便统计"完成了几项 / 正在加载哪一个"
		for (const FSoftObjectPath& Path : RequestedPaths)
		{
			const FStreamableDelegate Delegate = FStreamableDelegate::CreateUObject(
				this, &UMetamorphosisPreloadSubsystem::OnAssetLoaded, Path);

			TSharedPtr<FStreamableHandle> Handle =
				UAssetManager::GetStreamableManager().RequestAsyncLoad(Path, Delegate);

			if (Handle.IsValid())
			{
				ItemHandles.Add(Handle);
				TotalCount++;
			}
			else
			{
				// 请求失败也当完成，别把玩家卡在菜单里
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Preload] 资源请求失败：%s"), *Path.ToString());
				LoadedCount++;
			}
		}

		CurrentAssetName = RequestedPaths.Num() > 0 ? RequestedPaths[0].GetAssetName() : FString();
	}

	// 用核心 ticker 驱动（不依赖 World，菜单/游戏中都活着）
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMetamorphosisPreloadSubsystem::Tick), 0.f);

	BroadcastStateChanged();
}

void UMetamorphosisPreloadSubsystem::OnAssetLoaded(FSoftObjectPath Path)
{
	LoadedCount++;

	UE_LOG(LogMetamorphosis, Log, TEXT("[Preload] 完成 %d / %d：%s（已用 %.1f 秒）"),
		LoadedCount, TotalCount, *Path.ToString(), FPlatformTime::Seconds() - StartRealTime);

	if (LoadedCount >= TotalCount)
	{
		OnLoadFinished();
	}

	BroadcastStateChanged();
}

void UMetamorphosisPreloadSubsystem::OnLoadFinished()
{
	if (bLoadComplete)
	{
		return;
	}

	bLoadComplete = true;
	CurrentAssetName.Reset();

	UE_LOG(LogMetamorphosis, Log, TEXT("[Preload] 全部加载完成，用时 %.2f 秒"), FPlatformTime::Seconds() - StartRealTime);
	BroadcastStateChanged();
}

bool UMetamorphosisPreloadSubsystem::Tick(float DeltaSeconds)
{
	if (!bStarted)
	{
		return true;
	}

	const double Elapsed = FPlatformTime::Seconds() - StartRealTime;

	if (!bLoadComplete && Elapsed > MaxLoadingSeconds)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Preload] 超过 %.0f 秒还没加载完（%d / %d），先放行进游戏"),
			MaxLoadingSeconds, LoadedCount, TotalCount);
		OnLoadFinished();
	}

	if (!bReady && bLoadComplete && Elapsed >= MinimumLoadingSeconds)
	{
		bReady = true;
		UE_LOG(LogMetamorphosis, Log, TEXT("[Preload] 资源就绪，可以开始了（总用时 %.2f 秒）"), Elapsed);
		BroadcastStateChanged();
	}

	if (bReady && bAutoEnterWhenReady)
	{
		EnterGameInternal();
	}

	// 加载中时也周期性刷一下进度，方便 UI 不订阅也能显示
	if (!bLoadComplete)
	{
		const float Percent = GetProgress();
		if (FMath::Abs(Percent - LastBroadcastProgress) >= 0.01f)
		{
			BroadcastStateChanged();
		}
	}

	return true;
}

float UMetamorphosisPreloadSubsystem::GetProgress() const
{
	if (bLoadComplete)
	{
		return 1.f;
	}

	if (TotalCount <= 0)
	{
		return 0.f;
	}

	// 用各句柄的真实进度取平均，进度条不会一格一格跳
	float Sum = 0.f;
	for (const TSharedPtr<FStreamableHandle>& Handle : ItemHandles)
	{
		Sum += Handle.IsValid() ? Handle->GetProgress() : 0.f;
	}

	// 请求失败的项也算已完成，避免进度条停在中途
	const int32 FailedCount = RequestedPaths.Num() - TotalCount;
	Sum += static_cast<float>(FailedCount);

	return FMath::Clamp(Sum / static_cast<float>(RequestedPaths.Num()), 0.f, 1.f);
}

void UMetamorphosisPreloadSubsystem::GetCounts(int32& OutLoaded, int32& OutTotal) const
{
	OutLoaded = LoadedCount;
	OutTotal = TotalCount;
}

void UMetamorphosisPreloadSubsystem::BroadcastStateChanged()
{
	LastBroadcastProgress = GetProgress();
	OnStateChanged.Broadcast();
}

void UMetamorphosisPreloadSubsystem::EnterGame()
{
	EnterGameInternal();
}

void UMetamorphosisPreloadSubsystem::EnterGameInternal()
{
	if (bEntered)
	{
		return;
	}

	bEntered = true;

	// OpenLevel 的两个坑（都踩过）：
	//   1) bAbsolute 默认是 false（相对当前关卡），结果会原地重载当前关 —— 必须给 true；
	//   2) 绝对跳转的 URL 要**完整包路径**（/Game/mymap/map）。
	//      只写 mymap/map 会被引擎判成 "Can't Find URL" 然后退回到默认（当前）关卡。
	FString LevelName = NormalizeMapPackage(GameMap);

	if (LevelName.IsEmpty())
	{
		LevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Preload] 进入游戏关卡：%s（加载用时 %.2f 秒）"),
		*LevelName, FPlatformTime::Seconds() - StartRealTime);

	UGameplayStatics::OpenLevel(this, FName(*LevelName), true);
}
