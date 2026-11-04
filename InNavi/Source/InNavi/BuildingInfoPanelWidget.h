#pragma once

#include "CoreMinimal.h"
#include "Components/ComboBoxString.h"
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
	// The type the drop-down shows, how many types it lists, and choosing one in it (what a click on an item does).
	FString GetTypeTextForTests() const;
	int32 GetTypeOptionCountForTests() const;
	void SelectTypeForTests(const FString& Type);
	int32 GetOutlinePointCountForTests() const;
	// The See-through controls: whether the button shows ON, what the floor drop-down shows, how many items it lists, and the user using them.
	bool IsTransparentOnForTests() const { return bTransparentShownOn; }
	FString GetTransparentTextForTests() const;
	FString GetRoomFloorTextForTests() const;
	int32 GetRoomFloorOptionCountForTests() const;
	void ClickTransparentForTests() { HandleTransparentClicked(); }
	void SelectRoomFloorForTests(const FString& Item) { HandleRoomFloorSelected(Item, ESelectInfo::OnMouseClick); }
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
	// The user chose a type in the drop-down.
	UFUNCTION()
	void HandleTypeSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	// An item of the drop-down: the type's colour as a small square before its name.
	UFUNCTION()
	UWidget* HandleGenerateTypeItem(FString Item);

	// The button that makes the building transparent (ON) or not (OFF).
	UFUNCTION()
	void HandleTransparentClicked();
	// Makes the button look ON (orange) or OFF (the usual look) and say so.
	void ShowTransparentState(bool bOn);
	// An item of the floor drop-down, in the light text of the panel.
	UFUNCTION()
	UWidget* HandleGenerateRoomFloorItem(FString Item);
	// The user chose "All floors" or a floor in the drop-down of the floor whose rooms show.
	UFUNCTION()
	void HandleRoomFloorSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	// The label and the drop-down for the building's type, as rows at the end of Stack.
	void AddTypeRow(UVerticalBox* Stack);

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
	UComboBoxString* TypeCombo = nullptr;
	// What the drop-down hands to Slate is a widget made on the spot that nothing else owns; without this it can be collected while
	// the drop-down still draws it. Only the newest are kept.
	UPROPERTY()
	TArray<TObjectPtr<UWidget>> RetainedTypeItems;
	// True while the drop-down is being set from the building, so that does not count as the user choosing a type.
	bool bRefreshingType = false;
	UPROPERTY()
	UButton* TransparentButton = nullptr;
	UPROPERTY()
	UTextBlock* TransparentLabel = nullptr;
	bool bTransparentShownOn = false;
	// "All floors", then one item per floor of the selected building.
	UPROPERTY()
	UComboBoxString* RoomFloorCombo = nullptr;
	// True while the floor drop-down is being set from the controller, so that does not count as the user using it.
	bool bRefreshingReveal = false;
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
