using UnrealBuildTool;
public class LayoutValidatorDemo : ModuleRules
{
    public LayoutValidatorDemo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Json", "InNaviLayoutValidator" });
    }
}
