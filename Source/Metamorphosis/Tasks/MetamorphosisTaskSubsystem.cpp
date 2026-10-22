// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Tasks/MetamorphosisTaskSubsystem.h"

#include "Config/MetamorphosisConfigSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "Metamorphosis.h"
#include "Story/MetamorphosisStorySubsystem.h"

namespace
{
	/** 任务配表的静态空数组：配置没载入时给调用方一个安全的引用 */
	const TArray<FTaskConfigEntry>& EmptyTasks()
	{
		static const TArray<FTaskConfigEntry> Empty;
		return Empty;
	}
}

void UMetamorphosisTaskSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 配置子系统要先于本系统初始化（取任务表时直接拿它，不做延迟绑定）
	Collection.InitializeDependency<UMetamorphosisConfigSubsystem>();

	// 注意：这里是 GameInstance 生命周期，此时还没有 Pawn，所以**不能**在 Initialize 里绑定
	// 物品栏组件的委托（Pawn 是每关重新生成的）。事件入口走 NotifyItemObtained()，
	// 由物品栏组件在"获得/吃掉道具"时直接调用（见 MetamorphosisInventoryComponent::NotifyItemObtained）。

	const TArray<FTaskConfigEntry>& List = Tasks();
	UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务系统就绪：配表 %d 条，当前第 %d 条%s"),
		List.Num(), List.Num() > 0 ? 1 : 0,
		List.Num() > 0 ? *FString::Printf(TEXT("（%s）"), *List[0].Title) : TEXT(""));

	// 订阅剧情结束：任务完成剧情如果撞上别的剧情，就排队等它结束再补播
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMetamorphosisStorySubsystem* Story = GameInstance->GetSubsystem<UMetamorphosisStorySubsystem>())
		{
			Story->OnStoryChanged.AddDynamic(this, &UMetamorphosisTaskSubsystem::HandleStoryChanged);
		}
	}
}

void UMetamorphosisTaskSubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMetamorphosisStorySubsystem* Story = GameInstance->GetSubsystem<UMetamorphosisStorySubsystem>())
		{
			Story->OnStoryChanged.RemoveDynamic(this, &UMetamorphosisTaskSubsystem::HandleStoryChanged);
		}
	}
	Super::Deinitialize();
}

void UMetamorphosisTaskSubsystem::HandleStoryChanged(bool bPlaying, FName /*NodeId*/, const FText& /*Speaker*/,
	const FText& /*Text*/, bool /*bHasChoice*/)
{
	if (bPlaying || PendingCompletionStory.IsEmpty())
	{
		return;
	}

	const FString NodeToPlay = PendingCompletionStory;
	PendingCompletionStory.Reset();

	UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 上一段剧情已结束 -> 补播排队的完成剧情 '%s'"), *NodeToPlay);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMetamorphosisStorySubsystem* Story = GameInstance->GetSubsystem<UMetamorphosisStorySubsystem>())
		{
			if (!Story->StartStory(FName(*NodeToPlay)))
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] 排队的剧情节点 '%s' 播不了（配表里没有？）"), *NodeToPlay);
			}
		}
	}
}

UMetamorphosisTaskSubsystem* UMetamorphosisTaskSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	if (const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UMetamorphosisTaskSubsystem>();
		}
	}
	return nullptr;
}

UMetamorphosisConfigSubsystem* UMetamorphosisTaskSubsystem::GetConfigSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UMetamorphosisConfigSubsystem>();
	}
	return nullptr;
}

const TArray<FTaskConfigEntry>& UMetamorphosisTaskSubsystem::Tasks() const
{
	if (const UMetamorphosisConfigSubsystem* Config = GetConfigSubsystem())
	{
		return Config->GetTasks();
	}
	return EmptyTasks();
}

int32 UMetamorphosisTaskSubsystem::GetTaskCount() const
{
	return Tasks().Num();
}

const FTaskConfigEntry* UMetamorphosisTaskSubsystem::GetCurrentTask() const
{
	const TArray<FTaskConfigEntry>& List = Tasks();
	return List.IsValidIndex(CurrentIndex) ? &List[CurrentIndex] : nullptr;
}

