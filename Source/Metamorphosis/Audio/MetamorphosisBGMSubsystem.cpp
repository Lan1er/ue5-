// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Audio/MetamorphosisBGMSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Metamorphosis.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

// ---------------------------------------------------------------------------
// 小工具：从任意世界上下文里找到这个子系统（给控制台命令用）
// ---------------------------------------------------------------------------
namespace
{
	UMetamorphosisBGMSubsystem* FindBGMSubsystem()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World == nullptr)
			{
				continue;
			}
			if (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE)
			{
				continue;
			}
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UMetamorphosisBGMSubsystem* Subsystem = GameInstance->GetSubsystem<UMetamorphosisBGMSubsystem>())
				{
					return Subsystem;
				}
			}
		}
		return nullptr;
	}

	/** Meta.BGM.Info / Play / Stop —— 给无头验证与调试用 */
	void RegisterBGMConsoleCommands()
	{
		static FAutoConsoleCommand InfoCmd(
			TEXT("Meta.BGM.Info"),
			TEXT("打印 BGM 当前状态（曲目 / 是否在播 / 是否被结算或通关压住 / 音量）"),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				if (UMetamorphosisBGMSubsystem* Subsystem = FindBGMSubsystem())
				{
					UE_LOG(LogMetamorphosis, Log,
						TEXT("[BGM] 状态：曲目=%s 在播=%d 被人为停=%d 音量=%.2f"),
						Subsystem->GetCurrentBGM() ? *Subsystem->GetCurrentBGM()->GetName() : TEXT("(无)"),
						Subsystem->IsBGMPlaying() ? 1 : 0,
						Subsystem->IsBGMSuppressed() ? 1 : 0,
						Subsystem->BGMVolume);
				}
				else
				{
					UE_LOG(LogMetamorphosis, Warning, TEXT("[BGM] 状态：找不到 BGM 子系统"));
				}
			}));

		static FAutoConsoleCommand PlayCmd(
			TEXT("Meta.BGM.Play"),
			TEXT("重新开始播放 BGM（默认曲目）"),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				if (UMetamorphosisBGMSubsystem* Subsystem = FindBGMSubsystem())
				{
					Subsystem->PlayBGM(nullptr);
				}
			}));

		static FAutoConsoleCommand StopCmd(
			TEXT("Meta.BGM.Stop"),
			TEXT("停止 BGM（日志里记原因=控制台）"),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				if (UMetamorphosisBGMSubsystem* Subsystem = FindBGMSubsystem())
				{
					Subsystem->StopBGM(TEXT("控制台"));
				}
			}));
	}
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

void UMetamorphosisBGMSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RegisterBGMConsoleCommands();

	// 兜底：ini 没写或写错时也别把音量搞成 0（否则"播了但听不见"很难查）
	if (BGMVolume <= 0.f)
	{
		BGMVolume = 1.0f;
	}

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[BGM] 子系统就绪：默认曲目=%s 音量=%.2f 开关=%d 结算后从头=%d"),
		*BGMSoundPath.ToString(), BGMVolume, bEnableBGM ? 1 : 0, bRestartAfterSettlement ? 1 : 0);
}

void UMetamorphosisBGMSubsystem::Deinitialize()
{
	StopBGM(TEXT("子系统销毁"));

	CurrentSound = nullptr;

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// FTickableGameObject
// ---------------------------------------------------------------------------

TStatId UMetamorphosisBGMSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetamorphosisBGMSubsystem, STATGROUP_Tickables);
}

bool UMetamorphosisBGMSubsystem::IsTickable() const
{
	// 关掉开关就不 tick（控制台命令仍然可用）
	return bEnableBGM && !IsTemplate();
}

UWorld* UMetamorphosisBGMSubsystem::GetTickableGameObjectWorld() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetWorld();
	}
	return nullptr;
}

void UMetamorphosisBGMSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetTickableGameObjectWorld();
	if (World == nullptr || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	UpdateState(World, DeltaTime);
}

// ---------------------------------------------------------------------------
// 状态收敛：终局 -> 结算 -> 正常 -> 暂停
// ---------------------------------------------------------------------------

