// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 剧情过场控件（2026-10-16 开场竖切）
//
// 表现（策划已定死，不要改成静帧/动画）：
//   * **全屏纯黑** + 素文字：说话人（小字）+ 正文（大字，自动换行）+ 右上角「跳过」；
//   * 有分支的节点显示**两个可点选项按钮**（PC 鼠标可点、移动端可点且认领触摸）；
//   * 无分支：点击/触摸任意处推进；键盘 空格/回车 推进，Esc 跳过。
//   * 过场期间由 PlayerController 隐藏其它 HUD（本控件只管自己）。
//
// 数据全部来自 UMetamorphosisStorySubsystem（文本来自配表，本控件不硬编码任何文案）。
//
// ⚠️ 本项目 UMG 踩过的坑（这里都照做）：
//   * 布局必须在 `Initialize()` 里搭（放 NativeConstruct 会尺寸恒为 0、屏幕上什么都没有）；
//   * 根节点全屏 Overlay；**别把整个控件 Collapsed**（Slate 不 tick 被折叠控件），显示/隐藏只动内部各行；
//   * 纯色要用 `FSlateColorBrush`（手搓 FSlateBrush 会渲染成白膜）；
//   * 构造函数里不能碰 FCoreStyle，字体用 ConstructorHelpers::FObjectFinder<UFont>；
//   * 匿名命名空间辅助函数起唯一名字（unity build 撞名会 already has a body）。
//
// 触摸认领：继承 UMetamorphosisTouchClaimWidget，靠基类用**正确手指号**认领，避免和摇杆抢手指。

#pragma once

#include "CoreMinimal.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "MetamorphosisStoryWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UButton;
class UTextBlock;
class UVerticalBox;


UCLASS(Blueprintable, Config = Game)
class METAMORPHOSIS_API UMetamorphosisStoryWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisStoryWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 触摸任意处推进（分支节点时忽略，必须点按钮） */
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// ---------- 换皮钩子（做 WBP 子类时用这些名字会被自动接管） ----------
	// StoryRoot(UOverlay) / StoryBlackout(UBorder) / StorySpeaker(UTextBlock) / StoryBody(UTextBlock)
	// StoryChoiceRow(UHorizontalBox) / StoryChoiceA(UButton) / StoryChoiceAText(UTextBlock)
	// StoryChoiceB(UButton) / StoryChoiceBText(UTextBlock) / StorySkipButton(UButton)