FName UMetamorphosisTaskSubsystem::GetCurrentTaskId() const
{
	const FTaskConfigEntry* Task = GetCurrentTask();
	return Task != nullptr ? FName(*Task->TaskId) : NAME_None;
}

FText UMetamorphosisTaskSubsystem::GetCurrentTitle() const
{
	const FTaskConfigEntry* Task = GetCurrentTask();
	return Task != nullptr ? FText::FromString(Task->Title) : FText::GetEmpty();
}

FText UMetamorphosisTaskSubsystem::GetCurrentDetail() const
{
	const FTaskConfigEntry* Task = GetCurrentTask();
	return Task != nullptr ? FText::FromString(Task->Detail) : FText::GetEmpty();
}

FText UMetamorphosisTaskSubsystem::GetTitleAt(int32 Index) const
{
	const TArray<FTaskConfigEntry>& List = Tasks();
	return List.IsValidIndex(Index) ? FText::FromString(List[Index].Title) : FText::GetEmpty();
}

bool UMetamorphosisTaskSubsystem::IsAllCompleted() const
{
	const int32 Count = GetTaskCount();
	return Count > 0 && CurrentIndex >= Count;
}

void UMetamorphosisTaskSubsystem::SetCurrentIndexInternal(int32 NewIndex, const TCHAR* Reason)
{
	const TArray<FTaskConfigEntry>& List = Tasks();
	const int32 Clamped = FMath::Clamp(NewIndex, 0, List.Num());
	const int32 PreviousIndex = CurrentIndex;

	if (Clamped == CurrentIndex)
	{
		return;
	}
	CurrentIndex = Clamped;

	const FName TaskId = GetCurrentTaskId();
	const FText Title = GetCurrentTitle();

	if (List.IsValidIndex(CurrentIndex))
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务推进 -> 第 %d/%d 条：%s（%s）｜%s"),
			CurrentIndex + 1, List.Num(), *Title.ToString(), *TaskId.ToString(), Reason);
	}
	else
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 全部 %d 条任务已完成（%s）"), List.Num(), Reason);
	}

	OnTaskChanged.Broadcast(TaskId, CurrentIndex, Title);

	// 前一条任务"正常完成"（刚好往后挪一条）时，播它配的 OnCompleteStory。
	// 只认 +1 的推进：调试 SetTask 跳转/ResetToFirst 不应触发剧情。
	if (Clamped == PreviousIndex + 1 && List.IsValidIndex(PreviousIndex))
	{
		PlayCompletionStory(List[PreviousIndex]);
	}

	// 回溯判定：新任务可能"触发道具早就全在背包里"（玩家顺序颠倒），立刻收尾
	if (!bResolvingBacktrack)
	{
		TryCompleteCurrentByInventory(TEXT("进入任务时的回溯判定"));
	}
}

void UMetamorphosisTaskSubsystem::PlayCompletionStory(const FTaskConfigEntry& CompletedTask)
{
	if (CompletedTask.OnCompleteStory.IsEmpty())
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMetamorphosisStorySubsystem* Story = GameInstance->GetSubsystem<UMetamorphosisStorySubsystem>())
		{
			if (Story->IsPlaying())
			{
				// 撞车：不丢弃，排队等当前剧情结束再补播（否则"顺序颠倒"时第二章转场会丢）
				PendingCompletionStory = CompletedTask.OnCompleteStory;
				UE_LOG(LogMetamorphosis, Warning,
					TEXT("[Task] 任务「%s」完成后要播剧情 '%s'，但当前已有剧情在播 -> 排队等它结束后补播"),
					*CompletedTask.TaskId, *CompletedTask.OnCompleteStory);
				return;
			}

			UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务「%s」完成 -> 播放剧情节点 '%s'"),
				*CompletedTask.TaskId, *CompletedTask.OnCompleteStory);

			if (!Story->StartStory(FName(*CompletedTask.OnCompleteStory)))
			{
				UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] 剧情节点 '%s' 播不了（配表里没有？）"),
					*CompletedTask.OnCompleteStory);
			}
			return;
		}
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] 拿不到剧情子系统，任务「%s」的完成剧情 '%s' 没播"),
		*CompletedTask.TaskId, *CompletedTask.OnCompleteStory);
}

