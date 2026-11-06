#include "StartScreenWidget.h"

#include "EditScreenWidget.h"
#include "NavPlayerController.h"
#include "BrandTopBarWidget.h"
#include "NewProjectDialogWidget.h"
#include "ProjectLibraryWidget.h"
#include "TimerManager.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"

namespace
{
	// The design team's start screen is drawn in browser pixels; the project's UI scale is 0.77 (see
	// DefaultEngine.ini), so sizes are divided by it to come out the same on screen.
	constexpr float DesignToUi = 1.f / 0.77f;

	// Slate font sizes are in points, drawn at 96 DPI (4/3 of a pixel each), while the design's sizes are pixels.
	constexpr float PixelsToFontSize = 0.75f;

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}

	// A rounded, outlined fill for one state of a card button.
	FSlateBrush CardBrush(const FLinearColor& Fill, const FLinearColor& Outline)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(11.f * DesignToUi, 11.f * DesignToUi, 11.f * DesignToUi, 11.f * DesignToUi);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = 1.f;
		return Brush;
	}


	// A small RGBA texture filled by Fill(x, y) -> FColor (sRGB bytes).
	template <typename TFill>
	UTexture2D* MakeTexture(int32 Width, int32 Height, TextureFilter Filter, TextureAddress Address, TFill Fill)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture || !Texture->GetPlatformData())
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = Filter;
		Texture->AddressX = Address;
		Texture->AddressY = Address;
		Texture->NeverStream = true;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor Color = Fill(X, Y);
				uint8* Pixel = Data + (Y * Width + X) * 4;
				Pixel[0] = Color.B;
				Pixel[1] = Color.G;
				Pixel[2] = Color.R;
				Pixel[3] = Color.A;
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	// The page's background: a 130 degree gradient from #102033 to #173253 with a soft glow (#254C76) around 85% / 38%.
	// Interpolated in sRGB like a browser does. The picture is stretched over the screen, so a small one is enough.
	FColor BackgroundGradientPixel(int32 X, int32 Y, int32 Width, int32 Height)
	{
		const double W = Width;
		const double H = Height;
		const double Angle = FMath::DegreesToRadians(130.0);
		const double Dx = FMath::Sin(Angle);
		const double Dy = -FMath::Cos(Angle);
		const double Length = FMath::Abs(W * Dx) + FMath::Abs(H * Dy);
		const double T = FMath::Clamp((((X + 0.5) - W * 0.5) * Dx + ((Y + 0.5) - H * 0.5) * Dy) / Length + 0.5, 0.0, 1.0);

		// Stops: 0% (16,32,51), 49% (16,32,52), 100% (23,50,83).
		double Base[3];
		if (T < 0.49)
		{
			const double K = T / 0.49;
			const double From[3] = { 16, 32, 51 }, To[3] = { 16, 32, 52 };
			for (int32 I = 0; I < 3; ++I) { Base[I] = From[I] + (To[I] - From[I]) * K; }
		}
		else
		{
			const double K = (T - 0.49) / 0.51;
			const double From[3] = { 16, 32, 52 }, To[3] = { 23, 50, 83 };
			for (int32 I = 0; I < 3; ++I) { Base[I] = From[I] + (To[I] - From[I]) * K; }
		}

		// Glow: an ellipse through the farthest corner, fading out at 52% of it.
		const double Cx = W * 0.85, Cy = H * 0.38;
		const double Rx = FMath::Sqrt(2.0) * FMath::Max(Cx, W - Cx);
		const double Ry = FMath::Sqrt(2.0) * FMath::Max(Cy, H - Cy);
		const double Ex = ((X + 0.5) - Cx) / Rx, Ey = ((Y + 0.5) - Cy) / Ry;
		const double Alpha = FMath::Clamp(1.0 - FMath::Sqrt(Ex * Ex + Ey * Ey) / 0.52, 0.0, 1.0);
		const double Glow[3] = { 37, 76, 118 };
		uint8 Rgb[3];
		for (int32 I = 0; I < 3; ++I) { Rgb[I] = static_cast<uint8>(FMath::RoundToInt(Base[I] * (1.0 - Alpha) + Glow[I] * Alpha)); }
		return FColor(Rgb[0], Rgb[1], Rgb[2], 255);
	}


	// The page's grid: 36px cells of light 1px lines, laid on the floor - the page tilts it back 55 degrees with a
	// 700px perspective and 1.6x scale around a point at 75% / 65% of the area under the top bar, so the lines close
	// in towards a horizon and only the lower part of the screen shows them. Each texture pixel is traced back from
	// the screen onto the flat grid (the transform is a plain perspective, so it inverts exactly). On top of that the
	// grid is faint on the left, where the text is, and stronger on the right.
	FColor GridFloorPixel(int32 X, int32 Y, int32 Width, int32 Height)
	{
		constexpr double PageWidth = 1920.0;    // the design's reference screen, in its pixels
		constexpr double PageHeight = 1080.0;
		constexpr double TopBar = 64.0;
		constexpr double Cell = 36.0;
		constexpr double AreaHeight = PageHeight - TopBar;
		constexpr double Scale = 1.6;
		constexpr double Cos = 0.573576436;     // cos / sin of 55 degrees
		constexpr double Sin = 0.819152044;
		constexpr double K = Scale * Sin / 700.0;   // how fast the perspective divide grows with depth
		constexpr double Tilt = Scale * Cos;
		const double OriginX = 0.75 * PageWidth;
		const double OriginY = TopBar + 0.65 * AreaHeight;
		const double Step = PageWidth / Width;  // one texture pixel in design pixels

		double Covered = 0.0;
		constexpr int32 Sub = 3;
		for (int32 Sy = 0; Sy < Sub; ++Sy)
		{
			for (int32 Sx = 0; Sx < Sub; ++Sx)
			{
				const double ScreenX = (X + (Sx + 0.5) / Sub) * Step - OriginX;
				const double ScreenY = (Y + (Sy + 0.5) / Sub) * Step - OriginY;
				const double Denominator = Tilt + K * ScreenY;
				if (Denominator <= 1e-6)
				{
					continue; // above the horizon
				}
				const double V = ScreenY / Denominator;
				const double W = 1.0 - K * V; // perspective divide: 1 - z / 700
				if (W <= 1e-6)
				{
					continue;
				}
				const double U = ScreenX * W / Scale;
				const double GridU = 0.75 * PageWidth + U;
				const double GridV = 0.65 * AreaHeight + V;
				if (GridU < 0.0 || GridU > PageWidth || GridV < 0.0 || GridV > AreaHeight)
				{
					continue; // the grid only exists inside the page's own rectangle
				}
				// A line is 1 grid pixel wide; where one screen pixel covers more than that (far away) widen it and
				// dim it by the same amount so it fades instead of flickering.
				const double FootprintU = Step * W / Scale;
				const double FootprintV = Step * Tilt / (Denominator * Denominator);
				const double WidthU = FMath::Max(1.0, FootprintU);
				const double WidthV = FMath::Max(1.0, FootprintV);
				const double HitU = FMath::Fmod(GridU, Cell) < WidthU ? 1.0 / WidthU : 0.0;
				const double HitV = FMath::Fmod(GridV, Cell) < WidthV ? 1.0 / WidthV : 0.0;
				Covered += FMath::Max(HitU, HitV);
			}
		}
		Covered /= (Sub * Sub);

		const double Across = FMath::Clamp(((X + 0.5) / Width - 0.15) / 0.7, 0.0, 1.0);
		const double Strength = 0.2 + 0.8 * Across * Across * (3.0 - 2.0 * Across); // faint left, strong right
		const double Alpha = FMath::Clamp(Covered * 0.12 * Strength, 0.0, 1.0);
		return FColor(184, 217, 250, static_cast<uint8>(FMath::RoundToInt(Alpha * 255.0)));
	}

	// A rounded box with only an outline, for the logo mark.
	FSlateBrush OutlineBrush(const FLinearColor& Outline, float Radius, float Width)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(FLinearColor::Transparent);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = Width;
		return Brush;
	}
}

