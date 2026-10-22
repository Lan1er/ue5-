// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 游戏设置：音量、移动端摇杆（是否固定 / 大小）、物品栏大小、上下按钮大小。
//
// 存储：USaveGame，槽位 "MetamorphosisSettings"，落在 Saved/SaveGames/ 下。
// 访问：UMetamorphosisSettingsSubsystem::Get(任意对象) —— GameInstance 级子系统，跨关卡存活。
//
// 改动会广播 OnSettingsChanged，各 UI（快捷栏 / 移动端按钮 / 摇杆）订阅它即时刷新，
// 不需要重启或重进关卡。音量为 0~1；三个"大小"都是**倍率**（1.0 = 设计尺寸），
// 这样美术改基准尺寸时缩放比例不会乱。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MetamorphosisSettingsSubsystem.generated.h"

class UMetamorphosisSettingsSubsystem;

/** 设置内容（存盘结构，加字段时给默认值即可向后兼容） */
USTRUCT(BlueprintType)
struct FMetamorphosisGameSettings
{
	GENERATED_BODY()

	/** 主音量 0~1。声音系统做好之后接到 SoundClass/SoundMix 上 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	float MasterVolume = 1.f;

	/** 移动端摇杆是否固定在屏幕左下角（false = 浮动：手指按哪里，摇杆就出现在哪里） */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	bool bFixedJoystick = true;

	/** 摇杆大小倍率 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	float JoystickScale = 1.f;

	/** 物品栏（快捷栏）大小倍率 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	float HotbarScale = 1.f;

	/** 移动端「上/下」按钮大小倍率 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	float MobileButtonScale = 1.f;
};

/** 存盘对象 */
UCLASS()
class METAMORPHOSIS_API UMetamorphosisSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	FMetamorphosisGameSettings Settings;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	int32 Version = 1;
};

DECLARE_MULTICAST_DELEGATE(FMetamorphosisSettingsChanged);

UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 便捷取用（拿不到返回 nullptr） */
	static UMetamorphosisSettingsSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 当前设置（只读用） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Settings")
	const FMetamorphosisGameSettings& GetSettings() const { return Settings; }

	// ---- 单项设置（会自动存盘 + 广播 + 立即生效） ----

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void SetMasterVolume(float NewVolume);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void SetFixedJoystick(bool bFixed);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void SetJoystickScale(float NewScale);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void SetHotbarScale(float NewScale);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void SetMobileButtonScale(float NewScale);

	/** 恢复默认值 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void ResetToDefaults();

	/** 读盘（Initialize 会自动调一次） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void LoadSettings();

	/** 存盘 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void SaveSettings();

	/** 把设置里"需要主动应用"的部分生效（音量；进关卡后再调一次，因为音频设备会重建） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Settings")
	void ApplyAudioSettings();

	/** 设置变化广播 */
	FMetamorphosisSettingsChanged OnSettingsChanged;

	// ---- 取值范围（UI 滑条与代码校验共用） ----
	// 2026-10-19：用户反馈"最大值怎么只有 100"——上限原来 1.6（160%），改成 2.0（200%）：
	// 摇杆/物品栏/上下按钮都能调到两倍大。下限保持 0.6（60%）。
	static constexpr float MinScale = 0.6f;
	static constexpr float MaxScale = 2.0f;

protected:
	/** 三个倍率的合法区间校验 */
	static float ClampScale(float Value);

	UPROPERTY(config, EditAnywhere, Category = "Settings")
	FString SaveSlotName = TEXT("MetamorphosisSettings");

	UPROPERTY(config, EditAnywhere, Category = "Settings")
	int32 SaveUserIndex = 0;

	FMetamorphosisGameSettings Settings;
};

