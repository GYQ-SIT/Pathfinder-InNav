// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InNavi : ModuleRules
{
	public InNavi(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Runtime audio (see WavFileLoader.h and ANavPlayerController::PlayWavFile) reads/decodes .wav
		// files itself via FFileHelper + USoundWaveProcedural, both already covered by Engine - no
		// audio-specific module needed. (An earlier pass vendored a third-party mp3-decoding plugin
		// here, back when mp3 support was a hard requirement; removed once wav-only was confirmed
		// sufficient - see docs/deliverables.md.)
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Json", "JsonUtilities", "ProceduralMeshComponent", "UMG" });

		// DesktopPlatform: native "open file" dialog for importing building data.
		// Slate/SlateCore: needed to get the active window handle to parent that dialog to.
		PrivateDependencyModuleNames.AddRange(new string[] { "DesktopPlatform", "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}