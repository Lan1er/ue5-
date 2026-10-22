// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 交互提示（仿《原神》）：屏幕右侧偏中，显示「拾取 XX」/「吃掉 XX」，两种平台两套外观。
//
//   PC 外观（默认）—— 小圆徽标里显示按键字符 "R" + 小道具图标 + 一行文字
//   移动端外观      —— **大的可点圆按钮**（圆里画道具图标；没图就用配表占位色填充圆 + 一个方块符号）
//                      + 文字放在圆的**左侧**，字号更大
//
// 用哪套外观由 `AMetamorphosisPlayerController::IsMobileUI()` 决定（项目里"移动端模式"的唯一判据）：
// 真机恒为移动端；PC 上控制台 `Meta.UI.Mobile` 可强制切换（`Meta.UI.Desktop` 切回），方便调试与截图。
//
// 数据来自 UMetamorphosisInteractionComponent（挂在虫形主角身上），本控件每帧读一次、带脏检查，
// 没目标/暂停时只折叠内部那一行。
//
// ⚠️ 本项目 UMG 踩过的坑（这里都照做了）：
//   * 根节点必须是**全屏 Overlay + SelfHitTestInvisible**，否则会吞掉屏幕上所有点击
//     （只有圆按钮是 hit-testable；SelfHitTestInvisible 不影响子控件被点）；
//   * 位置用 Overlay -> CanvasPanel 的两层结构（CanvasPanel 直接当根会被裁掉）；
//   * **别把整个控件 Collapsed**：Slate 不会 tick 被折叠的控件，NativeTick 就再也不跑了 ——
//     显示/隐藏只动内部的行（PromptRow / MobilePromptRow）；
//   * 更新控件前先做脏检查，别每帧重建/改文本、也别每帧 LoadSynchronous 图标。
//
// 触摸认领：本类继承 UMetamorphosisTouchClaimWidget，**触摸按下时**由基类的
// NativeOnTouchStarted/Moved/Ended 用"正确的手指号"认领与释放（和移动端上/下按钮同一套机制）。
// 这里刻意**不**在 UButton 的 OnPressed/OnReleased 里认领：那两个回调不带手指号，
// 只能盲认领 0 号手指 —— 而玩家很可能正用 0 号手指按着摇杆，盲认领会把摇杆那根手指判成"UI 的"，
// 表现就是"点一下交互按钮，人物就不动了"。
//
// 换皮（不用改代码）：Project Settings -> Game -> Metamorphosis UI -> Interact Prompt Class 指到 WBP。
// 做 WBP 子类时按这些名字命名会被自动接管：
//   PC 行  ：InteractPromptRow（UHorizontalBox）、InteractBadgeButton（UButton）、InteractBadgeImage（UImage）、
//            InteractKeyHint（UTextBlock）、InteractItemIcon（UImage）、InteractPromptText（UTextBlock）
//   移动行 ：InteractMobileRow（UHorizontalBox）、InteractMobilePromptText（UTextBlock）、
//            InteractMobileButton（UButton）、InteractMobileBadgeImage（UImage）、
//            InteractMobileSlotMark（UImage，无真图时的方块符号）、InteractMobileItemIcon（UImage）

#pragma once

#include "CoreMinimal.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisInteractPromptWidget.generated.h"

