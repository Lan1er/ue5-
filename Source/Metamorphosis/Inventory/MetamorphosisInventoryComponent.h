// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 8 格快捷栏 / 物品栏的数据与选中状态。
//
// 2026-10-13：道具配表（Content/Config/game_config.json 的 Items）定稿，这里接上了
//   * AddItem()          —— 查配表、按 MaxStack 堆叠、找空格（bCarryable=false 会被拒绝）
//   * UseSelectedSlot()  —— 按配表施加效果（躁郁/怀疑/永久速度加成），可消耗的扣数量/清格
// 格子的数据结构仍是 FMetamorphosisItemSlot：ItemId / 名字 / 图标(软引用) / 占位色 / 数量。
// 图标按约定路径软引用：/Game/UI/Items/T_Item_<ItemId>（美术图没到就退化成 PlaceholderColor）。
//
// 输入对应关系：
//   PC   —— 数字键 1..9、0 直选；滚轮（或手柄左右方向键）翻页，见 MetamorphosisMoveInput
//   移动端 —— 直接点格子（由快捷栏 UI 调 SelectSlot）
//
// TODO（设计决策，未做）：使用道具的按键/按钮绑定（PC 右键？移动端长按？）还没定，
//   现在只能用调试命令 Meta.Inv.Use 触发。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MetamorphosisInventoryComponent.generated.h"

class UTexture2D;
class UStaticMesh;
class UMaterialInterface;
struct FItemConfigEntry;

/** 一个格子里的东西（占位数据结构，等道具表定稿后扩展） */
USTRUCT(BlueprintType)
struct FMetamorphosisItemSlot
{
	GENERATED_BODY()

	/** 格子里有没有东西 */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	bool bOccupied = false;

	/** 道具 ID（对应以后的策划道具表） */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FName ItemId;

	/** 显示名 */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	/** 图标（软引用，用的时候才加载；留空则用下面的占位色块） */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	/** 美术图还没到之前，用纯色块占位（方便验证 UI） */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FLinearColor PlaceholderColor = FLinearColor(0.75f, 0.75f, 0.78f, 1.f);

	/** 数量（0 表示不显示数量角标） */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Count = 0;

	// ---------------- 世界模型（2026-10-18：丢出来要还原成原本的模型） ----------------
	//
	// 拾取时把场景里那个 Actor 的**可视网格**记下来，DropSlot() 时照抄回世界。
	// 只记网格 + 变换 + 材质（都是资产引用，原 Actor 被 Destroy 也不影响）。
	// 没记到（例如 Meta.Inv.Give 直接塞进背包的）就退回"引擎球 + 占位色"，并打 Warning。

	/** 原物件用的静态网格（软引用） */
	UPROPERTY(BlueprintReadOnly, Category = "Item|World")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	/** 原网格在**世界空间**的变换：丢弃时只用它的旋转/缩放，位置按落点重算。
	 *  这样丢出来的道具朝向、大小都和地编摆的一致（例如翻倒的纸片不会站起来）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Item|World")
	FTransform WorldMeshTransform = FTransform::Identity;

	/** 原网格上的材质（含覆盖材质；动态材质实例是临时对象，不记录）。
	 *  有些 BP 道具的材质是挂在组件上而不是网格资产上，不记就会丢出来变灰。 */
	UPROPERTY(BlueprintReadOnly, Category = "Item|World")
	TArray<TSoftObjectPtr<UMaterialInterface>> WorldMeshMaterials;

	/** 有没有记到世界模型（UI/调试看这个） */
	bool HasWorldLook() const { return !WorldMesh.IsNull(); }

