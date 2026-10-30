#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileRoutePageWidget.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UMobileSelectWidget;
class UTextBlock;
class UVerticalBox;
class UWrapBox;

// The bottom sheet's "Your route" page, as in the prototype: From (My location, or a place), and for My location which building and level the
// person is on, To, the minutes and metres, which ways between levels the person will take (Lift, Stairs, Escalator, or the lift alone), the
// numbered steps, and Start navigation pinned at the bottom. It shows the request and the route it is given and reports any change the person
// makes as a new request; the screen finds the route again and gives the page the result.
UCLASS()
class INNAVI_API UMobileRoutePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The places and buildings the drop-downs offer.
	void SetChoices(const TArray<FMobilePlace>& InPlaces, const TArray<FMobileBuilding>& InBuildings);
	// What the page shows: the request (where from and to, the ways between levels) and the route found for it.
	void SetRequest(const FMobileRouteRequest& InRequest, const FMobileRoute& InRoute);

	void SetOnBack(TFunction<void()> InCallback) { OnBack = MoveTemp(InCallback); }
	void SetOnRequestChanged(TFunction<void(const FMobileRouteRequest&)> InCallback) { OnRequestChanged = MoveTemp(InCallback); }
	void SetOnStart(TFunction<void()> InCallback) { OnStart = MoveTemp(InCallback); }

	// What the person does: a tap on a way-between-levels chip ("lift", "stairs", "escalator", "liftonly"), and a choice in each drop-down
	// (the From and To values are place ids; From's empty value is My location; the level is its number).
	void TapOption(const FString& Option);
	void ChooseFrom(const FString& PlaceId);
	void ChooseTo(const FString& PlaceId);
	void ChooseMyBuilding(const FString& BuildingId);
	void ChooseMyLevel(int32 Level);

	const FMobileRouteRequest& GetRequest() const { return Request; }
	const FMobileRoutePreferences& GetPreferences() const { return Request.Preferences; }
	const FMobileRoute& GetRoute() const { return Route; }
	int32 GetShownStepCount() const { return ShownSteps; }
	UWidget* FindNamedWidget(const FName& Name) const;

	// What is on the page now (the drop-downs and the numbers live in widgets of their own, so a test cannot look them up by name).
	UMobileSelectWidget* GetFromSelect() const { return FromSelect; }
	UMobileSelectWidget* GetToSelect() const { return ToSelect; }
	UMobileSelectWidget* GetBuildingSelect() const { return BuildingSelect; }
	UMobileSelectWidget* GetLevelSelect() const { return LevelSelect; }
	bool IsMyLocationRowShown() const;
	FString GetMinutesString() const;
	FString GetMetresString() const;
	FString GetErrorString() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleBackClicked();
	UFUNCTION()
	void HandleStartClicked();

	void Refresh();
	void RefreshSelects();
	void RefreshOptions();
	void RefreshSteps();
	void CloseOtherSelects(const UMobileSelectWidget* Keep);
	void Changed();

	UPROPERTY()
	UMobileSelectWidget* FromSelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* ToSelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* BuildingSelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* LevelSelect = nullptr;
	UPROPERTY()
	UHorizontalBox* MyLocationRow = nullptr;
	UPROPERTY()
	UTextBlock* MinutesText = nullptr;
	UPROPERTY()
	UTextBlock* MetresText = nullptr;
	UPROPERTY()
	UTextBlock* ErrorText = nullptr;
	UPROPERTY()
	UWidget* StatsBox = nullptr;
	UPROPERTY()
	UWrapBox* OptionBox = nullptr;
	UPROPERTY()
	UVerticalBox* StepBox = nullptr;
	UPROPERTY()
	UButton* StartButton = nullptr;

	TArray<FMobilePlace> Places;
	TArray<FMobileBuilding> Buildings;
	FMobileRouteRequest Request;
	FMobileRoute Route;
	int32 ShownSteps = 0;

	TFunction<void()> OnBack;
	TFunction<void(const FMobileRouteRequest&)> OnRequestChanged;
	TFunction<void()> OnStart;
};
