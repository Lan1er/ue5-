// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetamorphosisPlayerController.h"
#include "Character/MetamorphosisBeetleCharacter.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"
#include "Metamorphosis.h"
#include "Input/MetamorphosisMoveInput.h"
#include "UI/MetamorphosisStatsWidget.h"
#include "UI/MetamorphosisLoopCountWidget.h"
#include "UI/MetamorphosisTaskWidget.h"
#include "UI/MetamorphosisMobileControlsWidget.h"
#include "UI/MetamorphosisMainMenuWidget.h"
#include "UI/MetamorphosisHotbarWidget.h"
#include "UI/MetamorphosisSettingsWidget.h"
#include "UI/MetamorphosisPauseMenuWidget.h"
#include "UI/MetamorphosisSettingsButtonWidget.h"
#include "UI/MetamorphosisJoystickWidget.h"
#include "UI/MetamorphosisInteractPromptWidget.h"
#include "UI/MetamorphosisClickFXWidget.h"
#include "Story/MetamorphosisStorySubsystem.h"
#include "UI/MetamorphosisStoryWidget.h"
#include "UI/MetamorphosisBackpackWidget.h"
#include "UI/MetamorphosisSettlementWidget.h"
#include "UI/MetamorphosisEndingWidget.h"
#include "Interaction/MetamorphosisInteractionComponent.h"
#include "Game/MetamorphosisSettingsSubsystem.h"
#include "Inventory/MetamorphosisInventoryComponent.h"
#include "MetamorphosisGameMode.h"
#include "Game/MetamorphosisPreloadSubsystem.h"
#include "Loop/MetamorphosisLoopSubsystem.h"
#include "Save/MetamorphosisSaveSubsystem.h"
#include "UI/MetamorphosisUISettings.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "HAL/IConsoleManager.h"

AMetamorphosisPlayerController::AMetamorphosisPlayerController()
{
	// 目标平台是移动端：不显示鼠标指针，点击屏幕也不驱动移动（模板的「点哪走哪」已删除）。
	// PC 上只保留 WASD 供调试，移动端走引擎自带的虚拟摇杆（输出 Gamepad_LeftX/LeftY）。
	bShowMouseCursor = false;
}

// ---------------------------------------------------------------------------
// 数值 HUD（怀疑值 / 躁郁值）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 开始界面关卡：只挂开始界面（界面上会把游戏资源提前加载完），不显示数值 HUD
	if (const AMetamorphosisGameMode* GameMode = GetWorld() != nullptr
		? GetWorld()->GetAuthGameMode<AMetamorphosisGameMode>() : nullptr)
	{
		if (GameMode->IsMenuLevel())
		{
			ShowMainMenu();
			return;
		}
	}

	ShowStatsHUD();
	ShowTaskHUD();
	ShowLoopCountHUD();
	ShowMobileControls();
	ShowHotbar();
	ShowVirtualJoystick();
	ShowGameplayMenuButton();
	ShowInteractPrompt();
	ShowClickFX();

	// 剧情过场：订阅节点变化，并在进关卡时自动播开场（每局一次；周目结算不重播）
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->OnStoryChanged.AddDynamic(this, &AMetamorphosisPlayerController::HandleStoryChanged);
		if (Story->IsPlaying())
		{
			ShowStoryOverlay();
		}
		else if (Story->bAutoPlayOpening)
		{
			Story->StartOpening();
		}
	}

	// 设置变化时即时刷新各 UI 的缩放
	if (UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this))
	{
		GameSettings->OnSettingsChanged.AddUObject(this, &AMetamorphosisPlayerController::HandleSettingsChanged);
	}

	// ---- 存档接线：进关卡后延迟 2 秒（等关卡/玩家/子系统全部 BeginPlay 完）兜一次存 + 应用待定位置 ----
	// 读档流程 RequestContinue() 会先 LoadNow() 再把玩家位置存成 pending，等这里应用。
	if (GetWorld() != nullptr)
	{
		FTimerHandle SaveWarmupTimer;
		GetWorld()->GetTimerManager().SetTimer(SaveWarmupTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (UMetamorphosisSaveSubsystem* Save = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UMetamorphosisSaveSubsystem>() : nullptr)
			{
				Save->ApplyPendingPlayerTransform();
				Save->SaveNow(TEXT("进关卡"));
			}
		}), 2.f, false);
	}
}

void AMetamorphosisPlayerController::ShowHotbar()
{
	if (HotbarWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowHotbar)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->HotbarClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisHotbarWidget::StaticClass();
	}

	HotbarWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (HotbarWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 快捷栏创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	HotbarWidget->AddToViewport(Settings->HotbarZOrder);

	// 格子要能点（移动端点击切换），所以输入模式要允许 UI 接收输入
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	// 大小跟随设置
	if (UMetamorphosisHotbarWidget* Hotbar = Cast<UMetamorphosisHotbarWidget>(HotbarWidget))
	{
		if (const UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Hotbar->ApplyScale(GameSettings->GetSettings().HotbarScale);
		}
	}

	// 快捷栏右侧的「背包」按钮 -> 打开背包
	if (UMetamorphosisHotbarWidget* Hotbar = Cast<UMetamorphosisHotbarWidget>(HotbarWidget))
	{
		Hotbar->OnBackpackRequested.AddUObject(this, &AMetamorphosisPlayerController::ToggleBackpack);
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 快捷栏已挂载：%s"), *GetNameSafe(WidgetClass));
}

// ---------------------------------------------------------------------------
// 背包界面（书本式道具库；开着时屏蔽游戏输入，但不暂停游戏）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowBackpack()
{
	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !IsLocalPlayerController())
	{
		return;
	}

	// 过场 / 结算 / 暂停弹窗期间不给开（避免多层 UI 叠在一起）
	if (bStoryPlaying || bInSettlement || bGameplayPaused)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 背包：当前状态不允许打开（过场=%d 结算=%d 暂停=%d）"),
			bStoryPlaying ? 1 : 0, bInSettlement ? 1 : 0, bGameplayPaused ? 1 : 0);
		return;
	}

	if (!Settings->bShowBackpack)
	{
		return;
	}

	if (BackpackWidget == nullptr)
	{
		TSubclassOf<UUserWidget> WidgetClass = Settings->BackpackClass;
		if (WidgetClass == nullptr)
		{
			WidgetClass = UMetamorphosisBackpackWidget::StaticClass();
		}

		UMetamorphosisBackpackWidget* Created = CreateWidget<UMetamorphosisBackpackWidget>(this, WidgetClass);
		if (Created == nullptr)
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 背包创建失败：%s"), *GetNameSafe(WidgetClass));
			return;
		}

		BackpackWidget = Created;
		BackpackWidget->OnBackpackClosed.AddUObject(this, &AMetamorphosisPlayerController::HideBackpack);
		BackpackWidget->AddToViewport(Settings->BackpackZOrder);
	}
	else
	{
		BackpackWidget->SetVisibility(ESlateVisibility::Visible);
	}

	bBackpackOpen = true;

	// 背包打开时把"其它"游戏 HUD 临时收起来（否则数值条/快捷栏会浮在压暗层上，观感乱）；
	// bKeepBackpack=true：背包自己不能被这套开关摘掉。
	SetGameplayHUDVisible(false, /*bKeepBackpack=*/true);

	// 背包里要能点：UI 优先 + 放出鼠标
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 背包已打开（游戏未暂停，游戏输入已屏蔽）"));
}

void AMetamorphosisPlayerController::HideBackpack()
{
	bBackpackOpen = false;

	if (BackpackWidget != nullptr)
	{
		BackpackWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 恢复被背包收起来的其它 HUD（过场/暂停期间不允许开背包，所以这里恢复是安全的）
	SetGameplayHUDVisible(true);

	bShowMouseCursor = false;

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 背包已关闭"));
}

void AMetamorphosisPlayerController::ToggleBackpack()
{
	if (bBackpackOpen)
	{
		HideBackpack();
	}
	else
	{
		ShowBackpack();
	}
}

bool AMetamorphosisPlayerController::BagSelect(int32 Index)
{
	if (BackpackWidget == nullptr || !bBackpackOpen)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 背包没打开，先 Meta.Bag.Open"));
		return false;
	}
	if (Index < 0 || Index >= UMetamorphosisInventoryComponent::NumSlots)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 格子下标 %d 越界（0..%d）"), Index, UMetamorphosisInventoryComponent::NumSlots - 1);
		return false;
	}
	BackpackWidget->InspectSlot(Index);
	UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 背包选中第 %d 格"), Index + 1);
	return true;
}

bool AMetamorphosisPlayerController::BagUse()
{
	if (BackpackWidget == nullptr || !bBackpackOpen)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 背包没打开，先 Meta.Bag.Open"));
		return false;
	}
	return BackpackWidget->UseInspectedSlot();
}

bool AMetamorphosisPlayerController::BagDrop()
{
	if (BackpackWidget == nullptr || !bBackpackOpen)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 背包没打开，先 Meta.Bag.Open"));
		return false;
	}
	return BackpackWidget->DropInspectedSlot();
}

// ---------------------------------------------------------------------------
// 设置界面
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowSettings()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (SettingsWidget == nullptr)
	{
		const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
		if (Settings == nullptr)
		{
			return;
		}

		TSubclassOf<UUserWidget> WidgetClass = Settings->SettingsClass;
		if (WidgetClass == nullptr)
		{
			WidgetClass = UMetamorphosisSettingsWidget::StaticClass();
		}

		SettingsWidget = CreateWidget<UUserWidget>(this, WidgetClass);
		if (SettingsWidget == nullptr)
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 设置界面创建失败：%s"), *GetNameSafe(WidgetClass));
			return;
		}

		// 返回按钮：切回开始界面
		if (UMetamorphosisSettingsWidget* SettingsMenu = Cast<UMetamorphosisSettingsWidget>(SettingsWidget))
		{
			SettingsMenu->OnBackRequested.AddUObject(this, &AMetamorphosisPlayerController::HandleSettingsBackRequested);
			// 2026-10-19：用户要求「下一周目」做在暂停界面 -> 设置面板里的按钮已移除，这里也不再绑定
			// （暂停菜单走 HandlePauseMenuOption 的 Tag == NextLoop 分支，复用 HandleSettingsNextLoop）。
			// 2026-10-20：用户要求设置界面不再有「返回主界面」按钮 -> 那个委托也不存在了；
			// 该入口在暂停菜单第四项（Tag == ReturnToTitle）里。
		}

		SettingsWidget->AddToViewport(Settings->SettingsZOrder);
	}
	else
	{
		SettingsWidget->SetVisibility(ESlateVisibility::Visible);
	}

	// 2026-10-20：设置界面不再有「返回主界面」按钮（入口在暂停菜单第四项），这里不再需要按 Pawn 设置显隐。

	// 菜单里要把鼠标放出来
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	// 游戏内打开设置也必须时停：否则玩家在设置里调数值时，时钟继续走、妻子继续巡视/抓人。
	// 只在"有受控角色且当前没暂停"时由我们主动暂停（开始界面没有 Pawn，不会被暂停）；
	// 如果本来就是从暂停菜单/结算进来（已经暂停），只记录、不重复设置。
	bSettingsPausedGame = false;
	if (GetPawn() != nullptr && !UGameplayStatics::IsGamePaused(this))
	{
		SetGameplayPaused(true);
		bSettingsPausedGame = true;
	}

	// 兜底断言：只要打开了设置且游戏里有角色，就**必须**是暂停状态。
	// （任何路径漏掉暂停都会让时钟继续走、妻子继续巡视/抓人 —— 玩家在设置里被抓就是这个原因。）
	if (GetPawn() != nullptr && !UGameplayStatics::IsGamePaused(this))
	{
		SetGameplayPaused(true);
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] 打开设置时发现游戏未暂停，已强制暂停（兜底）"));
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 设置界面已打开（游戏暂停 = %d，由设置暂停 = %d）"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0, bSettingsPausedGame ? 1 : 0);
}

UMetamorphosisSettingsWidget* AMetamorphosisPlayerController::GetSettingsWidget() const
{
	return Cast<UMetamorphosisSettingsWidget>(SettingsWidget.Get());
}

void AMetamorphosisPlayerController::HideSettings()
{
	if (SettingsWidget != nullptr)
	{
		SettingsWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 只恢复"为设置而暂停"的那一次；从暂停菜单进来的（bSettingsOpenedFromPauseMenu）
	// 或者结算/过场期间的暂停，一律不能在这里解除（否则设置一关游戏就自己跑起来了）。
	// 额外防御：只要游戏内弹窗还开着（可能是从弹窗进设置的），也不能恢复。
	const bool bPauseMenuVisible = PauseMenuWidget != nullptr
		&& PauseMenuWidget->GetVisibility() != ESlateVisibility::Collapsed
		&& PauseMenuWidget->GetVisibility() != ESlateVisibility::Hidden;
	if (bSettingsPausedGame && !bSettingsOpenedFromPauseMenu && !bPauseMenuVisible
		&& !bInSettlement && !bStoryPlaying)
	{
		SetGameplayPaused(false);
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 设置界面已关闭，恢复游戏（此前为设置暂停）"));
	}
	else if (bSettingsPausedGame && bPauseMenuVisible)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 设置界面已关闭，但游戏内弹窗还开着 -> 保持暂停"));
	}
	bSettingsPausedGame = false;

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 设置界面已关闭（游戏暂停 = %d）"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void AMetamorphosisPlayerController::HandleSettingsBackRequested()
{
	HideSettings();

	// 如果设置是从"游戏内弹窗"进来的，返回时回到弹窗（游戏仍然停着）
	if (bSettingsOpenedFromPauseMenu && PauseMenuWidget != nullptr)
	{
		// 防御：任何路径都不允许这里把暂停解除（否则从弹窗进设置再返回，游戏会自己跑起来）
		if (!UGameplayStatics::IsGamePaused(this))
		{
			SetGameplayPaused(true);
			UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] 返回弹窗时发现游戏未暂停，已重新暂停"));
		}

		PauseMenuWidget->SetVisibility(ESlateVisibility::Visible);
		PauseMenuWidget->FocusFirstOption();
		bSettingsOpenedFromPauseMenu = false;
	}
}

// ---------------------------------------------------------------------------
// 游戏内弹窗（暂停菜单）+ 右上角设置按钮
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowGameplayMenuButton()
{
	if (GameplayMenuButton != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowGameplayMenuButton)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->GameplayMenuButtonClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisSettingsButtonWidget::StaticClass();
	}

	UUserWidget* Created = CreateWidget<UUserWidget>(this, WidgetClass);
	GameplayMenuButton = Cast<UMetamorphosisSettingsButtonWidget>(Created);
	if (GameplayMenuButton == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 右上角设置按钮创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	GameplayMenuButton->OnClicked.AddUObject(this, &AMetamorphosisPlayerController::HandleGameplayMenuButtonClicked);

	// 右上角边距（控件内部用 Overlay 把按钮顶到右上角）
	GameplayMenuButton->CornerMargin = FMargin(0.f, Settings->GameplayMenuButtonMargin.Y,
		Settings->GameplayMenuButtonMargin.X, 0.f);
	GameplayMenuButton->AddToViewport(Settings->GameplayMenuButtonZOrder);

	// 命中检查：这些铺满屏幕的控件必须都是"自己不吃输入"（4=SelfHitTestInvisible / 3=HitTestInvisible），
	// 否则会挡住下面的快捷栏、上下按钮（踩过：设置按钮曾设成 0=Visible，整屏点击全被吃掉）
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[UI] 命中检查（0=Visible会挡输入, 3=HitTestInvisible, 4=SelfHitTestInvisible）：")
		TEXT("设置按钮=%d 快捷栏=%d 移动端按钮=%d 摇杆=%d 数值HUD=%d 弹窗=%d"),
		GameplayMenuButton != nullptr ? static_cast<int32>(GameplayMenuButton->GetVisibility()) : -1,
		HotbarWidget != nullptr ? static_cast<int32>(HotbarWidget->GetVisibility()) : -1,
		MobileControlsWidget != nullptr ? static_cast<int32>(MobileControlsWidget->GetVisibility()) : -1,
		JoystickWidget != nullptr ? static_cast<int32>(JoystickWidget->GetVisibility()) : -1,
		StatsWidget != nullptr ? static_cast<int32>(StatsWidget->GetVisibility()) : -1,
		PauseMenuWidget != nullptr ? static_cast<int32>(PauseMenuWidget->GetVisibility()) : -1);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 右上角设置按钮已挂载：%s"), *GetNameSafe(WidgetClass));
}