UMetamorphosisInventoryComponent* UMetamorphosisTaskSubsystem::GetPlayerInventory() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr)
	{
		return nullptr;
	}

	const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
	if (LocalPlayers.Num() == 0 || LocalPlayers[0] == nullptr)
	{
		return nullptr;
	}

	if (APlayerController* PC = LocalPlayers[0]->GetPlayerController(GameInstance->GetWorld()))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			return Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>();
		}
	}
	return nullptr;
}

bool UMetamorphosisTaskSubsystem::IsItemOwned(const FString& ItemId) const
{
	const UMetamorphosisInventoryComponent* Inventory = GetPlayerInventory();
	if (Inventory == nullptr)
	{
		return false;
	}

	const FName Want(*ItemId);
	for (int32 i = 0; i < UMetamorphosisInventoryComponent::NumSlots; ++i)
	{
		const FMetamorphosisItemSlot Slot = Inventory->GetSlot(i);
		if (Slot.bOccupied && Slot.ItemId == Want && Slot.Count > 0)
		{
			return true;
		}
	}
	return false;
}

bool UMetamorphosisTaskSubsystem::IsItemSatisfied(const FString& ItemId) const
{
	// 背包里有 = 满足；本局已经获得/吃掉过 = 也算满足
	// （红糖/面条这类「不可携带、交互即就地吃掉」的道具永远不会进背包）
	return ObtainedItemIds.Contains(ItemId) || IsItemOwned(ItemId);
}

bool UMetamorphosisTaskSubsystem::AreTriggerItemsSatisfied(const FTaskConfigEntry& Task) const
{
	const TArray<FString> Wanted = Task.GetTriggerItemIds();
	if (Wanted.Num() == 0)
	{
		return false;
	}

	for (const FString& Id : Wanted)
	{
		if (!IsItemSatisfied(Id))
		{
			return false;
		}
	}
	return true;
}

bool UMetamorphosisTaskSubsystem::TryCompleteCurrentByInventory(const FString& Reason)
{
	if (bResolvingBacktrack)
	{
		return false;
	}

	TGuardValue<bool> Guard(bResolvingBacktrack, true);

	bool bAnyAdvanced = false;
	// 可能会连续完成多条（例如两个任务各自的道具早就都拿到过）
	while (const FTaskConfigEntry* Task = GetCurrentTask())
	{
		if (!AreTriggerItemsSatisfied(*Task))
		{
			break;
		}

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Task] 回溯判定：任务「%s」（第 %d 条）的触发道具已全部拿到过（%s）-> 立即完成（%s）"),
			*Task->TaskId, CurrentIndex + 1, *Task->TriggerItemId, *Reason);

		const int32 Before = CurrentIndex;
		Advance();
		bAnyAdvanced = true;

		// 保险：Advance 没推动（已经是最后一条之后）就停，避免死循环
		if (CurrentIndex == Before)
		{
			break;
		}
	}
	return bAnyAdvanced;
}

void UMetamorphosisTaskSubsystem::Advance()
{
	SetCurrentIndexInternal(CurrentIndex + 1, TEXT("Advance"));
}

void UMetamorphosisTaskSubsystem::SetTask(FName TaskId)
{
	const TArray<FTaskConfigEntry>& List = Tasks();
	const FString Want = TaskId.ToString();
	for (int32 i = 0; i < List.Num(); ++i)
	{
		if (List[i].TaskId == Want)
		{
			SetCurrentIndexInternal(i, TEXT("SetTask"));
			return;
		}
	}

	UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] SetTask 失败：任务表里没有 '%s'（共 %d 条）"),
		*Want, List.Num());
}

void UMetamorphosisTaskSubsystem::ResetToFirst()
{
	// 回到第一条：即使当前已经在第一条也要广播一次，方便 UI/剧情重新对齐
	const int32 Before = CurrentIndex;
	CurrentIndex = 0;
	bLoggedCompleted = false;
	// 周目重置：道具也都清空了，记账表一起清（否则上一周目吃过的红糖会顶掉这一周目的任务）
	ObtainedItemIds.Reset();
	PendingCompletionStory.Reset();

	const FName TaskId = GetCurrentTaskId();
	const FText Title = GetCurrentTitle();
	UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务重置到第一条（原第 %d 条）：%s（%s）"),
		Before + 1, *Title.ToString(), *TaskId.ToString());
	OnTaskChanged.Broadcast(TaskId, CurrentIndex, Title);
}

