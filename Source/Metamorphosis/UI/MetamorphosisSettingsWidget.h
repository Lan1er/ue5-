// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 设置界面：音量、摇杆是否固定、摇杆大小、物品栏大小、上下按钮大小。
//
// 每行 = 左侧标题 + 右侧控件（滑条或勾选框）+ 最右数值。
// 改动立刻写进 UMetamorphosisSettingsSubsystem（自动存盘 + 广播，UI 即时刷新）。
//
// 外观与开始界面一致：纯白底 + 深色文字，美术后面换 BackgroundBrush / 字体 / 颜色。
// 做 WBP 子类时按这些名字命名控件会被自动接管：
//   Background（UImage）、SettingsTitle（UTextBlock）、BackButton（UButton）、
//   VolumeSlider / JoystickSizeSlider / HotbarSizeSlider / MobileButtonSizeSlider（USlider）、
//   FixedJoystickCheck（UCheckBox）

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MetamorphosisTouchClaimWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "MetamorphosisSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class UImage;
class USlider;
class UTextBlock;
class UVerticalBox;

/** 返回按钮被点的通知（由 PlayerController 接，用来切回开始界面） */
DECLARE_MULTICAST_DELEGATE(FMetamorphosisSettingsBackRequested);

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisSettingsWidget : public UMetamorphosisTouchClaimWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisSettingsWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	/** 点「返回」时广播 */
	FMetamorphosisSettingsBackRequested OnBackRequested;

	// 2026-10-20：用户要求「返回主界面」不要放在设置界面里（暂停菜单第二项已有该入口）->
	// 本控件的「返回主界面」按钮、委托、显隐开关与相关常量全部删除。

	// ---------------- 外观（美术可改） ----------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FLinearColor BackgroundColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FSlateBrush BackgroundBrush;

	/** 交付 2「设置页面」面板：投影 / 暗绿面板 / 哥特金属外框 / 金色「设置」标题图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FSlateBrush PanelShadowBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FSlateBrush PanelBodyBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FSlateBrush PanelFrameBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FSlateBrush TitleArtBrush;

	/** 面板在屏幕上的宽度（Slate 单位），高度按素材 810:690 比例算 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Layout")
	float PanelWidth = 760.f;

	/** 金色标题图宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Layout")
	float TitleArtWidth = 190.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FText TitleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FText BackButtonText;

	/** 勾选框里的勾（默认 √）与它的颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FText CheckMarkText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Art")
	FLinearColor CheckMarkColor = FLinearColor(0.16f, 0.62f, 0.26f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FSlateFontInfo BodyFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FLinearColor TitleColor = FLinearColor(0.05f, 0.05f, 0.06f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Text")
	FLinearColor BodyColor = FLinearColor(0.2f, 0.2f, 0.22f, 1.f);

	/** 每行宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Layout")
	float RowWidth = 620.f;

	/** 行距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Layout")
	float RowSpacing = 16.f;

	/** 滑条长度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Layout")
	float SliderWidth = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|Settings|Layout")
	float SliderHeight = 14.f;

protected:
	UFUNCTION() void HandleVolumeChanged(float Value);
	UFUNCTION() void HandleJoystickSizeChanged(float Value);
	UFUNCTION() void HandleHotbarSizeChanged(float Value);
	UFUNCTION() void HandleMobileButtonSizeChanged(float Value);
	UFUNCTION() void HandleFixedJoystickChanged(bool bChecked);
	UFUNCTION() void HandleBackClicked();

	void BuildDefaultLayout();
	/** 2026-10-19：旧的代码搭建布局，已被 BuildDesignLayout 取代（保留以便对照/回退，不再调用） */
	void BuildDefaultLayoutLegacy_Unused();
	void CacheWidgets();

	/**
	 * 2026-10-19：按「变形记_设置UI完整交付_v3」复刻的设置界面布局。
	 *
	 * 设计空间 1672x941（取自 图层坐标清单.json 的 canvas）：
	 *   Overlay -> ScaleBox(等比) -> SizeBox(1672x941) -> CanvasPanel
	 * Canvas 上第一层是**烤好的静态背景** BG_Settings（房间/面板/外框/分隔线/标题/五个标签/符文/轨道/开关底板/返回按钮），
	 * 其余是"活的"控件：四个滑条的填充+滑块+数值、开关钮、两个按钮的透明热区。
	 * 坐标全部是设计空间常量（ScaleBox 负责缩放），所以不会随分辨率跑偏。
	 */
	void BuildDesignLayout();

	/** 按当前设置值刷新"活的"控件（填充宽度、滑块位置、开关钮位置、数值文字） */
	void RefreshDesignWidgets();

	/**
	 * 2026-10-20：背景铺满（cover）—— **零侵入**做法。
	 *
	 * 之前那版（已被回退）把背景层单独拆成一个全屏 ScaleBox，结果把内容层从 Overlay 的直接子节点
	 * 移到了新的 ScaleBox 里，Canvas 子控件的基准变了 -> 标签/滑条/开关/返回全部错位 ✗。
	 * 现在**完全不动内容层**（面板/标题/标签/滑条/开关/按钮的父级与坐标一律不变 ✓），
	 * 只做一件事：把"背景层"（兜底底 + 渐变 + 暗角 + 美术图 BG_Settings）在 Canvas 里的矩形
	 * 绕设计空间中心**等比放大 k 倍**，k = cover / contain —— 等价于把整组背景放大，
	 * 于是四边没有黑边，而内容层仍按 ScaleToFit 居中、一个像素都不动。
	 */
	void UpdateDesignCover();



	// ---------------- 复刻布局用的素材（构造时按路径载入，载入失败则该控件不显示） ----------------
	UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> DesignCanvas;
	/** 承载整个设计（背景图 + 控件）的 ScaleBox —— cover 时把它的缩放手改大（contain -> cover） */
	UPROPERTY(Transient) TObjectPtr<class UScaleBox> DesignScaleBox;
	UPROPERTY(Transient) TObjectPtr<class UImage> BgImage;
	UPROPERTY(Transient) TObjectPtr<class UImage> SliderFills[4];
	UPROPERTY(Transient) TObjectPtr<class UImage> SliderKnobs[4];
	UPROPERTY(Transient) TObjectPtr<class UImage> JoystickKnobImage;
	/** 摇杆固定的开关底槽（代码画的 UBorder，**胶囊/椭圆**形状；打开时变墨绿、关闭时暗色） */
	UPROPERTY(Transient) TObjectPtr<class UBorder> JoystickSwitchTrack;


	/** 上一次应用的 cover 缩放（日志/排查用） */
	float LastCoverScale = -1.f;
	/** 上一次算 cover 时的视口像素尺寸与"设计框在屏幕上的像素矩形" —— 都没变就早退，避免每帧刷布局 */
	FVector2D LastCoverViewport  = FVector2D(-1.f, -1.f);
	FVector2D LastCoverCanvasBox = FVector2D(-1.f, -1.f);

	/** 设计空间常量（改这里就能微调，不用动别的） */
	static constexpr float DesignW = 1672.f;
	static constexpr float DesignH = 941.f;
	static constexpr float TrackX = 673.f;
	static constexpr float TrackW = 282.f;
	static constexpr float TrackY[4] = { 320.f, 489.f, 574.f, 660.f };
	static constexpr float ValueX = 576.f;
	static constexpr float ValueW = 80.f;
	static constexpr float HandleSize = 30.f;

	/**
	 * 摇杆固定开关的几何（2026-10-20 改）：
	 * 取自交付包 图层坐标清单.json —— 开关底板 x=867 y=391 w=88 h=43、开关钮（开）x=916 y=395 w=36 h=36。
	 * 底板做成**胶囊/椭圆**（圆角半径 = 高度的一半），与美术图那个椭圆槽重合 ✓
	 * （原来是 92x22 的矩形，看起来像"绿方块盖在椭圆上" ✗）。
	 */
	static constexpr float SwitchX = 867.f;
	static constexpr float SwitchY = 391.f;
	static constexpr float SwitchW = 88.f;
	static constexpr float SwitchH = 43.f;
	static constexpr float SwitchKnobSize = 36.f;
	static constexpr float SwitchKnobOnX = 916.f;    // 开：钮在右（对齐美术图层坐标）
	static constexpr float SwitchKnobOffX = 870.f;   // 关：钮在左
	static constexpr float SwitchKnobY = 395.f;
	void ApplyStaticStyle();

	/**
	 * 从设置子系统刷新控件（打开界面 / 外部改动时调）。
	 *
	 * 2026-10-19（用户反馈"滑条拖不动 / 改了没反应"）：
	 * 原来 NativeTick 每帧都调它，而它会把**滑条的值反向写回去**（SetValue）——
	 * 玩家正在拖的时候，下一帧就被"拉回旧值"，表现就是"拖不动 / 一松手弹回去"。
	 * 现在拆成两半：bSyncSliderValues=true 才回写控件值（只在打开界面 / 外部改动时），
	 * 每帧只刷新文字和填充这些**只读**表现。
	 */
	void RefreshFromSettings(bool bSyncSliderValues = true);

