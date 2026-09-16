// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InNavi : ModuleRules
{
	public InNavi(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Json", "JsonUtilities", "ProceduralMeshComponent", "UMG" });

		// DesktopPlatform: native "open file" dialog for importing building data.
		// Slate/SlateCore: needed to get the active window handle to parent that dialog to.
		PrivateDependencyModuleNames.AddRange(new string[] { "DesktopPlatform", "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}