void UMetamorphosisBGMSubsystem::UpdateState(UWorld* World, float DeltaTime)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UMetamorphosisLoopSubsystem* Loop =
		GameInstance ? GameInstance->GetSubsystem<UMetamorphosisLoopSubsystem>() : nullptr;

	// 1) 通关终局：停止，且保持停止（终局不再恢复）
	if (Loop != nullptr && Loop->IsWinEndingVisible())
	{
		if (!bStoppedForWin || IsBGMPlaying())
		{
			StopBGM(TEXT("通关"));
			bStoppedForWin = true;
		}
		return;
	}

	// 通关界面收起（玩家点「重新开始」进了新的一局）-> 解除终局压制
	if (bStoppedForWin)
	{
		bStoppedForWin = false;
		LogBGM(TEXT("[BGM] 解除通关压制（重新开始了一局）"));
	}

	// 2) 周目结算：停止（不是压低音量），结算结束后恢复
	const bool bSettling = (Loop != nullptr && Loop->IsSettling());
	if (bSettling)
	{
		if (!bStoppedForSettlement)
		{
			StopBGM(TEXT("周目结算"));
			bStoppedForSettlement = true;
		}
		return;
	}

	if (bStoppedForSettlement)
	{
		bStoppedForSettlement = false;
		LogBGM(bRestartAfterSettlement
			? TEXT("[BGM] 恢复（周目结算结束，从头重播）")
			: TEXT("[BGM] 恢复（周目结算结束，接着放）"));
	}

	// 3) 正常态：没在播就播（曲目资产已置 looping，播完会自己循环）
	if (!IsBGMPlaying())
	{
		// 失败退避：无音频设备（-nosound / 独占声卡）时不要每帧重试、更不要每帧刷日志 ✗
		if (RetryDelay > 0.f)
		{
			RetryElapsed += DeltaTime;
			if (RetryElapsed < RetryDelay)
			{
				return;
			}
			RetryElapsed = 0.f;
		}

		PlayBGM(nullptr);
		if (!IsBGMPlaying())
		{
			return;
		}
	}

	// 4) 暂停联动：跟 IsGamePaused 走（项目原则：暂停期间一切冻结）
	const bool bGamePaused = UGameplayStatics::IsGamePaused(World);
	if (AudioComp != nullptr && bGamePaused != bAudioPaused)
	{
		AudioComp->SetPaused(bGamePaused);
		bAudioPaused = bGamePaused;
		LogBGM(bGamePaused ? TEXT("[BGM] 暂停") : TEXT("[BGM] 恢复"));
	}
}

// ---------------------------------------------------------------------------
// 播放 / 停止
// ---------------------------------------------------------------------------

USoundBase* UMetamorphosisBGMSubsystem::ResolveDefaultSound()
{
	if (!BGMSoundPath.IsNull())
	{
		if (USoundBase* Loaded = Cast<USoundBase>(BGMSoundPath.TryLoad()))
		{
			return Loaded;
		}
		UE_LOG(LogMetamorphosis, Warning, TEXT("[BGM] 曲目加载失败：%s"), *BGMSoundPath.ToString());
		return nullptr;
	}

	// 没配 ini 时的兜底路径
	static const TCHAR* FallbackPath = TEXT("/Game/Audio/BGM/BGM.BGM");
	USoundBase* Loaded = LoadObject<USoundBase>(nullptr, FallbackPath);
	if (Loaded == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[BGM] 默认曲目不存在：%s"), FallbackPath);
	}
	return Loaded;
}

bool UMetamorphosisBGMSubsystem::PlayBGM(USoundBase* Sound)
{
	if (!bEnableBGM)
	{
		return false;
	}

	USoundBase* Target = (Sound != nullptr) ? Sound : CurrentSound.Get();
	if (Target == nullptr)
	{
		Target = ResolveDefaultSound();
	}
	if (Target == nullptr)
	{
		return false;
	}

	UWorld* World = GetTickableGameObjectWorld();
	if (World == nullptr)
	{
		return false;
	}

	// 已经在播同一首 -> 不重复启动（避免叠音）
	if (AudioComp != nullptr && AudioComp->IsPlaying() && CurrentSound == Target)
	{
		return true;
	}

	if (AudioComp != nullptr)
	{
		AudioComp->Stop();
		AudioComp = nullptr;
	}

	CurrentSound = Target;
	return StartAudio(World, Target);
}

