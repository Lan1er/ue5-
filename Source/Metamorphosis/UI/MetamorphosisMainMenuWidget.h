// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 开始界面：纯白背景（美术后面换成图/视频）+ 标题 + 加载进度 + 「开始游戏」按钮。
//
// 它只做三件事：
//   1. 出现时调 UMetamorphosisPreloadSubsystem::StartPreload()，在菜单里把游戏资源加载完；
//   2. 每帧显示进度；
//   3. 加载完成后点亮「开始游戏」，点了才进关卡。
//
// 换皮：BackgroundBrush / BackgroundColor / TitleText / TitleFont / ButtonStyle …
// 做 WBP 子类时控件按这些名字命名会被自动接管：
//   Background（UImage）、TitleLabel / ProgressLabel（UTextBlock）、
//   LoadingBar（UProgressBar）、StartButton（UButton）、StartLabel（UTextBlock）

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisMainMenuWidget.generated.h"

class UButton;
class UImage;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	// ---------------- 外观（美术可改） ----------------

	/** 背景色：暂时纯白，后面把 BackgroundBrush 换成图就自动用它做底 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FLinearColor BackgroundColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** 背景画刷（留空 = 用上面的纯色）。换成图片资源就在 WBP 里改这个 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush BackgroundBrush;

	/** 交付 2 开始界面素材：logo / 红色划痕装饰 / 暗角 / 金色聚焦线 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush LogoTexBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush ScratchesBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush VignetteBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush FocusLineBrush;

	/** logo 在屏幕上的宽度（Slate 单位），高度按素材比例算 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float LogoWidth = 430.f;   // 设计图 logo 463/1583 = 29% -> 1922 设计宽约 562   // 560 时 logo 高 380 单位，加上放大后的按钮会超出屏幕（顶部被切）-> 480（高 326）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FText TitleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FText SubtitleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FText StartButtonText;

	/** 「设置」按钮文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FText SettingsButtonText;

	/** 「继续游戏」按钮文字（本作没有存档系统 -> 按钮保留但置灰，点击提示暂无存档） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FText ContinueButtonText;

	/** 「退出」按钮文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FText QuitButtonText;

	// ---------------- 左侧竖向布局（按设计图：logo + 四个按钮靠左） ----------------

	/** 左侧内容列的最大宽度（Slate 单位） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float LeftColumnWidth = 820.f;

	/** 左侧内容距屏幕左边的距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float LeftPadding = 154.f;  // 设计图按钮左 127/1583 = 8% -> 1922 设计宽约 154

	/** 按钮行高（>= ButtonArtHeight 即可；实际绘制尺寸由 ButtonArtHeight 决定） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ButtonRowHeight = 78.f;

	/**
	 * 美术文字图的【目标高度】（Slate 单位）。
	 * ★ 2026-10-19：之前只给了 ArtWidth（固定宽度 166/176/92/78），高度没约束 ->
	 *   UImage 按素材原生尺寸（154x42）绘制，调 ButtonRowHeight 完全没用，所以"怎么调都小"。
	 *   现在改成【高度驱动】：高度 = 这个值 x MenuScale，宽度按素材长宽比自动算 -> 图不变形、能真正放大。
	 * ★ 注意本工程 1.5 倍 DPI：Slate 单位 x 0.667 = 720p 下的物理像素。112 单位 ~ 75 物理像素。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ButtonArtHeight = 78.f;

	/** 按钮最小宽度（点击热区；文字图比它窄时也按这个宽度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ButtonMinWidth = 400.f;

	/** 按钮之间的竖向间距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ButtonRowSpacing = 12.f;

	/**
	 * 整块菜单的统一缩放（1.0 = 默认）。
	 * 一次性缩放 logo 宽度、按钮高度、行距、热区宽度 —— 以后想整体大小改这一个数就行。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float MenuScale = 1.0f;

	/** 底部进度条距屏幕底边的距离（设计图里那块在左下） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ProgressBottomPadding = 16.f;

	/**
	 * 左列底部留白：给底部进度条让位。
	 * ★ 2026-10-19：按钮放大到 112 高后四行加 logo 共约 912 单位，屏幕（1081 单位）居中时最后一颗「退出」
	 *   会正好压在左下进度条上 ✗ —— 加这个留白把整块内容往上抬（居中区域变小 = 视觉重心上移），
	 *   既避开进度条，也更接近设计图"内容偏上"的构图。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ColumnBottomPadding = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FSlateFontInfo BodyFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FLinearColor TitleColor = FLinearColor(0.05f, 0.05f, 0.06f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Text")
	FLinearColor BodyColor = FLinearColor(0.25f, 0.25f, 0.28f, 1.f);

	/** 内容整体在屏幕上的位置（0.5,0.35 = 水平居中、偏上一点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	FVector2D ContentAlignment = FVector2D(0.5f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ProgressBarWidth = 420.f;

	// 进度条高度（UMG 单位）。24 太薄，在暗背景上几乎看不见；32 + 金框后能看清。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	float ProgressBarHeight = 32.f;

	/** 按钮样式（不勾就用内置的纯色样式） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	bool bApplyButtonStyle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FButtonStyle StartButtonStyle;

	/** 进游戏前是否显示鼠标（PC 上点按钮要看得见） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	bool bShowCursorInMenu = true;

	/** 「退出」是否真的退出进程（默认 true；无头验证时用 Meta.Menu.Press Quit 不会真的退） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	bool bQuitEnabled = true;

	/** 调试用：Meta.Menu.Press Quit 时是否真的执行退出（默认 false，只打日志） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Layout")
	bool bQuitReally = false;

	// ---------------- 调试 ----------------
	// 无头环境点不了鼠标：用 SimulatePress 验证"按钮回调链路通不通"，
	// 用 LogControlTree 把控件树 + Visibility 打出来排查"点不动"。

	/** 模拟点按钮：TEXT("Start") / TEXT("Settings")。返回是否找到按钮 */
	bool SimulatePress(const FString& Which);

	/** 打印控件树 + 每个控件的 Visibility（排查点击被装饰层吃掉） */
	void LogControlTree() const;

	/** 取存档子系统（GameInstance 级，跨关卡存活） */
	class UMetamorphosisSaveSubsystem* GetSaveSubsystem() const;

	/** 刷新「继续游戏」的置灰状态（有存档=可用，无存档=半透明） */
	void RefreshContinueAvailability();

