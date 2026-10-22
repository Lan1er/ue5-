// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 单存档位：负责"采集/应用/自动存"。挂在 GameInstance 上（跨关卡存活）。
//
// 用法：
//   Save->SaveNow(TEXT("进关卡"));         // 存
//   Save->HasSave()                        // 开始界面「继续游戏」是否可用
//   Save->RequestContinue(this);           // 读档并回到关卡（由 PlayerController 调）
//   Save->DeleteSave();                    // 「开始游戏」= 新游戏时清掉

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MetamorphosisSaveSubsystem.generated.h"

class UMetamorphosisSaveGame;

UCLASS(Config = Game)
class METAMORPHOSIS_API UMetamorphosisSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 存档格式版本，改动 SaveGame 字段时 +1（旧档会被拒绝读取并提示） */
	static constexpr int32 CurrentVersion = 1;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Save")
	bool HasSave() const;

	/**
	 * 存一次。
	 * @param bForce true = 即使剧情/门正在播也存（"返回主界面/退出"这种玩家主动离场时必须存，
	 *               否则玩家在剧情里退出，背包和位置就丢在最后一次自动存上了 ✗）。
	 *               结算中永远跳过（状态不完整）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Save")
	bool SaveNow(const FString& Reason, bool bForce = false);

	/** 只读进内存（应用到 subsystems）。关卡切换由调用方负责。
	 *  bApplyImmediately：
	 *    true  = 若当前已在游戏关卡里（有 Pawn）就**立刻**应用挂起的背包/位置（给调试命令 Meta.Save.Load 用 ✓）
	 *    false = **只挂起**、绝不应用 ✗（RequestContinue 必须用这个 ✓✓）
	 *  为什么：主界面是挂在**游戏关卡**上的（不换图）✗ -> RequestContinue 里调 LoadNow 时"有 Pawn"是**上一局残留的** ✗
	 *  -> 若立刻应用，背包/位置会被写进**旧角色**、挂起标记被清空 ✗ -> 随后 OpenLevel 换了新角色 -> 新角色永远拿不到 ✗
	 *  = 用户报的"继续游戏后背包消失、位置不对" ✓✓ */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Save")
	bool LoadNow(bool bApplyImmediately = true);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Save")
	bool DeleteSave();

	/** 一行摘要：周目 / 怀疑 / 躁郁 / 任务 / 存档时间 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Save")
	FString Summary() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Save")
	FString GetSavePath() const;

	/** 通关后点「继续游戏」用：读档 -> 打开主地图 -> 下一帧应用位置 */
	void RequestContinue(UObject* WorldContextObject);

	/** 自动存心跳（秒，0=关） */
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Save")
	float AutoSaveIntervalSeconds = 60.f;

	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Save")
	bool bLogSave = true;

	/** 「开始游戏」= 新游戏：清档 + 各子系统回到初始 */
	void StartNewGame();

	/** 读档后等关卡加载完再应用玩家位置（由 PlayerController 的 BeginPlay 延迟调用） */
	void ApplyPendingPlayerTransform();

	/** 读档后清理场景里"玩家已经捡走过"的道具 Actor（否则关卡重载后它们又回来 -> 能再捡一次，物品重复） */
	void PurgeAlreadyObtainedWorldItems();

private:
	UPROPERTY()
	TObjectPtr<UMetamorphosisSaveGame> PendingSave;

	FTimerHandle AutoSaveTimer;

	/** 从当前世界采集 -> 填进 Save */
	void FillFromWorld(UMetamorphosisSaveGame& Out) const;

	/** 把 Save 应用到各子系统（数值/任务/背包/剧情） */
	void ApplyToSubsystems(const UMetamorphosisSaveGame& In);

	bool bHasPendingTransform = false;
	FVector PendingLocation = FVector::ZeroVector;
	float PendingYaw = 0.f;

	/** 背包必须在“游戏关卡加载完、Beetle 存在”之后再恢复 ✗
	 *  原因：LoadNow() 是在主界面（MainMenu 关卡）调用的 ✓ 那时没有 Beetle ✓
	 *  GetInventoryComponent() 返回 null -> 整段背包恢复被跳过 -> 进关卡后背包是空的（用户反馈的“道具消失”✗）。
	 *  所以这里先记下存档对象，等 ApplyPendingPlayerTransform() 时再补上。 */
	UPROPERTY()
	TObjectPtr<UMetamorphosisSaveGame> PendingInventorySource;

	bool bHasPendingInventory = false;

	/** 把 PendingInventorySource 里的背包恢复到当前 Beetle 的 InventoryComponent */
	void ApplyPendingInventory();

	// ---------- 剧情：延迟恢复（2026-10-19） ----------
	/** 存档里记的剧情节点 Id，进关卡后才恢复 ✓
	 *  不能在 LoadNow() 里直接 StartStory ✗ —— 那会**重跑该节点的 Effect** ✗：
	 *  若节点是 Death -> 读档瞬间触发周目结算 -> 清空背包 + 传回出生点 ✗✗
	 *  （用户报的“位置不对 + 背包消失”就是这个 ✓） */
	FString PendingStoryNodeId;

	bool bHasPendingStory = false;
};
