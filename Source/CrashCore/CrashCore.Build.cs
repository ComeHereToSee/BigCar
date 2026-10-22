// Copyright BigCar Team. All Rights Reserved.

using UnrealBuildTool;

// Engine-agnostic gameplay core (pure C++20, no Unreal types). The very same sources are compiled and unit-tested by
// CMake in CI (see CMakeLists.txt at the repository root), so keep this module free of Unreal headers.
public class CrashCore : ModuleRules
{
	public CrashCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bUseUnity = false;
		bEnableExceptions = false;
		bUseRTTI = false;

		PublicIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Public"));
		PublicDependencyModuleNames.Add("Core");
	}
}
