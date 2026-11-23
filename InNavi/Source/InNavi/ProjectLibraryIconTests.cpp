#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingInfoPanelWidget.h"
#include "Components/Border.h"
#include "Components/OverlaySlot.h"
#include "Components/ButtonSlot.h"
#include "EditorIcons.h"
#include "MapPanelWidget.h"
#include "Components/ComboBoxString.h"
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
	const TCHAR* const EditorIconAssets[] = { TEXT("Icon_NewProject"), TEXT("Icon_OpenProject"), TEXT("Icon_Back"), TEXT("Icon_Search"), TEXT("Icon_Rename"), TEXT("Icon_Duplicate"), TEXT("Icon_Delete"), TEXT("Icon_ChevronDown"), TEXT("Icon_ChevronRight"), TEXT("Icon_Open2D"), TEXT("Icon_Open3D"), TEXT("Icon_Eye"), TEXT("Icon_EyeOff"), TEXT("Icon_SaveState"),
		TEXT("Icon_Route"), TEXT("Icon_Close"), TEXT("Icon_Pointer"), TEXT("Icon_Warning"), TEXT("Icon_CircleDot"), TEXT("Icon_MapPin"),
		TEXT("Icon_ListChecks"), TEXT("Icon_FileOutput"),
		TEXT("Icon_ZoomIn"), TEXT("Icon_ZoomOut"), TEXT("Icon_Reset"),
		TEXT("Icon_ToolSelect"), TEXT("Icon_ToolDrawRoom"), TEXT("Icon_ToolDrawWall"), TEXT("Icon_ToolRectangle"), TEXT("Icon_ToolTriangle"), TEXT("Icon_ToolCircle"),
		TEXT("Icon_ToolDoor"), TEXT("Icon_ToolStairs"), TEXT("Icon_ToolElevator"), TEXT("Icon_ToolEscalator"),
		TEXT("Icon_Grow"), TEXT("Icon_Shrink"), TEXT("Icon_RotateLeft"), TEXT("Icon_RotateRight"),
		TEXT("Icon_UtilityAed"), TEXT("Icon_UtilityBicycle"), TEXT("Icon_UtilityDropOff"), TEXT("Icon_UtilityOther"),
		TEXT("Icon_RoomClassroom"), TEXT("Icon_RoomLectureHall"), TEXT("Icon_RoomLab"), TEXT("Icon_RoomOffice"), TEXT("Icon_RoomStudyArea"), TEXT("Icon_RoomMeetingRoom"),
		TEXT("Icon_RoomCanteen"), TEXT("Icon_RoomToilet"), TEXT("Icon_RoomLiftLobby"), TEXT("Icon_RoomCorridor"), TEXT("Icon_RoomStorage"), TEXT("Icon_RoomOther"),
		TEXT("Icon_HistoryUndo"), TEXT("Icon_HistoryRedo"), TEXT("Icon_Snap"), TEXT("Icon_Mode2D"), TEXT("Icon_Mode3D"),
		TEXT("Icon_Building"), TEXT("Icon_CircleAlert"), TEXT("Icon_CircleCheck"), TEXT("Icon_Isolate"), TEXT("Icon_Orbit"), TEXT("Icon_Grab") };
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
#if WITH_EDITORONLY_DATA
		// (The import settings and the source picture exist only in the editor; the Android build compiles these tests too.)
		TestTrue(*FString::Printf(TEXT("%s has no mipmaps"), Asset), Texture->MipGenSettings == TMGS_NoMipmaps);
		TestTrue(*FString::Printf(TEXT("%s is 128 x 128"), Asset), Texture->Source.GetSizeX() == 128 && Texture->Source.GetSizeY() == 128);
#endif
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