UStartScreenWidget::UStartScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UStartScreenWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("StartScreenCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		// Opaque background, filling the whole screen, behind everything else - without this the
		// 3D viewport shows through. #102033, the page's base colour.
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(Srgb(16, 32, 51));
		UCanvasPanelSlot* BackgroundSlot = Canvas->AddChildToCanvas(Background);
		BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackgroundSlot->SetOffsets(FMargin(0.f));

		// The colour gradient and glow of the design team's page, made in code.
		BackgroundGradientTexture = MakeTexture(480, 270, TF_Bilinear, TA_Clamp,
			[](int32 X, int32 Y) { return BackgroundGradientPixel(X, Y, 480, 270); });
		if (BackgroundGradientTexture)
		{
			UImage* Gradient = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_BackgroundGradient"));
			Gradient->SetBrushFromTexture(BackgroundGradientTexture);
			Gradient->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCanvasPanelSlot* GradientSlot = Canvas->AddChildToCanvas(Gradient);
			GradientSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			GradientSlot->SetOffsets(FMargin(0.f));
		}

		// The faint floor grid over it (see GridFloorPixel).
		BackgroundGridTexture = MakeTexture(960, 540, TF_Bilinear, TA_Clamp,
			[](int32 X, int32 Y) { return GridFloorPixel(X, Y, 960, 540); });
		if (BackgroundGridTexture)
		{
			UImage* Grid = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_BackgroundGrid"));
			Grid->SetBrushFromTexture(BackgroundGridTexture);
			Grid->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCanvasPanelSlot* GridSlot = Canvas->AddChildToCanvas(Grid);
			GridSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			GridSlot->SetOffsets(FMargin(0.f));
		}

		// The buildings (Content/ImageAssets/Splash_Buildings: the splash picture with its blue taken out, so they sit
		// on the gradient). 16:9 art with the buildings on the right; scaled to fill the screen with its aspect ratio
		// kept, so a different window shape crops the edges instead of stretching it. If the asset is missing only
		// the gradient shows.
		if (UTexture2D* ArtTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/ImageAssets/Splash_Buildings.Splash_Buildings")))
		{
			UScaleBox* SplashScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("Scale_SplashBackground"));
			SplashScale->SetStretch(EStretch::ScaleToFill);
			SplashScale->SetVisibility(ESlateVisibility::HitTestInvisible);
			// A little larger than the page's own framing, grown from the right edge so the buildings stay against it
			// and the extra is cropped off the top right like the page does.
			SplashScale->SetRenderTransformPivot(FVector2D(1.f, 0.45f));
			SplashScale->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, FVector2D(1.2f, 1.2f), FVector2D::ZeroVector, 0.f));
			UImage* SplashImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_SplashBackground"));
			SplashImage->SetBrushFromTexture(ArtTexture, true);
			SplashScale->SetContent(SplashImage);
			UCanvasPanelSlot* SplashSlot = Canvas->AddChildToCanvas(SplashScale);
			SplashSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			SplashSlot->SetOffsets(FMargin(0.f));
		}

		// The top bar (shared with the project library).
		UBrandTopBarWidget* TopBar = CreateWidget<UBrandTopBarWidget>(this, UBrandTopBarWidget::StaticClass());
		UCanvasPanelSlot* TopBarSlot = Canvas->AddChildToCanvas(TopBar);
		TopBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		TopBarSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, UBrandTopBarWidget::DesignHeight()));

		auto MakeText = [this](const FString& Name, const TCHAR* Value, float Size, const FLinearColor& Color, bool bBold, int32 Spacing = 0) -> UTextBlock*
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
			Text->SetText(FText::FromString(FString(Value)));
			Text->SetColorAndOpacity(FSlateColor(Color));
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = FMath::RoundToInt(Size * PixelsToFontSize);
			Font.LetterSpacing = Spacing;
			if (bBold)
			{
				Font.TypefaceFontName = TEXT("Bold");
			}
			Text->SetFont(Font);
			return Text;
		};

		// Everything on the left, top to bottom: the heading block, then the two launch cards, then a plain Quit.
		// Starts at the same height as the design's heading and is placed from the top-left, so the block follows the
		// bar above it and not the window height.
		UVerticalBox* ButtonStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonStack"));
		UCanvasPanelSlot* StackSlot = Canvas->AddChildToCanvas(ButtonStack);
		StackSlot->SetAnchors(FAnchors(0.f, 0.f));
		StackSlot->SetAlignment(FVector2D(0.f, 0.f));
		StackSlot->SetPosition(FVector2D(72.f * DesignToUi, 143.f * DesignToUi));
		StackSlot->SetAutoSize(true);

		const FLinearColor White = Srgb(250, 252, 255);
		const FLinearColor Blue = Srgb(132, 194, 255);

		// Small line above the logo: "TEAM PATHFINDER . INDOOR NAVIGATION EDITOR".
		UHorizontalBox* Kicker = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Kicker"));
		const FLinearColor KickerBlue = Srgb(136, 196, 255);
		Kicker->AddChildToHorizontalBox(MakeText(TEXT("Txt_KickerTeam"), TEXT("TEAM PATHFINDER"), 11.f * DesignToUi, KickerBlue, true, 180));
		Kicker->AddChildToHorizontalBox(MakeText(TEXT("Txt_KickerDot"), TEXT("\u2022"), 11.f * DesignToUi, Srgb(88, 120, 160), true))
			->SetPadding(FMargin(10.f * DesignToUi, 0.f, 10.f * DesignToUi, 0.f));
		Kicker->AddChildToHorizontalBox(MakeText(TEXT("Txt_KickerProduct"), TEXT("INDOOR NAVIGATION EDITOR"), 11.f * DesignToUi, KickerBlue, true, 180));
		ButtonStack->AddChildToVerticalBox(Kicker)->SetPadding(FMargin(0.f, 0.f, 0.f, 36.f * DesignToUi));

		// Logo mark (a blue outlined square with a two-column symbol in it) beside "InNav" and the product name.
		UHorizontalBox* LogoRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LogoRow"));
		const FLinearColor LogoBlue = Srgb(52, 142, 255);
		USizeBox* MarkSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LogoMarkSize"));
		MarkSize->SetWidthOverride(72.f * DesignToUi);
		MarkSize->SetHeightOverride(72.f * DesignToUi);
		UBorder* MarkFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LogoMark"));
		MarkFrame->SetBrush(OutlineBrush(LogoBlue, 14.f * DesignToUi, 2.f));
		MarkFrame->SetBrushColor(FLinearColor::White);
		MarkFrame->SetHorizontalAlignment(HAlign_Center);
		MarkFrame->SetVerticalAlignment(VAlign_Center);
		USizeBox* SymbolSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LogoSymbolSize"));
		SymbolSize->SetWidthOverride(32.f * DesignToUi);
		SymbolSize->SetHeightOverride(32.f * DesignToUi);
		UOverlay* Symbol = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LogoSymbol"));
		UBorder* SymbolBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LogoSymbolBox"));
		SymbolBox->SetBrush(OutlineBrush(LogoBlue, 3.f * DesignToUi, 3.f));
		SymbolBox->SetBrushColor(FLinearColor::White);
		UOverlaySlot* SymbolBoxSlot = Symbol->AddChildToOverlay(SymbolBox);
		SymbolBoxSlot->SetHorizontalAlignment(HAlign_Fill);
		SymbolBoxSlot->SetVerticalAlignment(VAlign_Fill);
		USizeBox* SymbolLineSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LogoSymbolLineSize"));
		SymbolLineSize->SetWidthOverride(3.f * DesignToUi);
		UBorder* SymbolLine = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LogoSymbolLine"));
		SymbolLine->SetBrushColor(LogoBlue);
		SymbolLineSize->SetContent(SymbolLine);
		UOverlaySlot* SymbolLineSlot = Symbol->AddChildToOverlay(SymbolLineSize);
		SymbolLineSlot->SetHorizontalAlignment(HAlign_Center);
		SymbolLineSlot->SetVerticalAlignment(VAlign_Fill);
		SymbolSize->SetContent(Symbol);
		MarkFrame->SetContent(SymbolSize);
		MarkSize->SetContent(MarkFrame);
		LogoRow->AddChildToHorizontalBox(MarkSize)->SetVerticalAlignment(VAlign_Center);

		UVerticalBox* NameBlock = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NameBlock"));
		UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NameRow"));
		NameRow->AddChildToHorizontalBox(MakeText(TEXT("Txt_NameIn"), TEXT("In"), 58.f * DesignToUi, White, true, -20));
		NameRow->AddChildToHorizontalBox(MakeText(TEXT("Txt_NameNav"), TEXT("Nav"), 58.f * DesignToUi, Srgb(92, 172, 255), true, -20));
		NameBlock->AddChildToVerticalBox(NameRow);
		NameBlock->AddChildToVerticalBox(MakeText(TEXT("Txt_ProductName"), TEXT("Indoor Navigation Editor"), 20.f * DesignToUi, Srgb(226, 238, 252), false));
		UHorizontalBoxSlot* NameSlot = LogoRow->AddChildToHorizontalBox(NameBlock);
		NameSlot->SetVerticalAlignment(VAlign_Center);
		NameSlot->SetPadding(FMargin(18.f * DesignToUi, 0.f, 0.f, 0.f));
		ButtonStack->AddChildToVerticalBox(LogoRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 34.f * DesignToUi));

		// The two-line headline and the paragraph under it.
		ButtonStack->AddChildToVerticalBox(MakeText(TEXT("Txt_HeadlineBuild"), TEXT("Build spaces."), 52.f * DesignToUi, White, true, -30));
		ButtonStack->AddChildToVerticalBox(MakeText(TEXT("Txt_HeadlineConnect"), TEXT("Connect destinations."), 52.f * DesignToUi, Blue, true, -30))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f * DesignToUi));
		UTextBlock* Intro = MakeText(TEXT("Txt_Intro"),
			TEXT("Your workspace to create, configure and validate indoor navigation maps \u2014 from the building footprint to the final route."),
			16.f * DesignToUi, Srgb(200, 216, 234), false);
		Intro->SetAutoWrapText(true);
		Intro->SetLineHeightPercentage(1.5f);
		USizeBox* IntroSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Txt_IntroSize"));
		IntroSize->SetWidthOverride(700.f * DesignToUi);
		IntroSize->SetContent(Intro);
		ButtonStack->AddChildToVerticalBox(IntroSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 34.f * DesignToUi));

		UHorizontalBox* CardRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardRow"));
		ButtonStack->AddChildToVerticalBox(CardRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f * DesignToUi));

		// One card: icon top-left, arrow top-right, title and caption bottom-left. Colours and sizes come from the
		// design team's page. The browser draws a gradient across each card; a button brush is a single colour, so
		// the middle of each gradient is used, with the two ends for hover and press.
		auto AddCard = [this, CardRow, &MakeText](const TCHAR* Name, const TCHAR* Icon, const TCHAR* TitleText, const TCHAR* Caption,
			const FLinearColor& Normal, const FLinearColor& Hover, const FLinearColor& Pressed,
			const FLinearColor& Outline, const FLinearColor& OutlineHover, bool bFirst) -> UButton*
		{
			const FString Base(Name);
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			FButtonStyle Style;
			Style.Normal = CardBrush(Normal, Outline);
			Style.Hovered = CardBrush(Hover, OutlineHover);
			Style.Pressed = CardBrush(Pressed, OutlineHover);
			Style.Disabled = CardBrush(Normal * 0.5f, Outline);
			Style.NormalPadding = FMargin(0.f);
			Style.PressedPadding = FMargin(0.f);
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);

			UOverlay* Face = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), FName(Base + TEXT("Face")));
			const FMargin Inset(18.f * DesignToUi, 22.f * DesignToUi, 18.f * DesignToUi, 18.f * DesignToUi);

			UOverlaySlot* IconSlot = Face->AddChildToOverlay(MakeText(Base + TEXT("Icon"), Icon, 26.f * DesignToUi, Srgb(231, 241, 255), false));
			IconSlot->SetHorizontalAlignment(HAlign_Left);
			IconSlot->SetVerticalAlignment(VAlign_Top);
			IconSlot->SetPadding(Inset);

			UOverlaySlot* ArrowSlot = Face->AddChildToOverlay(MakeText(Base + TEXT("Arrow"), TEXT("\u2197"), 16.f * DesignToUi, Srgb(175, 212, 255), false));
			ArrowSlot->SetHorizontalAlignment(HAlign_Right);
			ArrowSlot->SetVerticalAlignment(VAlign_Top);
			ArrowSlot->SetPadding(FMargin(Inset.Left, 17.f * DesignToUi, 16.f * DesignToUi, Inset.Bottom));

			UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(Base + TEXT("Texts")));
			Texts->AddChildToVerticalBox(MakeText(Base + TEXT("Title"), TitleText, 15.f * DesignToUi, FLinearColor::White, true));
			Texts->AddChildToVerticalBox(MakeText(Base + TEXT("Caption"), Caption, 12.f * DesignToUi, Srgb(205, 224, 241), false))
				->SetPadding(FMargin(0.f, 3.f * DesignToUi, 0.f, 0.f));
			UOverlaySlot* TextsSlot = Face->AddChildToOverlay(Texts);
			TextsSlot->SetHorizontalAlignment(HAlign_Left);
			TextsSlot->SetVerticalAlignment(VAlign_Bottom);
			TextsSlot->SetPadding(Inset);

			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Base + TEXT("Size")));
			SizeBox->SetWidthOverride(250.f * DesignToUi);
			SizeBox->SetHeightOverride(150.f * DesignToUi);
			SizeBox->SetContent(Face);
			Button->SetContent(SizeBox);

			UHorizontalBoxSlot* Slot = CardRow->AddChildToHorizontalBox(Button);
			Slot->SetPadding(FMargin(bFirst ? 0.f : 13.f * DesignToUi, 0.f, 0.f, 0.f));
			return Button;
		};

		// New project: the blue card; Open project: the slate one.
		NewButton = AddCard(TEXT("Btn_New"), TEXT("+"), TEXT("New project"), TEXT("Start with a building"),
			Srgb(11, 105, 231), Srgb(21, 124, 255), Srgb(0, 87, 207), Srgb(74, 158, 255), Srgb(120, 184, 255), true);
		OpenButton = AddCard(TEXT("Btn_Open"), TEXT("\u25A4"), TEXT("Open project"), TEXT("Continue editing"),
			Srgb(30, 53, 78), Srgb(42, 70, 101), Srgb(27, 48, 72), Srgb(61, 86, 116), Srgb(90, 120, 155), false);

		// Quit has no card in the design (a web page has no need for it); it stays as a quiet text button because a
		// packaged build has no other way out of this screen.
		QuitButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Quit"));
		FButtonStyle QuitStyle;
		// The outline is always drawn; only the fill appears on hover.
		QuitStyle.Normal = CardBrush(FLinearColor::Transparent, Srgb(61, 86, 116));
		QuitStyle.Hovered = CardBrush(Srgb(30, 53, 78), Srgb(61, 86, 116));
		QuitStyle.Pressed = CardBrush(Srgb(27, 48, 72), Srgb(61, 86, 116));
		QuitStyle.NormalPadding = FMargin(0.f);
		QuitStyle.PressedPadding = FMargin(0.f);
		QuitButton->SetStyle(QuitStyle);
		QuitButton->SetBackgroundColor(FLinearColor::White);
		UTextBlock* QuitText = MakeText(TEXT("Btn_QuitLabel"), TEXT("Quit"), 17.f * DesignToUi, Srgb(205, 224, 241), true);
		QuitText->SetJustification(ETextJustify::Center);
		// The label is the button's own content (centred by the button); the size box goes around the button, so the
		// button is a fixed size and not as wide as the column.
		QuitButton->SetContent(QuitText);
		USizeBox* QuitSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_QuitSize"));
		QuitSize->SetWidthOverride(120.f * DesignToUi);
		QuitSize->SetHeightOverride(44.f * DesignToUi);
		QuitSize->SetContent(QuitButton);
		ButtonStack->AddChildToVerticalBox(QuitSize)->SetHorizontalAlignment(HAlign_Left);
	}

	return Super::RebuildWidget();
}

