#if WITH_DEV_AUTOMATION_TESTS

#include "EditorIcons.h"
#include "ProjectCardWidget.h"
#include "ProjectLibraryWidget.h"
#include "StartScreenStyle.h"
#include "UiTestFixture.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// The design team's icons on the start screen and in the project library (Content/ImageAssets/Icon_*): the textures are set up so their transparent
// edges stay clean, the icons are the size the screen asks for, and what is put next to them still fits the card.

namespace
{
	const TCHAR* const EditorIconAssets[] = { TEXT("Icon_NewProject"), TEXT("Icon_OpenProject"), TEXT("Icon_Back"), TEXT("Icon_Search"), TEXT("Icon_Rename"), TEXT("Icon_Duplicate"), TEXT("Icon_Delete") };
}

// An icon with transparent edges has to be uncompressed and without mips, or its edges turn into a solid square.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorIconTexturesTest, "InNavi.UI.EditorIcons.Textures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorIconTexturesTest::RunTest(const FString& Parameters)
{
	int32 Imported = 0;
	for (const TCHAR* Asset : EditorIconAssets)
	{
		UTexture2D* Texture = EditorIcons::Load(Asset);
		if (!Texture)
		{
			AddInfo(FString::Printf(TEXT("%s is not imported yet."), Asset));
			continue;
		}
		++Imported;
		TestTrue(*FString::Printf(TEXT("%s is Uncompressed (RGBA8)"), Asset), Texture->CompressionSettings == TC_EditorIcon);
		TestTrue(*FString::Printf(TEXT("%s has no mipmaps"), Asset), Texture->MipGenSettings == TMGS_NoMipmaps);
		TestTrue(*FString::Printf(TEXT("%s is 128 x 128"), Asset), Texture->Source.GetSizeX() == 128 && Texture->Source.GetSizeY() == 128);
	}
	AddInfo(FString::Printf(TEXT("%d of %d editor icons are imported."), Imported, UE_ARRAY_COUNT(EditorIconAssets)));
	return true;
}

// A picture is as big as its screen says (the box around it), and the card's buttons, with their icons and words, fit the width of the card.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryIconSizeTest, "InNavi.UI.ProjectLibrary.IconSizeAndFit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryIconSizeTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Folder, true);
	Controller->SetProjectsDirectoryOverrideForTests(Folder);
	FFileHelper::SaveStringToFile(TEXT("{\"projectName\":\"Alpha\",\"buildings\":[{}]}"), *(Folder / TEXT("alpha.json")));
	UProjectLibraryWidget* Library = NewObject<UProjectLibraryWidget>();
	Library->Initialize();
	const TSharedRef<SWidget> Slate = Library->TakeWidget();
	Library->SetOwningPlayer(Controller);
	Library->Refresh();
	if (!TestEqual(TEXT("one card"), Library->GetCardsForTests().Num(), 1)) return false;
	UProjectCardWidget* Card = Library->GetCardsForTests()[0];
	const TSharedRef<SWidget> CardSlate = Card->TakeWidget();
	CardSlate->SlatePrepass(1.f);

	// Every picture the screens made is in a box of exactly the size the screen gave it.
	for (UWidget* Widget : { Card->WidgetTree->FindWidget(TEXT("Btn_OpenProjectIconSize")), Card->WidgetTree->FindWidget(TEXT("Btn_DuplicateProjectIconSize")), Card->WidgetTree->FindWidget(TEXT("Btn_RenameProjectIconSize")) })
	{
		if (!Widget) continue; // the texture is not imported: the button has its words only
		const USizeBox* Box = Cast<USizeBox>(Widget);
		TestTrue(TEXT("a button's icon is in a box of its own size"), Box && FMath::IsNearlyEqual(Box->GetWidthOverride(), 13.f * StartStyle::DesignToUi) && FMath::IsNearlyEqual(Box->GetHeightOverride(), 13.f * StartStyle::DesignToUi));
	}

	// The row of buttons is no wider than what the card has for it (the card's width less the padding of the body).
	if (UWidget* Row = Card->WidgetTree->FindWidget(TEXT("CardButtons")))
	{
		const float Available = (378.f - 40.f) * StartStyle::DesignToUi;
		TestTrue(*FString::Printf(TEXT("the buttons fit the card (%.0f of %.0f)"), Row->GetCachedWidget()->GetDesiredSize().X, Available), Row->GetCachedWidget()->GetDesiredSize().X <= Available);
	}

	// The card is laid out 378 wide and drawn CardScale of that.
	TestTrue(TEXT("the card is drawn smaller than it is laid out"), FMath::IsNearlyEqual(CardSlate->GetDesiredSize().X, 378.f * StartStyle::DesignToUi * UProjectCardWidget::CardScale, 0.5f));

	// The page is drawn PageScale bigger than it is laid out, so that its Back button comes out as big as the start screen's Quit button (120 x 44).
	const USizeBox* Back = Cast<USizeBox>(Library->WidgetTree->FindWidget(TEXT("Btn_LibraryBackSize")));
	if (TestNotNull(TEXT("the Back button's box"), Back))
	{
		TestTrue(TEXT("Back is as wide as Quit once the page is scaled"), FMath::IsNearlyEqual(Back->GetMinDesiredWidth() * UProjectLibraryWidget::PageScale, 120.f * StartStyle::DesignToUi, 0.1f));
		TestTrue(TEXT("and as high"), FMath::IsNearlyEqual(Back->GetHeightOverride() * UProjectLibraryWidget::PageScale, 44.f * StartStyle::DesignToUi, 0.1f));
	}
	// The scale box is as big as the page inside it, scaled (so the scroll box under it gets the right height).
	{
		Slate->SlatePrepass(1.f);
		const UWidget* Scaler = Library->WidgetTree->FindWidget(TEXT("PageScale"));
		const UWidget* Inner = Library->WidgetTree->FindWidget(TEXT("PageInner"));
		if (Scaler && Inner && Scaler->GetCachedWidget().IsValid() && Inner->GetCachedWidget().IsValid())
		{
			const FVector2D ScalerSize = Scaler->GetCachedWidget()->GetDesiredSize();
			const FVector2D InnerSize = Inner->GetCachedWidget()->GetDesiredSize();
			AddInfo(FString::Printf(TEXT("page %s, scaled %s"), *InnerSize.ToString(), *ScalerSize.ToString()));
			TestTrue(TEXT("the scale box is the page's size times the scale"), FMath::IsNearlyEqual(ScalerSize.Y, InnerSize.Y * UProjectLibraryWidget::PageScale, 1.0f));
		}
	}
	TestTrue(TEXT("the page's text is the size of the start screen's buttons (12 laid out, 17 drawn)"), FMath::IsNearlyEqual(12.f * UProjectLibraryWidget::PageScale, 17.f, 0.01f));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif
