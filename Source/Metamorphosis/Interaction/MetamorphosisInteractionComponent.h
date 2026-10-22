// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 交互系统（仿《原神》）：走近 -> 提示 -> 按键/点按钮 -> 拾取进背包 或 就地吃掉。
//
// 职责：
//   * 定时扫描（默认 0.15 秒一次，不是每帧全表遍历）附近的可交互物，取最近的一个作为当前目标；
//   * 认两类目标（见 ScanForTargets）：
//       1) AMetamorphosisInteractable —— 正式路
//       2) 任意 Actor 上带标签 "Meta.Item:<ItemId>[:<Count>]" —— 标签路（地编不用换类）
//   * 执行交互：PickUp -> 物品栏 AddItem（失败则物件保留）；EatInPlace -> 直接结算配表效果后销毁；
//   * 给 UI 提供提示文字（「拾取 麦片」/「吃掉 红糖」）。
//
// 「不可携带 -> 就地吃掉」这条规则**只在 ResolveMode() 一处**，别在别处再写一遍。
//
// ini 可调（[/Script/Metamorphosis.MetamorphosisInteractionComponent]）：
//   InteractRangeCm=150 / InteractScanInterval=0.15 / bInteractBob=True
//   ⚠️ UE 是先跑构造函数再套 ini，所以这些值在 BeginPlay 里应用（本项目踩过这个坑）。
//
// 调试：Meta.Interact.Scan（列候选）/ Meta.Interact.Info（当前目标）/ Meta.Interact.Do（执行一次）

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/MetamorphosisInteractable.h"   // EMetamorphosisInteractMode
#include "MetamorphosisInteractionComponent.generated.h"

struct FItemConfigEntry;
class UMetamorphosisInventoryComponent;

/** 当前交互目标（纯 C++ 结构，UI/调试用；不做蓝图类型是因为里面有配表裸指针） */
struct FMetamorphosisInteractTarget
{
	/** 目标 Actor（可能已销毁，用之前先 IsValid()） */
	TWeakObjectPtr<AActor> Actor;

	/** 道具 ID（配表 Items 的 ItemId） */
	FString ItemId;

	/** 一次交互给几个 */
	int32 Count = 1;

	/** 解析后的实际交互方式（Auto 已经被算成 PickUp / EatInPlace） */
	EMetamorphosisInteractMode Mode = EMetamorphosisInteractMode::Auto;

	/** 距离（厘米，从虫子到交互点） */
	float DistanceCm = 0.f;

	/** 配表条目（配表里查不到时为 nullptr —— 这种目标会被扫描跳过） */
	const FItemConfigEntry* Config = nullptr;

	/** 自定义提示（正式 Actor 填了才非空；留空则用自动文案） */
	FText PromptOverride;

	/** 是不是"通关点"（门缝）：没有道具配表，交互 = 直接通关。
	 *  2026-10-18：原剧情的"出去"分支（蜘蛛 / 道具判定 / 大雪 / 强风）按用户要求整段删除，
	 *  改成"走到缝隙旁 -> 提示「从缝隙出去」-> 交互 -> 通关"。 */
	bool bIsEscape = false;

	/** 是不是"剧情道具"（配表 Category=Story）：
	 *  2026-10-18 用户要求"剧情道具是交互后触发剧情" —— 交互先播它的回忆文本（Description），
	 *  读完/跳过后再拾取进背包。 */
	bool bIsStoryItem = false;

	/** 这份目标能不能用（Actor 还在 + （通关点 或 配表查得到）） */
	bool IsValid() const { return Actor.IsValid() && (bIsEscape || Config != nullptr); }

	void Reset()
	{
		Actor = nullptr;
		ItemId.Reset();
		Count = 1;
		Mode = EMetamorphosisInteractMode::Auto;
		DistanceCm = 0.f;
		Config = nullptr;
		PromptOverride = FText::GetEmpty();
		bIsEscape = false;
		bIsStoryItem = false;
	}
};