void AMetamorphosisPlayerController::HandleGameplayMenuButtonClicked()
{
	// 周目结算中不弹暂停菜单：结算有自己的全屏反馈，再叠一层弹窗观感很怪
	// （结算流程走的是真实时间，不会因为弹窗暂停而卡住，但玩家会看到两层遮罩）
	if (bInSettlement)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算中：忽略右上角设置按钮"));
		return;
	}

	OpenPauseMenu();
}

void AMetamorphosisPlayerController::OpenPauseMenu()
{
	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !IsLocalPlayerController())
	{
		return;
	}

	// 结算中 / 通关界面显示中：不许打开弹窗（终局界面必须独占，不然会出来两层 UI 打架）
	if (bInSettlement || bWinEndingVisible)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算中或通关界面显示中：忽略弹窗请求"));
		return;
	}

	if (PauseMenuWidget == nullptr)
	{
		TSubclassOf<UUserWidget> WidgetClass = Settings->PauseMenuClass;
		if (WidgetClass == nullptr)
		{
			WidgetClass = UMetamorphosisPauseMenuWidget::StaticClass();
		}

		UUserWidget* Created = CreateWidget<UUserWidget>(this, WidgetClass);
		PauseMenuWidget = Cast<UMetamorphosisPauseMenuWidget>(Created);
		if (PauseMenuWidget == nullptr)
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 弹窗创建失败：%s"), *GetNameSafe(WidgetClass));
			return;
		}

		PauseMenuWidget->OnOptionChosen.AddUObject(this, &AMetamorphosisPlayerController::HandlePauseMenuOption);
		PauseMenuWidget->AddToViewport(Settings->PauseMenuZOrder);
	}
	else
	{
		PauseMenuWidget->SetVisibility(ESlateVisibility::Visible);
	}

	SetGameplayPaused(true);
	PauseMenuWidget->FocusFirstOption();

	// 弹窗里要能点：UI 优先，PC 上放出鼠标
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 弹窗已打开（游戏暂停 = %d）"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void AMetamorphosisPlayerController::ClosePauseMenu()
{
	if (PauseMenuWidget != nullptr)
	{
		PauseMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	bSettingsOpenedFromPauseMenu = false;

	// 防御：设置界面还开着的时候绝不能解除暂停（例如 Esc 同时关弹窗又关设置之类的路径）。
	if (SettingsWidget != nullptr
		&& SettingsWidget->GetVisibility() != ESlateVisibility::Collapsed
		&& SettingsWidget->GetVisibility() != ESlateVisibility::Hidden)
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[UI] 关闭弹窗时设置界面仍开着 -> 保持暂停（不恢复游戏）"));
	}
	else
	{
		SetGameplayPaused(false);
	}

	// 回到"游戏内"输入模式：鼠标收起来
	bShowMouseCursor = false;

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 弹窗已关闭（游戏暂停 = %d）"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void AMetamorphosisPlayerController::SetGameplayPaused(bool bPaused)
{
	bGameplayPaused = bPaused;

	// 控制器要在暂停时也能 tick（触摸/输入相关的处理需要），但 PlayerTick 里对触摸做了暂停判断
	SetTickableWhenPaused(bPaused);
	UGameplayStatics::SetGamePaused(this, bPaused);
}

// ---------------------------------------------------------------------------
// 周目结算（被捉 / 入夜）：冻结输入 + 反馈 UI
// ---------------------------------------------------------------------------

bool AMetamorphosisPlayerController::IsInputBlocked() const
{
	return bGameplayPaused || bInSettlement || bStoryPlaying || bBackpackOpen || UGameplayStatics::IsGamePaused(this);
}

void AMetamorphosisPlayerController::SetSettlementActive(bool bActive)
{
	if (bInSettlement == bActive)
	{
		return;
	}

	bInSettlement = bActive;

	if (bActive)
	{
		// 清掉所有"按住"状态并把升降输入推成 0：
		// 否则结算开始时还按着空格，主角会一直往上飞（输入虽然早退了，但按住状态还在）。
		bAscendHeld = false;
		bDescendHeld = false;
		bAscendButtonHeld = false;
		bDescendButtonHeld = false;
		PushVerticalInput();

		// 停住角色（惯性也清掉）
		if (APawn* ControlledPawn = GetPawn())
		{
			if (UCharacterMovementComponent* Move = ControlledPawn->FindComponentByClass<UCharacterMovementComponent>())
			{
				Move->StopMovementImmediately();
			}
		}
	}

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 控制器结算中 = %d（输入%s）"),
		bInSettlement ? 1 : 0, bInSettlement ? TEXT("已冻结") : TEXT("已恢复"));
}

// ---------------------------------------------------------------------------
// 剧情过场（全黑 + 文字 + 分支按钮）：挂控件、隐藏其它 HUD、屏蔽游戏输入
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::SetGameplayHUDVisible(bool bVisible, bool bKeepBackpack)
{
	if (!bVisible)
	{
		HideStatsHUD();
		HideTaskHUD();
		HideLoopCountHUD();

		if (HotbarWidget != nullptr) { HotbarWidget->RemoveFromParent(); HotbarWidget = nullptr; }
		if (MobileControlsWidget != nullptr) { MobileControlsWidget->RemoveFromParent(); MobileControlsWidget = nullptr; }
		if (JoystickWidget != nullptr) { JoystickWidget->RemoveFromParent(); JoystickWidget = nullptr; }
		if (InteractPromptWidget != nullptr) { InteractPromptWidget->RemoveFromParent(); InteractPromptWidget = nullptr; }
		if (GameplayMenuButton != nullptr) { GameplayMenuButton->RemoveFromParent(); GameplayMenuButton = nullptr; }

		// 背包自己就是"被打开的那个界面"时不能摘掉它（打开背包时需要隐藏的是**其它** HUD）
		if (!bKeepBackpack)
		{
			bBackpackOpen = false;
			if (BackpackWidget != nullptr) { BackpackWidget->RemoveFromParent(); BackpackWidget = nullptr; }
		}

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[UI] 其它 HUD 已全部隐藏（保留背包=%d）"), bKeepBackpack ? 1 : 0);
	}
	else
	{
		// 按 UISettings 重新挂一遍（各 Show* 内部自己判断开关）
		ShowStatsHUD();
		ShowTaskHUD();
		ShowLoopCountHUD();
		ShowMobileControls();
		ShowHotbar();
		ShowVirtualJoystick();
		ShowGameplayMenuButton();
		ShowInteractPrompt();

		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 其它 HUD 已恢复"));
	}
}

void AMetamorphosisPlayerController::ShowStoryOverlay()
{
	if (bStoryPlaying && StoryWidget != nullptr)
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowStory || !IsLocalPlayerController())
	{
		bStoryPlaying = true; // 即使不挂控件也要屏蔽输入，否则过场期间还能操作角色
		return;
	}

	if (StoryWidget == nullptr)
	{
		TSubclassOf<UUserWidget> WidgetClass = Settings->StoryWidgetClass;
		if (WidgetClass == nullptr)
		{
			WidgetClass = UMetamorphosisStoryWidget::StaticClass();
		}
		StoryWidget = CreateWidget<UUserWidget>(this, WidgetClass);
		if (StoryWidget == nullptr)
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 剧情过场控件创建失败"));
			bStoryPlaying = true;
			return;
		}
		StoryWidget->AddToViewport(Settings->StoryZOrder);
	}

	StoryWidget->SetVisibility(ESlateVisibility::Visible);
	bStoryPlaying = true;
	SetGameplayHUDVisible(false);

	// 2026-10-19【关键修复】过场是"要点击才能推进"的模态 UI，输入必须交给 UI：
	// 之前如果游戏刚从一个 GameOnly 状态（例如关闭暂停菜单/设置）进来，Slate 收不到点击，
	// 表现就是"剧情出来了但点屏幕不换句"。这里统一切到 GameAndUI。
	{
		FInputModeGameAndUI StoryInput;
		StoryInput.SetHideCursorDuringCapture(false);
		StoryInput.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(StoryInput);
	}

	// 2026-10-19 诊断：过场显示时的输入状态（"点屏幕不推进"主要看这几项）
	UE_LOG(LogMetamorphosis, Log,
		TEXT("[DIAG] 过场显示：Paused=%d bGameplayPaused=%d 鼠标=%d ZOrder=%d 可见性=%d"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0, bGameplayPaused ? 1 : 0, bShowMouseCursor ? 1 : 0,
		Settings->StoryZOrder, static_cast<int32>(StoryWidget->GetVisibility()));

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 剧情过场已显示（其它 HUD 隐藏，游戏输入已屏蔽）"));
}

void AMetamorphosisPlayerController::HideStoryOverlay()
{
	if (!bStoryPlaying && StoryWidget == nullptr)
	{
		return;
	}

	bStoryPlaying = false;

	if (StoryWidget != nullptr)
	{
		StoryWidget->RemoveFromParent();
		StoryWidget = nullptr;
	}

	SetGameplayHUDVisible(true);

	// 与 ShowStoryOverlay 成对：过场收起后回到"只给游戏输入"（角色可操作、点击直接进 PC）
	SetInputMode(FInputModeGameOnly());

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 剧情过场已收起（输入恢复，HUD 恢复）"));
}

void AMetamorphosisPlayerController::HandleStoryChanged(bool bPlaying, FName NodeId, const FText& Speaker,
	const FText& Text, bool bHasChoice)
{
	// 剧情有两种阶段：
	//   Text = 全黑过场读文本（显示黑幕、隐藏 HUD、屏蔽输入）
	//   Gate = 玩法门（收起黑幕、恢复 HUD、玩家自由行动，任务 HUD 显示目标文字）
	// 所以黑幕的显隐要看"过场是否该显示"，而不是剧情是否在播。
	const UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this);
	const bool bOverlayVisible = Story != nullptr ? Story->IsOverlayVisible() : bPlaying;

	if (bOverlayVisible)
	{
		ShowStoryOverlay();
	}
	else
	{
		HideStoryOverlay();
	}
}

void AMetamorphosisPlayerController::ShowSettlementFeedback(const FText& Title, const FText& Subtitle,
	float MaxOpacity, float FadeInSeconds)
{
	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowSettlementFeedback)
	{
		return;
	}

	if (SettlementWidget == nullptr)
	{
		TSubclassOf<UUserWidget> WidgetClass = Settings->SettlementWidgetClass;
		if (WidgetClass == nullptr)
		{
			WidgetClass = UMetamorphosisSettlementWidget::StaticClass();
		}

		SettlementWidget = CreateWidget<UMetamorphosisSettlementWidget>(this, WidgetClass);
		if (SettlementWidget == nullptr)
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 结算反馈控件创建失败：%s"), *GetNameSafe(WidgetClass));
			return;
		}

		SettlementWidget->AddToViewport(Settings->SettlementZOrder);

		// 2026-10-19：结算界面不再自动重置，两个按钮必须真的能点 -> 绑到周目子系统的选择接口
		SettlementWidget->OnRestartRequested.AddDynamic(this, &AMetamorphosisPlayerController::HandleSettlementRestartRequested);
		SettlementWidget->OnReturnToTitleRequested.AddDynamic(this, &AMetamorphosisPlayerController::HandleSettlementReturnToTitleRequested);

		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算反馈已挂载：%s（ZOrder %d）"),
			*GetNameSafe(WidgetClass), Settings->SettlementZOrder);
	}

	// 结算界面是"终态画面"，要盖住游戏内 HUD（否则数值条/任务条/快捷栏会浮在插画上面，很乱）
	SetGameplayHUDVisible(false);

	SettlementWidget->PlayFeedback(Title, Subtitle, MaxOpacity, FadeInSeconds);
}

void AMetamorphosisPlayerController::HideSettlementFeedback()
{
	if (SettlementWidget != nullptr)
	{
		SettlementWidget->StopFeedback();
	}

	// 收起结算后恢复 HUD（结算期间被隐藏了）
	SetGameplayHUDVisible(true);
}

void AMetamorphosisPlayerController::SetSettlementChoiceInput(bool bEnable)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (bEnable)
	{
		// 等待选择：鼠标可见 + GameAndUI（否则按钮点不到），但不解开"结算中"的输入冻结
		bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算界面：已把输入交给 UI（显示鼠标，等待玩家选择）"));
	}
	else
	{
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算界面：已收回 UI 输入（回到 GameOnly）"));
	}
}

void AMetamorphosisPlayerController::HandleSettlementRestartRequested()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算界面：点击「重新开始」"));

	if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
	{
		Loop->ChooseSettlementRestart();
	}
}

void AMetamorphosisPlayerController::HandleSettlementReturnToTitleRequested()
{
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 结算界面：点击「返回主菜单」"));

	if (UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this))
	{
		Loop->ChooseSettlementReturnToTitle();
	}
}

// ---------------------------------------------------------------------------
// 通关界面（2026-10-18：从门缝出去 = 直接通关）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowWinEnding(const FText& Title, const FText& Subtitle,
	const FText& StatsLine, bool bFreezeGame)
{
	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowWinEnding || !IsLocalPlayerController())
	{
		bWinEndingVisible = true;   // 即使不挂控件也要屏蔽输入（否则通关界面背后还能操作角色）
		SetSettlementActive(true);
		return;
	}

	if (WinEndingWidget == nullptr)
	{
		TSubclassOf<UUserWidget> WidgetClass = Settings->WinEndingClass;
		if (WidgetClass == nullptr)
		{
			WidgetClass = UMetamorphosisEndingWidget::StaticClass();
		}

		WinEndingWidget = CreateWidget<UMetamorphosisEndingWidget>(this, WidgetClass);
		if (WinEndingWidget == nullptr)
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 通关界面控件创建失败：%s"), *GetNameSafe(WidgetClass));
			bWinEndingVisible = true;
			SetSettlementActive(true);
			return;
		}

		WinEndingWidget->AddToViewport(Settings->WinEndingZOrder);
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 通关界面已挂载：%s（ZOrder %d）"),
			*GetNameSafe(WidgetClass), Settings->WinEndingZOrder);
	}

	WinEndingWidget->ShowEnding(Title, Subtitle, StatsLine);
	bWinEndingVisible = true;

	// 终局：隐藏其它 HUD（通关界面自己就是全屏黑幕，但把 HUD 摘掉更干净）
	SetGameplayHUDVisible(false);

	// 屏蔽游戏输入（"结算中"这套早退已经被所有输入入口共用）
	SetSettlementActive(true);

	// 冻结时钟/妻子/玩法门：直接暂停游戏（它们都有 IsGamePaused 守卫）
	if (bFreezeGame)
	{
		SetGameplayPaused(true);
	}

	// 界面上要点按钮：放出鼠标
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 通关界面已显示（暂停游戏=%d，输入已屏蔽）"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void AMetamorphosisPlayerController::HideWinEnding()
{
	bWinEndingVisible = false;

	if (WinEndingWidget != nullptr)
	{
		WinEndingWidget->HideEnding();
		WinEndingWidget->RemoveFromParent();
		WinEndingWidget = nullptr;
	}

	// 顺序：先解除暂停（否则 SetGameplayHUDVisible 里挂的控件可能与暂停态打架），再恢复输入与 HUD
	if (bGameplayPaused)
	{
		SetGameplayPaused(false);
	}

	SetSettlementActive(false);
	SetGameplayHUDVisible(true);

	bShowMouseCursor = false;

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 通关界面已收起（游戏暂停=%d，输入已恢复）"),
		UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void AMetamorphosisPlayerController::ReturnToTitleFromEnding()
{
	// 收起通关界面（但不恢复运行：回到标题时游戏应保持暂停，避免菜单背后被妻子追）
	bWinEndingVisible = false;

	if (WinEndingWidget != nullptr)
	{
		WinEndingWidget->HideEnding();
		WinEndingWidget->RemoveFromParent();
		WinEndingWidget = nullptr;
	}

	SetSettlementActive(false);
	SetGameplayHUDVisible(false);

	// ✗ 2026-10-19：用户反馈"点返回主菜单后出现黑底 + 白色矩形 + 居中旁白"——
	//   根因：本函数只收结算界面，**没有停剧情、也没有隐藏过场控件**。若被抓住时正好在播剧情
	//   （或关卡 BeginPlay 里 `if (Story->IsPlaying()) ShowStoryOverlay()` 又把过场显示了一遍），
	//   过场会留在主界面上。这里必须把剧情与过场控件一起收干净。
	if (UMetamorphosisStorySubsystem* Story = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UMetamorphosisStorySubsystem>() : nullptr)
	{
		if (Story->IsPlaying())
		{
			Story->StopStory();
		}
	}
	HideStoryOverlay();

	// 自检：回到主界面 1 秒后打印一次，以后再出现"残留界面"一眼可查
	GetWorldTimerManager().SetTimer(MainMenuSelfCheckTimer, this,
		&AMetamorphosisPlayerController::LogMainMenuSelfCheck, 1.0f, false);

	// 让「开始游戏」还能用：预加载子系统里的 bEntered 是一次性标志，回到标题时必须清掉
	if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this))
	{
		Preload->ResetEntered();
	}

	ShowMainMenu();

	SetGameplayPaused(true);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 已返回标题（开始界面已挂载，游戏保持暂停）"));
}

