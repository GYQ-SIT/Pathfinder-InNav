#if WITH_DEV_AUTOMATION_TESTS

#include "StartScreenWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

// The two cards of the start screen carry the design team's pictures (New project: a folder with a plus; Open project: an open folder) once their textures
// are imported (Content/ImageAssets/Icon_NewProject, Icon_OpenProject), and keep their symbols until then.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStartScreenCardIconsTest, "InNavi.UI.StartScreen.CardIcons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStartScreenCardIconsTest::RunTest(const FString& Parameters)
{
	UStartScreenWidget* Start = NewObject<UStartScreenWidget>();
	Start->Initialize();
	const TSharedRef<SWidget> Slate = Start->TakeWidget();
	struct FCard { const TCHAR* Widget; const TCHAR* Asset; };
	for (const FCard& Card : { FCard{ TEXT("Btn_NewIcon"), TEXT("/Game/ImageAssets/Icon_NewProject.Icon_NewProject") }, FCard{ TEXT("Btn_OpenIcon"), TEXT("/Game/ImageAssets/Icon_OpenProject.Icon_OpenProject") } })
	{
		UWidget* Icon = Start->FindNamedWidgetForTests(Card.Widget);
		if (!TestNotNull(*FString::Printf(TEXT("%s is on the card"), Card.Widget), Icon)) continue;
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, Card.Asset, nullptr, LOAD_Quiet | LOAD_NoWarn);
		const bool bImported = Texture != nullptr;
		if (bImported)
		{
			TestTrue(*FString::Printf(TEXT("%s is the picture when its texture is imported"), Card.Widget), Cast<UImage>(Icon) != nullptr);
			// An icon with transparent edges has to be uncompressed and without mips, or its edges turn into a solid square.
			TestTrue(*FString::Printf(TEXT("%s is Uncompressed (RGBA8)"), Card.Asset), Texture->CompressionSettings == TC_EditorIcon);
#if WITH_EDITORONLY_DATA
			// (The import settings exist only in the editor; the Android build compiles this test too.)
			TestTrue(*FString::Printf(TEXT("%s has no mipmaps"), Card.Asset), Texture->MipGenSettings == TMGS_NoMipmaps);
#endif
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("%s is the symbol until its texture is imported"), Card.Widget), Cast<UTextBlock>(Icon) != nullptr);
			AddInfo(FString::Printf(TEXT("%s is not imported yet: the card shows its symbol."), Card.Asset));
		}
	}
	return true;
}

#endif
