// Copyright BigCar Team. All Rights Reserved.

using UnrealBuildTool;

// Client-only build (no server code paths) for players who only play online.
public class BigCarClientTarget : TargetRules
{
	public BigCarClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "CrashCore", "BigCar" });
	}
}
