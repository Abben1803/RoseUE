// RoseEditor — native, in-engine import of ROSE map data.
//
// WHY THIS MODULE EXISTS
// The old path is ROSE binaries -> Python (mapforge) -> a multi-GB .glb ->
// Interchange -> assets, then five more headless editor launches for part
// flags, spawners, materials, lighting and metallic (tools/import_zone.ps1).
// Editor start/stop dominates: ~8-10 minutes per launch, six launches a zone.
//
// This module parses the ROSE files directly in C++ and builds UStaticMesh /
// UTexture2D / UMaterialInstanceConstant / the LEVEL itself in one process.
// No intermediate file, no Interchange — which also removes the three bugs
// Interchange gave us (Nanite cracking world-baked terrain, Substrate importing
// MASK as translucent, the unreliable material re-skin), because nothing is
// guessing any more.
//
// Editor-only: it is never compiled into the Game or Server targets.

using System.IO;
using UnrealBuildTool;

public class RoseEditor : ModuleRules
{
	public RoseEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// RoseUE keeps its headers in the module root (no Public/Private split),
		// so they are not on the include path by default.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "RoseUE"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// The gameplay actor classes the importer places: ARoseNpc,
			// ARoseMonsterSpawner, ARoseWarpPortal.
			"RoseUE",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",          // commandlet, asset creation, editor-only setters
			// UMaterialEditingLibrary: the master materials are built in-engine
			// (RoseMaterialBuilder), not by a Python round trip.
			"MaterialEditor",
			"AssetRegistry",     // registering the assets we create
			"MeshDescription",       // FMeshDescription
			"StaticMeshDescription", // FStaticMeshAttributes
			"RenderCore",
			"RHI",
			"Projects",
			// Skeletal import: FSkeletalMeshImportData, FSkeletalMeshBuilder,
			// and the target-platform handle the builder needs.
			"MeshBuilder",
			"SkeletalMeshDescription",
			"AnimationCore",
			"TargetPlatform",
			"DesktopPlatform",
			// The in-editor importer panel.
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"EditorFramework",
			"WorkspaceMenuStructure",
			"DesktopPlatform",       // folder picker for the asset root
			// UE's Water plugin.  ROSE's IFO Ocean block gives flat rectangular
			// water patches (JPT01 has 45, all at one height), which become real
			// water bodies rather than translucent planes.
			"Water",
			// UWaterEditorSettings — the default water materials the editor's
			// placement tools assign.  SpawnActor does NOT apply them, so a
			// C++-spawned body has no water material and renders as grey noise.
			"WaterEditor",
			// USkeletalMergingLibrary — the audit runs the SAME merge the runtime
			// does, so a break there is visible without launching the game.
			"SkeletalMerging",
			// The sky importer writes Content/Sky/sky.json (zone -> LIST_SKY row),
			// which the runtime dome reads the same way RoseCharacter reads
			// zone_bgm.json.  Header-only use still needs the module to link.
			"Json",
		});
	}
}
