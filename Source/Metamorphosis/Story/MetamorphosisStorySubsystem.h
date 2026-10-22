// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 剧情系统（2026-10-16 开场竖切）
//
// 职责边界：
//   * 按配表（ConfigSource/数值配置表.xlsx 的「Story」sheet -> game_config.json 的 Story[]）走节点图：
//     StartStory/StartOpening -> Advance（或 ChooseOption）-> ... -> Next 为空即结束；
//   * 结算节点上的 Effect（Mood/Sus/Give/Task/Death），判定 Condition（Stage/HasItem）；
//   * 广播 OnStoryChanged 给 UI（全黑过场控件），自己不碰 UI、不碰 HUD 显隐（那是 PlayerController 的事）。
//
// **文本一律来自配表**，本文件不硬编码任何剧情文案（策划只改 Excel + 跑 export_config.py）。
//
// Effect 语法（分号分隔，按顺序执行；进入节点时结算）：
//   Mood+5 / Mood-5     躁郁值增减（剧情内一律 ±5；配表 Mood.StressEventGain=20 属另一套，互不影响）
//   Sus+5  / Sus-5      怀疑值增减
//   Give:ItemId[:N]     给道具（走 InventoryComponent::AddItem）
//   Task:TaskId         把当前任务切到该任务（走 TaskSubsystem::SetTask）
//   Death               死亡 -> 复用现有周目结算 LoopSubsystem::RequestLoopEnd(Caught)
// Condition 语法：空 / Stage>=2 / Stage<=1 / HasItem:ItemId（不满足则该节点被跳过，直接走 Next）
//
// 调试：Meta.Story.Play <NodeId> / Meta.Story.Restart / Meta.Story.Info / Meta.Story.Advance / Meta.Story.Choose <A|B>

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Config/GameConfigTypes.h"
#include "MetamorphosisStorySubsystem.generated.h"

class UMetamorphosisConfigSubsystem;