bool UMetamorphosisTaskSubsystem::NotifyItemObtained(FName ItemId)
{
	// 先记账：「就地吃掉」的道具（红糖/面条）永远不会进背包，只能靠这份记录判定
	const FString Obtained = ItemId.ToString();
	if (!Obtained.IsEmpty())
	{
		ObtainedItemIds.Add(Obtained);
	}

	const FTaskConfigEntry* Task = GetCurrentTask();
	if (Task == nullptr)
	{
		return false;
	}

	// 只有"当前任务"的触发道具才推进：后面的任务不会因为提前捡到东西而跳步。
	// 2026-10-18：触发道具可以是逗号分隔的**多件**（含义=全部拿到），所以这里不比对单件，
	// 而是交给"是否全部满足"统一判断 -> 顺序颠倒也能过（先拿工资条、后拿红糖）。
	const TArray<FString> Wanted = Task->GetTriggerItemIds();
	if (Wanted.Num() == 0)
	{
		return false;
	}

	if (!AreTriggerItemsSatisfied(*Task))
	{
		// 只拿到其中一部分：把还差哪件打出来，方便调剧情（Log 级别，一轮只会出现几次）
		FString Missing;
		for (const FString& Id : Wanted)
		{
			if (!IsItemSatisfied(Id))
			{
				Missing += Missing.IsEmpty() ? Id : (TEXT(",") + Id);
			}
		}
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Task] 拿到 %s：还不够当前任务「%s」（还差 %s）"),
			*ItemId.ToString(), *Task->TaskId, *Missing);
		return false;
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 道具事件命中任务触发条件：%s -> 完成「%s」"),
		*ItemId.ToString(), *Task->Title);
	Advance();
	return true;
}

// ---------------------------------------------------------------------------
// 调试命令（Meta.Task.*）
// 与 Meta.Inv.* / Meta.Interact.* 同一风格：谁的系统谁提供命令。
// 注意：匿名命名空间里的辅助函数都带 Task 前缀，避免 unity build 拼到同一编译单元时撞名。
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
namespace MetamorphosisTaskConsole
{
	static UMetamorphosisTaskSubsystem* GetTaskSubsystem(UWorld* World)
	{
		return World != nullptr ? UMetamorphosisTaskSubsystem::Get(World) : nullptr;
	}

	static void TaskPrint(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisTaskSubsystem* Tasks = GetTaskSubsystem(World);
		if (Tasks == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] 拿不到任务子系统"));
			return;
		}

