using UnrealBuildTool;

public class AdaVoicePackLoader : ModuleRules
{
	public AdaVoicePackLoader(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine",
			"DeveloperSettings", "PhysicsCore", "InputCore",
			"GeometryCollectionEngine", "AnimGraphRuntime", "AkAudio", "AssetRegistry",
			"NavigationSystem", "AIModule", "GameplayTasks",
			"SlateCore", "Slate", "UMG", "RenderCore", "CinematicCamera", "Foliage",
			"NetCore", "GameplayTags", "Json", "JsonUtilities",
			"DummyHeaders", "FactoryGame", "SML"
		});

		if (Target.Type == TargetRules.TargetType.Editor)
		{
			PublicDependencyModuleNames.AddRange(new[] { "AnimGraph" });
		}

		PrivateDependencyModuleNames.AddRange(new[] { "Projects", "ApplicationCore" });
	}
}