protected:
	UFUNCTION() void HandleStartClicked();
	UFUNCTION() void HandleSettingsClicked();
	UFUNCTION() void HandleContinueClicked();
	UFUNCTION() void HandleQuitClicked();

	/** 纯 C++ 默认布局（没有设计器树时生成） */
	void BuildDefaultLayout();
	void CacheWidgets();
	void ApplyStaticStyle();
	void RefreshFromPreload();

	/** 建文字块 */
	UTextBlock* MakeLabel(class UVerticalBox* Column, const TCHAR* Name, const FText& Text,
		const FSlateFontInfo& Font, const FLinearColor& Color, float BottomPadding);

	UPROPERTY(Transient) TObjectPtr<UImage> BackgroundImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressLabel;

	/** 进度的"裁剪容器"：宽度 = 百分比 × ProgressBarWidth，配合 ClipToBounds 精确露出填充图
	 *  （★ 不用 UProgressBar：本工程里它的 Mask 裁剪实测只画出左端几像素，见 .cpp 里的说明） */
	UPROPERTY(Transient) TObjectPtr<class USizeBox> LoadingBarFillHolder;
	UPROPERTY(Transient) TObjectPtr<UImage> LoadingBarFillImage;

	/** 旧实现（UProgressBar）保留声明仅为兼容老代码路径；现在为 null */
	UPROPERTY(Transient) TObjectPtr<UProgressBar> LoadingBar;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StartLabel;
	UPROPERTY(Transient) TObjectPtr<UButton> SettingsButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SettingsLabel;
	UPROPERTY(Transient) TObjectPtr<UButton> ContinueButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ContinueLabel;
	UPROPERTY(Transient) TObjectPtr<UButton> QuitButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> QuitLabel;

	/** 交付 2：按钮文字图（美术把「开始游戏」「设置」切成了图片；拿到就用图片，拿不到退回纯文字） */
	UPROPERTY(Transient) TObjectPtr<UImage> StartArt;
	UPROPERTY(Transient) TObjectPtr<UImage> SettingsArt;
	UPROPERTY(Transient) TObjectPtr<UImage> ContinueArt;
	UPROPERTY(Transient) TObjectPtr<UImage> QuitArt;

	/** 文字图刷子（构造函数里从 /Game/UI/Delivery2/Start 载入；空则继续用文字） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush StartArtBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush SettingsArtBrush;

	/** 「继续游戏」「退出」的文字图（同上：拿到用图，拿不到用文字） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush ContinueArtBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	FSlateBrush QuitArtBrush;

	/** 文字图显示宽度（Slate 单位，高度按原图比例） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	float StartArtWidth = 176.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	float SettingsArtWidth = 92.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	float ContinueArtWidth = 166.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Art")
	float QuitArtWidth = 78.f;

	bool bBuiltLayout = false;

	/** 上一次刷新时进度/就绪状态，避免每帧都改控件（只用于"文字"变化检测，进度条本身每帧更新） */
	float LastProgress = -1.f;
	bool bLastReady = false;

	// ------------------------------------------------------------------
	// 进度条的"看得见在涨"（用户反馈：加载时进度条不会变化 ✗）
	// 原因有两层：
	//   ① 数据层：预加载虽然逐项异步，但资源在本地很可能是"一口气就绪" -> 真实进度会在极短时间里 0 -> 100，
	//      中间值只存在一两帧，玩家看不到 ✗
	//   ② 显示层：以前只在"进度数值变了"时才 SetPercent，且没有缓动 -> 数值跳变 = 视觉上"不动" ✗
	// 现在的做法：真实进度 + 时间地板（至少用 ProgressMinAnimateSeconds 走完一遍）+ FInterpTo 缓动，
	// 每帧都 SetPercent -> 无论资源多快，玩家一定看到它在稳定地涨 ✓
	// ------------------------------------------------------------------

	/** 进度条显示值向目标缓动的速度（越大越跟手；6 左右观感自然） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Progress")
	float ProgressInterpSpeed = 6.f;

	/**
	 * 进度条"至少花多少秒走完 0->100%"。
	 * 预加载最短也停留 MinimumLoadingSeconds(2.5 秒)，所以 1.4 秒的动画不会拖慢进游戏，
	 * 只是保证玩家能明显看到进度在动 ✓
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Menu|Progress")
	float ProgressMinAnimateSeconds = 1.4f;

	/** 进度条当前"显示用"的百分比（缓动后的值，0..1） */
	float ProgressDisplay = 0.f;

	/** 本控件出现后经过的时间（秒），用于时间地板 */
	float ProgressElapsed = 0.f;

	/** 上一次重建"加载中"文字时的整数百分比（-1 = 还没建过） */
	int32 LastPercentShown = -1;

	/** 已经打过"进度源"诊断日志 */
	bool bLoggedProgressSource = false;

	/** 按钮实际绘制尺寸只打一次日志（排查"看着太小"用） */
	bool bLoggedButtonSizes = false;

	/** 「新游戏」在"资源还没就绪"时被点过一次 -> 就绪后自动进关卡
	 *  否则玩家第一次点只是触发加载（看起来像"点了没反应"✗），要再点一次才行 ✗ */
	bool bPendingStart = false;

	/** 就绪后自动开局的延时（秒），给"正在加载"提示一点停留时间 */
	float PendingStartDelaySeconds = 0.35f;
	float PendingStartElapsed = -1.f;
};
