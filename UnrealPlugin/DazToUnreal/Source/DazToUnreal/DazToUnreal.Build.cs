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
				"ControlRig",
				"ControlRigDeveloper",
				"ToolMenus",
				"ContentBrowser",
				"MaterialEditor",
				// ... add private dependencies that you statically link with here ...
			}
			);

#if UE_4_26_OR_LATER
		PrivateDependencyModuleNames.Add("DeveloperSettings");
#endif

#if UE_5_0_OR_LATER
		// UDynamicMesh appears in UFUNCTION signatures in DazToUnrealBlueprintUtils.h on every
		// engine version (the bodies are 5.8+ only), so UHT emits a GEOMETRYFRAMEWORK_API cross
		// module reference regardless - the dependency has to be declared for all UE5 builds.
		PrivateDependencyModuleNames.Add("GeometryCore");
		PrivateDependencyModuleNames.Add("GeometryFramework");
#endif

#if UE_5_1_OR_LATER
		PrivateDependencyModuleNames.Add("AlembicLibrary");
		PrivateDependencyModuleNames.Add("AlembicImporter");
		PrivateDependencyModuleNames.Add("MLDeformerFramework");
		PrivateDependencyModuleNames.Add("MLDeformerFrameworkEditor");
		PrivateDependencyModuleNames.Add("NeuralNetworkInference");
		PrivateDependencyModuleNames.Add("GeometryCache");
#endif

#if UE_5_2_OR_LATER
		PrivateDependencyModuleNames.Add("IKRig");
		PrivateDependencyModuleNames.Add("IKRigEditor");
#endif

#if UE_5_3_OR_LATER
		PrivateDependencyModuleNames.Remove("NeuralNetworkInference");
#endif

#if UE_5_4_OR_LATER
		PrivateDependencyModuleNames.Add("SkeletalMeshModifiers");
		PrivateDependencyModuleNames.Add("PhysicsUtilities");
#endif

#if UE_5_8_OR_LATER
		// Convert to MetaHuman: local body conform (MetaHumanCharacterEditor subsystem)
		// and Mutable reshape/fit/combine (bake-at-edit-time). New deps for this plugin.
		PrivateDependencyModuleNames.Add("MetaHumanCharacter");
		PrivateDependencyModuleNames.Add("MetaHumanCharacterEditor");
		PrivateDependencyModuleNames.Add("CustomizableObject");
		PrivateDependencyModuleNames.Add("CustomizableObjectEditor");
		PrivateDependencyModuleNames.Add("MutableTools");
		// Component template editing for MetaHuman character blueprint assembly
		PrivateDependencyModuleNames.Add("SubobjectDataInterface");
		// FImage pixel access for Daz skin-tone estimation
		PrivateDependencyModuleNames.Add("ImageCore");
		// Groom follicle mask generation (FGroomTextureBuilder) for scalp coverage under hair
		PrivateDependencyModuleNames.Add("HairStrandsCore");
		// Groom hair color: UMetaHumanInstance / FMetaHumanPaletteItemPath (instance parameter bags)
		PrivateDependencyModuleNames.Add("MetaHumanCharacterPalette");
#endif

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
		AddEngineThirdPartyPrivateStaticDependencies(Target, new string[] { "MikkTSpace" });

		// Filter UE5 Content from UE4 builds
		string VersionSpecificFilterIni = Path.Combine(PluginDirectory, "Resources", "UE4_FilterPlugin.ini");
#if UE_5_0_OR_LATER
		VersionSpecificFilterIni = Path.Combine(PluginDirectory, "Resources", "UE5_FilterPlugin.ini");
#endif
#if UE_5_4_OR_LATER
		VersionSpecificFilterIni = Path.Combine(PluginDirectory, "Resources", "UE5_4_FilterPlugin.ini");
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

#if UE_5_4_OR_LATER
		// MLDeformerFramework can't be optional in 5.4 or the plugin will fail to load.
		string PluginFilePath = Path.Combine(PluginDirectory, "DazToUnreal.uplugin");
		PluginDescriptor Descriptor = PluginDescriptor.FromFile(EpicGames.Core.FileReference.FromString(PluginFilePath));
		foreach(var RequestedPlugin in Descriptor.Plugins)
		{
			if((RequestedPlugin.Name == "MLDeformerFramework" || 
				RequestedPlugin.Name == "NeuralMorphModel") && 
				RequestedPlugin.bOptional == true)
			{
				RequestedPlugin.bOptional = false;
				try
				{
					Descriptor.Save2(PluginFilePath);
					break;
				}
				catch { }
			}
		}
#endif
	}
}