void AMetamorphosisPlayerController::LogMainMenuSelfCheck()
{
	UMetamorphosisStorySubsystem* Story = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UMetamorphosisStorySubsystem>() : nullptr;
	UE_LOG(LogMetamorphosis, Warning,
		TEXT("[DIAG] 主界面自检：StoryPlaying=%d StoryWidgetVisible=%d SettlementVisible=%d WinEnding=%d"),
		(Story != nullptr && Story->IsPlaying()) ? 1 : 0,
		(StoryWidget != nullptr && StoryWidget->IsVisible()) ? 1 : 0,
		(SettlementWidget != nullptr && SettlementWidget->IsVisible()) ? 1 : 0,
		bWinEndingVisible ? 1 : 0);
}

void AMetamorphosisPlayerController::ReturnToTitleFromSettings()
{
	// 两个入口共用：游戏内设置面板的「返回主界面」按钮 + 暂停菜单的第三个选项
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 返回主界面（入口：设置面板或暂停菜单）"));

	// 1) 先存档（**强制**：剧情/门进行中也存 —— 玩家主动离场时背包和位置必须保留 ✓；
	//    结算中仍会跳过，不会存出脏档）
	if (UMetamorphosisSaveSubsystem* Save = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UMetamorphosisSaveSubsystem>() : nullptr)
	{
		Save->SaveNow(TEXT("返回主界面"), /*bForce=*/true);
	}

	// 2) 关掉设置（只收界面，不动"打开必暂停"的三道锁）
	bSettingsOpenedFromPauseMenu = false;
	if (SettingsWidget != nullptr)
	{
		SettingsWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 3) 让「开始游戏」还能用：预加载子系统里的 bEntered 是一次性标志，回到标题时必须清掉
	if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(this))
	{
		Preload->ResetEntered();
	}

	// 4) 收起其它 HUD 与弹窗，挂上开始界面
	if (PauseMenuWidget != nullptr)
	{
		PauseMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 4.5) 停掉还在播的剧情！剧情子系统挂在 GameInstance 上、跨关卡存活，
	//      如果不清掉 bPlaying/玩法门，重新进关卡时 PC 的 BeginPlay 会走 "Story->IsPlaying() -> 只显示过场" 这条分支，
	//      玩家看到的还是上一局那句、而且门还在等（Advance 被门吞掉）-> 表现就是"剧情出来了但点屏幕不换句"。
	if (UMetamorphosisStorySubsystem* Story = UMetamorphosisStorySubsystem::Get(this))
	{
		Story->StopStory();
	}

	bBackpackOpen = false;
	SetSettlementActive(false);
	SetGameplayHUDVisible(false);
	ShowMainMenu();

	// 5) 回到标题时保持暂停（避免菜单背后时钟继续走/妻子继续巡视）+ 恢复鼠标
	SetGameplayPaused(true);
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 已返回主界面（开始界面已挂载，游戏保持暂停）"));
}

void AMetamorphosisPlayerController::HandleSettingsNextLoop()
{
	// 设置面板的「下一周目」：手动跳到下一个周目（不是失败，不播被捉反馈）
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 玩家点击「下一周目」"));

	UMetamorphosisLoopSubsystem* Loop = UMetamorphosisLoopSubsystem::Get(this);
	if (Loop == nullptr)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] 「下一周目」失败：拿不到 LoopSubsystem"));
		return;
	}

	// 结算中/通关界面显示中 -> 底层会拒绝（这里只把状态如实打出来）
	if (!Loop->RequestManualNextLoop())
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] 「下一周目」被拒绝（结算中或通关界面显示中）"));
		return;
	}

	// 重置已经做完（周目+1、玩家回出生点、数值/任务/剧情按新周目重置）。现在把界面收掉、回到游戏。
	bSettingsOpenedFromPauseMenu = false;
	if (SettingsWidget != nullptr)
	{
		SettingsWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (PauseMenuWidget != nullptr)
	{
		PauseMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsGameplayPaused())
	{
		SetGameplayPaused(false);
	}
	bShowMouseCursor = false;
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 「下一周目」完成：已关闭设置并回到游戏（当前第 %d 周目）"), Loop->GetLoopCount());
}

void AMetamorphosisPlayerController::ExitFrontendMode(const TCHAR* Reason)
{
	// 与 ReturnToTitleFromSettings 的"最后一段"成对：解除暂停 + 输入回 GameOnly + 隐藏鼠标 + 收起开始界面
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 离开主界面，回到游戏（%s）"), Reason != nullptr ? Reason : TEXT("未知"));

	SetGameplayPaused(false);

	bShowMouseCursor = false;
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);

	if (MainMenuWidget != nullptr)
	{
		MainMenuWidget->RemoveFromParent();
		MainMenuWidget = nullptr;
	}
}

void AMetamorphosisPlayerController::HandlePauseMenuOption(FName Tag)
{
	// 这里是弹窗的"总入口"：以后加选项只要加一个 Tag 分支
	if (Tag == TEXT("Resume"))
	{
		ClosePauseMenu();
	}
	else if (Tag == TEXT("OpenSettings"))
	{
		// 隐藏弹窗（不恢复游戏），设置界面盖在上面；返回时 HandleSettingsBackRequested 会把弹窗放回来
		if (PauseMenuWidget != nullptr)
		{
			PauseMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
		}bSettingsOpenedFromPauseMenu = true;
		ShowSettings();
	}
	else if (Tag == TEXT("NextLoop"))
	{
		// 2026-10-19：用户要求「下一周目」挪到暂停菜单（原来在设置面板里，已移除）。
		// 复用设置面板那套处理器：RequestManualNextLoop（周目+1 + 完整重置，不播被捉反馈、不碰 BGM）
		// -> 关设置/弹窗 -> 解除暂停 + GameOnly + 隐藏鼠标。从暂停菜单进来时设置本来就没开，关设置那步无害。
		HandleSettingsNextLoop();
	}
	else if (Tag == TEXT("ReturnToTitle"))
	{
		// 暂停菜单里的「返回主界面」：走和设置面板里那个按钮同一条路（先存档 -> 清 bEntered -> 挂开始界面 -> 保持暂停）
		ReturnToTitleFromSettings();
	}
	else
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] 弹窗选项 %s 还没有对应处理"), *Tag.ToString());
	}
}

void AMetamorphosisPlayerController::HandleSettingsChanged()
{
	const UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this);
	if (GameSettings == nullptr)
	{
		return;
	}

	const FMetamorphosisGameSettings& Current = GameSettings->GetSettings();

	if (UMetamorphosisHotbarWidget* Hotbar = Cast<UMetamorphosisHotbarWidget>(HotbarWidget))
	{
		Hotbar->ApplyScale(Current.HotbarScale);
	}

	if (UMetamorphosisMobileControlsWidget* Controls = Cast<UMetamorphosisMobileControlsWidget>(MobileControlsWidget))
	{
		Controls->ApplyScale(Current.MobileButtonScale);
	}
}

void AMetamorphosisPlayerController::DebugSimulateJoystick(const FVector2D& DeltaPx)
{
	if (DeltaPx.IsNearlyZero())
	{
		bJoystickActive = false;
		VirtualMoveAxis = FVector2D::ZeroVector;
		ApplyMoveAxis(FVector2D::ZeroVector);
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟摇杆：松开"));
		return;
	}

	JoystickCenterPx = GetFixedJoystickCenter();
	JoystickCurrentPx = JoystickCenterPx + DeltaPx;
	bJoystickActive = true;
	VirtualMoveAxis = ComputeJoystickAxis(DeltaPx);

	UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟摇杆：位移 (%.0f, %.0f) px -> 移动轴 (%.2f, %.2f)"),
		DeltaPx.X, DeltaPx.Y, VirtualMoveAxis.X, VirtualMoveAxis.Y);
}

FVector2D AMetamorphosisPlayerController::ComputeJoystickAxis(const FVector2D& DeltaPx) const
{
	// 触摸位移是屏幕像素，而摇杆圆是按 Slate 单位画的（DPI 缩放后两者差一个系数）。
	// 换算到同一尺度再比，保证"手指拖到画出来的圆边 = 满速"。
	const float SafeScale = GetUITouchScale();

	const UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this);
	const float JoystickScale = GameSettings != nullptr ? GameSettings->GetSettings().JoystickScale : 1.f;
	const float RadiusSlate = FMath::Max(10.f, JoystickRadiusPx * JoystickScale);

	const FVector2D DeltaSlate = DeltaPx / SafeScale;
	FVector2D Axis = DeltaSlate / RadiusSlate;

	if (Axis.Size() < FMath::Clamp(JoystickDeadZone, 0.f, 0.9f))
	{
		return FVector2D::ZeroVector;
	}

	if (Axis.SizeSquared() > 1.f)
	{
		Axis = Axis.GetSafeNormal();
	}

	// 屏幕 Y 向下 → 前进是向上，所以取负
	return FVector2D(Axis.X, -Axis.Y);
}

// ---------------------------------------------------------------------------
// 移动端移动摇杆（自己做的：支持固定/浮动 + 大小）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowVirtualJoystick()
{
	if (JoystickWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowVirtualJoystick)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->JoystickClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisJoystickWidget::StaticClass();
	}

	UUserWidget* Created = CreateWidget<UUserWidget>(this, WidgetClass);
	JoystickWidget = Cast<UMetamorphosisJoystickWidget>(Created);
	if (JoystickWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 摇杆创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	// 视觉尺寸与控制器的算法共用同一组基准值，避免"看到的半径"和"算出来的半径"不一致
	JoystickWidget->BaseRadius = JoystickRadiusPx;
	JoystickWidget->KnobRadius = JoystickKnobRadiusPx;
	JoystickWidget->AddToViewport(Settings->JoystickZOrder);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 移动摇杆已挂载：%s（半径 %.0f px）"),
		*GetNameSafe(WidgetClass), JoystickRadiusPx);
}

void AMetamorphosisPlayerController::ShowInteractPrompt()
{
	if (InteractPromptWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowInteractPrompt)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->InteractPromptClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisInteractPromptWidget::StaticClass();
	}

	UUserWidget* Created = CreateWidget<UUserWidget>(this, WidgetClass);
	InteractPromptWidget = Cast<UMetamorphosisInteractPromptWidget>(Created);
	if (InteractPromptWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 交互提示创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	InteractPromptWidget->AddToViewport(Settings->InteractPromptZOrder);

	// 屏幕上有可点的圆徽标，输入模式要允许 UI 接收触摸；没被按钮吃掉的手势仍然进游戏
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[UI] 交互提示已挂载：%s @ 锚点 (%.2f, %.2f)（有目标时才显示）"),
		*GetNameSafe(WidgetClass), InteractPromptWidget->ScreenAnchor.X, InteractPromptWidget->ScreenAnchor.Y);
}

UMetamorphosisInteractionComponent* AMetamorphosisPlayerController::GetInteraction() const
{
	const APawn* ControlledPawn = GetPawn();
	return ControlledPawn != nullptr
		? ControlledPawn->FindComponentByClass<UMetamorphosisInteractionComponent>()
		: nullptr;
}

bool AMetamorphosisPlayerController::IsMobileUI() const
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	// 真机永远按移动端外观
	return true;
#else
	// PC：默认键盘鼠标外观；Meta.UI.Mobile 可以强制切成移动端外观（调试/截图用）
	return bForceMobileUI;
#endif
}

void AMetamorphosisPlayerController::SetForceMobileUI(bool bForce)
{
	bForceMobileUI = bForce;

	// UI 侧下一次 tick 会自己按 IsMobileUI() 换外观，这里只报一句（方便无头验证）
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] %s（交互提示等改用%s外观）"),
		bForce ? TEXT("已强制切到移动端 UI 外观") : TEXT("已切回 PC（键盘鼠标）UI 外观"),
		bForce ? TEXT("移动端") : TEXT("PC"));
}

bool AMetamorphosisPlayerController::TryInteract()
{
	// 暂停/结算中不响应（交互组件内部也会再判一次，这里早退省一次日志）
	if (IsInputBlocked())
	{
		return false;
	}

	// 记下"按了交互键"：剧情玩法门（GateKind=Interact）靠这个时间戳判断玩家是否真的按了
	NotifyInteractInput();

	UMetamorphosisInteractionComponent* Interaction = GetInteraction();
	return Interaction != nullptr ? Interaction->TryInteract() : false;
}

void AMetamorphosisPlayerController::OnInteractStarted()
{
	TryInteract();
}

void AMetamorphosisPlayerController::OnBackpackStarted()
{
	ToggleBackpack();
}

FVector2D AMetamorphosisPlayerController::GetFixedJoystickCenter() const
{
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	const_cast<AMetamorphosisPlayerController*>(this)->GetViewportSize(ViewportX, ViewportY);

	const float Scale = 1.f;   // 屏幕像素坐标，尺寸倍率只影响半径与边距
	return FVector2D(JoystickLeftMargin + JoystickRadiusPx * Scale,
		ViewportY - JoystickBottomMargin - JoystickRadiusPx * Scale);
}

void AMetamorphosisPlayerController::UpdateTouchInput()
{
	// 弹窗暂停 / 周目结算时不处理触摸：时停就该彻底停住（否则还能转视角、摇杆还会推移动）
	if (IsInputBlocked())
	{
		return;
	}

	UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this);
	const bool bFixed = GameSettings != nullptr ? GameSettings->GetSettings().bFixedJoystick : true;
	const float JoystickScale = GameSettings != nullptr ? GameSettings->GetSettings().JoystickScale : 1.f;

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	const float SplitX = ViewportX * FMath::Clamp(TouchJoystickScreenRatio, 0.1f, 0.9f);

	// 屏幕上"看到的"摇杆半径（像素）：Slate 半径 × DPI，用来界定触发区，
	// 这样"手指落在摇杆圆附近"就是能否起摇杆的判据，和玩家看到的一致。
	// 「画出来的半径」用摇杆控件自己的几何缩放换算成屏幕像素：
	// 这样「看到的圆」和「能起摇杆的范围 / 满速判定半径」永远是同一个尺度（之前用 WidgetLayoutLibrary 的 DPI 值，两者来源不同会对不上）
	const float ViewportScale = GetUITouchScale();
	const float VisualRadiusPx = JoystickRadiusPx * JoystickScale * ViewportScale;
	const float ZoneRadiusPx = VisualRadiusPx * FMath::Max(1.f, JoystickTouchZoneScale);
	const FVector2D FixedCenterPx = GetFixedJoystickCenter();

	// 浮动模式下允许起摇杆的范围：左半屏 + 屏幕下方一块（比例可配）
	const float FloatingZoneTopY = ViewportY * (1.f - FMath::Clamp(JoystickFloatingZoneHeightRatio, 0.2f, 1.f));

	for (int32 Finger = 0; Finger < MaxTrackedTouches; ++Finger)
	{
		const ETouchIndex::Type TouchIndex = static_cast<ETouchIndex::Type>(Finger);

		float LocationX = 0.f;
		float LocationY = 0.f;
		bool bPressed = false;
		GetInputTouchState(TouchIndex, LocationX, LocationY, bPressed);

		const FVector2D Position(LocationX, LocationY);
		const bool bWasPressed = TouchWasPressed[Finger];
		TouchWasPressed[Finger] = bPressed;

		// 点击反馈：按下的那一帧在手指位置播一次（UI 按钮上的点击也播，符合"点击/触摸的视觉反馈"）
		if (bPressed && !bWasPressed && bShowClickFX)
		{
			PlayClickFX(LocationX, LocationY);
		}

		if (!bPressed)
		{
			// 抬起：释放角色，顺便解除 UI 认领（下一根手指重新判定）
			if (TouchRoles[Finger] == ETouchRole::Joystick)
			{
				bJoystickActive = false;
				VirtualMoveAxis = FVector2D::ZeroVector;
				ApplyMoveAxis(FVector2D::ZeroVector);   // 停住
			}
			TouchRoles[Finger] = ETouchRole::None;
			TouchClaimedByUI[Finger] = false;
			continue;
		}

		// 被 UI 认领的手指（正按着快捷栏/按钮/弹窗）：摇杆和转视角都不许抢
		if (TouchClaimedByUI[Finger])
		{
			if (TouchRoles[Finger] == ETouchRole::Joystick)
			{
				// UI 是在这一帧才认领的，把摇杆让出去
				bJoystickActive = false;
				VirtualMoveAxis = FVector2D::ZeroVector;
				ApplyMoveAxis(FVector2D::ZeroVector);
				TouchRoles[Finger] = ETouchRole::None;
				UE_LOG(LogMetamorphosis, Log, TEXT("[Input] 手指 %d 被 UI 认领，摇杆让出"), Finger);
			}
			continue;
		}

		// 新按下的手指：决定它是"摇杆"还是"转视角"，或者谁也不管
		if (!bWasPressed && TouchRoles[Finger] == ETouchRole::None)
		{
			const bool bJoystickTaken = bJoystickActive;

			// 摇杆触发区：固定模式 = 摇杆圆附近；浮动模式 = 左下角一块区域
			const bool bInJoystickZone = bFixed
				? (FVector2D::Distance(Position, FixedCenterPx) <= ZoneRadiusPx)
				: (Position.Y >= FloatingZoneTopY);

			if (Position.X < SplitX && !bJoystickTaken && bInJoystickZone)
			{
				TouchRoles[Finger] = ETouchRole::Joystick;
				JoystickCenterPx = bFixed ? FixedCenterPx : Position;
				JoystickCurrentPx = Position;
				bJoystickActive = true;
				VirtualMoveAxis = FVector2D::ZeroVector;
				UE_LOG(LogMetamorphosis, Log, TEXT("[Input] 手指 %d -> 摇杆（位置 %.0f,%.0f）"),
					Finger, Position.X, Position.Y);
			}
			else if (Position.X >= SplitX)
			{
				// 只有**右半屏**才转视角：左半屏那些"没起摇杆"的触摸（比如点在快捷栏/空白处）
				// 一律不管，避免点按钮时视角乱转
				TouchRoles[Finger] = ETouchRole::Look;
				LastLookPosition = Position;
			}
			else
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Input] 手指 %d 落在左半屏但不在摇杆触发区（位置 %.0f,%.0f，触发半径 %.0f），不参与操作"),
					Finger, Position.X, Position.Y, ZoneRadiusPx);
			}
			continue;
		}

		if (TouchRoles[Finger] == ETouchRole::Joystick)
		{
			JoystickCurrentPx = Position;
			VirtualMoveAxis = ComputeJoystickAxis(Position - JoystickCenterPx);
		}
		else if (TouchRoles[Finger] == ETouchRole::Look)
		{
			ApplyTouchLookDelta(Position.X - LastLookPosition.X);
			LastLookPosition = Position;
		}
	}
}

