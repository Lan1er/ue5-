// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 单存档位的存档对象。所有字段都是 UPROPERTY（才能被 USaveGame 序列化）。
// 注意：TSoftObjectPtr 在存档里不好往返，这里统一存【路径字符串】，读档时再 LoadSynchronous。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MetamorphosisSaveGame.generated.h"

UCLASS()
class METAMORPHOSIS_API UMetamorphosisSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 存档版本：与 Subsystem 的 CurrentVersion 不一致时拒绝读取（避免改了结构后读到脏数据） */
	UPROPERTY() int32 Version = 1;

	UPROPERTY() FString SavedAtIso;
	UPROPERTY() FString SavedReason;

	// ---- 进度 ----
	UPROPERTY() int32 LoopCount = 1;
	UPROPERTY() int32 SettledCount = 0;

	// ---- 数值 ----
	UPROPERTY() float Suspicion = 0.f;
	UPROPERTY() float Mood = 0.f;
	UPROPERTY() float DayMinutes = 0.f;

	/** 当前时段 Id（2026-10-19 时间/次数系统）：读档照抄，不按时间重算 */
	UPROPERTY() FString CurrentPeriodId;

	/** 本时段剩余探索次数（-1 = 老存档没有这项，读档时按时间重算/拉满） */
	UPROPERTY() int32 ExploreChancesRemaining = -1;

	// ---- 任务 ----
	UPROPERTY() int32 CurrentTaskIndex = 0;
	UPROPERTY() FString CurrentTaskId;
	/** 本局"获得过"的道具（任务回溯判定用，含不可携带、就地吃掉的） */
	UPROPERTY() TArray<FString> ObtainedItemIds;

	// ---- 剧情（只存节点 Id；读档后从该节点重播） ----
	UPROPERTY() FString StoryNodeId;
	/** 存档那一刻是否正在【玩法门】里（躲床下/离婴儿床远一点/区域门等） */
	UPROPERTY() bool bWasInGate = false;
	/** 门所在节点 Id（读档后用它重新布防 -> 倒计时从 0 开始，任务不会被跳过） */
	UPROPERTY() FString GateNodeId;

	// ---- 玩家 ----
	UPROPERTY() FVector PlayerLocation = FVector::ZeroVector;
	UPROPERTY() float CameraYaw = 0.f;

	// ---- 背包（并行数组，比结构体数组更容易序列化） ----
	UPROPERTY() TArray<FString> SlotItemIds;
	UPROPERTY() TArray<int32> SlotCounts;
	/** 每个槽原世界模型（网格路径，空格 = 没有） */
	UPROPERTY() TArray<FString> SlotWorldMeshes;
	UPROPERTY() TArray<FTransform> SlotWorldTransforms;
	/** 材质路径，多个用 ';' 分隔 */
	UPROPERTY() TArray<FString> SlotWorldMaterials;
};