/**
 *  挂在虫形主角上的交互组件：负责"找目标 + 执行交互"。
 *  玩家控制器只负责把按键/按钮事件转成 TryInteract()，UI 只负责显示 GetPromptText()。
 */
UCLASS(ClassGroup = (Metamorphosis), meta = (BlueprintSpawnableComponent))
class METAMORPHOSIS_API UMetamorphosisInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMetamorphosisInteractionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------------------------------------------------------
	// 参数（ini 可调，BeginPlay 应用）
	// ---------------------------------------------------------------

	/** 交互距离（厘米）。虫子精灵高 16cm，150cm ≈ 9 个身长 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	float InteractRangeCm = 150.f;

	/** 扫描间隔（秒）：定时扫，别每帧遍历 Actor */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	float InteractScanInterval = 0.15f;

	/** 全局待机表现开关（正式 Actor 自己也有一层开关，两者都开才动） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	bool bInteractBob = true;

	/** 拾取/吃掉后是否输出日志（验证用，默认开） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	bool bLogInteract = true;

	// ---------------- 通关点（门缝，2026-10-18） ----------------
	// 用户要求：原剧情的"出去"分支整段删除，改成"从缝隙出去 = 直接通关"。
	// 场景里任意 Actor 打上这个标签就是通关点，交互提示文字也在这里配（不写死在代码里）。

	/** 通关点标签：世界里带这个标签的 Actor = 通关点（交互后直接通关），默认 Meta.Spot:EscapeHole */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Escape")
	FString EscapeTag = TEXT("Meta.Spot:EscapeHole");

	/** 通关点的交互提示文字（显示在走近时的那条提示上） */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Escape")
	FText EscapePromptText = FText::FromString(TEXT("从缝隙出去"));

	/** 通关点的交互距离（厘米）。0 = 用 InteractRangeCm。
	 *  默认给大一点：缝隙往往是墙边/角落里的小目标，玩家不一定能贴得很准。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Escape")
	float EscapeRangeCm = 200.f;

	/** 通关点交互后是否输出日志 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Escape")
	bool bLogEscape = true;

	/** 条件不满足（例如第二章剧情碎片还没集齐）时弹给玩家的提示文本。
	 *  走剧情对话框（打字机/分页/暂停冻结都复用），读完回到游玩；不会通关。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Escape")
	FText EscapeLockedText = FText::FromString(TEXT("还不行。这屋里还有没弄明白的东西，你不想就这么走。"));

	/** 通关点的高度容差（厘米）：只比水平距离，但 Z 差超过这个值就不算（防止隔着楼层/天花板交互）。
	 *  缝隙常摆在门框/墙脚，Z 与玩家差很多，所以不用 3D 距离。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Escape")
	float EscapeHeightToleranceCm = 200.f;

	// ---------------- 剧情道具（2026-10-18：交互后播回忆文本，读完再拾取） ----------------

	/** 剧情道具的提示文字格式（%s = 道具显示名），默认「查看 %s」。
	 *  剧情道具（配表 Category=Story）交互时先播它的 Description（回忆文本），读完/跳过后才拾取。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	FString StoryPromptFormat = TEXT("查看 %s");

	/** 剧情道具回忆文本的说话人；留空 = 用道具显示名 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	FString StoryItemSpeaker;

	/** 本时段探索机会用完时，交互被拒绝后弹给玩家的提示（2026-10-19 时间/次数系统）。
	 *  空串 = 只打日志不弹框。用剧情对话框播（和"缝隙被锁"同一套做法），读起来像主控自己的念头。 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Metamorphosis|Interact")
	FText NoExploreChanceText = FText::FromString(TEXT("这屋子里的响动太多了，再翻下去要出事。先歇一会儿吧。"));

	// ---------------------------------------------------------------
	// 查询（UI 每帧读这几个）
	// ---------------------------------------------------------------

	/** 当前有没有可交互目标 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	bool HasTarget() const { return CurrentTarget.IsValid(); }

	/** 当前目标（注意里面是裸指针 + 弱引用，用之前判断 IsValid()） */
	const FMetamorphosisInteractTarget& GetTarget() const { return CurrentTarget; }

	/** 提示文字，如「拾取 麦片」/「吃掉 红糖」；没目标返回空串 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	FString GetPromptText() const;

	/** 同上，给 UI 直接用的 FText */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	FText GetPromptTextText() const;

	/** 当前目标道具的显示名（没目标返回空） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	FString GetTargetDisplayName() const;

	// ---------------------------------------------------------------
	// 执行
	// ---------------------------------------------------------------

	/** 执行一次交互。返回是否真的做成了（拾取失败/没目标/暂停时返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Interact")
	bool TryInteract();

	/** 立刻重扫一次（调试、或交互后想马上刷新 UI） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Interact")
	void ForceScan();

	// ---------------------------------------------------------------
	// 静态工具（两条路共用；测试也用它们）
	// ---------------------------------------------------------------

	/** 解析标签 "Meta.Item:<ItemId>" 或 "Meta.Item:<ItemId>:<Count>"；成功返回 true */
	static bool ParseItemTag(const FString& Tag, FString& OutItemId, int32& OutCount);

	/** 交互模式解析（**规则唯一一处**）：Auto -> bCarryable ? PickUp : EatInPlace */
	static EMetamorphosisInteractMode ResolveMode(EMetamorphosisInteractMode Requested, const FItemConfigEntry& Entry);

	/** 模式名（日志/调试用） */
	static const TCHAR* ModeToString(EMetamorphosisInteractMode Mode);

	/** 交互提示文案（PickUp -> 拾取 / EatInPlace -> 吃掉）；自定义文字优先 */
	static FString MakePromptText(EMetamorphosisInteractMode Mode, const FString& DisplayName, const FText& Override);

