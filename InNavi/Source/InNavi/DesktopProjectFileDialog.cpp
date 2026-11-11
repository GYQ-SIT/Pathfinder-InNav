#include "DesktopProjectFileDialog.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Microsoft/COMPointer.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/SlateApplication.h"
#include <shobjidl.h>
#endif

bool DesktopProjectFileDialog::Pick(const void* ParentWindow, const FString& Directory, bool bSave, FString& OutPath, FString& Error)
{
	OutPath.Reset();
	Error.Reset();
#if PLATFORM_WINDOWS
	if (FApp::IsUnattended()) return false;
	const HRESULT Init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(Init) && Init != RPC_E_CHANGED_MODE)
	{
		Error = TEXT("Could not initialize the Windows file dialog.");
		return false;
	}
	ON_SCOPE_EXIT { if (SUCCEEDED(Init)) ::CoUninitialize(); };
	TComPtr<IFileDialog> Dialog;
	HRESULT Result = ::CoCreateInstance(bSave ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		bSave ? IID_IFileSaveDialog : IID_IFileOpenDialog, IID_PPV_ARGS_Helper(&Dialog));
	if (FAILED(Result))
	{
		Error = TEXT("Could not create the Windows file dialog.");
		return false;
	}
	DWORD Options = 0;
	Dialog->GetOptions(&Options);
	Dialog->SetOptions(Options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR
		| (bSave ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
	Dialog->SetTitle(bSave ? TEXT("Save building data") : TEXT("Import building data"));
	const COMDLG_FILTERSPEC Filter = { bSave ? TEXT("JSON files (*.json)") : TEXT("Building data (*.json, *.db)"),
		bSave ? TEXT("*.json") : TEXT("*.json;*.db") };
	Dialog->SetFileTypes(1, &Filter);
	if (bSave)
	{
		Dialog->SetDefaultExtension(TEXT("json"));
		Dialog->SetFileName(TEXT("NewProject.json"));
	}
	TComPtr<IShellItem> Folder;
	FString AbsoluteDirectory = FPaths::ConvertRelativePathToFull(Directory);
	FPaths::MakePlatformFilename(AbsoluteDirectory);
	if (SUCCEEDED(::SHCreateItemFromParsingName(*AbsoluteDirectory, nullptr, IID_PPV_ARGS(&Folder)))) Dialog->SetFolder(Folder);
	const bool bSlate = FSlateApplication::IsInitialized();
	if (bSlate) FSlateApplication::Get().ExternalModalStart();
	ON_SCOPE_EXIT { if (bSlate && FSlateApplication::IsInitialized()) FSlateApplication::Get().ExternalModalStop(); };
	Result = Dialog->Show(static_cast<HWND>(const_cast<void*>(ParentWindow)));
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
	TComPtr<IShellItem> Item;
	if (FAILED(Result) || FAILED(Dialog->GetResult(&Item)))
	{
		Error = TEXT("The Windows file dialog could not select a file.");
		return false;
	}
	PWSTR Path = nullptr;
	Result = Item->GetDisplayName(SIGDN_FILESYSPATH, &Path);
	ON_SCOPE_EXIT { if (Path) ::CoTaskMemFree(Path); };
	if (FAILED(Result) || !Path)
	{
		Error = TEXT("Could not read the selected file path.");
		return false;
	}
	OutPath = Path;
	FPaths::NormalizeFilename(OutPath);
	return true;
#else
	Error = TEXT("Windows file dialogs are unavailable on this platform.");
	return false;
#endif
}