void UStartScreenWidget::BlockClicksToTheScene()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetPointerOverUICallback(this, [](const FVector2D&) { return true; });
	}
}

void UStartScreenWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ClearPointerOverUICallback(this);
	}
	Super::NativeDestruct();
}

void UStartScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BlockClicksToTheScene();

	if (NewButton)
	{
		NewButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnNewClicked);
		NewButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::PlayButtonClickSound);
	}
	if (OpenButton)
	{
		OpenButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnOpenClicked);
		OpenButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::PlayButtonClickSound);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::PlayButtonClickSound);
		QuitButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnQuitClicked);
	}
}

void UStartScreenWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UStartScreenWidget::OnNewClicked()
{
	// Ask for a project name first; the project file is created when the dialog is confirmed.
	TWeakObjectPtr<UStartScreenWidget> WeakThis(this);
	UNewProjectDialogWidget::OpenNewProject(GetOwningPlayer<ANavPlayerController>(), [WeakThis]()
	{
		if (UStartScreenWidget* Screen = WeakThis.Get())
		{
			Screen->GoToEditScreen();
		}
	});
}

void UStartScreenWidget::OnOpenClicked()
{
	// The library lists the Data folder itself; there is no file dialog any more.
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	if (UProjectLibraryWidget* Library = CreateWidget<UProjectLibraryWidget>(PC, UProjectLibraryWidget::StaticClass()))
	{
		Library->AddToViewport();
		RemoveFromParent();
	}
}

void UStartScreenWidget::OnQuitClicked()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void UStartScreenWidget::GoToEditScreen()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (UEditScreenWidget* EditScreen = CreateWidget<UEditScreenWidget>(PC, UEditScreenWidget::StaticClass()))
	{
		EditScreen->AddToViewport();
		RemoveFromParent();
	}
}
