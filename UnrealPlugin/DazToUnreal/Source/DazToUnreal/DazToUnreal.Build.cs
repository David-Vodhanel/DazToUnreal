// Copyright 2018-2019 David Vodhanel. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DazToUnreal : ModuleRules
{
	public DazToUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Sockets",
				"Networking",
				"Json",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"BlueprintGraph",
				"Projects",
				"InputCore",
				"UnrealEd",
				"LevelEditor",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorScriptingUtilities",
				"SkeletalMeshUtilitiesCommon",
				"DazToUnrealRuntime",
				// ... add private dependencies that you statically link with here ...	
			}
			);

#if UE_4_26_OR_LATER
		PrivateDependencyModuleNames.Add("DeveloperSettings");
#endif

#if UE_5_1_OR_LATER
		PrivateDependencyModuleNames.Add("AlembicLibrary");
		PrivateDependencyModuleNames.Add("AlembicImporter");
		PrivateDependencyModuleNames.Add("MLDeformerFramework");
		PrivateDependencyModuleNames.Add("NeuralNetworkInference");
#endif

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
		AddEngineThirdPartyPrivateStaticDependencies(Target, new string[] { "MikkTSpace", "OpenSubdiv" });

		// Filter UE5 Content from UE4 builds
		string VersionSpecificFilterIni = Path.Combine(PluginDirectory, "Resources", "UE4_FilterPlugin.ini");
#if UE_5_0_OR_LATER
		VersionSpecificFilterIni = Path.Combine(PluginDirectory, "Resources", "UE5_FilterPlugin.ini");
#endif
		string TargetFilterIni = Path.Combine(PluginDirectory, "Config", "FilterPlugin.ini");
		if (File.Exists(VersionSpecificFilterIni))
		{
			// Don't want to break builds if the path isn't writable
			try
			{
				Directory.CreateDirectory(Path.Combine(PluginDirectory, "Config"));
				File.Copy(VersionSpecificFilterIni, TargetFilterIni, true);
			}
			catch{}
		}
	}
}