class UButton;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UTextBlock;
struct FMetamorphosisInteractTarget;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisInteractPromptWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisInteractPromptWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	// ---------------------------------------------------------------
	// PC 外观
	// ---------------------------------------------------------------

	/** PC 提示在屏幕上的归一化锚点（0.62, 0.45 ≈ 右侧偏中） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|PC")
	FVector2D ScreenAnchor = FVector2D(0.62f, 0.45f);

	/** PC 圆形徽标直径（Slate 单位） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|PC")
	float BadgeSize = 56.f;

	/** PC 道具图标边长 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|PC")
	float IconSize = 40.f;

	/** PC 圆徽标与图标、图标与文字之间的间距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|PC")
	float ElementSpacing = 10.f;

	// ---------------------------------------------------------------
	// 移动端外观
	// ---------------------------------------------------------------

	/** 移动端提示的归一化锚点。
	 *  0.62 / 0.50：纵向居中偏下，横向避开右下角的「上/下」按钮（它们在 0.84 往右、
	 *  纵向 0.5 以下；本行最右端大约到 0.72，放大到 160% 也不会压上）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Mobile")
	FVector2D MobileScreenAnchor = FVector2D(0.62f, 0.50f);

	/** 移动端大圆按钮直径（触摸目标，别小于 88） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Mobile")
	float MobileButtonSize = 104.f;

	/** 移动端圆内道具图标边长 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Mobile")
	float MobileIconSize = 68.f;

	/** 移动端文字与圆之间的间距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Mobile")
	float MobileTextSpacing = 16.f;

	/** 移动端文字字体（比 PC 大一点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Mobile")
	FSlateFontInfo MobilePromptFont;

	/** 移动端无真图时的方块符号边长（占圆的比例） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Mobile")
	float MobileSlotMarkRatio = 0.38f;

	// ---------------------------------------------------------------
	// 共用外观
	// ---------------------------------------------------------------

	/** 圆填充色（半透明深底 + 亮边，浅色场景里也看得清） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor BadgeFillColor = FLinearColor(0.03f, 0.03f, 0.05f, 0.62f);

	/** 圆描边色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor BadgeEdgeColor = FLinearColor(0.95f, 0.85f, 0.45f, 0.95f);

	/** 描边宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	float BadgeEdgeWidth = 2.5f;

	/** 按键字符颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor KeyHintColor = FLinearColor::White;

	/** 提示文字颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor PromptTextColor = FLinearColor(1.f, 0.97f, 0.88f, 1.f);

	/** 文字后面的深色描边（浅色场景里保证可读） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor PromptShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);

	/** 按键字符（PC 显示；2026-10-14 起交互键是 R） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Text")
	FText KeyHintText;

	/** 是否显示按键字符；移动端外观下自动关掉（圆按钮本身就是操作方式） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Text")
	bool bShowKeyHint = true;

	/** 按键字符字体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Text")
	FSlateFontInfo KeyHintFont;

	/** PC 提示文字字体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Text")
	FSlateFontInfo PromptFont;

	/** 给美术换图用：留空用 RoundedBox 画的纯色圆（两套外观共用）
	 *  交付 2（2026-10-18）起默认填 `T_PromptBadge`：美术的方形金环徽章（指针裁掉了——
	 *  我们的提示是固定在屏幕上的 HUD，不是跟随物件的世界标记）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FSlateBrush BadgeBrush;

	// ---------------------------------------------------------------
	// 交付 2：提示底板（美术 T_PromptPlate）
	// ---------------------------------------------------------------
	// 美术原图是「E 拾取」整块 tooltip（172x94），字是烘死在图里的 ✗ —— 我们的提示文字是动态的
	// （拾取/吃掉/查看/从缝隙出去），所以把原图拆成了两件东西：
	//   ① T_PromptBadge：金环徽章（擦掉了烘死的「E」，保留金环）-> 里面画我们自己的按键字符 R
	//   ② T_PromptPlate：**无文字**的板身（左端 24px + 中间 18px 可拉伸 + 右端 33px，取自原图三段无字区）
	// 板身用九宫格拉伸，动态文字叠在上面 —— 见 Tools/stage_ui_delivery2.py 的说明。

	/** 提示文字底板（交付 2：`T_PromptPlate`，九宫格） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FSlateBrush PromptPlateBrush;

	/** 底板的九宫格边距：左右与裁切一致，上下留出撕边（中间段纵向是均匀的，可以拉）
	 *  ⚠️ 上+下不能超过文字的实际绘制高度（约 40px @24 号字），否则中间段为负、Slate 直接不画 ✗
	 *  （2026-10-18 踩过：一开始给 22/22 = 44 > 40，底板整块不显示）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FMargin PromptPlateMargin = FMargin(24.f, 11.f, 33.f, 11.f);

	/** PC：文字在底板里留的空白 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FMargin PromptPlatePadding = FMargin(30.f, 8.f, 26.f, 8.f);

	/** 移动端：底板里留的空白（字更大，留多一点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FMargin PromptPlatePaddingMobile = FMargin(34.f, 10.f, 26.f, 10.f);

	/** 底板提亮倍数（美术板身是近黑色，暗场景里按原样画几乎看不见；
	 *  >1 的 tint 是"提亮"不是"改素材"，撕边与金色描线才读得出来）
	 *  注：交付 2 的板身贴图当前画不出来（见 .cpp 里的说明），已改用下方的纯色兜底参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	float PromptPlateBrightness = 2.1f;

	/** 兜底底板：填充色（深墨绿，与美术板身同色系） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor PromptPlateFillColor = FLinearColor(0.043f, 0.055f, 0.043f, 0.94f);

	/** 兜底底板：金色细边（与美术描线同色系） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	FLinearColor PromptPlateEdgeColor = FLinearColor(0.62f, 0.55f, 0.36f, 0.95f);

	/** 兜底底板：边宽 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	float PromptPlateEdgeWidth = 1.5f;

	/** 兜底底板：圆角半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	float PromptPlateCornerRadius = 6.f;

	/** 底板高度（Slate 单位）：给承载文字的 SizeBox 一个下限，保证底板一定画得出来
	 *  （踩过：靠 Overlay 的 desired size 推，拿到 0 高度 -> 底板整块不画） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Interact|Art")
	float PromptPlateHeight = 46.f;

	/** 取当前 Pawn 上的交互组件（可能为空） */
	class UMetamorphosisInteractionComponent* GetInteraction() const;

	/** 立即按当前目标刷新一次（不等下一帧） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|Interact")
	void RefreshNow();

	/** 当前该用移动端外观吗（转问 PlayerController::IsMobileUI） */
	UFUNCTION(BlueprintPure, Category = "Metamorphosis|Interact")
	bool ShouldUseMobileAppearance() const;