/** 剧情变化广播：bPlaying=false 表示剧情结束（此时其余参数无意义）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FMetamorphosisStoryChanged,
	bool, bPlaying, FName, NodeId, const FText&, Speaker, const FText&, Text, bool, bHasChoice);

UCLASS(config = Game)
class METAMORPHOSIS_API UMetamorphosisStorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 从任意 WorldContext 取本系统（拿不到返回 nullptr） */
	static UMetamorphosisStorySubsystem* Get(const UObject* WorldContextObject);

	// ---------- 播放控制 ----------
	/** 从指定节点开始播（NodeId 查不到 -> 打警告并返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	bool StartStory(FName NodeId);

	/** 从头播开场（配表里 Scene=Opening 的第一个节点） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	bool StartOpening();

	/** 推进到下一节点；分支节点上调用无效（必须用 ChooseOption） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	void Advance();

	/** 选分支：0 = A，1 = B。非分支节点/越界返回 false */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	bool ChooseOption(int32 Index);

	/** 直接结束剧情（跳过） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	void StopStory();

	/** 回到主界面后再开一局（「开始游戏」）前调用：彻底清掉上一局残留的剧情状态 —— 结束进行中的剧情、
	 *  清掉正在等的玩法门、把"已播过开场"标志归零，让新一局能重新从 Opening 节点播起。
	 *  ⚠️ 踩过的坑：剧情子系统挂在 GameInstance 上、跨关卡存活；从主界面重新进关卡时如果不清，
	 *     会残留 bPlaying / 门状态 -> 新一局的 StartOpening 被"已有剧情在播"挡掉，表现为"剧情出来了但点屏幕不换句"。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	void ResetForNewRun();

	/** 新周目：无视"已播过开场"标志，强制把开场从头再播一遍（供周目结算调用）。
	 *  用户要求"每次新周目都全部从头开始"，所以这里必须先清 bPlayedOpening，否则会被挡住。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Story")
	bool ReplayOpeningForNewLoop();

	// ---------- 临时文本（2026-10-18：剧情道具的"回忆文本"） ----------
	/** 播放一段**不来自配表**的临时文本（剧情道具交互时用）。
	 *
	 *  复用同一套过场显示：全黑 + 纸面板 + 打字机 + 分页 + 点击推进 + 暂停冻结，
	 *  只是节点不走配表 —— 文本来自道具表的 Description。
	 *
	 *  - 已有剧情在播 -> **拒绝**（不嵌套），返回 false；
	 *  - 文本为空 -> **拒绝**（调用方直接走原流程：拾取 / 就地吃），返回 false；
	 *  - 玩家读完（或点「跳过」）-> 自动结束 -> 执行 OnDone（**保证只执行一次**）。
	 *
	 *  @return 是否真的开始播放（false 时调用方不要等回调） */
	bool PlayInlineText(const FText& Speaker, const FText& Text, TFunction<void()> OnDone = nullptr);

	/** 当前播的是不是"临时文本"（UI/调试用；剧情道具读回忆文本时为 true） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool IsInlineTextPlaying() const { return bInlineText; }

	// ---------- 存档恢复（2026-10-19） ----------
	/** 从存档恢复剧情：把指定节点**当纯文本**重新摆出来，但**不重跑该节点的 Effect** ✗、
	 *  也**不重新布防玩法门** ✗。
	 *
	 *  为什么必须这样：
	 *    - 存档里记的是"存档那一刻的当前节点" ✗；直接 StartStory() 会 **再结算一次 Effect** ✗
	 *      -> 若该节点是 Death（或被门超时带到 Death）✗ 就会在读档瞬间触发周目结算 ✗✗
	 *      -> 结算的 ApplyFullLoopReset 会 **清空背包 + 把玩家传回出生点** ✓✓
	 *      这正是"读档后位置不对、背包被清空"的真正原因 ✓。
	 *    - 若该节点带玩法门（GateKind 非空）✗：重新布防会立刻开始计时 ✗ -> 读档几秒后超时 ✗；
	 *      所以这种节点**不恢复**（直接返回 false，让剧情从"没有进行中的剧情"状态开始 ✓）。
	 *
	 *  @return 是否恢复成功（false = 存档节点不适合恢复，调用方按"无剧情"处理即可） */
	bool StartStoryRestored(FName NodeId);

	// ---------- 查询 ----------
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool IsPlaying() const { return bPlaying; }

	// ---------- 玩法门（2026-10-16：分支抉择 = 回到游戏里做一件事） ----------
	/** 过场黑幕是否应该显示（文本阶段 = true；玩法门阶段 = false，此时回到游戏画面） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool IsOverlayVisible() const { return bPlaying && Phase == EMetamorphosisStoryPhase::Text; }

	/** 玩法门是否正在等待玩家操作（黑幕收起、HUD 恢复、任务 HUD 显示目标文字） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool IsGateActive() const { return bPlaying && Phase == EMetamorphosisStoryPhase::Gate; }

	/** 门激活期间显示在任务 HUD 上的目标文字（来自配表 ObjectiveText） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	FText GetObjectiveText() const;

	/** 当前玩法门所在节点（无门时 NAME_None） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	FName GetGateNodeId() const { return GateNodeId; }

	/** 当前门的可读状态串（Meta.Story.Info / Meta.Story.Gate 诊断用） */
	FString DescribeGate() const;

	/** Region 门的专门诊断：区域盒 + 8 个角 + 玩家坐标与逐轴差距（Meta.Story.Region 用）。
	 *  必须是成员函数：区域盒缓存与玩家位置都是私有状态。 */
	void LogRegionDiagnostics() const;

	/** 当前节点（未播放时返回 nullptr） */
	const FStoryNodeConfig* GetCurrentNode() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	FName GetCurrentNodeId() const { return CurrentNodeId; }

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	FText GetSpeakerText() const;

	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	/** 当前页文本（分页后的）；没超过阈值时等于原文 */
	FText GetBodyText() const;

	/** 打字机速度（字/秒，0=关闭）——UI 只读 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	float GetDialogueTypewriterCharsPerSecond() const { return DialogueTypewriterCharsPerSecond; }

	/** 当前节点分了几页（长文本自动分页；没分页时为 1） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	int32 GetPageCount() const { return Pages.Num(); }

	/** 当前第几页（0 基） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	int32 GetPageIndex() const { return PageIndex; }

	/** 当前节点是不是分支（是的话 UI 显示两个按钮） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool HasChoice() const;

	/** 分支按钮文字（Index 0=A / 1=B），非分支返回空文本 */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	FText GetChoiceText(int32 Index) const;

	/** 本局是否已经播过开场（每局只自动播一次；周目结算不重播） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Story")
	bool HasPlayedOpening() const { return bPlayedOpening; }

	UPROPERTY(BlueprintAssignable, Category = "Metamorphosis|Story")
	FMetamorphosisStoryChanged OnStoryChanged;

	// ---------- 设置（Project Settings -> Game -> Metamorphosis Story） ----------
	/** 进游戏关卡时是否自动播开场 */
	UPROPERTY(config, EditAnywhere, Category = "Story")
	bool bAutoPlayOpening = true;

	/** 周目结算后是否重播开场（默认 false：结算只把主控放回起点，不重看开场） */
	UPROPERTY(config, EditAnywhere, Category = "Story")
	bool bReplayOpeningOnLoop = false;

	/** 开场起始节点（留空则取配表里第一条 Scene=Opening 的节点） */
	UPROPERTY(config, EditAnywhere, Category = "Story")
	FName OpeningStartNode;

	UPROPERTY(config, EditAnywhere, Category = "Story")
	bool bLogStory = true;

	/** 剧情文本的默认推进间隔（秒）——目前不使用（推进完全由玩家输入驱动），留作自动播放备用 */
	UPROPERTY(config, EditAnywhere, Category = "Story")
	float AutoAdvanceSeconds = 0.f;

	// ---------- 文本自动分页（2026-10-17） ----------
	/** 每页最多多少字：超过就自动分页（点一下翻下一页，**最后一页点完才推进节点**）。
	 *  优先在中文标点后断，不切断词。
	 *  2026-10-17 面板放大后重算：书写区宽度 = 面板宽 × (1-0.30-0.05) = 0.65×面板；
	 *  正文 21px（面板 1100px 时约 34 字/行），上限 4 行 -> 约 136 字；
	 *  取 120 字留余量（窄屏时字号会自动缩小、每行字数基本不变，所以这个值仍然稳）。
	 *  规范：策划单节点正文 ≤ 120 字（最好 ≤ 90 字，一行一句读起来最舒服）。 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Text")
	int32 DialogueMaxCharsPerPage = 120;

	/** 打字机速度（字/秒）。0 = 关闭（整页立即显示）。默认 35 字/秒 ≈ 3~4 秒读完一页。 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Text")
	float DialogueTypewriterCharsPerSecond = 35.f;

	// ---------- 玩法门参数（Project Settings -> Game -> Metamorphosis Story -> Story|Gate） ----------
	/** 门的轮询间隔（秒）——用 FTSTicker 真实时间，暂停也能走 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float GatePollIntervalSeconds = 0.05f;

	/** 门激活后的"布防延迟"（秒）：这段时间只观察不判定。
	 *  防止黑幕刚收起、任务条刚显示目标的那一瞬间就判定完成（玩家根本来不及反应）。 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float GateArmDelaySeconds = 0.3f;

	/** 场景区域标签前缀：被打上 <前缀><Id> 的 Actor 就是该区域的判定体（如 Meta.Spot:HideUnderBed） */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	FString SpotTagPrefix = TEXT("Meta.Spot:");

	/** 没打标签时按网格资产名找 Actor：格式 Id:网格名（多个用 ; 分隔），用它自己的运行时包围盒 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	TArray<FString> SpotMeshNames;

	/** 最后的坐标盒兜底：格式 Id,X,Y,Z,ExtentX,ExtentY,ExtentZ（场景标签/网格名都找不到时用） */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	TArray<FString> SpotFallbacks;

	/** Spot 判定在包围盒 XY 上外扩多少 cm（"躲到床边"也算躲起来） */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float SpotEnterInflateCm = 60.f;

	/** Spot 判定要求玩家低于"包围盒顶面"多少 cm（避免站在床上也算躲到床下） */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float SpotBelowTopMarginCm = 20.f;

	// ---------- UnderMesh 门（2026-10-17：真正"藏在其下方"的判定） ----------
	// 为什么需要它：Spot 用网格包围盒判"在不在床底"，但包围盒会把床周围一大片都算进去
	// （本图 SM_Bed_twin 被缩放到 4 倍，包围盒 832x520x374cm，连重生点都在盒子边上），
	// 于是"往床的方向走一步"就被判成躲好了（用户报的 bug）。
	// UnderMesh 改成**从玩家向上打一条竖直线**：命中"目标物件"就说明玩家真的在它下面
	// （竖直射线天然保证 XY 落在它的投影范围内），站在床边/床前/床上都不会命中。

	/** UnderMesh 向上射线的最大距离（cm）。0 = 用本项默认值 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float UnderMeshMaxCm = 400.f;

	/** UnderMesh 命中距离下限（cm）：小于它算"贴着打"，不算藏在下面 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float UnderMeshMinCm = 10.f;

	/** 门进行中，判定明细日志的打印间隔（秒；0 = 不打）。调试用：能直接看出"差在哪" */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	float GateDetailLogIntervalSeconds = 1.f;

	// ---------- Region 门区域来源（2026-10-17） ----------
	/** Region 门的 ini 覆盖：格式 "节点Id:XMin,XMax,YMin,YMax,ZMin,ZMax"，每条必须带 + 前缀。
	 *  优先级：本项 > GateTarget 指向的标签 Actor（Meta.Spot:<Id>）包围盒 > 配表里的 GateRegionBox。
	 *  用途：用户在实机上微调区域时，改 ini 即可立刻生效，**不用重跑导出脚本、不用改 Excel**。 */
	UPROPERTY(config, EditAnywhere, Category = "Story|Gate")
	TArray<FString> RegionBoxOverrides;