void AMetamorphosisPlayerController::NotifyUITouchClaimed(int32 Finger)
{
	if (!TouchClaimedByUI[Finger])
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[Input] 手指 %d 被 UI 认领（摇杆/视角都不抢）"), Finger);
	}
	TouchClaimedByUI[Finger] = true;
}

void AMetamorphosisPlayerController::NotifyUITouchReleased(int32 Finger)
{
	TouchClaimedByUI[Finger] = false;
}

UMetamorphosisInventoryComponent* AMetamorphosisPlayerController::GetInventoryComponent() const
{
	return GetInventory();
}

UMetamorphosisInventoryComponent* AMetamorphosisPlayerController::GetInventory() const
{
	const APawn* ControlledPawn = GetPawn();
	return ControlledPawn != nullptr ? ControlledPawn->FindComponentByClass<UMetamorphosisInventoryComponent>() : nullptr;
}

void AMetamorphosisPlayerController::SelectHotbarSlot(int32 Index)
{
	if (IsInputBlocked())
	{
		return;
	}
	if (UMetamorphosisInventoryComponent* Inventory = GetInventory())
	{
		Inventory->SelectSlot(Index);
	}
}

void AMetamorphosisPlayerController::OnCycleHotbarSlot(const FInputActionValue& Value)
{
	if (IsInputBlocked())
	{
		return;
	}
	const float Axis = Value.Get<float>();
	if (FMath::IsNearlyZero(Axis, 0.1f))
	{
		return;
	}

	if (UMetamorphosisInventoryComponent* Inventory = GetInventory())
	{
		Inventory->CycleSelection(Axis > 0.f ? 1 : -1);
	}
}

void AMetamorphosisPlayerController::ShowMainMenu()
{
	if (MainMenuWidget != nullptr || !IsLocalPlayerController())
	{
		// 菜单已经存在（第二次"返回主界面"）-> 它不会再走 NativeConstruct ✗
		// 所以这里必须**主动刷新一次**「继续游戏」的可用状态 ✓
		// （否则玩家删了档/存了档再回主界面，按钮的灰态还是上一次的 ✗ —— 用户反馈的"继续游戏依旧灰"就是这个）
		if (UMetamorphosisMainMenuWidget* CachedMenu = Cast<UMetamorphosisMainMenuWidget>(MainMenuWidget))
		{
			CachedMenu->RefreshContinueAvailability();
		}
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->MainMenuClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisMainMenuWidget::StaticClass();
	}

	MainMenuWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (MainMenuWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 开始界面创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	MainMenuWidget->AddToViewport(Settings->MainMenuZOrder);

	// 刚创建时刷新一次（NativeConstruct 里也会刷，这里兜一层 ✓）
	if (UMetamorphosisMainMenuWidget* Menu = Cast<UMetamorphosisMainMenuWidget>(MainMenuWidget))
	{
		Menu->RefreshContinueAvailability();
	}

	// 菜单要能点：UI 优先接收输入，鼠标也放出来
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 开始界面已挂载：%s"), *GetNameSafe(WidgetClass));
}

void AMetamorphosisPlayerController::ShowMobileControls()
{
	if (MobileControlsWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowMobileControls)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->MobileControlsClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisMobileControlsWidget::StaticClass();
	}

	MobileControlsWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (MobileControlsWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 移动端上下按钮创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	// 边距由控件自己管（它内部用 Overlay 把按钮组顶到右下角）
	if (UMetamorphosisMobileControlsWidget* Controls = Cast<UMetamorphosisMobileControlsWidget>(MobileControlsWidget))
	{
		Controls->PanelMargin = FMargin(0.f, 0.f, Settings->MobileControlsMargin.X, Settings->MobileControlsMargin.Y);

		// 大小按已保存的设置来：控件是进关卡时才创建的，而玩家通常是在开始界面的设置里改的，
		// 所以这里必须主动套一次（之前漏了这一步，表现为"上下按钮大小调 60%/160% 没区别"）
		if (const UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this))
		{
			Controls->ApplyScale(GameSettings->GetSettings().MobileButtonScale);
		}
	}

	MobileControlsWidget->AddToViewport(Settings->MobileControlsZOrder);

	// 屏幕上有按钮，输入模式要允许 UI 接收触摸/点击，没被按钮吃掉的手势仍然进游戏
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 移动端上下按钮已挂载：%s @ 右下角 (%.0f, %.0f)"),
		*GetNameSafe(WidgetClass), Settings->MobileControlsMargin.X, Settings->MobileControlsMargin.Y);
}

void AMetamorphosisPlayerController::ShowStatsHUD()
{
	if (StatsWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowStatsHUD)
	{
		return;
	}

	// 没配就退回到 C++ 自带的默认样式
	TSubclassOf<UUserWidget> WidgetClass = Settings->StatsWidgetClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisStatsWidget::StaticClass();
	}

	StatsWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (StatsWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 数值 HUD 创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	StatsWidget->AddToViewport(Settings->StatsHUDZOrder);
	StatsWidget->SetAnchorsInViewport(FAnchors(0.f, 0.f));
	StatsWidget->SetAlignmentInViewport(FVector2D(0.f, 0.f));
	StatsWidget->SetPositionInViewport(Settings->StatsHUDPosition, false);

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 数值 HUD 已挂载：%s @ (%.0f, %.0f)"),
		*GetNameSafe(WidgetClass), Settings->StatsHUDPosition.X, Settings->StatsHUDPosition.Y);
}

void AMetamorphosisPlayerController::HideStatsHUD()
{
	if (StatsWidget != nullptr)
	{
		StatsWidget->RemoveFromParent();
		StatsWidget = nullptr;
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 数值 HUD 已移除"));
	}
}

// ---------------------------------------------------------------------------
// 当前任务 HUD（挂在数值 HUD 正下方）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowTaskHUD()
{
	if (TaskWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowTaskHUD)
	{
		return;
	}

	// 没配就退回到 C++ 自带的默认样式
	TSubclassOf<UUserWidget> WidgetClass = Settings->TaskWidgetClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisTaskWidget::StaticClass();
	}

	TaskWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (TaskWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 任务 HUD 创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	TaskWidget->AddToViewport(Settings->TaskHUDZOrder);
	TaskWidget->SetAnchorsInViewport(FAnchors(0.f, 0.f));
	TaskWidget->SetAlignmentInViewport(FVector2D(0.f, 0.f));

	// 先按兜底位置摆上，等数值 HUD 量出高度后 RepositionTaskHUD 会贴到它正下方
	bTaskHUDPlaced = false;
	LastStatsHeight = -1.f;
	RepositionTaskHUD();

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 任务 HUD 已挂载：%s（贴在数值 HUD 下方，间距 %.0f）"),
		*GetNameSafe(WidgetClass), Settings->TaskHUDGap);
}

void AMetamorphosisPlayerController::HideTaskHUD()
{
	if (TaskWidget != nullptr)
	{
		TaskWidget->RemoveFromParent();
		TaskWidget = nullptr;
		bTaskHUDPlaced = false;
		LastStatsHeight = -1.f;
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 任务 HUD 已移除"));
	}
}

void AMetamorphosisPlayerController::RepositionTaskHUD()
{
	if (TaskWidget == nullptr)
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr)
	{
		return;
	}

	// 数值 HUD 的实测高度（Slate 排版完才有；没量到就用兜底偏移，下一帧再试）
	float StatsHeight = 0.f;
	if (StatsWidget != nullptr)
	{
		// 先问"期望尺寸"，再问"当前几何"，最后才用配置里的约定高度 ——
		// 视口控件在 Slate 排版前这两者都可能是 0（交付 2 换皮后踩到的坑：
		// 量到 0 会让任务条叠在数值条上）。
		const FVector2D DesiredSize = StatsWidget->GetDesiredSize();
		StatsHeight = static_cast<float>(DesiredSize.Y);
		if (StatsHeight <= 1.f)
		{
			StatsHeight = static_cast<float>(StatsWidget->GetCachedGeometry().GetLocalSize().Y);
		}
	}
	if (StatsHeight <= 1.f)
	{
		// 换皮后数值 HUD 的高度有确定算法（屏宽 × 设计高/设计宽），直接问控件要，
		// 比"量 Slate 几何"稳 —— 交付 2 踩到的坑：量到 0 会让任务条叠在数值条上。
		if (const UMetamorphosisStatsWidget* StatsHUD = Cast<UMetamorphosisStatsWidget>(StatsWidget))
		{
			StatsHeight = StatsHUD->GetPreferredHUDHeight();
		}
	}
	if (StatsHeight <= 1.f && Settings->StatsHUDFixedHeight > 0.f)
	{
		StatsHeight = Settings->StatsHUDFixedHeight;
	}
	const bool bMeasured = StatsHeight > 1.f;

	// 已经量到过且高度没变 -> 不用重摆
	if (bTaskHUDPlaced && bMeasured && FMath::IsNearlyEqual(StatsHeight, LastStatsHeight, 1.f))
	{
		return;
	}

	const float OffsetY = bMeasured ? StatsHeight + Settings->TaskHUDGap : Settings->TaskHUDFallbackOffsetY;
	TaskWidget->SetPositionInViewport(
		FVector2D(Settings->StatsHUDPosition.X, Settings->StatsHUDPosition.Y + OffsetY), false);

	LastStatsHeight = StatsHeight;
	bTaskHUDPlaced = bMeasured;

	if (bMeasured)
	{
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 任务 HUD 已对齐到数值 HUD 下方：Y=%.0f（数值 HUD 实测高度 %.0f + 间距 %.0f）"),
			Settings->StatsHUDPosition.Y + OffsetY, StatsHeight, Settings->TaskHUDGap);
	}
}

// ---------------------------------------------------------------------------
// 周目计数 HUD（挂在数值 HUD 正上方，显示「第 N 周目」）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowLoopCountHUD()
{
	if (LoopCountWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowLoopCount)
	{
		return;
	}

	// 没配就退回到 C++ 自带的默认样式
	TSubclassOf<UUserWidget> WidgetClass = Settings->LoopCountWidgetClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisLoopCountWidget::StaticClass();
	}

	LoopCountWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (LoopCountWidget == nullptr)
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[UI] 周目 HUD 创建失败：%s"), *GetNameSafe(WidgetClass));
		return;
	}

	LoopCountWidget->AddToViewport(Settings->LoopCountHUDZOrder);
	LoopCountWidget->SetAnchorsInViewport(FAnchors(0.f, 0.f));
	LoopCountWidget->SetAlignmentInViewport(FVector2D(0.f, 0.f));

	// 先按兜底高度摆上，等自身高度量出来后 RepositionLoopCountHUD 会贴到数值 HUD 上方
	bLoopCountHUDPlaced = false;
	LastLoopCountHeight = -1.f;
	RepositionLoopCountHUD();

	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 周目 HUD 已挂载：%s（贴在数值 HUD 上方，间距 %.0f）"),
		*GetNameSafe(WidgetClass), Settings->LoopCountHUDGap);
}

void AMetamorphosisPlayerController::HideLoopCountHUD()
{
	if (LoopCountWidget != nullptr)
	{
		LoopCountWidget->RemoveFromParent();
		LoopCountWidget = nullptr;
		bLoopCountHUDPlaced = false;
		LastLoopCountHeight = -1.f;
		UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 周目 HUD 已移除"));
	}
}

void AMetamorphosisPlayerController::RepositionLoopCountHUD()
{
	if (LoopCountWidget == nullptr)
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr)
	{
		return;
	}

	// 要摆在数值 HUD **上方**，所以需要的是"本控件自身的高度"（Slate 排版完才有；没量到就用兜底高度）
	// 注意：有些帧 CachedGeometry 还是 0（控件刚挂上、还没画过），所以再取一次 DesiredSize 兜底。
	const float GeometryHeight = static_cast<float>(LoopCountWidget->GetCachedGeometry().GetLocalSize().Y);
	const float DesiredHeight = static_cast<float>(LoopCountWidget->GetDesiredSize().Y);
	float OwnHeight = FMath::Max(GeometryHeight, DesiredHeight);
	const bool bMeasured = OwnHeight > 1.f;
	if (!bMeasured)
	{
		OwnHeight = Settings->LoopCountHUDFallbackHeight;
	}

	// 已经量到过且高度没变 -> 不用重摆
	if (bLoopCountHUDPlaced && FMath::IsNearlyEqual(OwnHeight, LastLoopCountHeight, 1.f))
	{
		return;
	}

	const float TargetY = FMath::Max(0.f, Settings->StatsHUDPosition.Y - Settings->LoopCountHUDGap - OwnHeight);
	LoopCountWidget->SetPositionInViewport(FVector2D(Settings->StatsHUDPosition.X, TargetY), false);

	LastLoopCountHeight = OwnHeight;
	bLoopCountHUDPlaced = bMeasured;

	if (bMeasured)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[UI] 周目 HUD 已对齐到数值 HUD 上方：Y=%.0f（自身高度 %.0f + 间距 %.0f，数值 HUD Y=%.0f）"),
			TargetY, OwnHeight, Settings->LoopCountHUDGap, Settings->StatsHUDPosition.Y);
	}
}

#if !UE_BUILD_SHIPPING
namespace MetamorphosisUIConsole
{
	static AMetamorphosisPlayerController* LocalController(UWorld* World)
	{
		return World != nullptr ? Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	/** 取 GameInstance 上的存档子系统（跨关卡存活） */
	static UMetamorphosisSaveSubsystem* SaveSubsystem(UWorld* World)
	{
		if (World == nullptr || World->GetGameInstance() == nullptr)
		{
			return nullptr;
		}
		return World->GetGameInstance()->GetSubsystem<UMetamorphosisSaveSubsystem>();
	}

	static void Show(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->ShowStatsHUD();
			Controller->ShowTaskHUD();
			Controller->ShowLoopCountHUD();
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[UI] 找不到 MetamorphosisPlayerController——当前 GameMode 的 PlayerControllerClass 可能不是它。"));
		}
	}

