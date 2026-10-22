// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 配置读取子系统：启动时载入数值配置表，全游戏从这里取数。
// 载入优先级：
//   1) Content/Config/game_config.json（编辑器/开发期：改 Excel -> 跑 Tools/export_config.py -> 重启即生效，不用重编）
//   2) 编译内嵌的同一份 JSON（Config/GameConfigJson.inl，由 export_config.py 生成）
//      —— 用于"文件读不到"的场合，典型是 Android APK：非资产文件没被带进包（试过 NonUFS/UFS 两种打包设置都不生效），
//         真机上任务栏/交互全空。内嵌进二进制后一定带得上。
//   注意：改完配表要【重新编译】一次，内嵌版本才会同步（否则包里的还是旧的）。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Config/GameConfigTypes.h"
#include "MetamorphosisConfigSubsystem.generated.h"

UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisConfigSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 当前配置（载入失败时返回空配置，保证调用方不崩） */
	const FGameConfig& GetConfig() const { return Config; }

	UFUNCTION(BlueprintPure, Category = "Config")
	const FGlobalConfig& GetGlobal() const { return Config.Global; }

	UFUNCTION(BlueprintPure, Category = "Config")
	const FSuspicionConfig& GetSuspicion() const { return Config.Suspicion; }

	UFUNCTION(BlueprintPure, Category = "Config")
	const FMoodConfig& GetMood() const { return Config.Mood; }

	/** 查询某家人的动线表（纯 C++ 接口，蓝图侧请遍历 Schedules） */
	const FFamilySchedule* GetSchedule(FName FamilyId) const { return Config.GetSchedule(FamilyId); }

	/** 查询道具配表条目（纯 C++ 接口，蓝图侧请遍历 Items），查不到返回 nullptr */
	const FItemConfigEntry* GetItem(FName ItemId) const { return Config.GetItem(ItemId); }

	/** 道具配表全部条目（调试列表 / 后续背包 UI 用） */
	const TArray<FItemConfigEntry>& GetItems() const { return Config.Items; }

	/** 查询任务配表条目（纯 C++ 接口，蓝图侧请遍历 Tasks），查不到返回 nullptr */
	const FTaskConfigEntry* GetTask(FName TaskId) const { return Config.GetTask(TaskId); }

	/** 按 Order 查任务（Order 从 1 开始） */
	const FTaskConfigEntry* GetTaskByOrder(int32 Order) const { return Config.GetTaskByOrder(Order); }

	/** 任务配表全部条目（调试列表 / 任务 UI 用） */
	const TArray<FTaskConfigEntry>& GetTasks() const { return Config.Tasks; }

	/** 查询剧情节点（纯 C++ 接口，蓝图侧请遍历 Story），查不到返回 nullptr */
	const FStoryNodeConfig* GetStoryNode(FName NodeId) const { return Config.GetStoryNode(NodeId); }

	/** 剧情配表全部节点（调试列表用） */
	const TArray<FStoryNodeConfig>& GetStory() const { return Config.Story; }

	/** 重新从磁盘读取 JSON（调试用；失败时回退内嵌） */
	void Reload();

	/** 本次配置的来源（"文件：<path>" 或 "编译内嵌"），供日志/调试显示 */
	const FString& GetConfigSource() const { return ConfigSource; }

	/** 是否走的是编译内嵌版本（true = 文件不可用或强制内嵌） */
	bool IsUsingEmbeddedConfig() const { return bUsingEmbedded; }

private:
	/** 统一载入入口：文件 -> 内嵌 的优先级在这里实现 */
	bool LoadConfig();

	/** 开发期自检：文件内容与内嵌内容不一致时提醒"内嵌比文件旧，重新编译可同步" */
	void CheckEmbeddedMatchesFile(const FString& FileJson) const;

	/**
	 * 强制使用编译内嵌配表（忽略文件）。仅用于验证/排查；
	 * ini：[/Script/Metamorphosis.MetamorphosisConfigSubsystem] bForceEmbeddedConfig=True
	 */
	UPROPERTY(config, EditAnywhere, Category = "Config")
	bool bForceEmbeddedConfig = false;

	FGameConfig Config;
	FString ConfigSource;
	bool bUsingEmbedded = false;
};
