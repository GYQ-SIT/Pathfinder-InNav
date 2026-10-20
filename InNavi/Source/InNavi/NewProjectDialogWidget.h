#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NewProjectDialogWidget.generated.h"

class ANavPlayerController;
class APlayerController;
class UButton;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class UTextBlock;

// The "Create a new project" dialog the start screen's New project card opens: a project name (required) and an
// optional description, over a dimmed backdrop. Pure C++ like the other screens (RebuildWidget builds the layout), in the
// style of the design team's page.
//
// The dialog does not know what creating a project means. Whoever opens it sets OnCreate, which is called with the typed
// name and description when the user presses Create (or Enter in the name field) and returns an empty string when it
// worked - the dialog then closes - or the reason it did not, which is shown under the name and keeps the dialog open.
UCLASS()
class INNAVI_API UNewProjectDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Opens the New project dialog over the current screen. When it is confirmed the project has been created (see
	// ANavPlayerController::CreateNewProject) and OnCreated runs - the caller moves on to the editor.
	static UNewProjectDialogWidget* OpenNewProject(ANavPlayerController* Controller, TFunction<void()> OnCreated);

	// Opens it as "Rename project" for a project called CurrentName. OnRename gets the typed name and returns an empty
	// string when it worked or the reason it did not (shown under the name).
	static UNewProjectDialogWidget* OpenRename(APlayerController* Controller, const FString& CurrentName, TFunction<FString(const FString& NewName)> OnRename);

	// Makes this a rename dialog: only the name, filled in with CurrentName. Has to be called before the dialog is shown.
	void ConfigureRename(const FString& CurrentName);

	TFunction<FString(const FString& Name, const FString& Description)> OnCreate;
	TFunction<void()> OnCancelled;

	// What the Create button does (tests and the Enter key use it too).
	void Submit();
	// What the Cancel button and Escape do.
	void Cancel();

	UEditableTextBox* GetNameBoxForTests() const { return NameBox; }
	UMultiLineEditableTextBox* GetDescriptionBoxForTests() const { return DescriptionBox; }
	FText GetErrorTextForTests() const;
	bool IsErrorShownForTests() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION()
	void HandleCreateClicked();
	UFUNCTION()
	void HandleCancelClicked();
	UFUNCTION()
	void HandleNameChanged(const FText& Text);
	UFUNCTION()
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void PlayButtonClickSound();

	void ShowError(const FString& Message);

	UPROPERTY()
	UEditableTextBox* NameBox = nullptr;
	UPROPERTY()
	UMultiLineEditableTextBox* DescriptionBox = nullptr;
	UPROPERTY()
	UTextBlock* ErrorText = nullptr;
	UPROPERTY()
	UButton* CancelButton = nullptr;
	UPROPERTY()
	UButton* CreateButton = nullptr;

	// The name box takes keyboard focus on the first tick, once the dialog is on screen.
	bool bFocusedName = false;

	bool bRenameMode = false;
	FString InitialName;
};
