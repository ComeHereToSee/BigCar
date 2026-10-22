// Copyright BigCar Team. All Rights Reserved.

using UnrealBuildTool;

// Dedicated server (one match per process). Built in CI inside Epic's Linux development container.
public class BigCarServerTarget : TargetRules
{
	public BigCarServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "CrashCore", "BigCar" });
	}
}
