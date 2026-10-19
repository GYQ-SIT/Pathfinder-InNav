#include "InteriorDockLayout.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace InteriorDock
{
	const FName Toolbar(TEXT("Toolbar"));
	const FName Floor(TEXT("Floor"));
	const FName Properties(TEXT("Properties"));
	const FName Layers(TEXT("Layers"));
	const FName Navigation(TEXT("Navigation"));
	const FName Audio(TEXT("Audio"));
	const FName Map(TEXT("Map"));
	const FName Debug(TEXT("Debug"));

	const TArray<FName>& AllPanels()
	{
		static const TArray<FName> Panels = { Toolbar, Floor, Properties, Layers, Navigation, Audio, Map, Debug };
		return Panels;
	}

	TSet<FName> KnownPanels()
	{
		TSet<FName> Set;
		for (const FName Panel : AllPanels())
		{
			Set.Add(Panel);
		}
		return Set;
	}

	EDockSide DefaultSideFor(FName Panel)
	{
		if (Panel == Toolbar || Panel == Map)
		{
			return EDockSide::Left;
		}
		if (Panel == Audio || Panel == Debug)
		{
			return EDockSide::Bottom;
		}
		return EDockSide::Right;
	}

	namespace
	{
		bool IsHiddenByDefault(FName Panel)
		{
			return Panel == Navigation || Panel == Audio || Panel == Debug;
		}

		void SetRatios(const TSharedPtr<FDockNode>& Root, const TArray<float>& Ratios)
		{
			if (Root.IsValid() && Root->IsSplit() && Root->Ratios.Num() == Ratios.Num())
			{
				Root->Ratios = Ratios;
			}
		}
	}

	void BuildDefaultLayout(FDockLayout& L)
	{
		L.Clear();

		L.DockPanelToEdge(Map, EDockSide::Left);
		L.DockPanel(Toolbar, Map, EDockDropZone::Bottom);
		SetRatios(L.GetAreaRoot(EDockSide::Left), { 0.29f, 0.71f });
		L.SetAreaExtent(EDockSide::Left, 250.f);

		L.DockPanelToEdge(Floor, EDockSide::Right);
		L.DockPanel(Layers, Floor, EDockDropZone::Bottom);
		L.DockPanel(Properties, Layers, EDockDropZone::Bottom);
		SetRatios(L.GetAreaRoot(EDockSide::Right), { 0.315f, 0.299f, 0.386f });
		L.SetAreaExtent(EDockSide::Right, 270.f);
		L.SetActiveTab(Properties);

		// Hidden panels are placed first and then hidden so the layout remembers where each one belongs.
		L.DockPanel(Navigation, Properties, EDockDropZone::Center);
		L.HidePanel(Navigation);
		L.SetActiveTab(Properties);

		L.DockPanelToEdge(Audio, EDockSide::Bottom);
		L.DockPanel(Debug, Audio, EDockDropZone::Center);
		L.SetAreaExtent(EDockSide::Bottom, 220.f);
		L.HidePanel(Debug);
		L.HidePanel(Audio);
	}

	void PlaceMissingPanels(FDockLayout& L, const TArray<FName>& Missing)
	{
		for (const FName Panel : Missing)
		{
			if (!IsHiddenByDefault(Panel))
			{
				L.ShowPanel(Panel, DefaultSideFor(Panel));
			}
		}
	}

	FString UserLayoutPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("InteriorDockLayout.json");
	}

	FString TeamLayoutPath()
	{
		return FPaths::ProjectDir() / TEXT("Data") / TEXT("DefaultLayouts") / TEXT("DefaultInteriorDockLayout.json");
	}

	bool LoadFromFile(FDockLayout& Layout, const FString& Path, const FVector2D& Viewport)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Dock layout file %s is not valid JSON - ignoring it."), *Path);
			return false;
		}
		TArray<FName> Missing;
		if (!Layout.FromJson(Root, Viewport, KnownPanels(), Missing))
		{
			UE_LOG(LogTemp, Warning, TEXT("Dock layout file %s has the wrong version or shape - ignoring it."), *Path);
			return false;
		}
		PlaceMissingPanels(Layout, Missing);
		return true;
	}

	bool SaveToFile(const FDockLayout& Layout, const FString& Path, const FVector2D& Viewport)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		if (!FJsonSerializer::Serialize(Layout.ToJson(Viewport).ToSharedRef(), Writer))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(Text, *Path);
	}

	void LoadLayout(FDockLayout& Layout, const FVector2D& Viewport)
	{
		if (LoadFromFile(Layout, UserLayoutPath(), Viewport) || LoadFromFile(Layout, TeamLayoutPath(), Viewport))
		{
			return;
		}
		BuildDefaultLayout(Layout);
	}
}
