#include "ExteriorDockLayout.h"

#include "InteriorDockLayout.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace ExteriorDock
{
	const FName Audio(TEXT("Audio"));
	const FName Map(TEXT("Map"));
	const FName Building(TEXT("Building"));
	const FName Debug(TEXT("Debug"));
	const FName Navigation(TEXT("Navigation"));

	const TArray<FName>& AllPanels()
	{
		static const TArray<FName> Panels = { Audio, Map, Building, Debug, Navigation };
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
		return Panel == Map ? EDockSide::Left : ((Panel == Building || Panel == Navigation) ? EDockSide::Right : EDockSide::Bottom);
	}

	namespace
	{
		bool IsHiddenByDefault(FName Panel)
		{
			return Panel == Audio || Panel == Debug || Panel == Navigation;
		}
	}

	void BuildDefaultLayout(FDockLayout& L)
	{
		L.Clear();

		L.DockPanelToEdge(Map, EDockSide::Left);
		L.SetAreaExtent(EDockSide::Left, 250.f);
		L.DockPanelToEdge(Building, EDockSide::Right);
		L.SetAreaExtent(EDockSide::Right, 280.f);

		// Navigation shares the Building panel's tab group when shown (Window > Navigation); hidden until then.
		L.DockPanel(Navigation, Building, EDockDropZone::Center);
		L.HidePanel(Navigation);

		// Hidden panels are placed first and then hidden so the layout remembers where each one belongs.
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
		return FPaths::ProjectSavedDir() / TEXT("ExteriorDockLayout.json");
	}

	FString TeamLayoutPath()
	{
		return FPaths::ProjectDir() / TEXT("Data") / TEXT("DefaultLayouts") / TEXT("DefaultExteriorDockLayout.json");
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
		// Writing is not panel-specific, so share the interior editor's implementation.
		return InteriorDock::SaveToFile(Layout, Path, Viewport);
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
