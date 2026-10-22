// Copyright (C) 2026 Metamorphosis Team.
// SPDX-License-Identifier: MIT

#include "Config/MetamorphosisConfigSubsystem.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Metamorphosis.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"

// 编译内嵌的同一份配表（由 Tools/export_config.py 生成到 Config/GameConfigJson.inl）。
// 里面是 UTF-8 窄字符原始字符串：GEmbeddedGameConfigJsonUtf8 / GEmbeddedGameConfigJsonUtf8Len。
#include "Config/GameConfigJson.inl"

namespace
{
	const TCHAR* const ConfigFilePath = TEXT("Config/game_config.json");

	/** 内嵌的 UTF-8 原始字符串块 -> FString（分块生成、这里运行时拼接） */
	FString EmbeddedConfigToString()
	{
		if (GEmbeddedGameConfigJsonChunkCount <= 0)
		{
			return FString();
		}

		FString Out;
		Out.Reserve(GEmbeddedGameConfigJsonUtf8Len);
		for (int32 Index = 0; Index < GEmbeddedGameConfigJsonChunkCount; ++Index)
		{
			const char* Chunk = GEmbeddedGameConfigJsonChunks[Index];
			if (Chunk == nullptr)
			{
				continue;
			}
			// 每块都按 UTF-8 字符边界切过，可以单独转换
			const FUTF8ToTCHAR Convert(Chunk, static_cast<int32>(FCStringAnsi::Strlen(Chunk)));
			Out.Append(Convert.Get(), Convert.Length());
		}
		return Out;
	}

	/**
	 * 解析 JSON 并做"关键表非空"的合理性检查。
	 * 返回 false 时 OutReason 说明卡在哪一步（空串 / JSON 语法 / 表全空），方便日志定位。
	 */
	bool ParseConfigJson(const FString& Json, FGameConfig& OutConfig, FString& OutReason)
	{
		if (Json.IsEmpty())
		{
			OutReason = TEXT("内容为空");
			return false;
		}
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutConfig, 0, 0))
		{
			OutReason = TEXT("JSON 解析失败（语法错误或字段类型不匹配）");
			return false;
		}
		if (OutConfig.Items.Num() == 0 && OutConfig.Tasks.Num() == 0 && OutConfig.Story.Num() == 0)
		{
			OutReason = TEXT("解析成功但关键表全空（Items/Tasks/Story 都是 0）");
			return false;
		}
		return true;
	}

	void LogConfigCounts(const TCHAR* Where, const FGameConfig& Cfg)
	{
		UE_LOG(LogMetamorphosis, Log,
			TEXT("[Config] %s：怀疑阈值 %f/%f，时段 %d 个，家人动线 %d 条，道具 %d 件，任务 %d 条，剧情 %d 节点"),
			Where,
			Cfg.Suspicion.ThresholdSearch, Cfg.Suspicion.ThresholdExposed,
			Cfg.Periods.Num(), Cfg.Schedules.Num(), Cfg.Items.Num(), Cfg.Tasks.Num(), Cfg.Story.Num());
	}
}

void UMetamorphosisConfigSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!LoadConfig())
	{
		UE_LOG(LogMetamorphosis, Error,
			TEXT("[Config] 数值配置表载入失败（文件与编译内嵌都不可用），本次运行将使用默认值。"
				 "请检查 Content/Config/game_config.json 是否存在，或重新运行 Tools/export_config.py 并重新编译。"));
	}
}

void UMetamorphosisConfigSubsystem::Reload()
{
	if (!LoadConfig())
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[Config] 重新载入数值配置表失败，保留旧配置。"));
	}
}

bool UMetamorphosisConfigSubsystem::LoadConfig()
{
	const FString Path = FPaths::ProjectContentDir() / ConfigFilePath;
	FString Reason;

	// ---- 1) 优先读文件（编辑器里改配表后重启即生效，不需要重编）----
	FString FileJson;
	bool bFileUsable = false;
	if (bForceEmbeddedConfig)
	{
		UE_LOG(LogMetamorphosis, Warning, TEXT("[Config] bForceEmbeddedConfig=True，跳过文件，直接使用编译内嵌配表。"));
	}
	else if (!FFileHelper::LoadFileToString(FileJson, *Path))
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Config] 配表文件读取失败（不存在或无权限）：%s -> 改用编译内嵌配表。"), *Path);
	}
	else
	{
		FGameConfig Parsed;
		if (ParseConfigJson(FileJson, Parsed, Reason))
		{
			Config = Parsed;
			bUsingEmbedded = false;
			ConfigSource = FString::Printf(TEXT("文件：%s"), *Path);
			LogConfigCounts(TEXT("从文件载入"), Config);
			CheckEmbeddedMatchesFile(FileJson);
			return true;
		}

		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Config] 文件解析失败（%s）：%s -> 改用编译内嵌配表。"), *Reason, *Path);
	}

	// ---- 2) 回退：编译内嵌的那一份 ----
	const FString EmbeddedJson = EmbeddedConfigToString();
	FGameConfig Parsed;
	if (!ParseConfigJson(EmbeddedJson, Parsed, Reason))
	{
		UE_LOG(LogMetamorphosis, Error, TEXT("[Config] 编译内嵌配表也不可用（%s）。"), *Reason);
		return false;
	}

	Config = Parsed;
	bUsingEmbedded = true;
	ConfigSource = TEXT("编译内嵌（GameConfigJson.inl）");
	LogConfigCounts(TEXT("改用编译内嵌配表"), Config);
	return true;
}

void UMetamorphosisConfigSubsystem::CheckEmbeddedMatchesFile(const FString& FileJson) const
{
	const FString EmbeddedJson = EmbeddedConfigToString();
	if (EmbeddedJson.IsEmpty())
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Config] 编译内嵌配表为空（GameConfigJson.inl 可能没生成），打包后文件读不到时会没有配表。"));
		return;
	}

	// 只做粗比较（忽略行尾），两者由同一个脚本生成，正常情况下应完全一致
	FString A = FileJson;
	FString B = EmbeddedJson;
	A.ReplaceInline(TEXT("\r"), TEXT(""));
	B.ReplaceInline(TEXT("\r"), TEXT(""));
	if (A.Len() != B.Len() || !A.Equals(B))
	{
		UE_LOG(LogMetamorphosis, Warning,
			TEXT("[Config] 文件配表与编译内嵌配表不一致（文件 %d 字节 / 内嵌 %d 字节）——"
				 "内嵌版本较旧，重新编译一次即可同步（编辑器里仍以文件为准，不影响当前运行）。"),
			A.Len(), B.Len());
	}
}
