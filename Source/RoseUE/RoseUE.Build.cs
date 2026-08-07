// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RoseUE : ModuleRules
{
	public RoseUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AssetRegistry", "SkeletalMerging" });

		// Slate: the in-game stats panel + ROSE UI windows; Json: the converted
		// Arua dialog layouts (Content/UI/Layouts/*.json — no XML at runtime).
		// MoviePlayer: the between-zone loading screen (renders on its own thread
		// during the blocking OpenLevel, so the spinner animates).
		// HTTP: the backend service (accounts, character roster, persistence —
		// backend/README.md).  Client calls the player-facing API; the zone
		// server calls /internal with the service key.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "Json", "MoviePlayer", "HTTP" });

		// lua4/ — Lua 4.0.1 (the exact interpreter the ROSE client links)
		// runs the CON dialog scripts unmodified (lundump.c carries a small
		// ROSE-UE64 patch: on-disk size_t is 4 bytes). Plain C89 — keep UE's
		// C++-oriented warning knobs off it.
		ShadowVariableWarningLevel = WarningLevel.Off;
		UndefinedIdentifierWarningLevel = WarningLevel.Off;
		bUseUnity = false;

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