protected:
	/** 圆按钮被点（移动端的主要交互方式；PC 也能点） */
	UFUNCTION() void HandleBadgeClicked();
	UFUNCTION() void HandleMobileBadgeClicked();

	void BuildDefaultLayout();
	void BuildPcRow(class UCanvasPanel* Canvas);
	void BuildMobileRow(class UCanvasPanel* Canvas);
	void CacheWidgets();

	/** 按外观套一次静态样式（颜色/字体/锚点/圆角刷子）+ 切换显示哪一行 */
	void ApplyAppearanceMode(bool bMobile);

	/** 只让当前外观那一行显示/隐藏（另一行永远收起） */
	void SetActiveRowVisible(bool bVisible);

	/** 按目标刷新（带脏检查：ItemId/数量/文字/图标/外观没变就不动控件） */
	void Refresh(const struct FMetamorphosisInteractTarget& Target);

	/** 把提示文字写到两行的文本控件上 */
	void SetPromptText(const FString& Text);

	/** 把道具图标刷到两行上（真图或占位色/方块符号） */
	void ApplyItemVisuals(const struct FMetamorphosisInteractTarget& Target);

	/** 画一个圆（有 BadgeBrush 用美术图） */
	FSlateBrush MakeBadgeBrush(float Diameter) const;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> PromptCanvas;

	// PC 行
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PromptRow;
	UPROPERTY(Transient) TObjectPtr<UButton> BadgeButton;
	UPROPERTY(Transient) TObjectPtr<UImage> BadgeImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyHint;
	UPROPERTY(Transient) TObjectPtr<UImage> ItemIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptText;
	/** 文字底板（在 PromptText 下面一层；空则纯文字无底板） */
	UPROPERTY(Transient) TObjectPtr<UImage> PromptPlate;
	/** 叠在代码底板之上的美术板身（画不出来也不影响可读性） */
	UPROPERTY(Transient) TObjectPtr<UImage> PromptPlateArt;

	// 移动端行
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> MobilePromptRow;
	UPROPERTY(Transient) TObjectPtr<UButton> MobileBadgeButton;
	UPROPERTY(Transient) TObjectPtr<UImage> MobileBadgeImage;
	UPROPERTY(Transient) TObjectPtr<UImage> MobileSlotMark;
	UPROPERTY(Transient) TObjectPtr<UImage> MobileItemIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MobilePromptText;
	/** 移动端文字底板 */
	UPROPERTY(Transient) TObjectPtr<UImage> MobilePromptPlate;

	bool bBuiltLayout = false;

	// ---- 脏检查用的上一次状态 ----
	FString LastItemId;
	int32 LastCount = -1;
	FString LastPrompt;
	uint8 LastMode = 0;
	bool bLastVisible = false;
	bool bLastMobileUI = false;
	bool bAppearanceApplied = false;
};
