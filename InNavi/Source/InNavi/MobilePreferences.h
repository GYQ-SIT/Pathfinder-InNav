#pragma once

#include "CoreMinimal.h"

enum class EMobileTheme : uint8 { Light, Night };

// What the person chose on the Profile tab (the look of the app), and a small file to keep it in so it is there the next time the app opens.
// The app's own state, not the site's data. It works without a file (then nothing is kept), so tests and the first run touch no files.
class INNAVI_API FMobilePreferences
{
public:
	EMobileTheme GetTheme() const { return Theme; }
	bool IsLargerText() const { return bLargerText; }
	bool SetTheme(EMobileTheme InTheme);
	bool SetLargerText(bool bInLargerText);
	// Back to the app's own choices (light, normal text).
	bool Reset();

	// Keeps the choices in this file from now on and reads what is in it (a missing or damaged file is the defaults). Every change is written.
	void UseFile(const FString& InPath);
	bool Load();
	bool Save() const;
	const FString& GetFile() const { return Path; }

	// Where the app keeps it.
	static FString DefaultFile();

private:
	bool Apply(EMobileTheme InTheme, bool bInLargerText);

	EMobileTheme Theme = EMobileTheme::Light;
	bool bLargerText = false;
	FString Path;
};
