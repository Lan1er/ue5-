// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Metamorphosis : ModuleRules
{
	public Metamorphosis(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",

			"ApplicationCore",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json",
			"JsonUtilities"
		});

		PublicIncludePaths.AddRange(new string[] {
			"Metamorphosis",
			"Metamorphosis/Character",
			"Metamorphosis/Config",
			"Metamorphosis/Input",
			"Metamorphosis/Stats",
			"Metamorphosis/UI",
			"Metamorphosis/Variant_Strategy",
			"Metamorphosis/Variant_Strategy/UI",
			"Metamorphosis/Variant_TwinStick",
			"Metamorphosis/Variant_TwinStick/AI",
			"Metamorphosis/Variant_TwinStick/Gameplay",
			"Metamorphosis/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

