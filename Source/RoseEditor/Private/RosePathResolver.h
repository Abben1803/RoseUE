// Resolve a ROSE-internal asset path to a real file on disk.
//
// Two problems, both of which bite every format that references another file
// (ZON tile textures, ZSC textures and meshes, CHR/ZMS/ZMO):
//
//  1. The stored path may or may not begin with "3DDATA\".  ZON tile paths do
//     ("3DData\Terrain\Tiles\..."), other tables do not.
//  2. Case does not match.  The VFS was case-insensitive, so the data says
//     "T025_01.dds" while the extracted file is "T025_01.DDS" — and NTFS is
//     case-insensitive but the ROSE paths also disagree on directory casing.
//
// This is the C++ twin of mapforge/export_map.py::resolve(), which walks each
// component case-insensitively.  Directory listings are cached because a zone
// resolves thousands of paths through the same handful of folders.
#pragma once

#include "CoreMinimal.h"

class FRosePathResolver
{
public:
	// AssetRoot is the extract root (the folder CONTAINING 3DDATA).
	explicit FRosePathResolver(const FString& InAssetRoot);

	// Returns an absolute path to a FILE, or empty if nothing matches.
	FString Resolve(const FString& RosePath);

	// Same walk, but the target is a DIRECTORY (e.g. "MOTION/AVATAR", which the
	// motion importer scans for *.ZMO).  Resolve() rejects those: it ends on
	// FileExists, which is false for a folder.
	FString ResolveDir(const FString& RosePath);

private:
	// The shared case-insensitive component walk.  bDirectory picks which
	// existence test the final component has to pass.
	FString ResolveInternal(const FString& RosePath, bool bDirectory);

	FString DataRoot;   // <AssetRoot>/3DDATA, with its real on-disk casing
	// directory -> (lowercase entry name -> real entry name)
	TMap<FString, TMap<FString, FString>> DirCache;

	const TMap<FString, FString>& ListDir(const FString& Dir);
};
