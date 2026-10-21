#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileTapTarget.generated.h"

class UButton;
class UWidget;

// Something on a phone page that can be tapped: a row of a list, a filter chip. A button's click carries no information, so each of these
// carries its own (a place's id, a filter's name) and hands it to whoever made it.
UCLASS()
class INNAVI_API UMobileTapTarget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Content is what the tap target shows; Fill is the button's colour when it is up (transparent for a plain row).
	void Setup(UWidget* Content, const FString& InPayload, const FLinearColor& Fill, float CornerRadius);

	void SetOnTapped(TFunction<void(const FString&)> InOnTapped) { OnTapped = MoveTemp(InOnTapped); }
	const FString& GetPayload() const { return Payload; }

	// What a tap does; tests call it.
	void Tap();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleClicked();

	UPROPERTY()
	UButton* Button = nullptr;

	FString Payload;
	TFunction<void(const FString&)> OnTapped;
};
