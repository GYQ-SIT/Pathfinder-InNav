using UnrealBuildTool;
using System.IO;

public class InNaviValidationTools : ModuleRules
{
    public InNaviValidationTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Json", "InNavi", "InNaviLayoutValidator" });
        // The existing game module keeps its exported headers directly in its module directory.
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "InNavi"));
    }
}
