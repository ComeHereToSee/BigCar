// Copyright BigCar Team. All Rights Reserved.

using UnrealBuildTool;

public class BigCar : ModuleRules
{
	public BigCar(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicIncludePaths.AddRange(new string[]
		{
			System.IO.Path.Combine(ModuleDirectory, "Public"),
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"ProceduralMeshComponent",
			"AIModule",
			"GameplayTasks",
			"CrashCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"HTTP",
			"Json",
			"AudioMixer",
			"AudioExtensions",
			"NetCore",
			"DeveloperSettings",
			"RenderCore",
		});
	}
}