protected:
	UFUNCTION() void HandleChoiceA();
	UFUNCTION() void HandleChoiceB();
	UFUNCTION() void HandleSkip();
	UFUNCTION() void HandleStoryChanged(bool bPlaying, FName NodeId, const FText& Speaker, const FText& Text, bool bHasChoice);

	// ---------- 对话框纸面板（美术交付 1） ----------

	/** 是否播放 48 帧待机动画（关掉就只用静态一帧，省性能/便于排查） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	bool bDialogueBoxAnimated = true;

	/** 动画帧率（48 帧 / 12fps ≈ 4 秒一轮） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	float DialogueBoxFps = 12.f;

	/** 纸面板宽度占屏幕宽度的比例（2026-10-17 放大：原 800px 固定 -> 屏宽 86%） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	float DialoguePanelScreenWidthRatio = 0.86f;

	/** 纸面板宽高比（美术帧 1200x480 = 2.5；这里保持 800:320） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	float DialoguePanelAspect = 0.4f;

	/** 面板最小/最大宽度（像素），防止极端分辨率下过小或过大 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	FVector2D DialoguePanelWidthRange = FVector2D(560.f, 1500.f);

	/** 距屏幕底部的边距（比例，按屏高算） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	float DialoguePanelBottomMarginRatio = 0.035f;

	/** 书写区内缩（占面板宽/高的比例）：左侧要避开人物卡片，所以左内缩最大。
	 *  ⚠️ 2026-10-17 修正：美术纸面板上方有一条**浅米色撕纸带**、下方才是深绿旧纸书写区，
	 *  原来 Top=0.10 会把白字压到浅色带上（对比度极低、看起来像溢出）。现 Top=0.38，
	 *  说话人与正文**整体落在深绿区内**；Bottom=0.12 保证最后一行也不出纸；Left 仍 0.30 避开人物卡片。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Story|Art")
	FVector4 DialogueTextInsets = FVector4(0.30f, 0.38f, 0.10f, 0.12f); // Left, Top, Right, Bottom

private:
	void BuildStoryLayout();
	void ApplyStaticStyle();
	void RefreshFromStory();

	/** 按当前视口尺寸重排对话框（面板放大 + 书写区内缩 + 字号/换行宽度跟着面板走） */
	void UpdateDialogueLayout();
	/** 面板尺寸变了才重排（避免每帧设 Slot） */
	FVector2D LastLayoutViewportSize = FVector2D::ZeroVector;

	// ---------- 打字机（2026-10-17） ----------
	/** 本页完整文本（打字机目标） */
	FString TypewriterFullText;
	/** 当前已显示的字符数 */
	int32 TypewriterVisibleChars = 0;
	float TypewriterAccum = 0.f;

	/** 打字机速度（字/秒，来自 StorySubsystem 的 ini 配置；0=关闭） */
	float GetTypewriterSpeed() const;
	/** 是否正在逐字显示（还有没显示完的字） */
	bool IsTypewriterRunning() const;
	/** 立即显示本页全文（打字中点击时调用） */
	void CompleteTypewriter();
	void ApplyTypewriterPrefix();

	/** 预加载 48 帧 + 静态层（第一次 tick 时做，避免构造期加载） */
	void EnsureDialogueFrames();
	void AdvanceDialogueFrame(float DeltaTime);
	void ApplyDialogueFrame();

	UPROPERTY(Transient) TObjectPtr<UBorder> Blackout = nullptr;

	/** 底部对话框纸面板（48 帧循环） */
	UPROPERTY(Transient) TObjectPtr<class UImage> DialoguePanel = nullptr;

	/** 对话框整体容器（画布槽尺寸 = 运行时的面板尺寸） */
	UPROPERTY(Transient) TObjectPtr<class UOverlay> DialogueBoxOverlay = nullptr;
	/** 对话面板缩放（设计尺寸 800x320 -> 屏幕上 = 屏宽 86%）；文字也在这个设计空间里排，天然贴合纸面 */
	UPROPERTY(Transient) TObjectPtr<class UScaleBox> DialogueScale = nullptr;
	UPROPERTY(Transient) TObjectPtr<class USizeBox> DialogueDesignBox = nullptr;
	/** 设计尺寸（美术帧原生 800x320）：所有内缩/字号都按这个空间的单位算 */
	FVector2D DialogueDesignSize = FVector2D(800.f, 320.f);
	/** 对话框在画布里的槽（用来改尺寸/位置） */
	TWeakObjectPtr<class UCanvasPanelSlot> DialogueBoxSlot;
	/** 书写区（带内缩 + 裁剪，保证文字不出纸面） */
	UPROPERTY(Transient) TObjectPtr<UBorder> DialogueTextArea = nullptr;
	TWeakObjectPtr<class UOverlaySlot> DialogueTextAreaSlot;
	/** 选项行在画布里的槽（跟着面板高度移动） */
	TWeakObjectPtr<class UCanvasPanelSlot> ChoiceRowSlot;
	/** 页码在画布里的槽 */
	TWeakObjectPtr<class UCanvasPanelSlot> PageIndicatorSlot;

	/** 当前面板尺寸（运行时算出来） */
	FVector2D DialoguePanelSize = FVector2D(800.f, 320.f);

	/** 48 帧贴图（懒加载；bDialogueBoxAnimated=false 时只用第 0 帧） */
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> DialogueFrames;
	bool bDialogueFramesLoaded = false;
	int32 DialogueFrameIndex = 0;
	float DialogueFrameAccum = 0.f;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeakerText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BodyText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> ChoiceRow = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> ChoiceAButton = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> ChoiceBButton = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChoiceAText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChoiceBText = nullptr;

	FSlateFontInfo SpeakerFont;
	FSlateFontInfo BodyFont;

	// ---------------- 头像（左侧人物卡片 · 预合成整层） ----------------
	// 说话人 -> **预合成卡片层**资产名（"说话人:资产名"，资产名是 PortraitCardFolder 下的名字）。
	// 用 ini 配，不要把"中文说话人 -> 资产"写死在代码里。
	// 为什么要预合成：美术卡片是**透视倾斜的四边形 + 纸框**，用一个旋转的矩形去贴永远贴不齐（用户两次反馈 ✗）。
	// 现在改成 PIL 把 512x512 头像按透视"烤"进卡片内层画面，再叠上纸框像素，输出整张 800x320 的 RGBA 层
	// （生成脚本见 Tools/make_story_cards.py），控件只需要整张画上去 -> 像素级贴合、无需旋转。
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	TArray<FString> PortraitMap;

	// 说话人**显示名**映射（"原名:显示名"）。只影响界面上显示的文字，不影响头像映射/剧情逻辑/存档里的节点 id。
	// 例：+SpeakerDisplayName=主控:bumy -> Story 表里仍写「主控」，画面上显示「bumy」。
	// 放在这里而不是改配表：改 Excel 会牵动 58 行 + 内嵌 .inl，风险大；显示名属于纯表现层。
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	TArray<FString> SpeakerDisplayName;

	/** 预合成卡片层所在目录（资产名为 800x320、只含卡片区域像素、其余透明） */
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	FString PortraitCardFolder = TEXT("/Game/UI/Dialogue/Cards");

	// （已废弃，保留仅为不破坏旧 ini）—— 预合成方案下头像由整层绘制，不再按矩形/角度摆放。
	//    卡片四角（800x320 设计空间）实测：外框 (34,37)(173,25)(192,167)(41,189)；
	//    内层画面 = 外框绕中心 0.93 缩放。生成脚本用的就是这两个多边形。
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	FVector4 PortraitRect = FVector4(48.f, 26.f, 112.f, 132.f);

	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	float PortraitAngleDegrees = -3.6f;

	// 说话人文字移到上方浅色纸带后的位置（设计空间 X,Y）与颜色（浅带要用深字）。
	// 实测浅色纸带在美术帧里约 x[192..780] y[55..110]，所以名字放在卡片右侧、带子垂直居中处。
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	FVector2D SpeakerOnBandPosition = FVector2D(206.f, 68.f);
	// 浅色纸带很亮，名字要够深才看得清（原来 0.22/0.17/0.11 偏淡）。
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	FLinearColor SpeakerOnBandColor = FLinearColor(0.10f, 0.07f, 0.04f, 1.f);
	// 名字字号（设计空间）。原来 11 在屏上约 18px 偏小；13 更醒目。
	UPROPERTY(Config, EditAnywhere, Category = "Metamorphosis|Story")
	int32 SpeakerFontSize = 13;

	/** 头像图片（叠在纸面板的人物卡片区域上；没有对应头像时隐藏） */
	UPROPERTY(Transient)
	TObjectPtr<class UImage> PortraitImage = nullptr;

	/** 说话人文字（从正文列里挪出来，改成画布子控件放到浅色纸带上） */
	UPROPERTY(Transient)
	TObjectPtr<class UCanvasPanel> DialogueCanvas = nullptr;

	/** 头像缓存：UPROPERTY 才有 GC 追踪（本项目踩过"软引用无硬引用 -> 换关卡白块"的坑） */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<class UTexture2D>> PortraitCache;

	/** 按说话人刷新头像（没有映射/没有资产 -> 隐藏卡片里的头像） */
	void RefreshPortrait(const FText& Speaker);

	/** 说话人的**显示名**（命中 SpeakerDisplayName 就用映射值，否则原样返回）——纯显示层，不改任何逻辑键 */
	FText ResolveSpeakerDisplayName(const FText& Speaker) const;

	/** 取头像资产（带缓存 + 失效重载） */
	class UTexture2D* ResolvePortrait(const FString& Speaker);
	FSlateFontInfo ChoiceFont;

	bool bBuiltLayout = false;

	// 脏检查
	FName LastNodeId;
	bool bLastHasChoice = false;

	/** 右下角「1/2」页码（分页状态在 StorySubsystem 里，本控件只显示） */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageIndicator = nullptr;
};


