using UnrealBuildTool;

public class InNaviLayoutValidator : ModuleRules
{
    public InNaviLayoutValidator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Json" });
        PrivateDependencyModuleNames.Add("GeometryCore");
    }
}
