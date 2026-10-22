// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 当前任务系统（P0 骨架，2026-10-14）
//
// 职责边界：
//   * 维护"当前进行到第几条任务"，并把任务配表（ConfigSource/数值配置表.xlsx 的「Tasks」sheet ->
//     Content/Config/game_config.json 的 Tasks[]）暴露给 UI；
//   * 推进方式有两种，除此之外不做任何剧情判断：
//       1) 事件推进：拿到/吃掉某件道具 -> NotifyItemObtained()，命中的话自动 Advance()；
//       2) 显式推进：剧情系统或策划调试调用 Advance() / SetTask() / ResetToFirst()；
//   * 不做任务列表 UI、不做存档、不做奖励结算——这些等剧情系统定稿后接在这几个 public 函数上。
//
// 跨关卡保留：挂 GameInstance，与 Stats 子系统一致（重置游戏才回到第一条）。
// 调试：控制台 Meta.Task.Print / Meta.Task.Next / Meta.Task.Set <TaskId> / Meta.Task.Reset

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Config/GameConfigTypes.h"
#include "MetamorphosisTaskSubsystem.generated.h"

class UMetamorphosisConfigSubsystem;

/** 任务变化广播：UI 订阅这个即可（无需每帧轮询）。
 *  TaskIndex 是 0 基下标（-1 表示没有任务），Title 已取好文案方便直接显示。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMetamorphosisTaskChanged, FName, TaskId, int32, TaskIndex, const FText&, Title);

UCLASS()
class METAMORPHOSIS_API UMetamorphosisTaskSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ---------- USubsystem ----------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 从任意 WorldContext 取本系统（拿不到返回 nullptr，调用方自行兜底） */
	static UMetamorphosisTaskSubsystem* Get(const UObject* WorldContextObject);

	// ---------- 查询 ----------
	/** 当前任务（没有任务时返回 nullptr） */
	const FTaskConfigEntry* GetCurrentTask() const;

	/** 当前任务下标（0 基；没有任务返回 INDEX_NONE） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	int32 GetCurrentIndex() const { return CurrentIndex; }

	/** 任务总数 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	int32 GetTaskCount() const;

	/** 是否已经做到最后一条之后（全部完成） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	bool IsAllCompleted() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	FText GetCurrentTitle() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	FText GetCurrentDetail() const;

	/** 当前任务的 Id（无任务返回 NAME_None） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	FName GetCurrentTaskId() const;

	/** 第 N 条任务的标题（N 为 0 基下标），越界返回空文本。任务列表面板将来用得上 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	FText GetTitleAt(int32 Index) const;

	// ---------- 推进 ----------
	/** 推进到下一条（已是最后一条则标记全部完成并广播一次） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	void Advance();

	/** 跳到指定任务（TaskId 查不到则打警告并保持原状） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	void SetTask(FName TaskId);

	/** 回到第一条（周目重置/重新开始时调用） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	void ResetToFirst();

	/** 读档用：本局"获得过"的道具记账（任务回溯判定依赖它） */
	const TSet<FString>& GetObtainedItemIds() const { return ObtainedItemIds; }
	void SetObtainedItemIds(const TArray<FString>& In)
	{
		ObtainedItemIds.Reset();
		for (const FString& Id : In)
		{
			if (!Id.IsEmpty())
			{
				ObtainedItemIds.Add(Id);
			}
		}
	}

	/** 道具事件入口：拾取或吃掉某件道具时调用。当前任务的触发道具**全部拥有**就自动推进。
	 *  返回 true 表示"这一下把任务推进了"（调用方可据此播特效/音效）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	bool NotifyItemObtained(FName ItemId);

	/** 回溯判定：当前任务如果"触发道具已经全在背包里"，立刻完成并继续往下推进。
	 *  用途：玩家**顺序颠倒**（先拿到工资条、后拿到红糖）时不会卡死；也用于剧情 SetTask 跳到
	 *  一个早就满足的任务时立即收尾。返回 true = 至少推进了一条。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	bool TryCompleteCurrentByInventory(const FString& Reason);

	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Task")
	FMetamorphosisTaskChanged OnTaskChanged;

	// ---------- 剧情目标覆盖（玩法门期间显示在任务 HUD 上，2026-10-16） ----------
	/** 用剧情给的目标文字临时覆盖任务 HUD 的标题（门结束时务必 ClearObjectiveOverride）。
	 *  TimeLimitSeconds > 0 时到点自动失效（保险，正常由剧情系统在门结束时清）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	void SetObjectiveOverride(const FText& InText, float TimeLimitSeconds = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Task")
	void ClearObjectiveOverride();

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	bool HasObjectiveOverride() const { return !ObjectiveOverride.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	FText GetObjectiveOverride() const { return ObjectiveOverride; }

	/** 覆盖还剩几秒（<0 = 不限时或没有覆盖） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Task")
	float GetObjectiveSecondsLeft() const;

private:
	UMetamorphosisConfigSubsystem* GetConfigSubsystem() const;

	/** 本地玩家的物品栏组件（Pawn 还没生成/没有本地玩家时返回 nullptr） */
	class UMetamorphosisInventoryComponent* GetPlayerInventory() const;

	/** 背包里是否已有该道具（数量 > 0） */
	bool IsItemOwned(const FString& ItemId) const;

	/** 该道具是否"已满足"：背包里有 **或** 本局已经获得/吃掉过。
	 *  后者是必须的：红糖/面条这类「不可携带、交互即就地吃掉」的道具永远不会进背包，
	 *  只用背包判定的话 "找红糖" 任务永远完不成。 */
	bool IsItemSatisfied(const FString& ItemId) const;

	/** 当前任务的触发道具是否"全部满足"（没有触发道具 = false） */
	bool AreTriggerItemsSatisfied(const FTaskConfigEntry& Task) const;

	/** 任务配表（按 Order 升序，缓存一份序号表；配置没载入时为空） */
	const TArray<FTaskConfigEntry>& Tasks() const;

	void SetCurrentIndexInternal(int32 NewIndex, const TCHAR* Reason);

	/** 当前任务完成时播它配的 OnCompleteStory（空则不播）。
	 *  如果此刻已有别的剧情在播（例如"顺序颠倒"时两条任务的完成剧情撞在一起），
	 *  就排进 PendingCompletionStory，等当前剧情结束后自动补播（见 HandleStoryChanged）。 */
	void PlayCompletionStory(const FTaskConfigEntry& CompletedTask);

	/** 剧情系统广播：bPlaying 变 false（剧情结束）时，把排队的完成剧情播出去。
	 *  注意 OnStoryChanged 是**动态**多播委托，所以这里必须是 UFUNCTION 并用 AddDynamic 绑定。 */
	UFUNCTION()
	void HandleStoryChanged(bool bPlaying, FName NodeId, const FText& Speaker, const FText& Text, bool bHasChoice);

	/** 等当前剧情结束后要补播的节点（空 = 没有） */
	FString PendingCompletionStory;

	/** 本局已经获得/吃掉过的道具 Id（用于"不可携带"道具的任务判定与回溯判定；周目重置时清空） */
	TSet<FString> ObtainedItemIds;

	/** 回溯判定的重入保护：SetCurrentIndexInternal -> TryComplete -> Advance -> SetCurrentIndexInternal ... */
	bool bResolvingBacktrack = false;

	/** 当前任务下标；INDEX_NONE = 没有任务 */
	int32 CurrentIndex = 0;

	/** 是否已广播过"全部完成"（避免重复刷屏） */
	bool bLoggedCompleted = false;

	/** 配置是否已就绪（首帧配置可能还没载入） */
	bool bInitializedFromConfig = false;

	/** 剧情玩法门的目标文字覆盖（空 = 没有覆盖，显示正常任务） */
	FText ObjectiveOverride;

	/** 覆盖的失效时刻（FPlatformTime::Seconds；<=0 = 不限时） */
	double ObjectiveOverrideDeadline = -1.0;
};