bool UMetamorphosisBGMSubsystem::StartAudio(UWorld* World, USoundBase* Sound)
{
	if (Sound == nullptr)
	{
		return false;
	}

	// 没有音频设备（-nosound / 独占声卡 / 服务端）：一次提示，然后彻底不重试
	if (GEngine == nullptr || !GEngine->GetMainAudioDevice().IsValid())
	{
		if (!bLoggedNoDevice)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[BGM] 没有可用音频设备（-nosound 或被独占）-> 本次运行不播 BGM"));
			bLoggedNoDevice = true;
		}
		// 拉长到很长，避免每帧重试
		RetryDelay = 3600.f;
		RetryElapsed = 0.f;
		return false;
	}

	// 首次播放：把曲目信息打出来（时长 / 是否循环）
	if (!bLoggedLoad)
	{
		float Duration = 0.f;
		bool bLooping = false;
		if (const USoundWave* Wave = Cast<USoundWave>(Sound))
		{
			Duration = Wave->Duration;
			bLooping = Wave->bLooping;
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[BGM] 载入 BGM：%s（时长 %.2f 秒，循环=%d）"),
			*Sound->GetPathName(), Duration, bLooping ? 1 : 0);
		bLoggedLoad = true;
	}

	// 2D 声音（不空间化）+ 跨关卡持久 + 不自动销毁
	UAudioComponent* Comp = UGameplayStatics::SpawnSound2D(
		World,
		Sound,
		FMath::Max(0.f, BGMVolume),
		1.0f,
		0.0f,
		nullptr,
		/*bPersistAcrossLevelTransition=*/true,
		/*bAutoDestroy=*/false);

	if (Comp == nullptr)
	{
		// 指数退避重试（1x/2x/4x… 最长 30 秒），只在第 1/2/4/8/16… 次失败时打日志
		++FailCount;
		const float Base = FMath::Max(0.5f, BGMRetrySeconds);
		RetryDelay = FMath::Min(30.f, Base * FMath::Pow(2.f, static_cast<float>(FailCount - 1)));
		RetryElapsed = 0.f;

		const bool bPowerOfTwo = (FailCount <= 1) || ((FailCount & (FailCount - 1)) == 0);
		if (bPowerOfTwo)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[BGM] 启动失败（SpawnSound2D 返回空），第 %d 次失败，%.1f 秒后重试"), FailCount, RetryDelay);
		}
		return false;
	}

	// 成功：清掉退避状态
	FailCount = 0;
	RetryDelay = 0.f;
	RetryElapsed = 0.f;

	// 二次保证：这个组件由我们控制生命周期；跟游戏暂停一起停
	Comp->bAutoDestroy = false;
	Comp->bIsUISound = false;
	Comp->bAllowSpatialization = false;
	Comp->SetPaused(false);

	AudioComp = Comp;
	bAudioPaused = false;
	bStartedOnce = true;

	UE_LOG(LogMetamorphosis, Log, TEXT("[BGM] 开始播放（音量 %.2f，循环曲目，跨关卡持久）"), BGMVolume);
	return true;
}

void UMetamorphosisBGMSubsystem::StopBGM(const FString& Reason)
{
	if (AudioComp != nullptr)
	{
		AudioComp->Stop();
		AudioComp = nullptr;
	}

	bAudioPaused = false;

	UE_LOG(LogMetamorphosis, Log, TEXT("[BGM] 停止（%s）"), *Reason);
}

bool UMetamorphosisBGMSubsystem::IsBGMPlaying() const
{
	return AudioComp != nullptr && AudioComp->IsPlaying();
}

void UMetamorphosisBGMSubsystem::LogBGM(const FString& Message) const
{
	if (bLogBGM)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("%s"), *Message);
	}
}
