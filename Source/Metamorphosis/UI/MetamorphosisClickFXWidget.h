// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT
//
// 点击反馈（点击/触摸的视觉反馈）—— 美术交付「点击动画序列」里的 stylized_click_v2。
//
// 素材规格（来自美术的《制作说明.txt》）：15 帧、320x320、RGBA 真透明、30fps、单次 500ms、
// **对齐点是画布正中心**（不要逐帧裁边）。所以这里每一帧都按中心对齐绘制。
//
// 结构：全屏 UOverlay（SelfHitTestInvisible，**绝不挡输入**）-> UCanvasPanel（用来按绝对坐标摆 burst）
//
// ⚠️ 本工程 UMG 踩过的坑（照做）：
//   * 布局必须在 `Initialize()` 里搭（放 NativeConstruct 会尺寸恒 0）；
//   * 根节点必须 SelfHitTestInvisible，否则会吞掉屏幕上所有点击；
//   * **别把整个控件 Collapsed**：Slate 不会 tick 被折叠的控件 —— 空闲时保持可见，只是没有子节点；
//   * 坐标要用 **UMG 本地坐标**（本工程 1.5 倍 DPI：视口像素 ≠ UMG 本地单位），
//     换算统一由 PlayerController 做（见 MetamorphosisPlayerController::PlayClickFX）。
//
// 换皮：Project Settings -> Game -> Metamorphosis UI -> Click FX Class 指到 WBP；
// 帧图前缀/数量/尺寸/时长/上限都在 DefaultGame.ini 的 [Metamorphosis.ClickFX] 段。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MetamorphosisClickFXWidget.generated.h"

class UCanvasPanel;
class UImage;
class UTexture2D;

UCLASS(Blueprintable)
class METAMORPHOSIS_API UMetamorphosisClickFXWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMetamorphosisClickFXWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 在 **UMG 本地坐标**处播一次点击动效（坐标是控件坐标系，不是视口像素）。
	 * 超过同位置限频/达到并发上限时会静默丢弃（返回 false），不会报错。
	 */
	bool PlayAt(const FVector2D& LocalPosition);

	/** 立刻清掉所有正在播的 burst（关背包/过场开始时调用） */
	void StopAll();

	/** 当前存活 burst 数（调试命令 Meta.ClickFX.Info 用） */
	int32 GetAliveCount() const { return Bursts.Num(); }

	/** 载入到的帧数（0 = 素材没导入，播放会被跳过） */
	int32 GetLoadedFrameCount() const { return Frames.Num(); }

	/** 每帧显示尺寸（UMG 本地单位） */
	float GetBurstSize() const { return BurstSize; }

	/** 从 ini 段应用参数（PlayerController 在 BeginPlay 调；ini 在构造函数里读不到） */
	void ApplyConfig(float InBurstSize, float InDurationSeconds, int32 InMaxAlive,
		float InMinIntervalSeconds, float InMinDistance, const FString& InFrameFolder, int32 InFrameCount);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 一个正在播的点击动效 */
	struct FBurst
	{
		TWeakObjectPtr<UImage> Image;
		FVector2D Center = FVector2D::ZeroVector;   // UMG 本地坐标（中心点）
		float Elapsed = 0.f;
	};

	void BuildClickFXLayout();
	void LoadFrames();
	void RemoveBurstAt(int32 Index);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> BurstCanvas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Frames;

	TArray<FBurst> Bursts;

	/** 每帧显示尺寸（UMG 本地单位）。美术是 320x320，这里缩到 ~96 更贴近手指大小 */
	float BurstSize = 96.f;

	/** 一次播放总时长（秒）。美术规格 500ms */
	float Duration = 0.5f;

	/** 同时最多几个（超出就先移除最老的） */
	int32 MaxAlive = 6;

	/** 同一位置的限频（秒）：这段时间内同位置的点不重复播 */
	float MinInterval = 0.12f;

	/** "同位置"的判定半径（UMG 本地单位） */
	float MinDistance = 40.f;

	/** 帧图资源前缀（默认 /Game/UI/Click/click_）与帧数（默认 15） */
	FString FrameFolder = TEXT("/Game/UI/Click/click_");
	int32 FrameCount = 15;

	/** 最近一次成功播放的时间/位置（用于限频） */
	float LastSpawnTime = -1000.f;
	FVector2D LastSpawnCenter = FVector2D(-1e9f, -1e9f);

	/** 自增时钟（秒） */
	float Clock = 0.f;

	/** 是否已经尝试过加载帧（避免每帧重复 LoadObject） */
	bool bTriedLoadFrames = false;
};
