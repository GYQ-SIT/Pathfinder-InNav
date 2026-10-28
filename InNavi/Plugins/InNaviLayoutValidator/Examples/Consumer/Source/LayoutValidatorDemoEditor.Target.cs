using UnrealBuildTool;
public class LayoutValidatorDemoEditorTarget : TargetRules
{
    public LayoutValidatorDemoEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("LayoutValidatorDemo");
    }
}
