#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "BuildingInfoPanelWidget.generated.h"

class UBorder;
class UButton;
class UFootprintPreviewWidget;
class USizeBox;
class UTextBlock;
class UVerticalBox;

// The Building dock panel of the exterior screen, in the style of the design team's "Selected building" page: the selected
// building's outline, its name and id, how many floors, rooms and lifts & stairs it has, whether it has rooms yet, and the
// two ways into it - the 2D floor editor and the 3D building view. With nothing selected it asks for a selection.
UCLASS()
class INNAVI_API UBuildingInfoPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// What the two buttons do (the screen opens the interior editor).
	TFunction<void()> OnOpen2D;
	TFunction<void()> OnOpen3D;

	// The width the content is sized to (the dock group's width minus the panel padding).
	void SetContentWidth(float Width);

	// Shows the controller's selected building (or the empty state).
	void Refresh();
	// Same, but only when the selection or the buildings changed since the last time - cheap enough to call every tick.
	void RefreshIfChanged();

	// Tests.
	bool IsShowingBuildingForTests() const { return bShowingBuilding; }
	FString GetNameTextForTests() const;
	FString GetIdTextForTests() const;
	FString GetFloorsTextForTests() const;
	FString GetRoomsTextForTests() const;
	FString GetLinksTextForTests() const;
	FString GetStatusTextForTests() const;
	int32 GetOutlinePointCountForTests() const;
	UButton* GetOpen2DButtonForTests() const { return Open2DButton; }
	UButton* GetOpen3DButtonForTests() const { return Open3DButton; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleOpen2DClicked();
	UFUNCTION()
	void HandleOpen3DClicked();

	UPROPERTY()
	USizeBox* ContentSize = nullptr;
	UPROPERTY()
	UTextBlock* EmptyText = nullptr;
	UPROPERTY()
	UVerticalBox* Content = nullptr;
	UPROPERTY()
	UFootprintPreviewWidget* Outline = nullptr;
	UPROPERTY()
	UTextBlock* NameText = nullptr;
	UPROPERTY()
	UTextBlock* IdText = nullptr;
	UPROPERTY()
	UTextBlock* FloorsValue = nullptr;
	UPROPERTY()
	UTextBlock* RoomsValue = nullptr;
	UPROPERTY()
	UTextBlock* LinksValue = nullptr;
	UPROPERTY()
	UBorder* StatusTag = nullptr;
	UPROPERTY()
	UTextBlock* StatusText = nullptr;
	UPROPERTY()
	UButton* Open2DButton = nullptr;
	UPROPERTY()
	UButton* Open3DButton = nullptr;

	bool bShowingBuilding = false;
	FString DisplayedId;
	uint64 DisplayedRevision = MAX_uint64;
	float ContentWidth = 220.f;
};