	static void Hide(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->HideStatsHUD();
			Controller->HideTaskHUD();
			Controller->HideLoopCountHUD();
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMetaUIShow(
	TEXT("Meta.UI.Show"),
	TEXT("（重新）挂载怀疑值/躁郁值 HUD"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisUIConsole::Show));

static FAutoConsoleCommandWithWorldAndArgs GMetaUIHide(
	TEXT("Meta.UI.Hide"),
	TEXT("移除怀疑值/躁郁值 HUD"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisUIConsole::Hide));

namespace MetamorphosisBagConsole
{
	static void Open(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World))
		{
			Controller->ShowBackpack();
		}
	}

	static void Close(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World))
		{
			Controller->HideBackpack();
		}
	}

	static void Info(const TArray<FString>& /*Args*/, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World);
		if (Controller == nullptr)
		{
			return;
		}

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 背包：开着=%d 输入已屏蔽=%d"),
			Controller->IsBackpackOpen() ? 1 : 0, Controller->IsInputBlocked() ? 1 : 0);

		const APawn* Pawn = Controller->GetPawn();
		const UMetamorphosisInventoryComponent* Inventory = Pawn != nullptr
			? Pawn->FindComponentByClass<UMetamorphosisInventoryComponent>() : nullptr;
		if (Inventory == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 背包：拿不到物品栏组件"));
			return;
		}

		FString Line;
		for (int32 Index = 0; Index < UMetamorphosisInventoryComponent::NumSlots; ++Index)
		{
			const FMetamorphosisItemSlot ItemSlot = Inventory->GetSlot(Index);
			Line += FString::Printf(TEXT(" [%s%d:%s"), Index == Inventory->GetSelectedIndex() ? TEXT("*") : TEXT(""),
				Index + 1, ItemSlot.bOccupied ? *ItemSlot.ItemId.ToString() : TEXT("空"));
			if (ItemSlot.bOccupied && ItemSlot.Count > 1)
			{
				Line += FString::Printf(TEXT(" x%d"), ItemSlot.Count);
			}
			Line += TEXT("]");
		}
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 背包内容（%d 格）：%s"), UMetamorphosisInventoryComponent::NumSlots, *Line);
	}

	/** Meta.Bag.Select <N>：选中第 N 格（0 起） */
	static void Select(const TArray<FString>& Args, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World);
		if (Controller == nullptr)
		{
			return;
		}
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		Controller->BagSelect(Index);
	}

	/** Meta.Bag.Use：对当前查看的格子执行 使用/查看 */
	static void Use(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World))
		{
			Controller->BagUse();
		}
	}

	/** Meta.Bag.Drop：对当前查看的格子执行 丢弃 */
	static void Drop(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World))
		{
			Controller->BagDrop();
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMetaBagOpen(
	TEXT("Meta.Bag.Open"),
	TEXT("打开背包界面"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisBagConsole::Open));

static FAutoConsoleCommandWithWorldAndArgs GMetaBagClose(
	TEXT("Meta.Bag.Close"),
	TEXT("关闭背包界面"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisBagConsole::Close));

static FAutoConsoleCommandWithWorldAndArgs GMetaBagInfo(
	TEXT("Meta.Bag.Info"),
	TEXT("打印背包状态与 8 格内容"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisBagConsole::Info));

static FAutoConsoleCommandWithWorldAndArgs GMetaBagSelect(
	TEXT("Meta.Bag.Select"),
	TEXT("选中背包第 N 格（0 起，如 Meta.Bag.Select 0），便于无头验证使用/丢弃"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisBagConsole::Select));

static FAutoConsoleCommandWithWorldAndArgs GMetaBagUse(
	TEXT("Meta.Bag.Use"),
	TEXT("对背包当前查看的格子执行「使用/查看」"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisBagConsole::Use));

static FAutoConsoleCommandWithWorldAndArgs GMetaBagDrop(
	TEXT("Meta.Bag.Drop"),
	TEXT("对背包当前查看的格子执行「丢弃」（世界里生成可拾回的道具）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisBagConsole::Drop));

	// ---------------------------------------------------------------------------
	// 点击反馈（无头环境点不了鼠标，所以给一条"在指定屏幕坐标播一次"的调试命令）
	// ---------------------------------------------------------------------------

	namespace MetamorphosisClickFXConsole
	{
		/** Meta.ClickFX.At <x> <y>：在视口像素坐标处播一次点击特效 */
		static void At(const TArray<FString>& Args, UWorld* World)
		{
			AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World);
			if (Controller == nullptr)
			{
				return;
			}

			const float X = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
			const float Y = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;
			const bool bPlayed = Controller->PlayClickFX(X, Y);

			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 点击反馈：在视口 (%.0f, %.0f) 播放 -> %s（存活 %d 个）"),
				X, Y, bPlayed ? TEXT("成功") : TEXT("被拒绝（限频/上限/界面遮挡或素材缺失）"),
				Controller->GetClickFXAliveCount());
		}

		/** Meta.ClickFX.Info：打印参数与当前存活数 */
		static void Info(const TArray<FString>& /*Args*/, UWorld* World)
		{
			AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World);
			if (Controller == nullptr)
			{
				return;
			}

			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 点击反馈：存活=%d 帧=%d 尺寸=%.0f 时长=%.2fs 上限=%d 输入已屏蔽=%d"),
				Controller->GetClickFXAliveCount(), Controller->GetClickFXFrameCount(),
				Controller->GetClickFXSize(), Controller->GetClickFXDurationSeconds(),
				Controller->GetClickFXMaxAlive(), Controller->IsInputBlocked() ? 1 : 0);
		}

		/** Meta.ClickFX.Clear：清掉正在播的特效（控件保留） */
		static void Clear(const TArray<FString>& /*Args*/, UWorld* World)
		{
			if (AMetamorphosisPlayerController* Controller = MetamorphosisUIConsole::LocalController(World))
			{
				Controller->ClearClickFX();
				UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 点击反馈：已清空正在播的特效"));
			}
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaClickFXAt(
		TEXT("Meta.ClickFX.At"),
		TEXT("在视口像素坐标处播一次点击特效：Meta.ClickFX.At 640 360"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisClickFXConsole::At));

	static FAutoConsoleCommandWithWorldAndArgs GMetaClickFXInfo(
		TEXT("Meta.ClickFX.Info"),
		TEXT("打印点击反馈状态（存活数量 / 帧数 / 参数 / 输入是否被屏蔽）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisClickFXConsole::Info));

	static FAutoConsoleCommandWithWorldAndArgs GMetaClickFXClear(
		TEXT("Meta.ClickFX.Clear"),
		TEXT("清空正在播的点击特效（控件保留）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisClickFXConsole::Clear));
#endif // !UE_BUILD_SHIPPING

// ---------------------------------------------------------------------------
// 点击反馈（点击 / 触摸的视觉反馈）
// ---------------------------------------------------------------------------

void AMetamorphosisPlayerController::ShowClickFX()
{
	if (ClickFXWidget != nullptr || !IsLocalPlayerController())
	{
		return;
	}

	const UMetamorphosisUISettings* Settings = GetDefault<UMetamorphosisUISettings>();
	if (Settings == nullptr || !Settings->bShowClickFX || !bShowClickFX)
	{
		return;
	}

	TSubclassOf<UUserWidget> WidgetClass = Settings->ClickFXClass;
	if (WidgetClass == nullptr)
	{
		WidgetClass = UMetamorphosisClickFXWidget::StaticClass();
	}

	UMetamorphosisClickFXWidget* Created = CreateWidget<UMetamorphosisClickFXWidget>(this, WidgetClass);
	if (Created == nullptr)
	{
		return;
	}

	Created->ApplyConfig(ClickFXSize, ClickFXDurationSeconds, ClickFXMaxAlive,
		ClickFXMinIntervalSeconds, 40.f, ClickFXFrameFolder, ClickFXFrameCount);
	Created->AddToViewport(Settings->ClickFXZOrder);
	ClickFXWidget = Created;

	UE_LOG(LogMetamorphosis, Log,
		TEXT("[UI] 点击反馈已挂载：%s（ZOrder=%d，尺寸 %.0f，时长 %.2fs，上限 %d 个，帧 %d）"),
		*GetNameSafe(WidgetClass), Settings->ClickFXZOrder, ClickFXSize, ClickFXDurationSeconds,
		ClickFXMaxAlive, ClickFXFrameCount);
}

void AMetamorphosisPlayerController::HideClickFX()
{
	if (ClickFXWidget == nullptr)
	{
		return;
	}

	ClickFXWidget->StopAll();
	ClickFXWidget->RemoveFromParent();
	ClickFXWidget = nullptr;
	UE_LOG(LogMetamorphosis, Log, TEXT("[UI] 点击反馈已移除"));
}

bool AMetamorphosisPlayerController::PlayClickFX(float ViewportX, float ViewportY)
{
	if (ClickFXWidget == nullptr || !bShowClickFX)
	{
		return false;
	}

	// 过场 / 背包 / 暂停 / 结算期间不播（避免干扰画面）
	if (bStoryPlaying || bBackpackOpen || bInSettlement || bGameplayPaused || UGameplayStatics::IsGamePaused(this))
	{
		return false;
	}

	// 视口像素 -> UMG 本地坐标。
	// 本工程以 1.5 倍 DPI 渲染：GetViewportSize 给的是物理像素（1280x720），
	// 而 UMG 布局用的是 DPI 缩放后的本地单位（1922x1081）——必须按两者比例换算，
	// 不能直接把像素当 UMG 坐标用（这是本工程踩过的坑）。
	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	GetViewportSize(ViewportSizeX, ViewportSizeY);

	const FVector2D LocalSize = ClickFXWidget->GetCachedGeometry().GetLocalSize();
	if (ViewportSizeX <= 0 || ViewportSizeY <= 0 || LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
	{
		return false;
	}

	const FVector2D LocalPosition(
		ViewportX * (LocalSize.X / static_cast<float>(ViewportSizeX)),
		ViewportY * (LocalSize.Y / static_cast<float>(ViewportSizeY)));

	return ClickFXWidget->PlayAt(LocalPosition);
}

int32 AMetamorphosisPlayerController::GetClickFXAliveCount() const
{
	return ClickFXWidget != nullptr ? ClickFXWidget->GetAliveCount() : 0;
}

void AMetamorphosisPlayerController::ClearClickFX()
{
	if (ClickFXWidget != nullptr)
	{
		ClickFXWidget->StopAll();
	}
}

int32 AMetamorphosisPlayerController::GetClickFXFrameCount() const
{
	return ClickFXWidget != nullptr ? ClickFXWidget->GetLoadedFrameCount() : ClickFXFrameCount;
}

void AMetamorphosisPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Only set up input on local player controllers
	if (IsLocalPlayerController())
	{
		// 只挂移动映射上下文：PC=WASD，移动端=虚拟摇杆，手柄=左摇杆
		// （点击移动用的 IMC_Default 已不再挂载，见 Input/MetamorphosisMoveInput.h）
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(UMetamorphosisInputMappingContext_Move::Get(), 0);
		}

		// 绑定的事件必须用映射上下文里同一个动作实例
		if (MoveAction == nullptr)
		{
			MoveAction = UMetamorphosisInputMappingContext_Move::GetMoveAction();
		}
		if (LookAction == nullptr)
		{
			LookAction = UMetamorphosisInputMappingContext_Move::GetLookAction();
		}
		if (AscendAction == nullptr)
		{
			AscendAction = UMetamorphosisInputMappingContext_Move::GetAscendAction();
		}
		if (DescendAction == nullptr)
		{
			DescendAction = UMetamorphosisInputMappingContext_Move::GetDescendAction();
		}
		if (CycleSlotAction == nullptr)
		{
			CycleSlotAction = UMetamorphosisInputMappingContext_Move::GetCycleSlotAction();
		}
		if (InteractAction == nullptr)
		{
			InteractAction = UMetamorphosisInputMappingContext_Move::GetInteractAction();
		}
		if (BackpackAction == nullptr)
		{
			BackpackAction = UMetamorphosisInputMappingContext_Move::GetBackpackAction();
		}

		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
		{
			// 移动（Triggered 每帧触发；Completed/Canceled 补一帧零值，保证松开后立即停止）
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMetamorphosisPlayerController::OnMoveTriggered);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AMetamorphosisPlayerController::OnMoveTriggered);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AMetamorphosisPlayerController::OnMoveTriggered);

			// 视角水平旋转（每帧给速率）
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMetamorphosisPlayerController::OnLookTriggered);

			// 上升 / 起飞（空格）：Started 里还做双击判定（双击 = 立刻落地）
			EnhancedInputComponent->BindAction(AscendAction, ETriggerEvent::Started, this, &AMetamorphosisPlayerController::OnAscendStarted);
			EnhancedInputComponent->BindAction(AscendAction, ETriggerEvent::Completed, this, &AMetamorphosisPlayerController::OnAscendCompleted);
			EnhancedInputComponent->BindAction(AscendAction, ETriggerEvent::Canceled, this, &AMetamorphosisPlayerController::OnAscendCompleted);

			// 下降（X 键 / 手柄 B；移动端是右下角「下」按钮）
			EnhancedInputComponent->BindAction(DescendAction, ETriggerEvent::Started, this, &AMetamorphosisPlayerController::OnDescendStarted);
			EnhancedInputComponent->BindAction(DescendAction, ETriggerEvent::Completed, this, &AMetamorphosisPlayerController::OnDescendCompleted);
			EnhancedInputComponent->BindAction(DescendAction, ETriggerEvent::Canceled, this, &AMetamorphosisPlayerController::OnDescendCompleted);

			// 触摸（左摇杆 / 右转视角）不在这里绑定：统一在 PlayerTick 里按屏幕分区处理
			// （触摸位置用 GetInputTouchState 轮询，比逐个绑 Touch 动作更好处理多指与分区）

			// 快捷栏：滚轮 / 手柄左右方向键切换
			EnhancedInputComponent->BindAction(CycleSlotAction, ETriggerEvent::Triggered, this, &AMetamorphosisPlayerController::OnCycleHotbarSlot);

			// 交互 / 拾取（PC = E；移动端点交互提示上的圆徽标，不走这里）
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AMetamorphosisPlayerController::OnInteractStarted);

			// 背包开 / 关（PC = B）
			EnhancedInputComponent->BindAction(BackpackAction, ETriggerEvent::Started, this, &AMetamorphosisPlayerController::OnBackpackStarted);

			// 快捷栏：数字键 1..9、0 直选（10 个动作逐个绑，lambda 捕获格子号）
			for (int32 SlotIndex = 0; SlotIndex < UMetamorphosisInputMappingContext_Move::NumSlotKeys; ++SlotIndex)
			{
				if (UInputAction* SlotAction = UMetamorphosisInputMappingContext_Move::GetSlotAction(SlotIndex))
				{
					EnhancedInputComponent->BindActionValueLambda(SlotAction, ETriggerEvent::Started,
						[this, SlotIndex](const FInputActionValue& /*Value*/)
						{
							SelectHotbarSlot(SlotIndex);
						});
				}
			}
		}
		else
		{
			UE_LOG(LogMetamorphosis, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
		}
	}
}

void AMetamorphosisPlayerController::OnMoveTriggered(const FInputActionValue& Value)
{
	// 手指正按着虚拟摇杆时以摇杆为准（摇杆是每帧推的，不走这个事件）
	if (bJoystickActive)
	{
		return;
	}

	ApplyMoveAxis(Value.Get<FVector2D>());
}

void AMetamorphosisPlayerController::ApplyMoveAxis(const FVector2D& AxisValue)
{
	// 暂停 / 周目结算中不接受移动（摇杆模拟 Meta.Joystick.Sim 走的也是这条路，所以一并被挡）
	if (IsInputBlocked())
	{
		return;
	}

	// 移动轴就是纯移动：WASD / 摇杆前后左右，S 和摇杆下拉都是"后退"。
	// 升降是独立的按键（空格上升、X 下降），不从这里换算。
	if (AxisValue.IsNearlyZero())
	{
		return;
	}

	// 摇杆/WASD 说了算：先取消任何可能存在的自动寻路移动
	StopMovement();

	// 以相机朝向为基准换算世界方向：转过视角之后，WASD 依然是"往屏幕里走"
	float BaseYaw = GetControlRotation().Yaw;
	if (const AMetamorphosisBeetleCharacter* Beetle = GetBeetle())
	{
		BaseYaw = Beetle->GetCameraWorldYaw();
	}

	const FRotator YawRotation(0.f, BaseYaw, 0.f);
	const FRotationMatrix RotationMatrix(YawRotation);
	const FVector WorldDirection =
		RotationMatrix.GetUnitAxis(EAxis::X) * AxisValue.Y +
		RotationMatrix.GetUnitAxis(EAxis::Y) * AxisValue.X;

	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn != nullptr)
	{
		ControlledPawn->AddMovementInput(WorldDirection, 1.0f, false);
	}
}

