// Console-driven test bench for SDockHost: `InNavi.DockTest` in PIE or a Development/Debug build toggles
// a full-screen dock layout filled with demo panels. It touches nothing else in the project.
#include "DockHostWidget.h"

#if !UE_BUILD_SHIPPING

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FSlateColorBrush DemoWhiteBrush(FLinearColor::White);
	const TCHAR* const DemoPanels[] = { TEXT("Toolbar"), TEXT("Floor"), TEXT("Properties"), TEXT("Layers"), TEXT("Navigation"), TEXT("Audio"), TEXT("Map"), TEXT("Debug") };

	void BuildDemoLayout(FDockLayout& L)
	{
		L.Clear();
		L.DockPanelToEdge(TEXT("Toolbar"), EDockSide::Left);
		L.DockPanel(TEXT("Floor"), TEXT("Toolbar"), EDockDropZone::Bottom);
		L.DockPanel(TEXT("Layers"), TEXT("Floor"), EDockDropZone::Center);
		L.SetActiveTab(TEXT("Floor"));
		L.SetAreaExtent(EDockSide::Left, 270.f);

		L.DockPanelToEdge(TEXT("Properties"), EDockSide::Right);
		L.DockPanel(TEXT("Navigation"), TEXT("Properties"), EDockDropZone::Bottom);
		L.SetAreaExtent(EDockSide::Right, 320.f);

		L.DockPanelToEdge(TEXT("Audio"), EDockSide::Bottom);
		L.DockPanel(TEXT("Map"), TEXT("Audio"), EDockDropZone::Center);
		L.DockPanel(TEXT("Debug"), TEXT("Audio"), EDockDropZone::Center);
		L.SetActiveTab(TEXT("Audio"));
		L.SetAreaExtent(EDockSide::Bottom, 220.f);
	}

	TSharedRef<SWidget> MakeDemoContent(const FString& Name, int32 ButtonCount)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight().Padding(2.f)
		[
			SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s panel (demo content)"), *Name)))
		];
		for (int32 i = 1; i <= ButtonCount; ++i)
		{
			const FString Label = FString::Printf(TEXT("%s button %d"), *Name, i);
			Box->AddSlot().AutoHeight().Padding(2.f)
			[
				SNew(SButton)
				.Text(FText::FromString(Label))
				.OnClicked_Lambda([Label]()
				{
					UE_LOG(LogTemp, Display, TEXT("[DockTest] clicked: %s"), *Label);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, FString::Printf(TEXT("clicked: %s"), *Label));
					}
					return FReply::Handled();
				})
			];
		}
		Box->AddSlot().AutoHeight().Padding(2.f)
		[
			SNew(SEditableTextBox).HintText(FText::FromString(TEXT("type here (focus test)")))
		];
		return Box;
	}

	struct FDockDemo
	{
		TSharedPtr<SWidget> Overlay;
		TSharedPtr<SDockHost> Host;
		TSharedPtr<FDockLayout> Layout;
		TWeakObjectPtr<UGameViewportClient> Viewport;
	};
	FDockDemo GDemo;

	void HideDemo()
	{
		if (GDemo.Overlay.IsValid() && GDemo.Viewport.IsValid())
		{
			GDemo.Viewport->RemoveViewportWidgetContent(GDemo.Overlay.ToSharedRef());
		}
		GDemo = FDockDemo();
	}

	void ToggleDockTest(const TArray<FString>& Args, UWorld* World)
	{
		if (GDemo.Overlay.IsValid())
		{
			HideDemo();
			return;
		}

		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		if (!Viewport)
		{
			UE_LOG(LogTemp, Warning, TEXT("InNavi.DockTest needs a running game viewport (use it in PIE or a game build)."));
			return;
		}

		GDemo.Layout = MakeShared<FDockLayout>();
		BuildDemoLayout(*GDemo.Layout);
		const TSharedPtr<FDockLayout> Layout = GDemo.Layout;

		TSharedPtr<SDockHost> Host;
		SAssignNew(Host, SDockHost)
			.Layout(Layout)
			.OnLayoutChanged_Lambda([]() { UE_LOG(LogTemp, Display, TEXT("[DockTest] layout changed by the user")); })
			.OnPanelClosed_Lambda([](FName Panel) { UE_LOG(LogTemp, Display, TEXT("[DockTest] closed: %s"), *Panel.ToString()); });

		for (const TCHAR* Name : DemoPanels)
		{
			const FString Title(Name);
			// Layers is tall on purpose (scrolling); Debug has almost nothing (small content).
			const int32 Buttons = Title == TEXT("Layers") ? 40 : (Title == TEXT("Debug") ? 1 : 6);
			Host->RegisterPanel(FName(Name), FText::FromString(Title), MakeDemoContent(Title, Buttons));
		}

		TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
		Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 2.f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Dock test:")))
		];
		Strip->AddSlot().AutoWidth().Padding(2.f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Reset layout")))
			.OnClicked_Lambda([Layout, Host]()
			{
				BuildDemoLayout(*Layout);
				Host->NotifyLayoutChanged();
				return FReply::Handled();
			})
		];
		for (const TCHAR* Name : DemoPanels)
		{
			const FName PanelName(Name);
			Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 2.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Layout, PanelName]() { return Layout->ContainsPanel(PanelName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Layout, Host, PanelName](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked)
					{
						Layout->ShowPanel(PanelName, EDockSide::Left);
					}
					else
					{
						Layout->HidePanel(PanelName);
					}
					Host->NotifyLayoutChanged();
				})
				[
					SNew(STextBlock).Text(FText::FromName(PanelName))
				]
			];
		}

		GDemo.Host = Host;
		GDemo.Overlay = SNew(SVerticalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(&DemoWhiteBrush)
				.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.75f))
				[
					Strip
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				Host.ToSharedRef()
			];
		GDemo.Viewport = Viewport;
		Viewport->AddViewportWidgetContent(GDemo.Overlay.ToSharedRef(), 10000);
	}

	FAutoConsoleCommandWithWorldAndArgs GDockTestCommand(
		TEXT("InNavi.DockTest"),
		TEXT("Toggles a test overlay for the docking host (tabs, splitters, edge resizing, hide/show)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ToggleDockTest));
}

#endif // !UE_BUILD_SHIPPING