// The shared controls: every dark drop-down has the design team's arrow, and the Map panel's folds point right while closed and down while open.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorSharedIconsTest, "InNavi.UI.EditorIcons.SharedControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorSharedIconsTest::RunTest(const FString& Parameters)
{
	UTexture2D* Down = EditorIcons::Load(TEXT("Icon_ChevronDown"));
	UTexture2D* Right = EditorIcons::Load(TEXT("Icon_ChevronRight"));
	if (!Down || !Right)
	{
		AddInfo(TEXT("The chevron textures are not imported yet: the drop-downs and the folds keep the engine's arrow and the +/- signs."));
	}

	UComboBoxString* Combo = NewObject<UComboBoxString>();
	StartStyle::DarkCombo(Combo);
	// The arrow is drawn in the drop-down's foreground colour: it has to be a light one.
	const FLinearColor Foreground = Combo->GetForegroundColor().GetSpecifiedColor();
	TestTrue(TEXT("a dark drop-down has a light foreground, so its arrow shows"), Foreground.R > 0.4f && Foreground.G > 0.4f && Foreground.B > 0.4f);
	const FSlateBrush& Arrow = Combo->GetWidgetStyle().ComboButtonStyle.DownArrowImage;
	if (Down)
	{
		TestTrue(TEXT("a dark drop-down has the design team's arrow"), Arrow.GetResourceObject() == Down);
	}
	else
	{
		TestTrue(TEXT("a dark drop-down keeps the engine's arrow until the texture is imported"), Arrow.GetResourceObject() == nullptr || Arrow.GetResourceObject() != Right);
	}

	FNavigationSearchFixture Fixture;
	UMapPanelWidget* Map = NewObject<UMapPanelWidget>();
	Map->Initialize();
	Map->SetShowBuildingFilters(true);
	const TSharedRef<SWidget> Slate = Map->TakeWidget();
	UImage* Sign = Cast<UImage>(Map->WidgetTree->FindWidget(TEXT("FiltersSignArrow")));
	if (Down && Right)
	{
		if (TestNotNull(TEXT("the Filters fold has the arrow"), Sign))
		{
			TestTrue(TEXT("pointing right while closed"), Sign->GetBrush().GetResourceObject() == Right);
			Map->SetSectionOpenForTests(true, true);
			TestTrue(TEXT("and down while open"), Sign->GetBrush().GetResourceObject() == Down);
			Map->SetSectionOpenForTests(true, false);
			TestTrue(TEXT("and right again when closed"), Sign->GetBrush().GetResourceObject() == Right);
		}
		const UTextBlock* Text = Cast<UTextBlock>(Map->WidgetTree->FindWidget(TEXT("FiltersSign")));
		TestTrue(TEXT("the + sign is hidden then"), Text && Text->GetVisibility() == ESlateVisibility::Collapsed);
	}
	else
	{
		TestNull(TEXT("without the textures the fold keeps its + sign"), Sign);
	}
	return true;
}

// The panels' own buttons: the search field's magnifier, the eye on the two see-through buttons (open while on, crossed while off), and the icons of the two
// ways into a building (no arrow after the words once the icon is there).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorPanelButtonsIconsTest, "InNavi.UI.EditorIcons.PanelButtons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorPanelButtonsIconsTest::RunTest(const FString& Parameters)
{
	UTexture2D* Eye = EditorIcons::Load(TEXT("Icon_Eye"));
	UTexture2D* EyeOff = EditorIcons::Load(TEXT("Icon_EyeOff"));
	const bool bAll = Eye && EyeOff && EditorIcons::Load(TEXT("Icon_Search")) && EditorIcons::Load(TEXT("Icon_Open2D")) && EditorIcons::Load(TEXT("Icon_Open3D"));
	if (!bAll)
	{
		AddInfo(TEXT("Some of the panel icons are not imported yet: the panels keep what they had."));
	}

	FNavigationSearchFixture Fixture;
	UMapPanelWidget* Map = NewObject<UMapPanelWidget>();
	Map->Initialize();
	Map->SetShowBuildingFilters(true);
	const TSharedRef<SWidget> MapSlate = Map->TakeWidget();
	UBuildingInfoPanelWidget* Building = NewObject<UBuildingInfoPanelWidget>();
	Building->Initialize();
	const TSharedRef<SWidget> BuildingSlate = Building->TakeWidget();

	const auto Picture = [](UWidgetTree* Tree, const TCHAR* Name) { return Cast<UImage>(Tree->FindWidget(Name)); };
	UImage* SeeThroughEye = Picture(Map->WidgetTree, TEXT("Btn_SeeThroughAllIcon"));
	if (bAll)
	{
		TestNotNull(TEXT("the search field has its magnifier"), Picture(Map->WidgetTree, TEXT("BuildingSearchIcon")));
		// The field follows the panel's width like the list under it (its size box is around the overlay that holds the magnifier, not around the field).
		Map->SetContentWidth(333.f);
		const UWidget* Face = Map->WidgetTree->FindWidget(TEXT("BuildingSearchFace"));
		// and the field fills its overlay (a slot of an overlay is as big as its content, in the corner, unless told to fill)
		const UOverlaySlot* FieldSlot = Cast<UOverlaySlot>(Map->WidgetTree->FindWidget(TEXT("Txt_BuildingSearch"))->Slot);
		TestTrue(TEXT("the search field fills its face"), FieldSlot && FieldSlot->GetHorizontalAlignment() == HAlign_Fill && FieldSlot->GetVerticalAlignment() == VAlign_Fill);
		const USizeBox* FieldBox = Face ? Cast<USizeBox>(Face->GetParent()) : nullptr;
		TestTrue(TEXT("the search field is as wide as the panel's content"), FieldBox && FMath::IsNearlyEqual(FieldBox->GetWidthOverride(), 333.f));
		// The icons of the two ways into a building stand in one column: both buttons are laid out from the left edge.
		for (const TCHAR* PadName : { TEXT("Btn_Open2DEditorPad"), TEXT("Btn_Open3DViewPad") })
		{
			const UBorder* Pad = Cast<UBorder>(Building->WidgetTree->FindWidget(PadName));
			TestTrue(*FString::Printf(TEXT("%s lays out from the left"), PadName), Pad && Pad->GetHorizontalAlignment() == HAlign_Left);
			// A button centres its content unless its slot fills: the pad has to fill the button, or "from the left" means nothing.
			const UButtonSlot* PadSlot = Pad ? Cast<UButtonSlot>(Pad->Slot) : nullptr;
			TestTrue(*FString::Printf(TEXT("%s fills its button"), PadName), PadSlot && PadSlot->GetHorizontalAlignment() == HAlign_Fill);
		}
		if (TestNotNull(TEXT("the See-through button has its eye"), SeeThroughEye))
		{
			const FString Before = Map->GetAllSeeThroughTextForTests();
			TestTrue(TEXT("open while it is on, crossed while it is off"), (Before.EndsWith(TEXT("ON")) ? Eye : EyeOff) == SeeThroughEye->GetBrush().GetResourceObject());
			Map->ClickAllSeeThroughForTests();
			TestTrue(TEXT("and the other way after a click"), (Map->GetAllSeeThroughTextForTests().EndsWith(TEXT("ON")) ? Eye : EyeOff) == SeeThroughEye->GetBrush().GetResourceObject());
		}
		TestNotNull(TEXT("the Building panel's Transparent button has its eye"), Picture(Building->WidgetTree, TEXT("Btn_BuildingTransparentIcon")));
		TestNotNull(TEXT("and Open 2D its icon"), Picture(Building->WidgetTree, TEXT("Btn_Open2DEditorIcon")));
		TestNotNull(TEXT("and Open 3D its icon"), Picture(Building->WidgetTree, TEXT("Btn_Open3DViewIcon")));
		const UTextBlock* Label = Cast<UTextBlock>(Building->WidgetTree->FindWidget(TEXT("Btn_Open2DEditorLabel")));
		TestTrue(TEXT("with no arrow after the words"), Label && !Label->GetText().ToString().Contains(TEXT("→")) && !Label->GetText().ToString().EndsWith(TEXT("->")));
	}
	else
	{
		TestNull(TEXT("without the textures the See-through button has only its words"), SeeThroughEye);
	}
	return true;
}

