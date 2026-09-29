// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InNavi : ModuleRules
{
	public InNavi(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// RuntimeAudioImporter: URuntimeAudioImporterLibrary/UImportedSoundWave, used by the Audio panel
		// to decode mp3/wav files read at runtime from Data/Audio/. The engine's own built-in Media
		// Framework (WmfMedia) was tried first (no third-party plugin needed) but UE 5.8's WmfMedia has
		// dropped mp3 support entirely (a known engine regression, confirmed via the Output Log
		// rejecting every .mp3 as "URI scheme or file extension not supported") - this vendored plugin
		// (InNavi/Plugins/RuntimeAudioImporter/, MIT licensed) decodes mp3 itself instead of relying on
		// the OS's media framework, so it isn't affected by that regression.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Json", "JsonUtilities", "ProceduralMeshComponent", "UMG", "RuntimeAudioImporter" });

		// DesktopPlatform: native "open file" dialog for importing building data.
		// Slate/SlateCore: needed to get the active window handle to parent that dialog to.
		PrivateDependencyModuleNames.AddRange(new string[] { "DesktopPlatform", "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}