private:
	/** 剧情阶段：Text = 全黑过场读文本；Gate = 玩法门（回到游戏画面等玩家做事） */
	enum class EMetamorphosisStoryPhase : uint8
	{
		Idle,
		Text,
		Gate,
	};

	UMetamorphosisConfigSubsystem* GetConfigSubsystem() const;

	/** 进入节点：判定 Condition -> 结算 Effect -> 广播 */
	void EnterNode(const FStoryNodeConfig* Node);

	// ---------- 玩法门内部实现 ----------
	/** 激活玩法门（收起黑幕、把目标交给任务 HUD、开始轮询） */
	void ActivateGate(const FStoryNodeConfig& Node);

	/** 门轮询：返回 true 继续轮询，false 注销 */
	bool TickGate(float DeltaTime);

	/** 清掉门状态与任务 HUD 覆盖（不进入下一节点） */
	void ClearGate();

	/** 门结束后进入下一节点（bSatisfied 决定走 GateNext 还是 GateTimeoutNext） */
	void FinishGate(bool bSatisfied, const FString& Reason);

	/** 刷新任务 HUD 上的门目标文字（保持阶段把 {N} 换成剩余秒数；传入 <0 表示不显示剩余秒数） */
	void RefreshGateObjective(int32 RemainingSeconds);

	/** 判定门条件；OutDetail 给日志/诊断用（说明当前差距） */
	bool IsGateSatisfied(FString& OutDetail) const;

	/** 解析一个区域 Id 的判定盒（标签 -> 网格名 -> ini 坐标盒），OutSource 说明来源 */
	bool ResolveSpotBox(const FString& Id, FVector& OutCenter, FVector& OutExtent, FString& OutSource) const;

	/** 某个组件是不是"区域 Id"所指的物件（标签 Actor 的任一组件，或网格名匹配的网格组件）。
	 *  用于 UnderMesh 门：向上射线命中它 = 玩家真的藏在它下面。OutDesc 说明匹配方式。 */
	bool DoesComponentMatchSpotTarget(const UPrimitiveComponent* Comp, const FString& Id, FString& OutDesc) const;

	/** 找带 <SpotTagPrefix><Id> 标签的 Actor（没找到返回 nullptr） */
	AActor* FindTaggedActor(const FString& Id) const;

	/** 当前本地玩家 Pawn 的位置（拿不到返回 false） */
	bool GetPlayerLocation(FVector& OutLocation) const;

	/** 结算一个节点的 Effect 字符串（返回可读摘要，便于日志） */
	FString ApplyEffects(const FString& EffectText);

	/** 判定 Condition；没有条件或满足都返回 true */
	bool EvaluateCondition(const FString& ConditionText) const;

	/** 玩家背包里是否已有该道具（HasItem:/NoItem: 两种条件共用） */
	bool HasItemInInventory(const FString& ItemIdText) const;

	void BroadcastCurrent();
	void EndStory(const TCHAR* Reason);

	// ---------- 文本自动分页（2026-10-17） ----------

	/** 按 DialogueMaxCharsPerPage 把节点文本切页（进节点时调用；只切排版，不改文案） */
	void RebuildPages(const FString& RawText);

	/** 当前页的文本（没分页时就是原文） */
	FString GetCurrentPageString() const;

	/** 当前节点的分页结果 */
	TArray<FString> Pages;

	/** 当前第几页（0 基） */
	int32 PageIndex = 0;

	/** 当前节点 Id；NAME_None = 未播放 */
	FName CurrentNodeId;

	bool bPlaying = false;

	/** 存档恢复模式（2026-10-19）：下一次 EnterNode **不结算 Effect** ✗（避免读档重跑 Death/Give/Task ✗） */
	bool bSuppressEnterEffectsOnce = false;

	/** 本局是否已自动播过开场（跨关卡保留，所以放 GameInstance 子系统里） */
	bool bPlayedOpening = false;

	// ---------- 玩法门运行时状态 ----------
	EMetamorphosisStoryPhase Phase = EMetamorphosisStoryPhase::Idle;

	/** 当前门的节点 Id（诊断用） */
	FName GateNodeId;

	/** 门参数（复制一份，避免配表重载后指针失效） */
	FString GateKind;
	FString GateTarget;
	float GateParam = 0.f;
	float GateTimeLimit = 0.f;

	// ---------- Region 门（2026-10-17：用显式 AABB 判"在不在某个矩形区域里"） ----------
	// 为什么需要它：用户画了一个矩形框，要求"后退"必须退到框的右半边。
	// Spot 用网格包围盒不可靠（本图床被缩放 4 倍、盒子 8m 宽），所以 Region 直接读配表里的 AABB 数字。
	/** 解析后的区域盒（世界坐标）；bGateRegionValid=false 表示没配或配错了 */
	FVector GateRegionMin = FVector::ZeroVector;
	FVector GateRegionMax = FVector::ZeroVector;
	bool bGateRegionValid = false;

	/** 区域盒的来源（ini 覆盖 / 标签 Actor 包围盒 / 配表），诊断用 */
	FString GateRegionSource;

	/** 解析 "XMin,XMax,YMin,YMax,ZMin,ZMax"；失败返回 false（不修改输出） */
	static bool ParseRegionBox(const FString& InText, FVector& OutMin, FVector& OutMax);

	/** 游戏是否处于暂停（门的计时/打字机在暂停时必须冻结） */
	static bool IsGameplayPaused(const UObject* WorldContext);

	/** 门激活时刻（FPlatformTime::Seconds，真实时间）。
	 *  ⚠️ 只用于"本次门激活后是否按过交互键"的时间戳比较（输入时间戳本身是真实时间）。
	 *  所有**计时/限时/保持**一律用下面的 GateElapsedSeconds / GateHoldElapsedSeconds（累计未暂停时间）。 */
	double GateStartTime = 0.0;

	/** 门已过的「未暂停」累计秒数。
	 *  2026-10-17 修的严重 bug：原来用 `FPlatformTime::Seconds() - GateStartTime` 这种**绝对时间戳**，
	 *  而门的轮询走 FTSTicker（真实时间，不受 SetGamePaused 影响）—— 于是玩家暂停/开设置时，
	 *  躲藏门的 15 秒限时仍在走，暂停 15 秒后直接走"被看见→死亡"分支。
	 *  改成累计"只在游戏未暂停时"的 DeltaTime。 */
	float GateElapsedSeconds = 0.f;

	/** 保持阶段已累计的「未暂停」秒数（同样冻结暂停期间） */
	float GateHoldElapsedSeconds = 0.f;

	/** 上次记录的暂停状态（只在进/出暂停时各打一条日志，避免刷屏） */
	bool bGatePausedLogged = false;

	/** 上一次轮询的真实时刻（FPlatformTime::Seconds）。
	 *  计时改成"两次轮询之间的真实时间差（暂停期间不计）"——因为 FTSTicker 给的 DeltaTime 与实际调用频率不匹配
	 *  （实测每 1 秒只累计 0.2 秒，计时慢 5 倍），不能拿它当秒表。 */
	double GateLastTickRealTime = 0.0;

	/** Distance 门没填 GateTarget 时的参照点（= 门激活时玩家所在位置） */
	FVector GateAnchorLocation = FVector::ZeroVector;

	/** 轮询用的 ticker（用弱引用捕获 this，系统销毁时自动注销） */
	FTSTicker::FDelegateHandle GateTickerHandle;

	/** 是否至少观察到过一次"条件未满足"。	 *  门必须是**事件**而不是**状态**：激活瞬间玩家就已经在判定区内（或条件已成立）时不能算完成，
	 *  否则会出现"黑幕刚收起、玩家还没动，下一段剧情立刻弹出"（用户 2026-10-16 报的 bug）。
	 *  规则：先观察到一次"未满足"，之后的"满足"才被承认。 */
	bool bGateSawUnsatisfied = false;

	/** 是否已经提示过"激活时条件就成立、需要先离开"（只提示一次，避免刷日志） */
	bool bGateWarnedInstant = false;

	/** 上一次打印"门判定明细"的时刻（限频用） */
	double GateLastDetailLogTime = 0.0;

	// ---------- 保持阶段（2026-10-16：躲进去要"坚持 N 秒"才算完成） ----------
	/** 满足后需要连续保持的秒数（0 = 一满足就完成） */
	float GateHoldSeconds = 0.f;
	/** 保持阶段的 HUD 文案（支持 {N}）；空则用 ObjectiveText */
	FString GateObjectiveHoldText;
	/** 基础目标文案（ObjectiveText），保持阶段结束后恢复用 */
	FString GateObjectiveBaseText;
	/** 本次保持的开始时刻；< 0 表示当前不在保持中 */
	double GateHoldStartTime = -1.0;
	/** 上一次刷新 HUD/打日志时显示的"剩余秒数"，用于每秒只刷一次（避免每帧刷文本） */
	int32 GateHoldLastShownSeconds = -1;
	/** 是否正在保持（诊断/DescribeGate 用） */
	bool bGateHolding = false;

	// ---------- 通关（2026-10-18：Effect=Win） ----------
	/** 节点 Effect=Win 只是"标记待通关"，**剧情结束时**才真正显示通关界面。
	 *  这样最后一句文本玩家能读完，不会被通关界面一进节点就盖住。 */
	bool bPendingWin = false;

	// ---------- 临时文本（2026-10-18：剧情道具的回忆文本） ----------
	/** 临时节点（不入配表）：bInlineText=true 时 GetCurrentNode() 直接返回它 */
	FStoryNodeConfig InlineNode;

	/** 当前播的是不是临时文本 */
	bool bInlineText = false;

	/** 临时文本结束时要执行的回调（读过一次就 MoveTemp 走，保证只触发一次） */
	TFunction<void()> InlineOnDone;

	// ---------- 剧情链回调（2026-10-18：剧情道具"读完回忆文本再播一句人物台词"） ----------
	/** 配表剧情播完（读完/跳过）后要执行的回调；EndStory 里 MoveTemp 走，保证只触发一次 */
	TFunction<void()> StoryChainOnDone;

public:
	/** 播放一段**配表里的**剧情，并在它播完（玩家读完或点跳过）后执行 OnDone（只执行一次）。
	 *
	 *  用途：剧情道具的回忆文本读完后，接着播策划对话表里那句人物台词（Items.FollowUpStoryNode），
	 *  **这段播完才拾取**道具。与 PlayInlineText 的区别：这段是真实节点链（可多句、可换说话人）。
	 *
	 *  - 节点不存在 / 配置没就绪 -> 返回 false（调用方立刻走原流程，不要等回调）；
	 *  - 播完（含玩家点「跳过」/被别的剧情打断）都会执行 OnDone —— 道具不能因为跳过而丢。 */
	bool PlayStoryThen(FName NodeId, TFunction<void()> OnDone);
};