// ---------------------------------------------------------------------------
// 视角水平旋转 / 升空
// ---------------------------------------------------------------------------

AMetamorphosisBeetleCharacter* AMetamorphosisPlayerController::GetBeetle() const
{
	return Cast<AMetamorphosisBeetleCharacter>(GetPawn());
}

void AMetamorphosisPlayerController::OnLookTriggered(const FInputActionValue& Value)
{
	// 暂停 / 结算中不转视角
	if (IsInputBlocked())
	{
		return;
	}

	if (AMetamorphosisBeetleCharacter* Beetle = GetBeetle())
	{
		Beetle->AddCameraYawInput(Value.Get<float>(), GetWorld()->GetDeltaSeconds());
	}
}

void AMetamorphosisPlayerController::OnAscendStarted()
{
	// 暂停 / 结算中不响应升降
	if (IsInputBlocked())
	{
		return;
	}
	bAscendHeld = true;
	HandleAscendPress();
	PushVerticalInput();
}

void AMetamorphosisPlayerController::OnAscendCompleted()
{
	if (IsInputBlocked())
	{
		return;
	}
	bAscendHeld = false;
	PushVerticalInput();
}

void AMetamorphosisPlayerController::HandleAscendPress()
{
	// 双击 = 立刻落地（键盘空格与移动端上升按钮共用这套判定）
	const double Now = GetWorld() != nullptr ? GetWorld()->GetRealTimeSeconds() : 0.0;
	const bool bDoubleTap = (LastAscendPressTime >= 0.0) && ((Now - LastAscendPressTime) <= DoubleTapLandTime);
	LastAscendPressTime = Now;

	if (bDoubleTap)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = GetBeetle())
		{
			Beetle->LandNow();
		}
		LastAscendPressTime = -1.0;   // 避免三连击再触发一次
	}
}

void AMetamorphosisPlayerController::SetAscendButtonHeld(bool bHeld)
{
	if (IsInputBlocked())
	{
		return;
	}
	if (bAscendButtonHeld == bHeld)
	{
		return;
	}

	bAscendButtonHeld = bHeld;
	if (bHeld)
	{
		HandleAscendPress();
	}
	PushVerticalInput();
}

void AMetamorphosisPlayerController::SetDescendButtonHeld(bool bHeld)
{
	if (IsInputBlocked())
	{
		return;
	}
	if (bDescendButtonHeld == bHeld)
	{
		return;
	}

	bDescendButtonHeld = bHeld;
	PushVerticalInput();
}

void AMetamorphosisPlayerController::OnDescendStarted()
{
	if (IsInputBlocked())
	{
		return;
	}
	bDescendHeld = true;
	PushVerticalInput();
}

void AMetamorphosisPlayerController::OnDescendCompleted()
{
	if (IsInputBlocked())
	{
		return;
	}
	bDescendHeld = false;
	PushVerticalInput();
}


void AMetamorphosisPlayerController::ApplyTouchLookDelta(float DeltaX)
{
	if (IsInputBlocked())
	{
		return;
	}
	if (FMath::IsNearlyZero(DeltaX))
	{
		return;
	}

	// 手指往右搓 = 视角往右转（UE 里"右转"是 Yaw 减小），符合搓屏幕转镜头的直觉
	const float Direction = bInvertTouchLook ? 1.f : -1.f;
	if (AMetamorphosisBeetleCharacter* Beetle = GetBeetle())
	{
		Beetle->AddCameraYawDelta(Direction * DeltaX * TouchLookSensitivity);
	}
}

void AMetamorphosisPlayerController::PushVerticalInput()
{
	if (AMetamorphosisBeetleCharacter* Beetle = GetBeetle())
	{
		// 上升：键盘空格 / 手柄 A / 移动端「上」按钮
		const float Ascend = (bAscendHeld || bAscendButtonHeld) ? 1.f : 0.f;
		// 下降：X 键 / 手柄 B / 移动端「下」按钮
		const float Descend = (bDescendHeld || bDescendButtonHeld) ? 1.f : 0.f;

		Beetle->SetVerticalInput(Ascend - Descend);

		UE_LOG(LogMetamorphosis, Log, TEXT("[Input] 升降输入 -> %+.2f（上升键 %d / 下降键 %d）"),
			Ascend - Descend, bAscendHeld || bAscendButtonHeld, bDescendHeld || bDescendButtonHeld);
	}
}

void AMetamorphosisPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 触摸：左半屏 = 移动摇杆，右半屏 = 转视角（支持两指同时）
	UpdateTouchInput();

	// 点击反馈（鼠标）：只在"左键按下那一帧"播一次，按住拖动不刷屏。
	// 触摸那条链路在 UpdateTouchInput 里处理（真机与 Meta.Touch.Sim 都能触发）。
	if (bShowClickFX)
	{
		const bool bMouseDownNow = IsInputKeyDown(EKeys::LeftMouseButton);
		if (bMouseDownNow && !bWasMouseDown)
		{
			float MouseX = 0.f;
			float MouseY = 0.f;
			if (GetMousePosition(MouseX, MouseY))
			{
				PlayClickFX(MouseX, MouseY);
			}
		}
		bWasMouseDown = bMouseDownNow;
	}

	// 任务 HUD 贴着数值 HUD 下方：数值 HUD 的实测高度要等 Slate 排版完才有，
	// 所以这里每帧试一次（内容没变时会立刻早退，开销可忽略）
	RepositionTaskHUD();
	// 周目 HUD 贴在数值 HUD 上方，同理需要等它自身高度量出来
	RepositionLoopCountHUD();

	// 摇杆在操控时每帧推移动（摇杆不走 Enhanced Input 事件）
	if (bJoystickActive)
	{
		ApplyMoveAxis(VirtualMoveAxis);
	}

	// 暂停（弹窗打开）时把摇杆收起来
	// 摇杆的显示状态推给控件（控件自己不 tick，避免依赖 UMG 的 tick 机制）
	// 暂停（弹窗打开）时收起，别在弹窗底下露出来
	if (JoystickWidget != nullptr && bGameplayPaused)
	{
		JoystickWidget->UpdateVisual(false, false, FVector2D::ZeroVector, FVector2D::ZeroVector, 1.f);
	}
	else if (JoystickWidget != nullptr)
	{
		const UMetamorphosisSettingsSubsystem* GameSettings = UMetamorphosisSettingsSubsystem::Get(this);
		const FMetamorphosisGameSettings& Current = GameSettings != nullptr
			? GameSettings->GetSettings() : FMetamorphosisGameSettings();

		const float Scale = Current.JoystickScale;
		const float Radius = FMath::Max(10.f, JoystickRadiusPx * Scale);

		FVector2D KnobOffset = JoystickCurrentPx - JoystickCenterPx;
		if (KnobOffset.SizeSquared() > Radius * Radius)
		{
			KnobOffset = KnobOffset.GetSafeNormal() * Radius;
		}

		// 没在操控时也要把中心放在"固定摇杆位"上：常显底盘才知道该按哪儿
		// （之前空闲传 (0,0)，圆被画到屏幕左上角外面，看起来就是"摇杆不见了"）
		const FVector2D Center = bJoystickActive ? JoystickCenterPx : GetFixedJoystickCenter();

		JoystickWidget->UpdateVisual(bJoystickActive, !Current.bFixedJoystick, Center, KnobOffset, Scale);
	}
}

#if !UE_BUILD_SHIPPING
namespace MetamorphosisPlayerConsole
{
	static AMetamorphosisPlayerController* LocalController(UWorld* World)
	{
		return World != nullptr ? Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static AMetamorphosisBeetleCharacter* BeetleOf(UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = LocalController(World);
		return Controller != nullptr ? Cast<AMetamorphosisBeetleCharacter>(Controller->GetPawn()) : nullptr;
	}

	/** 取 GameInstance 上的存档子系统（跨关卡存活） */
	static UMetamorphosisSaveSubsystem* SaveSubsystem(UWorld* World)
	{
		if (World == nullptr || World->GetGameInstance() == nullptr)
		{
			return nullptr;
		}
		return World->GetGameInstance()->GetSubsystem<UMetamorphosisSaveSubsystem>();
	}

	static void Air(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			if (Beetle->IsAirborne())
			{
				Beetle->LandNow();
			}
			else
			{
				Beetle->TakeOff();
			}
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 找不到虫形主角，Meta.Air 无效"));
		}
	}

	static void CamYaw(const TArray<FString>& Args, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			Beetle->SetCameraYawOffset(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 视角水平偏移 -> %.1f 度"), Beetle->GetCameraYawOffset());
		}
	}

	/** 真实时间延时器（用 FTSTicker：**游戏暂停时也照走**）。
	 *  注意不能用世界/GameInstance 的 FTimerManager —— 那是游戏时间，暂停就不走了，
	 *  而"暂停之后"的验证步骤正需要它继续跑（否则命令永远不会执行）。 */
	class FRealTimeDelay : public FTSTickerObjectBase
	{
	public:
		FRealTimeDelay(float InDelaySeconds, TFunction<void()> InCallback)
			: FTSTickerObjectBase(0.f, FTSTicker::GetCoreTicker())
			, RemainingSeconds(FMath::Max(0.f, InDelaySeconds))
			, Callback(MoveTemp(InCallback))
		{
		}

		virtual bool Tick(float DeltaTime) override
		{
			RemainingSeconds -= DeltaTime;
			if (RemainingSeconds <= 0.f)
			{
				if (Callback)
				{
					Callback();
				}
				return false;   // 自己摘掉
			}
			return true;
		}

	private:
		float RemainingSeconds = 0.f;
		TFunction<void()> Callback;
	};

