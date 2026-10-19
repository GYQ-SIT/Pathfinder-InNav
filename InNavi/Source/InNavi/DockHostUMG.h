#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "DockHostWidget.h"
#include "DockHostUMG.generated.h"

// UMG wrapper around SDockHost so a pure-C++ UUserWidget can drop the dock system into its widget tree
// (e.g. as a full-stretch child of a canvas). Panel contents are ordinary UWidgets that are NOT parented
// anywhere in the owner's tree - they are registered here and placed by the dock host.
UCLASS()
class INNAVI_API UDockHostWidget : public UWidget
{
	GENERATED_BODY()

public:
	UDockHostWidget(const FObjectInitializer& ObjectInitializer);

	TSharedRef<FDockLayout> GetLayout() const { return Layout.ToSharedRef(); }
	void SetMetrics(const FDockMetrics& InMetrics);

	// Safe before or after the Slate widget exists. The widget is kept alive by this object.
	void RegisterPanel(FName Id, const FText& Title, UWidget* Content, bool bScrollable = true);

	// Call after editing the layout from outside (HidePanel, ShowPanel, FromJson...).
	void NotifyLayoutChanged();

	bool IsPanelShown(FName Id) const { return Layout->ContainsPanel(Id); }

	// True when ScreenPosition is over Id's visible content: false if the panel is hidden or is an inactive
	// tab. (A widget's cached geometry outlives it being swapped out, so it can't be trusted for this.)
	bool IsScreenPositionOverPanel(FName Id, const FVector2D& ScreenPosition) const;
	bool IsScreenPositionOverDock(const FVector2D& ScreenPosition) const;
	float GetPanelWidth(FName Id) const;

	// Fired after a user-driven change (resize finished, tab switched or closed, panel re-docked).
	FSimpleDelegate OnLayoutChangedNative;
	// Fired when a tab's close button hid a panel.
	FOnDockPanelEvent OnPanelClosedNative;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	struct FPanelInfo
	{
		FText Title;
		bool bScrollable = true;
	};

	TSharedPtr<FDockLayout> Layout;
	FDockMetrics Metrics;
	TSharedPtr<SDockHost> Host;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UWidget>> PanelContents;
	TMap<FName, FPanelInfo> PanelInfos;
};