	void Clear()
	{
		bOccupied = false;
		ItemId = NAME_None;
		DisplayName = FText::GetEmpty();
		Icon = nullptr;
		PlaceholderColor = FLinearColor(0.75f, 0.75f, 0.78f, 1.f);
		Count = 0;

		// 世界模型也要清：否则下一格同样的道具会"继承"上一格的模型（周目重置时尤其致命）
		WorldMesh = nullptr;
		WorldMeshTransform = FTransform::Identity;
		WorldMeshMaterials.Reset();
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMetamorphosisSlotSelected, int32, NewIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMetamorphosisInventoryChanged);

/** 拿到/吃掉一件道具（参数是 ItemId）。任务系统靠它做"事件推进"，UI/特效也可以订阅。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMetamorphosisItemObtained, FName, ItemId);

/**
 *  挂在虫形主角身上的物品栏组件。
 *  UI（快捷栏）与输入都通过它读写，别各自存一份状态。
 */
UCLASS(ClassGroup = (Metamorphosis), meta = (BlueprintSpawnableComponent))
class METAMORPHOSIS_API UMetamorphosisInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 格子数（改这里要同时改 UMetamorphosisInputMappingContext_Move::NumSlotKeys） */
	static constexpr int32 NumSlots = 8;

	UMetamorphosisInventoryComponent();

	/** 当前选中的格子（0 起） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Inventory")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/** 选中某一格（越界会被夹到合法范围） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void SelectSlot(int32 Index);

	/** 相对切换：Delta = +1 往右一格（滚轮向前），-1 往左；会环绕 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void CycleSelection(int32 Delta);

	/** 读某一格（越界返回安全空格），UI 用 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Inventory")
	FMetamorphosisItemSlot GetSlot(int32 Index) const;

	/** 写某一格（以后拾取/使用道具走这里） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void SetSlot(int32 Index, FMetamorphosisItemSlot NewSlot);

	/** 清空所有格子 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void ClearAllSlots();

	// ---------------- 道具（配表驱动，2026-10-13） ----------------

	/** 获得道具：查配表 -> 先堆到没满的同种格子 -> 再找空格。
	 *  这些情况会失败并打日志：配表里没有这个 ItemId、bCarryable=false、栏位放不下。
	 *  任何一格发生变化都会广播 OnInventoryChanged。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	bool AddItem(FName ItemId, int32 Count = 1);

	/** 从世界里的某个 Actor 拾取道具：**比 AddItem 多记一份"世界模型"**，
	 *  DropSlot() 时就能把原本的模型还原出来（而不是占位球）。
	 *  SourceActor 可以为空/没有网格 -> 行为等同 AddItem（丢出来还是球 + Warning）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	bool AddItemFromWorld(FName ItemId, int32 Count, AActor* SourceActor);

	/** 当前选中格的内容（空格子时 bOccupied=false） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Inventory")
	FMetamorphosisItemSlot GetSelectedSlot() const;

	/** 使用当前选中格的道具：按配表施加效果（MoodDelta / SuspicionDelta / SpeedBonusPercent）；
	 *  bConsumable 时扣数量（归零清格）；非可消耗的剧情道具只打一行"查看了 XXX"日志。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	bool UseSelectedSlot();

	/** 使用**指定格**的道具（背包界面用；与 UseSelectedSlot 共用同一份实现，效果逻辑只有一处）。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	bool UseSlot(int32 Index);

	/** 丢弃**指定格**的道具：在玩家前方 DropDistanceCm 处、贴地生成一个 AMetamorphosisInteractable
	 *  （可被交互系统重新拾取），并把该格数量扣掉（整格清空）。
	 *  失败情况：格子为空、没有世界/Pawn、生成失败。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	bool DropSlot(int32 Index);

	/** 丢弃时物品落到玩家前方的距离（厘米） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Inventory")
	float DropDistanceCm = 80.f;

	/** 丢弃时在"球底贴地"之外再额外抬高多少厘米（0 = 正好贴地，推荐）。
	 *  ⚠️ 历史坑：这个值以前默认 12，而当时是把**球心**放在地面 + 12cm，球半径又有 27.5cm
	 *  -> 半个球埋进地里、配合待机浮动看起来像"卡在地里一直弹"。现在高度按"球底贴地"算，
	 *  这个值只作为额外抬高（正常情况下保持 0）。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Inventory")
	float DropHeightCm = 0.f;

	/** 丢弃物**可见替身**的**直径**（厘米）。默认 55，和场景里原本的交互道具（Test1/Test2 那两个球）量级一致；
	 *  这是占位视觉（引擎球体 + 配表占位色），等美术给道具模型后替换。
	 *  注意：它和躁郁值体型阶段**无关**，每次丢出来都一样大；球心高度 = 地面 + 本值/2 + DropHeightCm。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Inventory")
	float DropVisualSizeCm = 55.f;

	// ---------------- 道具效果 / 外观（交互系统与 UI 共用） ----------------

	/** 施加配表里的道具效果（躁郁 / 怀疑 / 永久速度加成）。
	 *  **效果逻辑只有这一份**：UseSelectedSlot()（用快捷栏里的）与交互系统的"就地吃掉"都调它。
	 *  OutSummary 形如「躁郁 -10 / 怀疑 +0 / 速度 +0%」，直接进日志。 */
	static void ApplyItemEffects(const UObject* WorldContextObject, const FItemConfigEntry& Entry, FString& OutSummary);