private:
	/** 查道具配表（拿不到配置子系统/查不到都返回 nullptr） */
	const FItemConfigEntry* FindItemConfig(const FString& ItemId) const;

	/** 遍历世界找最近的合法目标（超时/暂停时不扫） */
	void ScanForTargets();

	/** 取当前 Pawn 上的物品栏组件（可能为空） */
	UMetamorphosisInventoryComponent* GetInventory() const;

	/** 剧情道具：回忆文本读完之后才真正拾取（由 PlayInlineText 的回调调用）。
	 *  用弱引用捕获调用，避免读文本期间 Actor 被销毁。 */
	void CompleteStoryItemPickup(const FString& ItemId, int32 Count, TWeakObjectPtr<AActor> TargetActor);

	/** 剧情道具的"后续台词"（策划对话表 B 组，Items.FollowUpStoryNode）：
	 *  回忆文本读完后先播这段配表剧情，**它结束**才拾取。
	 *  @return 是否真的开始播（false = 调用方立刻拾取，不要等回调） */
	bool PlayFollowUpThenPickup(const FString& NodeId, const FString& ItemId, int32 Count,
		TWeakObjectPtr<AActor> TargetActor);

	/** 剧情道具的回忆文本是不是正在播（播的时候拒绝一切交互，防重复拾取） */
	bool IsStoryTextPlaying() const;

	/** 剧情道具的"读完回忆文本 -> 播后续台词 -> 拾取"流程是否还在进行中（2026-10-18）。
	 *  后续台词是配表剧情链，IsInlineTextPlaying() 在这段里是 false，
	 *  所以额外记一个标志位，保证整条流程期间交互都被锁住。 */
	bool bAwaitingStoryItemPickup = false;

	/** 游戏被暂停（弹窗时停）时不响应交互 */
	bool IsInteractionBlocked() const;

	/** 记一下"配表里没有这个 ItemId"，同一个 ID 只报一次（扫描是 0.15 秒一次，不节流会刷屏） */
	void WarnMissingItemIdOnce(const FString& ItemId, const FString& SourceName);

	/** 当前目标 */
	FMetamorphosisInteractTarget CurrentTarget;

	/** 距离上次扫描过了多久 */
	float TimeSinceScan = 0.f;

	/** 已经报过"配表里没有"的 ItemId */
	TSet<FString> WarnedMissingItemIds;
};
