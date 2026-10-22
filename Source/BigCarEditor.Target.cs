// Copyright BigCar Team. All Rights Reserved.

using UnrealBuildTool;

public class BigCarEditorTarget : TargetRules
{
	public BigCarEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "CrashCore", "BigCar" });
	}
}
