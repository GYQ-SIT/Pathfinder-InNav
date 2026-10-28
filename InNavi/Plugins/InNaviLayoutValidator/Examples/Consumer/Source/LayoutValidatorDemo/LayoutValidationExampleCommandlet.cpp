#include "LayoutValidationExampleCommandlet.h"
#include "LayoutValidation.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, LayoutValidatorDemo, "LayoutValidatorDemo");

ULayoutValidationExampleCommandlet::ULayoutValidationExampleCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}
int32 ULayoutValidationExampleCommandlet::Main(const FString& Params)
{
    FString Input, Output;
    if (!FParse::Value(*Params, TEXT("Input="), Input) || !FParse::Value(*Params, TEXT("Report="), Output)) return 2;
    if (FPaths::ConvertRelativePathToFull(Input).Equals(FPaths::ConvertRelativePathToFull(Output), ESearchCase::IgnoreCase)) return 2;
    auto Report = LayoutValidation::ValidateFile(Input);
    if (!LayoutValidation::ExportReport(Report, Output)) return 2;
    UE_LOG(LogTemp, Display, TEXT("Example validation: %d errors, %d warnings"), Report.Count(LayoutValidation::ESeverity::Error), Report.Count(LayoutValidation::ESeverity::Warning));
    return Report.HasErrors() ? 1 : 0;
}