	/** 配表占位色（6 位十六进制，可带 #）-> 线性色；解析失败退回中性灰。
	 *  美术图到位前，UI 用它给图标染色（快捷栏与交互提示共用同一套解析）。 */
	static FLinearColor ParsePlaceholderColor(const FString& Hex);

	/** 按图标约定取某个道具的图标软引用（美术图没到 -> LoadSynchronous 为空 -> UI 退占位色） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Inventory")
	TSoftObjectPtr<UTexture2D> GetItemIcon(FName ItemId) const;

	/** 取图标贴图并【持有硬引用】：换关卡后资源包可能被卸载，只靠软引用会让 UImage 画成白块。
	 *  这里把加载到的贴图缓存进 UPROPERTY（GC 追踪 + 阻止卸载），换关卡后依然有效。 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Icon")
	UTexture2D* GetItemIconResolved(FName ItemId);

	/** 调试用：填 10 个占位物品（验证选中框/图标/数量显示） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void FillWithDebugItems();

	/** 道具事件入口：拿到（AddItem 成功）或就地吃掉一件道具时调用。
	 *  会广播 OnItemObtained，并转给任务系统做"事件推进"（当前任务的 TriggerItemId 命中就完成）。
	 *  调用方不需要知道有谁在听——任务系统是 GameInstance 级的，这里只是把事件送出去。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Inventory")
	void NotifyItemObtained(FName ItemId);

	/** 选中格变化 */
	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Inventory")
	FMetamorphosisSlotSelected OnSelectionChanged;

	/** 格子内容变化 */
	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Inventory")
	FMetamorphosisInventoryChanged OnInventoryChanged;

	/** 获得/吃掉道具（任务推进、音效、特效都可以挂这里） */
	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Inventory")
	FMetamorphosisItemObtained OnItemObtained;

	// ---------------- 图标约定（美术图到位前走占位色） ----------------

	/** 道具图标目录：软引用按 "<目录>/<前缀><ItemId>" 拼，例如 /Game/UI/Items/T_Item_BrownSugar */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Inventory|Icon")
	FString ItemIconFolder = TEXT("/Game/UI/Items");

	/** 道具图标文件名前缀 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Inventory|Icon")
	FString ItemIconPrefix = TEXT("T_Item_");

	/** 图标硬引用缓存（Key = ItemId）。UPROPERTY 保证 GC 追踪、换关卡不被卸载。 */
	UPROPERTY()
	TMap<FName, TObjectPtr<UTexture2D>> ItemIconCache;

protected:
	virtual void BeginPlay() override;

	/** 查道具配表（拿不到配置子系统/查不到都返回 nullptr） */
	const FItemConfigEntry* FindItemConfig(FName ItemId) const;

	/** 按上面的目录/前缀约定拼出该道具的图标软引用 */
	TSoftObjectPtr<UTexture2D> MakeIconPath(const FString& ItemIdString) const;

	/** 当前选中格 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Metamorphosis|Inventory")
	int32 SelectedIndex = 0;

	/** 格子内容 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Metamorphosis|Inventory")
	TArray<FMetamorphosisItemSlot> Slots;
};
