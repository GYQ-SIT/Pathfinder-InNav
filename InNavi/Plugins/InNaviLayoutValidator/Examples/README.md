# Standalone consumer example

This minimal Unreal 5.8 editor project consumes only the reusable plugin. It has no InNavi game module, actors, navigation planner or UI dependencies. Its commandlet performs JSON/geometry validation; campus route diagnostics are supplied by the real product's adapter.

From the Pathfinder-InNav repository root, install the pinned registry package first (`npm ci` with `NODE_AUTH_TOKEN` set as described in the plugin README). Copy `node_modules/@gyq-sit/innavi-layout-validator/Examples/Consumer` to a temporary directory **outside** the product and plugin tree, for example `C:/Temp/LayoutValidatorDemo`. Then run:

```powershell
node scripts/install-layout-validator.mjs --destination C:/Temp/LayoutValidatorDemo/Plugins/InNaviLayoutValidator
$engine = 'C:/Program Files/Epic Games/UE_5.8/Engine'
$demoProject = 'C:/Temp/LayoutValidatorDemo/LayoutValidatorDemo.uproject'
$samples = 'C:/Temp/LayoutValidatorDemo/Plugins/InNaviLayoutValidator/Examples'
& "$engine/Build/BatchFiles/Build.bat" LayoutValidatorDemoEditor Win64 Development "-Project=$demoProject" -WaitMutex
& "$engine/Binaries/Win64/UnrealEditor-Cmd.exe" $demoProject -run=LayoutValidationExample "-Input=$samples/valid-layout.json" '-Report=C:/Temp/LayoutValidatorDemo/valid-report.json' -unattended -nullrhi -nosplash -nosound
# Expected exit status: 0, no errors.
& "$engine/Binaries/Win64/UnrealEditor-Cmd.exe" $demoProject -run=LayoutValidationExample "-Input=$samples/invalid-layout.json" '-Report=C:/Temp/LayoutValidatorDemo/invalid-report.json' -unattended -nullrhi -nosplash -nosound
# Expected exit status: 1, OPENING_BOUNDS is present in the report.
```

Both inputs remain unchanged. Exit status 2 means invocation or output failure.