// The Save button of the editor screens: the design team's save icon, green when the project is saved and yellow while it has unsaved changes; and the Back
// button has the arrow in front of its word.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorBarIconsTest, "InNavi.UI.EditorIcons.MenuBar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorBarIconsTest::RunTest(const FString& Parameters)
{
	const bool bSave = EditorIcons::Load(TEXT("Icon_SaveState")) != nullptr;
	const bool bBack = EditorIcons::Load(TEXT("Icon_Back")) != nullptr;
	if (!bSave || !bBack) AddInfo(TEXT("Icon_SaveState or Icon_Back is not imported yet: the bar keeps the square and the word."));

	UWidgetTree* Tree = NewObject<UWidgetTree>();
	UBorder* Outer = nullptr;
	UBorder* Core = nullptr;
	StartStyle::SaveBarButton(Tree, Outer, Core, 70.f);
	if (TestNotNull(TEXT("the indicator"), Outer))
	{
		StartStyle::PaintSaveIndicator(Outer, Core, false);
		const USizeBox* IconBox = Cast<USizeBox>(Outer->GetContent());
		const UImage* Icon = IconBox ? Cast<UImage>(IconBox->GetContent()) : nullptr;
		if (bSave)
		{
			if (TestNotNull(TEXT("the save icon is the indicator"), Icon))
			{
				TestEqual(TEXT("no ground behind the icon (the border's own fill is clear)"), Outer->Background.TintColor.GetSpecifiedColor().A, 0.f);
				const FLinearColor Saved = Icon->GetColorAndOpacity();
				TestTrue(TEXT("green when saved"), Saved.G > Saved.R && Saved.G > Saved.B);
				StartStyle::PaintSaveIndicator(Outer, Core, true);
				const FLinearColor Unsaved = Icon->GetColorAndOpacity();
				TestTrue(TEXT("yellow while there are unsaved changes"), Unsaved.R > Unsaved.B && Unsaved.G > Unsaved.B);
			}
		}
		else
		{
			TestNull(TEXT("without the texture the indicator is the square"), Icon);
		}
	}
	return true;
}

#endif
