// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
// 主界面数值 HUD：怀疑值 / 躁郁值两条 + 档位文字。
//
// 换皮设计（美术后续换贴图不需要改代码）：
//   * 纯色进度条：SuspicionBarStyle / MoodBarStyle（FProgressBarStyle，里面就是 BackgroundImage / FillImage 两个 Brush）
//   * 图标：SuspicionIconBrush / MoodIconBrush（留空 = 不显示图标，赋了贴图就自动出现）
//   * 颜色：AlertLevelColors / BodyStageColors（0~1 按档位取色），LabelColor / ValueColor
//   * 字体：LabelFont / ValueFont
//   * 文案：SuspicionLabelText / MoodLabelText / AlertLevelTexts / BodyStageTexts
//   * 布局：PanelWidth / BarHeight / RowSpacing / SectionSpacing / IconSize
//
// 两种用法：
//   A. 什么都不做：C++ 自己搭一棵默认的树（靠上面的属性调样式）。
//   B. 做 WBP_StatsHUD 子类（父类选本类）：在设计器里自己摆控件，只要命名对得上就会被自动接管
//      —— 名字：SuspicionBar / MoodBar / SuspicionValue / MoodValue / SuspicionLabel / MoodLabel /
//               SuspicionIcon / MoodIcon。设计器树存在时不会再生成默认布局（缺的会打 Warning 提示）。
//
// 更复杂的换皮预留：OnStatsUpdated 是蓝图事件，换贴图后要做闪光/抖动/飘字动画挂在这里。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "MetamorphosisStatsWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisStatsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisStatsWidget(const FObjectInitializer& ObjectInitializer);

	// UUserWidget
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 立刻按当前数值刷新一次（正常由数值系统委托驱动，这里给蓝图/调试留入口） */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	void RefreshStats();

	/** 本 HUD 在屏幕上的高度（= 屏宽 x 设计高/设计宽）。
	 *  PC 用它把"任务条"贴到数值条正下方 —— 视口控件在 Slate 排版前量不到高度，
	 *  有这个约定值就不会叠在一起。 */
	UFUNCTION(BlueprintCallable, Category = "Metamorphosis|UI")
	float GetPreferredHUDHeight() const;

	/** 数值刷新后的蓝图事件：换皮后要做动效就挂这里 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Metamorphosis|UI")
	void OnStatsUpdated(float Suspicion, float Mood, int32 AlertLevel, int32 BodyStage);

protected:
	UFUNCTION()
	void HandleStatsChanged(float Suspicion, float Mood, int32 AlertLevel, int32 BodyStage);

	/** 纯 C++ 默认布局（只在没有设计器树时调用） */
	void BuildDefaultLayout();
	/** 按名字接管控件：设计器里同名控件会覆盖 C++ 默认控件 */
	void CacheWidgets();
	/** 字体/颜色/贴图这类静态样式，只在初始化时刷一次 */
	void ApplyStaticStyle();

	void BuildStatBlock(
		UVerticalBox* Column,
		const TCHAR* RowName,
		const TCHAR* IconName,
		const TCHAR* LabelName,
		const TCHAR* ValueName,
		const TCHAR* BarBoxName,
		const TCHAR* BarName,
		const FText& LabelText,
		const FSlateBrush& IconBrush,
		const FProgressBarStyle& BarStyle,
		float TopPadding,
		TObjectPtr<UImage>& OutIcon,
		TObjectPtr<UTextBlock>& OutLabel,
		TObjectPtr<UTextBlock>& OutValue,
		TObjectPtr<UProgressBar>& OutBar) = delete;

	// ------------------------------------------------------------------
	// 布局
	// ------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float PanelWidth = 340.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float BarHeight = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float RowSpacing = 6.f;

	/** 两条数值之间的间距 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float SectionSpacing = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float IconSize = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float LabelIconSpacing = 6.f;

	// ------------------------------------------------------------------
	// 文案 / 字体
	// ------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FText SuspicionLabelText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FText MoodLabelText;

	/** 怀疑档位文字，索引 = AlertLevel（0 忽略 / 1 搜寻 / 2 暴露） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	TArray<FText> AlertLevelTexts;

	/** 体型阶段文字，索引 = Stage - 1（阶段1 虫子 / 阶段2 2倍 / 阶段3 4倍） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	TArray<FText> BodyStageTexts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FSlateFontInfo ValueFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FLinearColor LabelColor = FLinearColor(0.85f, 0.85f, 0.88f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FLinearColor ValueColor = FLinearColor::White;

	/** 面板底色（按 UI 设定稿：半透明深褐） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FLinearColor PanelBackgroundColor = FLinearColor(0.09f, 0.07f, 0.05f, 0.72f);

	/** 进度条外框色（描金） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FLinearColor BarFrameColor = FLinearColor(0.72f, 0.60f, 0.38f, 0.55f);

	// ------------------------------------------------------------------
	// 美术资源
	// ------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FProgressBarStyle SuspicionBarStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FProgressBarStyle MoodBarStyle;

	/** 留空则不显示图标；赋贴图/Material 后自动出现 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush SuspicionIconBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush MoodIconBrush;

	/** 进度条填充色按怀疑档位取色（0 忽略 / 1 搜寻 / 2 暴露） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	TArray<FLinearColor> AlertLevelColors;

	/** 进度条填充色按体型阶段取色（1 / 2 / 3） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	TArray<FLinearColor> BodyStageColors;

	/** 关掉它，进度条填充色就完全交给美术在 WBP 里自己控制 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	bool bTintBarByTier = true;

	/** 是否用本类的 Style 覆盖进度条样式。纯 C++ 默认布局会自动置 true */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	bool bApplyBarStyle = false;

	/** 数值文字格式：{0}=当前值 {1}=上限 {2}=档位/阶段文字 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Text")
	FText ValueFormatText;

	// ------------------------------------------------------------------
	// 交付 2「三值 UI 分层包」的贴图（构造时自动从 /Game/UI/Delivery2/Stats 载入）
	// 设计空间 = 1118 x 289（美术给的 1536x1024 画布上抠出来的三值区，坐标见
	// Saved/_ui2_stage.txt 与美术的《图层坐标清单.json》）。
	// ------------------------------------------------------------------

	/** 设计空间宽高（美术导出尺寸；改它要同步 StatsDesignHeight 与各贴图位置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float StatsDesignWidth = 1118.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float StatsDesignHeight = 289.f;

	/** 屏幕上的实际宽度（UMG 单位）：整个三值面板等比缩放到这个宽度。
	 *  420 时设计空间里的文字在屏上只有约 10px，偏小看不清 -> 提到 620（约 +48%，文字约 15px）。
	 *  想更大就继续加；PC 会按 GetPreferredHUDHeight() 把周目条/任务条自动下移，不会压到。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Layout")
	float PanelScreenWidth = 620.f;

	/** 标题（左上角那块「三值」字样牌） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush TitleFrameBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush TitlePlateBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush TitleTextBrush;

	/** 两条数值的外框 / 底板 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush SuspicionFrameBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush SuspicionPlateBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush MoodFrameBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush MoodPlateBrush;

	/** 读条：槽底 / 轮廓 / 填充（绿=低值、红=高值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush BarTrackBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush BarOutlineBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush BarFillGreenBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush BarFillRedBrush;

	/** 预渲染的行标题字样（「怀疑值」「躁郁值」「体型阶段」） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush LabelSuspicionBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush LabelMoodBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush LabelBodyBrush;

	/** 图标：眼（怀疑三态）/ 茧（体型三阶段） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	TArray<FSlateBrush> EyeBrushes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	TArray<FSlateBrush> CocoonBrushes;

	/** 躁郁刻度圆点：空 / 绿 / 红 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush PipEmptyBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush PipGreenBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush PipRedBrush;

	/** 体型阶段牌：空底 / 阶段字符 I·II·III */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush StagePlateBrush;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	TArray<FSlateBrush> StageCharBrushes;

	/** 体型引导细线 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metamorphosis|UI|Art")
	FSlateBrush BodyLineBrush;

private:
	UPROPERTY(Transient) TObjectPtr<UProgressBar> SuspicionBar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> MoodBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SuspicionLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MoodLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SuspicionValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MoodValue;
	UPROPERTY(Transient) TObjectPtr<UImage> SuspicionIcon;
	UPROPERTY(Transient) TObjectPtr<UImage> MoodIcon;

	/** 交付 2 换皮后的各层图片（默认布局里创建；WBP 子类可同名接管） */
	UPROPERTY(Transient) TObjectPtr<UImage> SuspicionFillImage;
	UPROPERTY(Transient) TObjectPtr<UImage> BodyStageCharImage;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MoodPips;

	/** 面板底板（纯 C++ 布局时自动创建） */
	UPROPERTY(Transient) TObjectPtr<class UBorder> PanelBorder;

	/** 是否由本类生成了默认布局（决定要不要接管进度条样式） */
	bool bBuiltLayout = false;

	/** 按档位/阶段切贴图：眼 0/1/2、茧 0/1/2、阶段字符 I/II/III */
	void UpdateTierArt(int32 AlertLevel, int32 BodyStage);
};