		if (const UMetamorphosisConfigSubsystem* Config = World->GetGameInstance() != nullptr
			? World->GetGameInstance()->GetSubsystem<UMetamorphosisConfigSubsystem>() : nullptr)
		{
			const TArray<FTaskConfigEntry>& List = Config->GetTasks();
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 任务配表 %d 条，当前第 %d 条："),
				List.Num(), Tasks->GetCurrentIndex() + 1);
			for (int32 i = 0; i < List.Num(); ++i)
			{
				const FTaskConfigEntry& T = List[i];
				UE_LOG(LogMetamorphosis, Log, TEXT("[Debug]   %s%d. %s（%s）触发道具=%s%s%s"),
					i == Tasks->GetCurrentIndex() ? TEXT("★ ") : TEXT("   "),
					T.Order, *T.Title, *T.TaskId,
					T.TriggerItemId.IsEmpty() ? TEXT("（无）") : *T.TriggerItemId,
					T.Detail.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" ｜ %s"), *T.Detail),
					T.OnCompleteStory.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" ｜ 完成播剧情=%s"), *T.OnCompleteStory));
			}
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] 拿不到配置子系统，打印不了任务表"));
		}
	}

	static void TaskNext(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisTaskSubsystem* Tasks = GetTaskSubsystem(World))
		{
			Tasks->Advance();
		}
	}

	static void TaskSet(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Task] 用法：Meta.Task.Set <TaskId>（如 Meta.Task.Set Task03）"));
			return;
		}
		if (UMetamorphosisTaskSubsystem* Tasks = GetTaskSubsystem(World))
		{
			Tasks->SetTask(FName(*Args[0]));
		}
	}

	static void TaskReset(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisTaskSubsystem* Tasks = GetTaskSubsystem(World))
		{
			Tasks->ResetToFirst();
		}
	}

	/** 手动触发一次"回溯判定"（调试/自动化验证用：先给道具再敲这条，等价于"进入任务时的判定"） */
	static void TaskCheck(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisTaskSubsystem* Tasks = GetTaskSubsystem(World))
		{
			const bool bAdvanced = Tasks->TryCompleteCurrentByInventory(TEXT("调试命令 Meta.Task.Check"));
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 回溯判定结果：%s（当前第 %d 条）"),
				bAdvanced ? TEXT("推进了") : TEXT("没有推进"), Tasks->GetCurrentIndex() + 1);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaTaskPrint(
		TEXT("Meta.Task.Print"),
		TEXT("打印任务配表与当前进度"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TaskPrint));

	static FAutoConsoleCommandWithWorldAndArgs GMetaTaskNext(
		TEXT("Meta.Task.Next"),
		TEXT("推进到下一条任务"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TaskNext));

	static FAutoConsoleCommandWithWorldAndArgs GMetaTaskSet(
		TEXT("Meta.Task.Set"),
		TEXT("跳到指定任务：Meta.Task.Set <TaskId>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TaskSet));

	static FAutoConsoleCommandWithWorldAndArgs GMetaTaskReset(
		TEXT("Meta.Task.Reset"),
		TEXT("回到第一条任务"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TaskReset));

	static FAutoConsoleCommandWithWorldAndArgs GMetaTaskCheck(
		TEXT("Meta.Task.Check"),
		TEXT("对当前任务做一次回溯判定（触发道具已全在背包里就立即完成）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TaskCheck));
}
#endif // !UE_BUILD_SHIPPING

// ------------------------------------------------------------------
// 剧情目标覆盖（玩法门期间顶上任务标题；门结束由剧情系统清掉）
// ------------------------------------------------------------------

void UMetamorphosisTaskSubsystem::SetObjectiveOverride(const FText& InText, float TimeLimitSeconds)
{
	ObjectiveOverride = InText;
	ObjectiveOverrideDeadline = TimeLimitSeconds > 0.f ? FPlatformTime::Seconds() + TimeLimitSeconds : -1.0;

	if (TimeLimitSeconds > 0.f)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务 HUD 目标覆盖：%s（限时 %.0f 秒）"),
			*ObjectiveOverride.ToString(), TimeLimitSeconds);
	}
	else
	{
		// 保持阶段/不限时的目标：不要打「（限时 0 秒）」，否则看起来像 bug（2026-10-17）
		UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务 HUD 目标覆盖：%s"),
			*ObjectiveOverride.ToString());
	}

	// 广播一次让 UI 立刻刷新（内容变化即触发）
	OnTaskChanged.Broadcast(GetCurrentTaskId(), CurrentIndex, GetCurrentTitle());
}

void UMetamorphosisTaskSubsystem::ClearObjectiveOverride()
{
	if (ObjectiveOverride.IsEmpty())
	{
		return;
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[Task] 任务 HUD 目标覆盖已清除（恢复显示当前任务）"));

	ObjectiveOverride = FText::GetEmpty();
	ObjectiveOverrideDeadline = -1.0;
	OnTaskChanged.Broadcast(GetCurrentTaskId(), CurrentIndex, GetCurrentTitle());
}

float UMetamorphosisTaskSubsystem::GetObjectiveSecondsLeft() const
{
	if (ObjectiveOverride.IsEmpty())
	{
		return -1.f;
	}
	if (ObjectiveOverrideDeadline <= 0.0)
	{
		return -1.f;
	}
	return static_cast<float>(ObjectiveOverrideDeadline - FPlatformTime::Seconds());
}
