#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MapBuildingRowWidget.generated.h"

class UButton;

// One building in the Map panel's list: a small building mark, its name, how many floors it has and a chevron. The row of
// the selected building is highlighted in blue. The row does nothing itself - the panel sets what clicking it does.
UCLASS()
class INNAVI_API UMapBuildingRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Has to be called before the row is shown.
	void SetItem(const FString& InBuildingId, const FString& InName, int32 InFloors, bool bInSelected)
	{
		BuildingId = InBuildingId;
		Name = InName;
		Floors = InFloors;
		bSelected = bInSelected;
	}
	const FString& GetBuildingId() const { return BuildingId; }
	bool IsSelectedForTests() const { return bSelected; }

	TFunction<void(const FString& BuildingId)> OnClicked;

	// What a click does (tests).
	void ClickForTests() { HandleClicked(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleClicked();
	UFUNCTION()
	void PlayButtonClickSound();

	FString BuildingId;
	FString Name;
	int32 Floors = 0;
	bool bSelected = false;

	UPROPERTY()
	UButton* RowButton = nullptr;
};
