// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 常态 BGM：全程循环播放，跟暂停联动，周目结算/通关时停止。
//
// 设计要点
//   * 用一个 UGameInstanceSubsystem + FTickableGameObject 持有播放状态 —— 跨关卡不重建，
//     每帧只做「该播/该停/该暂停」的状态收敛，逻辑收敛在一处，不会有叠加播放/漏恢复；
//   * 暂停：跟 UGameplayStatics::IsGamePaused 走（项目原则「暂停期间一切冻结」）；
//     本类在暂停时仍 tick（IsTickableWhenPaused），才能感知"恢复"；
//   * 周目结算（被妻子抓住 → 冻结 → 反馈 → 重置）：**停止**（不是压低音量），结算结束**从头重播**
//     （周目重开的叙事节拍，音乐跟着重新开始）；
//   * 通关终局（EndingWidget 显示后）：**停止且不再恢复**；只有玩家点「重新开始」进入新的一局时才恢复；
//   * 扩展口：BGMSoundPath 可配（ini）+ 公开 PlayBGM(USoundBase*) / StopBGM(Reason)，
//     以后接"章节 BGM / 结算专用 BGM"时直接调用即可，不用改本类逻辑。
//
// ini：Config/DefaultGame.ini
//   [/Script/Metamorphosis.MetamorphosisBGMSubsystem]
//   BGMSoundPath=/Game/Audio/BGM/BGM.BGM
//   BGMVolume=1.0
//   bEnableBGM=True
//   bRestartAfterSettlement=True

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "MetamorphosisBGMSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class UWorld;

UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisBGMSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ---- USubsystem ----
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- FTickableGameObject ----
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	// -----------------------------------------------------------------------
	// 对外扩展口（以后接章节 BGM / 结算专用曲时用）
	// -----------------------------------------------------------------------

	/** 开始播放 BGM。传 nullptr = 用 ini 里的 BGMSoundPath（默认曲目）。已在播则不重复启动。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|BGM")
	bool PlayBGM(USoundBase* Sound = nullptr);

	/** 停止 BGM。Reason 只用于日志（例如「周目结算」「通关」「手动」）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|BGM")
	void StopBGM(const FString& Reason = TEXT("手动"));

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|BGM")
	bool IsBGMPlaying() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|BGM")
	USoundBase* GetCurrentBGM() const { return CurrentSound; }

	/** 当前是否因为周目结算/通关而被人为停掉（调试用） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|BGM")
	bool IsBGMSuppressed() const { return bStoppedForSettlement || bStoppedForWin; }

	// -----------------------------------------------------------------------
	// 配置（ini）
	// -----------------------------------------------------------------------

	/** 默认曲目（软路径）；PlayBGM(nullptr) 时用它 */
	UPROPERTY(config, EditAnywhere, Category = "BGM")
	FSoftObjectPath BGMSoundPath;

	/** BGM 音量（0~2）；设置页的主音量滑块由音频设备统一缩放，两者相乘 */
	UPROPERTY(config, EditAnywhere, Category = "BGM")
	float BGMVolume = 1.0f;

	/** 总开关：关掉就完全不播（也不 tick） */
	UPROPERTY(config, EditAnywhere, Category = "BGM")
	bool bEnableBGM = true;

	/** 周目结算结束后是否从头重播（true=从头，false=从原位置继续） */
	UPROPERTY(config, EditAnywhere, Category = "BGM")
	bool bRestartAfterSettlement = true;

	/** 是否打 BGM 状态日志（排查用，默认开） */
	UPROPERTY(config, EditAnywhere, Category = "BGM")
	bool bLogBGM = true;

	/** 启动失败后的首次重试间隔（秒）；失败会指数退避，最长 30 秒 —— 防止无音频设备时每帧刷屏 */
	UPROPERTY(config, EditAnywhere, Category = "BGM")
	float BGMRetrySeconds = 3.0f;

private:
	/** 解析默认曲目：先 FSoftObjectPath，再按路径字符串 LoadObject */
	USoundBase* ResolveDefaultSound();

	/** 每帧收敛一次状态：终局/结算/正常/暂停 */
	void UpdateState(UWorld* World, float DeltaTime);

	/** 真正启动声音组件 */
	bool StartAudio(UWorld* World, USoundBase* Sound);

	void LogBGM(const FString& Message) const;

	/** 当前播放的声音组件（跨关卡持久） */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AudioComp = nullptr;

	/** 当前曲目（防 GC） */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CurrentSound = nullptr;

	/** 已经打过"载入"日志，避免每次重播都刷 */
	bool bLoggedLoad = false;

	/** 因为周目结算而停掉（结算结束后要恢复） */
	bool bStoppedForSettlement = false;

	/** 因为通关而停掉（终局，不再恢复；除非玩家重新开始） */
	bool bStoppedForWin = false;

	/** 上一次观察到的暂停状态（变化时才调 SetPaused 并打日志） */
	bool bAudioPaused = false;

	/** 已经"开始播放"过一次（日志/调试用） */
	bool bStartedOnce = false;

	// ---- 启动失败后的指数退避（防止 -nosound / 无音频设备时每帧刷日志） ----
	float RetryElapsed = 0.f;
	float RetryDelay = 0.f;
	int32 FailCount = 0;

	/** 已经提示过"没有音频设备"，只提示一次 */
	bool bLoggedNoDevice = false;
};