public:
	/**
	 * 诊断：打印每个交互控件的几何尺寸与可见性。
	 * 尺寸为 0 / 不在视口 = "看得见但点不到"的典型原因；无头验证靠它取证。
	 */
	void DumpInteractiveGeometry();

	/**
	 * 模拟拖动第 Index 条滑条（0..3：音量/摇杆大小/物品栏大小/上下按钮大小）到 Value。
	 * 走的是与真实拖动**完全相同**的回调链路（USlider::SetValue -> OnValueChanged -> 写设置子系统），
	 * 用于无头/编辑器里验证"交互链路通不通"（鼠标拖拽本身在无头环境测不了）。
	 */
	void SimulateSliderDrag(int32 Index, float Value);

	/** 模拟点一下「摇杆固定」开关（同样走真实回调链路） */
	void SimulateToggleSwitch();

protected:
	/** 进视口后延迟一小会儿自动打一次几何日志（不需要任何控制台命令） */
	bool bGeometryDumped = false;
	float GeometryTimer = 0.f;

	/** 一行：标题 + 右侧控件槽 + 数值文本，返回那一行里放控件的水平框 */
	class UHorizontalBox* MakeRow(UVerticalBox* Column, const TCHAR* RowName, const FText& Label,
		const FSlateFontInfo& Font, const FLinearColor& Color, TObjectPtr<UTextBlock>& OutValueLabel);

	UPROPERTY(Transient) TObjectPtr<UImage> BackgroundImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleLabel;
	UPROPERTY(Transient) TObjectPtr<USlider> VolumeSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VolumeValue;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> FixedJoystickCheck;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FixedJoystickCheckMark;
	UPROPERTY(Transient) TObjectPtr<USlider> JoystickSizeSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoystickSizeValue;
	UPROPERTY(Transient) TObjectPtr<USlider> HotbarSizeSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HotbarSizeValue;
	UPROPERTY(Transient) TObjectPtr<USlider> MobileButtonSizeSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MobileButtonSizeValue;
	UPROPERTY(Transient) TObjectPtr<UButton> BackButton;

	// 2026-10-20：用户要求设置界面**不再有**「返回主界面」按钮（该功能在暂停菜单里）-> 相关成员与 FText 已删除。

	bool bBuiltLayout = false;

	/** 防止"程序刷新控件"反过来又触发 OnValueChanged 写设置 */
	bool bRefreshing = false;
};