	/** N 秒（真实时间）后执行一条控制台命令；跨关卡、暂停期间都有效。
	 *  例：Meta.Delay 12 Meta.Inv.Fill —— 等进关、角色生成之后再填占位道具 */
	/** 立即截一张图（存 Saved/Screenshots/<前缀><时间戳>.png）。无头验证用：先 Meta.Delay 等到位再截图。 */
	static void TakeScreenshot(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;
		const FString Prefix = Args.Num() > 0 ? Args[0] : TEXT("Meta");
		const FString FileName = FString::Printf(TEXT("%s_%s.png"), *Prefix, *FDateTime::Now().ToString(TEXT("HHmmss")));
		FScreenshotRequest::RequestScreenshot(FileName, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 截图请求：%s（Saved/Screenshots/）"), *FileName);
	}

	static FAutoConsoleCommandWithWorldAndArgs GMetaScreenshot(
		TEXT("Meta.Screenshot"),
		TEXT("Meta.Screenshot [前缀]：立即截图到 Saved/Screenshots/"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TakeScreenshot));

	static void DelayCommand(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;

		if (Args.Num() < 2)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Delay <秒> <控制台命令>"));
			return;
		}

		const float Delay = FMath::Max(0.f, FCString::Atof(*Args[0]));
		FString Command = Args[1];
		for (int32 Index = 2; Index < Args.Num(); ++Index)
		{
			Command += TEXT(" ") + Args[Index];
		}

		new FRealTimeDelay(Delay, [Command]()
		{
			if (GEngine == nullptr)
			{
				return;
			}

			// 执行时再找当前游戏世界（中间可能已经跨过关卡）
			UWorld* GameWorld = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
				{
					GameWorld = Context.World();
					break;
				}
			}

			GEngine->Exec(GameWorld, *Command);
		});

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] %.1f 秒后执行：%s"), Delay, *Command);
	}

	/** N 秒（真实时间）后自动截一张图（含 UI）；暂停期间也有效 */
	static void ShotDelayed(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;

		const float Delay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.f;
		new FRealTimeDelay(FMath::Max(0.1f, Delay), []()
		{
			FScreenshotRequest::RequestScreenshot(true);
		});

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] %.1f 秒后自动截图（含 UI）"), Delay);
	}

	/** 开始界面：查看预加载状态 / 强制进游戏（不用鼠标点按钮也能测） */
	static void MenuStatus(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(World))
		{
			int32 Loaded = 0;
			int32 Total = 0;
			Preload->GetCounts(Loaded, Total);
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug] 预加载：%.0f%%（%d/%d）加载完成=%d 就绪=%d"),
				Preload->GetProgress() * 100.f, Loaded, Total,
				Preload->IsLoadComplete() ? 1 : 0, Preload->IsReady() ? 1 : 0);
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 拿不到预加载子系统"));
		}
	}

	static void MenuStart(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisPreloadSubsystem* Preload = UMetamorphosisPreloadSubsystem::Get(World))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 强制进入游戏关卡（等价于点「开始游戏」）"));
			Preload->EnterGame();
		}
	}

	/** 取当前存活的开始界面控件（拿不到就 nullptr） */
	static UMetamorphosisMainMenuWidget* FindMainMenu(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController()))
		{
			return Cast<UMetamorphosisMainMenuWidget>(PC->GetMainMenuWidget());
		}
		return nullptr;
	}

	/** Meta.Menu.Press <Start|Settings>：模拟点按钮，验证"回调链路通不通"（无头点不了鼠标） */
	static void MenuPress(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisMainMenuWidget* Menu = FindMainMenu(World);
		if (Menu == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 当前没有开始界面控件（已经进关卡了？）"));
			return;
		}
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Menu.Press <Start|Settings>"));
			return;
		}
		Menu->SimulatePress(Args[0]);
	}

	/** Meta.Menu.Tree：打印开始界面控件树 + 每个控件的 Visibility */
	static void MenuTree(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisMainMenuWidget* Menu = FindMainMenu(World))
		{
			Menu->LogControlTree();
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 当前没有开始界面控件（已经进关卡了？）"));
		}
	}

	/** 找到当前的通关界面控件（从本机 PC 拿） */
	static UMetamorphosisEndingWidget* FindEnding(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		if (AMetamorphosisPlayerController* PC = Cast<AMetamorphosisPlayerController>(World->GetFirstPlayerController()))
		{
			return PC->GetWinEndingWidget();
		}
		return nullptr;
	}

	/** Meta.Ending.Press <Restart|Title>：模拟点通关界面按钮，验证"回调链路通不通"（无头点不了鼠标） */
	static void EndingPress(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisEndingWidget* Ending = FindEnding(World);
		if (Ending == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 当前没有通关界面控件（还没通关？）"));
			return;
		}
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Ending.Press <Restart|Title>"));
			return;
		}
		Ending->SimulatePress(Args[0]);
	}

	/** Meta.Ending.Tree：打印通关界面控件树 + 每个控件的 Visibility */
	static void EndingTree(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisEndingWidget* Ending = FindEnding(World))
		{
			Ending->LogControlTree();
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 当前没有通关界面控件（还没通关？）"));
		}
	}

	/** 设置：打印 / 修改（设置界面之外也能验证） */
	static void SettingsPrint(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (const UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(World))
		{
			const FMetamorphosisGameSettings& Current = Settings->GetSettings();
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug] 设置：音量 %.2f 摇杆%s 摇杆大小 %.2f 物品栏大小 %.2f 上下按钮大小 %.2f"),
				Current.MasterVolume, Current.bFixedJoystick ? TEXT("固定") : TEXT("浮动"),
				Current.JoystickScale, Current.HotbarScale, Current.MobileButtonScale);
		}
	}

	static void SettingsSet(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(World);
		if (Settings == nullptr || Args.Num() < 2)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Debug] 用法：Meta.Settings.Set <volume|fixed|joystick|hotbar|button> <值>"));
			return;
		}

		const FString Key = Args[0].ToLower();
		const float Value = FCString::Atof(*Args[1]);

		if (Key == TEXT("volume"))        { Settings->SetMasterVolume(Value); }
		else if (Key == TEXT("fixed"))    { Settings->SetFixedJoystick(Value > 0.5f); }
		else if (Key == TEXT("joystick")) { Settings->SetJoystickScale(Value); }
		else if (Key == TEXT("hotbar"))   { Settings->SetHotbarScale(Value); }
		else if (Key == TEXT("button"))   { Settings->SetMobileButtonScale(Value); }
		else { UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 未知设置项：%s"), *Key); return; }

		SettingsPrint(Args, World);
	}

	static void SettingsReset(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisSettingsSubsystem* Settings = UMetamorphosisSettingsSubsystem::Get(World))
		{
			Settings->ResetToDefaults();
			SettingsPrint(Args, World);
		}
	}

	/** 打开设置界面（拍图/自测用） */
	static void OpenSettings(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->ShowSettings();
		}
	}

	/** Meta.Settings.Dump：打印设置界面里每个交互控件的几何尺寸（判断"看得见但点不到"） */
	static void SettingsDump(const TArray<FString>& /*Args*/, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = LocalController(World);
		if (Controller == nullptr)
		{
			return;
		}
		if (UMetamorphosisSettingsWidget* SettingsMenu = Controller->GetSettingsWidget())
		{
			SettingsMenu->DumpInteractiveGeometry();
		}
		else
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Debug] 设置界面还没创建（先 Meta.UI.OpenSettings 打开它）"));
		}
	}

	/** Meta.Settings.Grab <0-3> <值>：模拟拖动第 N 条滑条（走与真实拖动相同的回调链路） */
	static void SettingsGrab(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogMetamorphosis, Warning,
				TEXT("[Debug] 用法：Meta.Settings.Grab <0音量|1摇杆大小|2物品栏大小|3上下按钮大小> <值>"));
			return;
		}

		AMetamorphosisPlayerController* Controller = LocalController(World);
		UMetamorphosisSettingsWidget* SettingsMenu = Controller != nullptr ? Controller->GetSettingsWidget() : nullptr;
		if (SettingsMenu == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 设置界面还没创建（先 Meta.UI.OpenSettings）"));
			return;
		}

		SettingsMenu->SimulateSliderDrag(FCString::Atoi(*Args[0]), FCString::Atof(*Args[1]));
		SettingsPrint(Args, World);
	}

	/** Meta.Settings.Toggle：模拟点一下「摇杆固定」开关（走真实回调链路） */
	static void SettingsToggle(const TArray<FString>& Args, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = LocalController(World);
		UMetamorphosisSettingsWidget* SettingsMenu = Controller != nullptr ? Controller->GetSettingsWidget() : nullptr;
		if (SettingsMenu == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 设置界面还没创建（先 Meta.UI.OpenSettings）"));
			return;
		}

		SettingsMenu->SimulateToggleSwitch();
		SettingsPrint(Args, World);
	}

	/** 走"设置里的返回"真实路径（用于无头验证"关闭设置后是否该恢复暂停"）：Meta.UI.SettingsBack */
	static void SettingsBack(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->HandleSettingsBackRequested();
		}
	}

	/** Meta.UI.ReturnToTitle：走设置里「返回主界面」的真实路径（无头验证用） */
	static void ReturnToTitle(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->ReturnToTitleFromSettings();
		}
	}

	/** Meta.Settlement.Press <Restart|Title>：模拟点结算界面的两个按钮（无头验证用） */
	static void SettlementPress(const TArray<FString>& Args, UWorld* World)
	{
		UMetamorphosisLoopSubsystem* Loop =
			World != nullptr && World->GetGameInstance() != nullptr
				? World->GetGameInstance()->GetSubsystem<UMetamorphosisLoopSubsystem>()
				: nullptr;
		if (Loop == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 结算命令：找不到周目子系统"));
			return;
		}

		const FString Which = Args.Num() > 0 ? Args[0] : TEXT("Restart");
		if (Which.Equals(TEXT("Title"), ESearchCase::IgnoreCase))
		{
			const bool bOk = Loop->ChooseSettlementReturnToTitle();
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟点击结算「返回主菜单」 -> %s"), bOk ? TEXT("成功") : TEXT("被拒绝（不在等待选择）"));
		}
		else
		{
			const bool bOk = Loop->ChooseSettlementRestart();
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟点击结算「重新开始」 -> %s"), bOk ? TEXT("成功") : TEXT("被拒绝（不在等待选择）"));
		}
	}

	/** Meta.Settlement.Info：打印结算阶段 + 是否在等待选择 */
	static void SettlementInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UMetamorphosisLoopSubsystem* Loop =
			World != nullptr && World->GetGameInstance() != nullptr
				? World->GetGameInstance()->GetSubsystem<UMetamorphosisLoopSubsystem>()
				: nullptr;
		if (Loop == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 结算命令：找不到周目子系统"));
			return;
		}

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 结算：阶段=%s｜等待选择=%d｜已结算=%d 次"),
			*Loop->GetPhaseText(), Loop->IsAwaitingSettlementChoice() ? 1 : 0, Loop->GetLoopCount());
	}

	// ---- 存档调试命令 ----

	static void SaveInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisSaveSubsystem* Save = SaveSubsystem(World))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 存档：存在=%d 路径=%s"), Save->HasSave() ? 1 : 0, *Save->GetSavePath());
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 摘要：%s"), *Save->Summary());
		}
	}

	static void SaveNow(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisSaveSubsystem* Save = SaveSubsystem(World))
		{
			Save->SaveNow(Args.Num() > 0 ? Args[0] : TEXT("调试命令"));
		}
	}

	/** 强制存（剧情/门正在播也存，但结算中/主界面仍跳过）—— 测试背包/位置用 */
	static void SaveNowForce(const TArray<FString>& Args, UWorld* World)
	{
		if (UMetamorphosisSaveSubsystem* Save = SaveSubsystem(World))
		{
			Save->SaveNow(Args.Num() > 0 ? Args[0] : TEXT("调试命令(强制)"), /*bForce=*/true);
		}
	}

	static void SaveLoad(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisSaveSubsystem* Save = SaveSubsystem(World))
		{
			Save->LoadNow();
		}
	}

	static void SaveDelete(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisSaveSubsystem* Save = SaveSubsystem(World))
		{
			Save->DeleteSave();
		}
	}

	static void SaveSummary(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UMetamorphosisSaveSubsystem* Save = SaveSubsystem(World))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 存档摘要：%s"), *Save->Summary());
		}
	}

	/** 模拟摇杆：Meta.Joystick.Sim <dx> <dy>（0 0 = 松开）。验证"摇杆 -> 显示 + 移动"整条链路 */
	static void JoystickSim(const TArray<FString>& Args, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = LocalController(World);
		if (Controller == nullptr)
		{
			return;
		}

		const float DeltaX = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		const float DeltaY = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;

		Controller->DebugSimulateJoystick(FVector2D(DeltaX, DeltaY));
	}

	/** 弹窗：打开 / 关闭 / 查看状态 / 模拟点选项 */
	static void PauseOpen(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->OpenPauseMenu();
		}
	}

	static void PauseClose(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->ClosePauseMenu();
		}
	}

	static void PauseStatus(const TArray<FString>& /*Args*/, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = LocalController(World);
		if (Controller == nullptr)
		{
			return;
		}

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 弹窗：打开=%d 世界暂停=%d 角色停止tick=%d"),
			Controller->IsGameplayPaused() ? 1 : 0,
			Controller->GetWorld() != nullptr && Controller->GetWorld()->IsPaused() ? 1 : 0,
			Controller->GetPawn() != nullptr && Controller->GetPawn()->IsActorTickEnabled() ? 0 : 1);
	}

	static void PauseOption(const TArray<FString>& Args, UWorld* World)
	{
		AMetamorphosisPlayerController* Controller = LocalController(World);
		if (Controller != nullptr && Args.Num() > 0)
		{
			Controller->DebugChoosePauseOption(FName(*Args[0]));
		}
	}

	/** 打印甲虫当前动画状态（阶段/序列/第几帧） */
	static void AnimInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] %s"), *Beetle->GetAnimDebugString());
		}
	}

	/** 模拟触摸：Meta.Touch.Sim <手指 0-3> <x> <y> <down|move|up>
	 *  直接喂给 GameViewportClient::InputTouch（和真机触摸同一条链路，只是绕过了 Slate 的 UI 命中），
	 *  用来在没有真机的情况下验证"触摸 -> 分区 -> 摇杆/视角"的逻辑。
	 *  另外 Meta.Touch.Claim <手指> / Meta.Touch.Unclaim <手指> 可以模拟"UI 认领了这根手指"。 */
	static void TouchSim(const TArray<FString>& Args, UWorld* World)
	{
		UGameViewportClient* ViewportClient = World != nullptr ? World->GetGameViewport() : nullptr;
		if (ViewportClient == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 没有 GameViewport，无法模拟触摸"));
			return;
		}

		if (Args.Num() < 4)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Touch.Sim <手指 0-3> <x> <y> <down|move|up>"));
			return;
		}

		const int32 Finger = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 3);
		const float X = FCString::Atof(*Args[1]);
		const float Y = FCString::Atof(*Args[2]);
		const FString Phase = Args[3].ToLower();

		ETouchType::Type Type = ETouchType::Began;
		if (Phase == TEXT("move"))      { Type = ETouchType::Moved; }
		else if (Phase == TEXT("up"))   { Type = ETouchType::Ended; }

		// 必须给**有效的输入设备 ID**，否则 UGameViewportClient::InputTouch 找不到 LocalPlayer 会直接把事件丢掉。
		const FInputDeviceId DeviceId = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();

		// Handle 用"手指号"：和 Slate/平台的约定一致（第一根手指 = 0）
		ViewportClient->InputTouch(ViewportClient->Viewport, DeviceId, static_cast<uint32>(Finger),
			Type, FVector2D(X, Y), 1.f, 0, FPlatformTime::Cycles64());

		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟触摸：手指 %d (%.0f, %.0f) %s"), Finger, X, Y, *Phase);
	}

	static void TouchClaim(const TArray<FString>& Args, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->NotifyUITouchClaimed(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		}
	}

	static void TouchUnclaim(const TArray<FString>& Args, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->NotifyUITouchReleased(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		}
	}

	/** 移动为什么不动：一次打印所有"会让角色卡住"的状态 */
	static void MoveInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}

		AMetamorphosisPlayerController* Controller = LocalController(World);
		APawn* ControlledPawn = Controller != nullptr ? Controller->GetPawn() : nullptr;

		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Debug] 世界：暂停=%d 时间膨胀=%.2f 已开始=%d 玩家控制器tick=%d（bGameplayPaused=%d）"),
			UGameplayStatics::IsGamePaused(World) ? 1 : 0, World->GetWorldSettings() != nullptr ? World->GetWorldSettings()->TimeDilation : -1.f,
			World->HasBegunPlay() ? 1 : 0,
			Controller != nullptr && Controller->IsActorTickEnabled() ? 1 : 0,
			Controller != nullptr && Controller->IsGameplayPaused() ? 1 : 0);

		if (ControlledPawn == nullptr)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 没有受控 Pawn"));
			return;
		}

		const UCharacterMovementComponent* Move = ControlledPawn->FindComponentByClass<UCharacterMovementComponent>();
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Debug] 角色[%s]：tick=%d 位置=%s 速度=%.2f 移动模式=%d 落在地面=%d 可走地面=%d MaxWalkSpeed=%.1f 重力=%.2f"),
			Move != nullptr ? *Move->GetClass()->GetName() : TEXT("-"),
			ControlledPawn->IsActorTickEnabled() ? 1 : 0,
			*ControlledPawn->GetActorLocation().ToCompactString(),
			ControlledPawn->GetVelocity().Size(),
			Move != nullptr ? static_cast<int32>(Move->MovementMode) : -1,
			Move != nullptr && Move->IsMovingOnGround() ? 1 : 0,
			Move != nullptr && Move->CurrentFloor.IsWalkableFloor() ? 1 : 0,
			Move != nullptr ? Move->MaxWalkSpeed : -1.f,
			Move != nullptr ? Move->GravityScale : -1.f);

		if (Move != nullptr)
		{
			const FFindFloorResult& Floor = Move->CurrentFloor;
			const FHitResult& FR = Floor.HitResult;
			UE_LOG(LogMetamorphosis, Log,
				TEXT("[Debug] 脚下地面：有效阻挡=%d 来源Actor=%s 网格资产=%s 法线=%s（夹角 %.1f 度）可走Z阈值=%.3f"),
				FR.bBlockingHit ? 1 : 0,
				*GetNameSafe(FR.GetComponent() != nullptr ? FR.GetComponent()->GetOwner() : nullptr),
				*GetNameSafe(Cast<UStaticMeshComponent>(FR.GetComponent()) != nullptr
					? Cast<UStaticMeshComponent>(FR.GetComponent())->GetStaticMesh() : nullptr),
				*FR.ImpactNormal.ToCompactString(),
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FR.ImpactNormal.Z, -1.f, 1.f))),
				Move->GetWalkableFloorZ());
		}
	}

	/** 出生点/场景分布诊断：换地图后确认"房间在哪、PlayerStart 在哪、脚下有没有东西" */
	static void SpawnInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}

		int32 StartCount = 0;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			++StartCount;
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] PlayerStart %d: %s  位置=%s"), StartCount,
				*It->GetName(), *It->GetActorLocation().ToCompactString());
		}
		if (StartCount == 0)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 关卡里没有 PlayerStart"));
		}

		// 场景实际分布：包围盒容易被 Landscape/天空盒撑爆，另外列出"最大的几个静态网格"
		struct FMeshBoxInfo
		{
			FString Name;
			FVector Center = FVector::ZeroVector;
			FVector Extent = FVector::ZeroVector;
			float Volume = 0.f;
		};
		TArray<FMeshBoxInfo> MeshBoxes;
		FBox SceneBox(ForceInit);
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const FBox Box = It->GetComponentsBoundingBox(true);
			SceneBox += Box;
			FMeshBoxInfo Info;
			Info.Name = It->GetName();
			Info.Center = Box.GetCenter();
			Info.Extent = Box.GetExtent();
			Info.Volume = static_cast<float>(Box.GetVolume());
			MeshBoxes.Add(Info);
		}

		if (MeshBoxes.Num() > 0)
		{
			MeshBoxes.Sort([](const FMeshBoxInfo& A, const FMeshBoxInfo& B) { return A.Volume > B.Volume; });
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 静态网格 %d 个，最大的 10 个（中心 / 半径）："), MeshBoxes.Num());
			for (int32 i = 0; i < FMath::Min(10, MeshBoxes.Num()); ++i)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Debug]   %2d. %-32s 中心=%s 半径=%s"),
					i + 1, *MeshBoxes[i].Name,
					*MeshBoxes[i].Center.ToCompactString(), *MeshBoxes[i].Extent.ToCompactString());
			}
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 场景包围盒：中心=%s 半径=%s"),
				*SceneBox.GetCenter().ToCompactString(), *SceneBox.GetExtent().ToCompactString());
		}

		// 从 PlayerStart 往下打线（比从正在掉落的角色更可靠）
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			const FVector Start = It->GetActorLocation();
			const FVector End = Start - FVector(0.f, 0.f, 200000.f);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SpawnInfoStart), false, nullptr);

			// Visibility 通道：能不能"看到"地板
			FHitResult HitVis;
			const bool bHitVis = World->LineTraceSingleByChannel(HitVis, Start, End, ECC_Visibility, Params);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] PlayerStart 正下方（Visibility）：命中=%d %s Z=%.0f"),
				bHitVis ? 1 : 0, bHitVis ? *GetNameSafe(HitVis.GetComponent()) : TEXT("-"),
				bHitVis ? HitVis.ImpactPoint.Z : 0.f);

			// Pawn 通道：角色胶囊真正会不会被挡住（这才是"会不会掉下去"的判据）
			FHitResult HitPawn;
			const bool bHitPawn = World->LineTraceSingleByChannel(HitPawn, Start, End, ECC_Pawn, Params);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] PlayerStart 正下方（Pawn）：命中=%d %s Z=%.0f"),
				bHitPawn ? 1 : 0, bHitPawn ? *GetNameSafe(HitPawn.GetComponent()) : TEXT("-"),
				bHitPawn ? HitPawn.ImpactPoint.Z : 0.f);

			if (bHitVis && HitVis.GetComponent() != nullptr)
			{
				UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 地板碰撞设置：CollisionEnabled=%d Profile=%s CollisionProfileName=%s"),
					static_cast<int32>(HitVis.GetComponent()->GetCollisionEnabled()),
					*HitVis.GetComponent()->GetCollisionProfileName().ToString(),
					*HitVis.GetComponent()->GetCollisionProfileName().ToString());
			}
			break;
		}

		// 脚下到底有什么
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* ControlledPawn = PC->GetPawn())
			{
				const FVector Start = ControlledPawn->GetActorLocation();
				const FVector End = Start - FVector(0.f, 0.f, 100000.f);
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SpawnInfo), false, ControlledPawn);
				const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
				if (bHit)
				{
					UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 向下探测：从 %s 命中 %s 于 Z=%.0f（距离 %.0f cm）"),
						*Start.ToCompactString(), *GetNameSafe(Hit.GetComponent()), Hit.ImpactPoint.Z,
						(Start - Hit.ImpactPoint).Size());
				}
				else
				{
					UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 向下探测：从 %s 往下 1000 米内没有任何东西"),
						*Start.ToCompactString());
				}
			}
		}

		if (const AWorldSettings* Settings = World->GetWorldSettings())
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 世界设置：KillZ=%.0f"), Settings->KillZ);
		}
	}

	/** 相机遮挡探针（查"为什么这面墙没被剖切掉"） */
	static void CamProbe(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			Beetle->DebugCameraProbe();
		}
	}

	/** 相机状态一览（查"这个物件为什么被藏了/为什么没被藏"） */
	static void CamInfo(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			Beetle->LogCameraInfo();
		}
	}

	/** 包围盒诊断：Meta.Cam.Bounds [X Y Z]（省略坐标 = 当前相机位置）。
	 *  查"相机埋在没有碰撞的空心物体里"专用：射线/overlap 感知不到无碰撞物体，包围盒能。 */
	static void CamBounds(const TArray<FString>& Args, UWorld* World)
	{
		AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World);
		if (Beetle == nullptr)
		{
			return;
		}

		if (Args.Num() >= 3)
		{
			Beetle->DebugCameraBoundsAt(FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2])));
		}
		else
		{
			Beetle->DebugCameraBoundsAt(Beetle->GetCameraWorldLocation());
		}
	}

	/** 锁定/解锁视角水平偏移（让机位在自动化验证里可复现） */
	static void CamLock(const TArray<FString>& Args, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				Beetle->SetCameraYawLocked(false);
			}
			else
			{
				Beetle->SetCameraYawLocked(true, Args.Num() > 0 ? FCString::Atof(*Args[0]) : Beetle->GetCameraYawOffset());
			}
		}
	}

	/** 把角色瞬移到指定坐标：Meta.Teleport X Y Z（调试用；验证"贴近家具时会不会被误隐藏"） */
	static void Teleport(const TArray<FString>& Args, UWorld* World)
	{
		AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World);
		if (Beetle == nullptr || Args.Num() < 3)
		{
			UE_LOG(LogMetamorphosis, Warning, TEXT("[Debug] 用法：Meta.Teleport <X> <Y> <Z>"));
			return;
		}

		const FVector Target(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
		Beetle->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 瞬移到 %s（实际 %s）"),
			*Target.ToCompactString(), *Beetle->GetActorLocation().ToCompactString());
	}

	/** 直接改机位距离（调试/调参用；拍特写也用它） */
	static void CamDistance(const TArray<FString>& Args, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			Beetle->SetCameraDistance(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 450.f);
		}
	}

	/** 模拟手指横向搓屏幕（验证移动端转视角逻辑） */
	static void CamSwipe(const TArray<FString>& Args, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			const float DeltaX = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 100.f;
			Controller->ApplyTouchLookDelta(DeltaX);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟搓屏 %.0f 像素 -> 视角偏移 %.1f 度"),
				DeltaX, BeetleOf(World) != nullptr ? BeetleOf(World)->GetCameraYawOffset() : 0.f);
		}
	}

	static void WhereAmI(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisBeetleCharacter* Beetle = BeetleOf(World))
		{
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 位置=%s 空中=%s 阶段=%d 精灵高=%.1f cm 视角偏移=%.1f 度"),
				*Beetle->GetActorLocation().ToCompactString(),
				Beetle->IsAirborne() ? TEXT("是") : TEXT("否"),
				Beetle->GetMoodStage(), Beetle->GetSpriteHeightCm(), Beetle->GetCameraYawOffset());
		}
	}

	/** 模拟移动端按钮（验证按钮 -> 控制器 -> 角色这条链路） */
	static void MobileUp(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->SetAscendButtonHeld(true);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟按下移动端「上升」按钮"));
		}
	}

	static void MobileDown(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->SetDescendButtonHeld(true);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 模拟按下移动端「下降」按钮"));
		}
	}

	static void MobileRelease(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->SetAscendButtonHeld(false);
			Controller->SetDescendButtonHeld(false);
			UE_LOG(LogMetamorphosis, Log, TEXT("[Debug] 松开移动端按钮"));
		}
	}

	static void ShowMobile(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			// 既挂上移动端上下按钮/摇杆，也把 UI 切成移动端外观（PC 上调试与截图用）
			Controller->ShowMobileControls();
			Controller->SetForceMobileUI(true);
		}
	}

	static void ShowDesktop(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AMetamorphosisPlayerController* Controller = LocalController(World))
		{
			Controller->SetForceMobileUI(false);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMetaAir(
	TEXT("Meta.Air"),
	TEXT("空中则落地、地面则起飞悬停（调试用；正式操作是空格上升、X 下降、双击空格落地）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::Air));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamYaw(
	TEXT("Meta.Cam.Yaw"),
	TEXT("把视角水平偏移设为 N 度，如 Meta.Cam.Yaw 90"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamYaw));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamBounds(
	TEXT("Meta.Cam.Bounds"),
	TEXT("包围盒诊断：Meta.Cam.Bounds [X Y Z]（省略=当前相机位置）。列出世界包围盒包含该点的可见构件"
		 "（名称/类/Mobility/碰撞/是否已隐藏/尺寸/会不会被剖切规则跳过及原因）——"
		 "查「相机埋在没有碰撞的空心物体里」专用：射线与 overlap 都要求碰撞，包围盒不要求"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamBounds));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamLock(
	TEXT("Meta.Cam.Lock"),
	TEXT("锁定视角水平偏移：Meta.Cam.Lock [度]（省略=锁在当前值），Meta.Cam.Lock off 解锁。"
		 "无头/自动化验证里存在幻影输入会让相机自己飘，锁住后同一机位才可复现"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamLock));

static FAutoConsoleCommandWithWorldAndArgs GMetaDelayCommand(
	TEXT("Meta.Delay"),
	TEXT("N 秒后执行一条控制台命令，如 Meta.Delay 12 Meta.Inv.Fill（跨关卡有效）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::DelayCommand));

static FAutoConsoleCommandWithWorldAndArgs GMetaShotDelayed(
	TEXT("Meta.Shot.Delay"),
	TEXT("N 秒后自动截一张含 UI 的图，如 Meta.Shot.Delay 20（跨关卡有效）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::ShotDelayed));

static FAutoConsoleCommandWithWorldAndArgs GMetaMenuStatus(
	TEXT("Meta.Menu.Status"),
	TEXT("打印开始界面的预加载进度/是否就绪"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MenuStatus));

static FAutoConsoleCommandWithWorldAndArgs GMetaMenuStart(
	TEXT("Meta.Menu.Start"),
	TEXT("强制进入游戏关卡（等价于在开始界面点「开始游戏」）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MenuStart));

static FAutoConsoleCommandWithWorldAndArgs GMetaMenuPress(
	TEXT("Meta.Menu.Press"),
	TEXT("模拟点击开始界面按钮：Meta.Menu.Press <Start|Settings>（无头环境验证回调链路用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MenuPress));

static FAutoConsoleCommandWithWorldAndArgs GMetaMenuTree(
	TEXT("Meta.Menu.Tree"),
	TEXT("打印开始界面控件树 + 每个控件的 Visibility（排查按钮点不动）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MenuTree));

static FAutoConsoleCommandWithWorldAndArgs GMetaEndingPress(
	TEXT("Meta.Ending.Press"),
	TEXT("模拟点击通关界面按钮：Meta.Ending.Press <Restart|Title>（无头环境验证回调链路用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::EndingPress));

static FAutoConsoleCommandWithWorldAndArgs GMetaEndingTree(
	TEXT("Meta.Ending.Tree"),
	TEXT("打印通关界面控件树 + 每个控件的 Visibility（排查按钮点不动）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::EndingTree));

static FAutoConsoleCommandWithWorldAndArgs GMetaMoveInfo(
	TEXT("Meta.Move.Info"),
	TEXT("打印移动相关状态（暂停/tick/移动模式/速度）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MoveInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaSpawnInfo(
	TEXT("Meta.Spawn.Info"),
	TEXT("打印出生点与场景分布（换地图后用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SpawnInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaAnimInfo(
	TEXT("Meta.Anim.Info"),
	TEXT("打印甲虫动画状态（阶段/序列/第几帧）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::AnimInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaTouchSim(
	TEXT("Meta.Touch.Sim"),
	TEXT("模拟触摸：Meta.Touch.Sim <手指0-3> <x> <y> <down|move|up>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::TouchSim));

static FAutoConsoleCommandWithWorldAndArgs GMetaTouchClaim(
	TEXT("Meta.Touch.Claim"),
	TEXT("模拟 UI 认领手指：Meta.Touch.Claim <手指>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::TouchClaim));

static FAutoConsoleCommandWithWorldAndArgs GMetaTouchUnclaim(
	TEXT("Meta.Touch.Unclaim"),
	TEXT("解除 UI 对某手指的认领"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::TouchUnclaim));

static FAutoConsoleCommandWithWorldAndArgs GMetaPauseOpen(
	TEXT("Meta.Pause.Open"),
	TEXT("打开游戏内弹窗并时停"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::PauseOpen));

static FAutoConsoleCommandWithWorldAndArgs GMetaPauseClose(
	TEXT("Meta.Pause.Close"),
	TEXT("关闭弹窗并恢复游戏"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::PauseClose));

static FAutoConsoleCommandWithWorldAndArgs GMetaPauseStatus(
	TEXT("Meta.Pause.Status"),
	TEXT("查看弹窗/暂停状态"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::PauseStatus));

static FAutoConsoleCommandWithWorldAndArgs GMetaPauseOption(
	TEXT("Meta.Pause.Option"),
	TEXT("模拟点弹窗选项：Meta.Pause.Option <Resume|OpenSettings|NextLoop|ReturnToTitle>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::PauseOption));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsPrint(
	TEXT("Meta.Settings.Print"),
	TEXT("打印当前设置（音量/摇杆/大小）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsPrint));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsSet(
	TEXT("Meta.Settings.Set"),
	TEXT("改设置：Meta.Settings.Set <volume|fixed|joystick|hotbar|button> <值>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsSet));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsReset(
	TEXT("Meta.Settings.Reset"),
	TEXT("设置恢复默认"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsReset));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettlementPress(
	TEXT("Meta.Settlement.Press"),
	TEXT("模拟点结算界面的按钮：Meta.Settlement.Press <Restart|Title>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettlementPress));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettlementInfo(
	TEXT("Meta.Settlement.Info"),
	TEXT("打印结算阶段 / 是否在等待玩家选择"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettlementInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaOpenSettings(
	TEXT("Meta.UI.Settings"),
	TEXT("打开设置界面"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::OpenSettings));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsBack(
	TEXT("Meta.UI.SettingsBack"),
	TEXT("走设置里的返回（无头验证用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsBack));

// 2026-10-19：设置界面"看得见但点不到"的排障三件套
static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsDump(
	TEXT("Meta.Settings.Dump"),
	TEXT("打印设置界面里每个交互控件的几何尺寸/可见性/是否在视口（尺寸为 0 就是点不到）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsDump));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsGrab(
	TEXT("Meta.Settings.Grab"),
	TEXT("模拟拖动滑条（走真实回调链路）：Meta.Settings.Grab <0音量|1摇杆|2物品栏|3上下按钮> <值>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsGrab));

static FAutoConsoleCommandWithWorldAndArgs GMetaSettingsToggle(
	TEXT("Meta.Settings.Toggle"),
	TEXT("模拟点一下「摇杆固定」开关（走真实回调链路）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SettingsToggle));

static FAutoConsoleCommandWithWorldAndArgs GMetaReturnToTitle(
	TEXT("Meta.UI.ReturnToTitle"),
	TEXT("走设置里「返回主界面」的真实路径（无头验证用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::ReturnToTitle));

static FAutoConsoleCommandWithWorldAndArgs GMetaSaveInfo(
	TEXT("Meta.Save.Info"),
	TEXT("打印存档是否存在 + 路径 + 摘要"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SaveInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaSaveNow(
	TEXT("Meta.Save.Now"),
	TEXT("立刻存档：Meta.Save.Now [原因]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SaveNow));

static FAutoConsoleCommandWithWorldAndArgs GMetaSaveNowForce(
	TEXT("Meta.Save.NowForce"),
	TEXT("强制存档（剧情/门进行中也存，结算中/主界面仍跳过）：Meta.Save.NowForce [原因]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SaveNowForce));

static FAutoConsoleCommandWithWorldAndArgs GMetaSaveLoad(
	TEXT("Meta.Save.Load"),
	TEXT("读档并应用到子系统（不切关卡）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SaveLoad));

static FAutoConsoleCommandWithWorldAndArgs GMetaSaveDelete(
	TEXT("Meta.Save.Delete"),
	TEXT("删除存档（等于新游戏）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SaveDelete));

static FAutoConsoleCommandWithWorldAndArgs GMetaSaveSummary(
	TEXT("Meta.Save.Summary"),
	TEXT("打印存档摘要"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::SaveSummary));

static FAutoConsoleCommandWithWorldAndArgs GMetaJoystickSim(
	TEXT("Meta.Joystick.Sim"),
	TEXT("模拟摇杆位移：Meta.Joystick.Sim <dx> <dy>（0 0 = 松开）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::JoystickSim));

static FAutoConsoleCommandWithWorldAndArgs GMetaTeleport(
	TEXT("Meta.Teleport"),
	TEXT("把角色瞬移到 X Y Z，如 Meta.Teleport -600 -1900 20"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::Teleport));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamProbe(
	TEXT("Meta.Cam.Probe"),
	TEXT("从相机往注视点扫一遍，打印各查询方式的命中结果（排查剖切失效）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamProbe));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamInfo(
	TEXT("Meta.Cam.Info"),
	TEXT("打印相机状态：坐标/是否在几何体内/被挡方向比例/规则开关/当前隐藏清单（排查剖切问题先敲它）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamInfo));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamDistance(
	TEXT("Meta.Cam.Distance"),
	TEXT("把机位距离设为 N 厘米，如 Meta.Cam.Distance 150（拍特写/调参用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamDistance));

static FAutoConsoleCommandWithWorldAndArgs GMetaCamSwipe(
	TEXT("Meta.Cam.Swipe"),
	TEXT("模拟手指横向搓屏 N 像素，如 Meta.Cam.Swipe 200"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::CamSwipe));

static FAutoConsoleCommandWithWorldAndArgs GMetaWhere(
	TEXT("Meta.Where"),
	TEXT("打印主角的位置/是否在空中/阶段/精灵高度/视角偏移"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::WhereAmI));

static FAutoConsoleCommandWithWorldAndArgs GMetaMobileUp(
	TEXT("Meta.Mobile.Up"),
	TEXT("模拟按住移动端「上升」按钮"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MobileUp));

static FAutoConsoleCommandWithWorldAndArgs GMetaMobileDown(
	TEXT("Meta.Mobile.Down"),
	TEXT("模拟按住移动端「下降」按钮"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MobileDown));

static FAutoConsoleCommandWithWorldAndArgs GMetaMobileRelease(
	TEXT("Meta.Mobile.Release"),
	TEXT("松开移动端上下按钮"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::MobileRelease));

static FAutoConsoleCommandWithWorldAndArgs GMetaUIMobile(
	TEXT("Meta.UI.Mobile"),
	TEXT("（重新）挂载移动端上下按钮，并把 UI 强制切成移动端外观（PC 上调试/截图用）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::ShowMobile));

static FAutoConsoleCommandWithWorldAndArgs GMetaUIDesktop(
	TEXT("Meta.UI.Desktop"),
	TEXT("切回 PC（键盘鼠标）UI 外观（与 Meta.UI.Mobile 配对，方便同一轮里截两张图）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MetamorphosisPlayerConsole::ShowDesktop));
#endif // !UE_BUILD_SHIPPING

float AMetamorphosisPlayerController::GetUITouchScale() const
{
	// 优先用摇杆控件的实际几何缩放：它决定了"UI 画出来的圆"在屏幕上到底多大。
	// 拿不到（摇杆还没创建）再退回 WidgetLayoutLibrary 的 DPI 值。
	if (JoystickWidget != nullptr)
	{
		const float WidgetScale = static_cast<float>(JoystickWidget->GetCachedGeometry().Scale);
		if (WidgetScale > KINDA_SMALL_NUMBER)
		{
			return FMath::Clamp(WidgetScale, 0.25f, 4.f);
		}
	}

	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(
		const_cast<AMetamorphosisPlayerController*>(this));
	return ViewportScale > KINDA_SMALL_NUMBER ? FMath::Clamp(ViewportScale, 0.25f, 4.f) : 1.f;
}












