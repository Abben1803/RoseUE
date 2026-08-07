// Dedicated-server target.  There is NO second project: the server is the same
// RoseUE module built headless (no rendering, no Slate UI, no scene captures),
// so every formula, DataTable and gameplay path stays in one codebase and
// HasAuthority() decides who runs what.
//
// Build:
//   Build.bat RoseUEServer Win64 Development -project="...\RoseUE.uproject" -waitmutex
// Run:
//   RoseUEServer.exe /Game/Maps/JPT01/L_JPT01 -log -port=7777
//
// ⚠ THIS TARGET CANNOT BE COMPILED BY THE LAUNCHER-INSTALLED ENGINE.
// UBT answers "Server targets are not currently supported from this engine
// distribution" — Server/Client targets need an engine built from source
// (github.com/EpicGames/UnrealEngine, branch 5.8).  The file is correct and
// ready; it only needs a source engine.  Until then, develop and test with:
//   * a LISTEN server — `RoseHost` in the console, `RoseJoin <ip>` on a second
//     instance.  Full replication, one player also hosts.
//   * a real DEDICATED netmode via the editor binary (dev only, not shippable):
//       UnrealEditor-Cmd.exe "<RoseUE.uproject>" /Game/Maps/JPT01/L_JPT01 \
//         -server -log -nullrhi -port=7777

using UnrealBuildTool;
using System.Collections.Generic;

public class RoseUEServerTarget : TargetRules
{
	public RoseUEServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("RoseUE");
	}
}
