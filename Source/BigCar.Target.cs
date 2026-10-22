// Copyright BigCar Team. All Rights Reserved.

using UnrealBuildTool;

public class BigCarTarget : TargetRules
{
	public BigCarTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "CrashCore", "BigCar" });
	}
}
