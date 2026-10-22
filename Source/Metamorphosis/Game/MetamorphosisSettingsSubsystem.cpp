// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Game/MetamorphosisSettingsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Metamorphosis.h"
#include "AudioDevice.h"

UMetamorphosisSettingsSubsystem* UMetamorphosisSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UMetamorphosisSettingsSubsystem>() : nullptr;
}

void UMetamorphosisSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadSettings();
	ApplyAudioSettings();
}

float UMetamorphosisSettingsSubsystem::ClampScale(float Value)
{
	return FMath::Clamp(Value, MinScale, MaxScale);
}

// ---------------------------------------------------------------------------
// 单项设置
// ---------------------------------------------------------------------------

void UMetamorphosisSettingsSubsystem::SetMasterVolume(float NewVolume)
{
	const float Clamped = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Clamped, Settings.MasterVolume))
	{
		return;
	}

	Settings.MasterVolume = Clamped;
	ApplyAudioSettings();
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UMetamorphosisSettingsSubsystem::SetFixedJoystick(bool bFixed)
{
	if (Settings.bFixedJoystick == bFixed)
	{
		return;
	}

	Settings.bFixedJoystick = bFixed;
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UMetamorphosisSettingsSubsystem::SetJoystickScale(float NewScale)
{
	const float Clamped = ClampScale(NewScale);
	if (FMath::IsNearlyEqual(Clamped, Settings.JoystickScale))
	{
		return;
	}

	Settings.JoystickScale = Clamped;
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UMetamorphosisSettingsSubsystem::SetHotbarScale(float NewScale)
{
	const float Clamped = ClampScale(NewScale);
	if (FMath::IsNearlyEqual(Clamped, Settings.HotbarScale))
	{
		return;
	}

	Settings.HotbarScale = Clamped;
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UMetamorphosisSettingsSubsystem::SetMobileButtonScale(float NewScale)
{
	const float Clamped = ClampScale(NewScale);
	if (FMath::IsNearlyEqual(Clamped, Settings.MobileButtonScale))
	{
		return;
	}

	Settings.MobileButtonScale = Clamped;
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UMetamorphosisSettingsSubsystem::ResetToDefaults()
{
	Settings = FMetamorphosisGameSettings();
	ApplyAudioSettings();
	SaveSettings();
	OnSettingsChanged.Broadcast();

	UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 已恢复默认设置"));
}

// ---------------------------------------------------------------------------
// 存盘 / 读盘
// ---------------------------------------------------------------------------

void UMetamorphosisSettingsSubsystem::LoadSettings()
{
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Settings] 没有存档，使用默认设置"));
		Settings = FMetamorphosisGameSettings();
		return;
	}

	USaveGame* Loaded = UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex);
	UMetamorphosisSettingsSave* Save = Cast<UMetamorphosisSettingsSave>(Loaded);
	if (Save == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Settings] 存档读取失败，使用默认设置"));
		Settings = FMetamorphosisGameSettings();
		return;
	}

	Settings = Save->Settings;
	Settings.MasterVolume = FMath::Clamp(Settings.MasterVolume, 0.f, 1.f);
	Settings.JoystickScale = ClampScale(Settings.JoystickScale);
	Settings.HotbarScale = ClampScale(Settings.HotbarScale);
	Settings.MobileButtonScale = ClampScale(Settings.MobileButtonScale);

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[Settings] 已读档：音量 %.2f 摇杆%s 摇杆%.2f 物品栏%.2f 上下按钮%.2f"),
		Settings.MasterVolume, Settings.bFixedJoystick ? TEXT("固定") : TEXT("浮动"),
		Settings.JoystickScale, Settings.HotbarScale, Settings.MobileButtonScale);
}

void UMetamorphosisSettingsSubsystem::SaveSettings()
{
	UMetamorphosisSettingsSave* Save = Cast<UMetamorphosisSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UMetamorphosisSettingsSave::StaticClass()));

	if (Save == nullptr)
	{
		return;
	}

	Save->Settings = Settings;
	UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, SaveUserIndex);
}

// ---------------------------------------------------------------------------
// 生效
// ---------------------------------------------------------------------------

void UMetamorphosisSettingsSubsystem::ApplyAudioSettings()
{
	if (GEngine == nullptr)
	{
		return;
	}

	// 声音系统还没做，先把主音量压到音频设备上（做了 SoundClass/SoundMix 之后在这里接）
	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	if (AudioDevice.IsValid())
	{
		AudioDevice->SetTransientPrimaryVolume(Settings.MasterVolume);
	}
}


