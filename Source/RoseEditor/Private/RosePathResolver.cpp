#include "RosePathResolver.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

FRosePathResolver::FRosePathResolver(const FString& InAssetRoot)
{
	DataRoot = FPaths::Combine(InAssetRoot, TEXT("3DDATA"));
	FPaths::NormalizeDirectoryName(DataRoot);
}

const TMap<FString, FString>& FRosePathResolver::ListDir(const FString& Dir)
{
	if (const TMap<FString, FString>* Cached = DirCache.Find(Dir))
		return *Cached;

	TMap<FString, FString>& Entries = DirCache.Add(Dir);

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*")), true, false);
	for (const FString& F : Files)
		Entries.Add(F.ToLower(), F);

	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Dir / TEXT("*")), false, true);
	for (const FString& D : Dirs)
		Entries.Add(D.ToLower(), D);

	return Entries;
}

FString FRosePathResolver::Resolve(const FString& RosePath)
{
	return ResolveInternal(RosePath, /*bDirectory*/ false);
}

FString FRosePathResolver::ResolveDir(const FString& RosePath)
{
	return ResolveInternal(RosePath, /*bDirectory*/ true);
}

FString FRosePathResolver::ResolveInternal(const FString& RosePath, bool bDirectory)
{
	if (RosePath.IsEmpty())
		return FString();

	FString Normalised = RosePath;
	Normalised.ReplaceInline(TEXT("\\"), TEXT("/"));

	TArray<FString> Parts;
	Normalised.ParseIntoArray(Parts, TEXT("/"), /*CullEmpty*/ true);
	if (Parts.Num() == 0)
		return FString();

	// Some tables store the path with the data root already on the front.
	if (Parts[0].Equals(TEXT("3DDATA"), ESearchCase::IgnoreCase))
		Parts.RemoveAt(0);

	FString Current = DataRoot;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		const TMap<FString, FString>& Entries = ListDir(Current);
		const FString* Real = Entries.Find(Parts[i].ToLower());
		if (!Real)
			return FString();
		Current = Current / *Real;
	}

	if (bDirectory)
		return IFileManager::Get().DirectoryExists(*Current) ? Current : FString();
	return IFileManager::Get().FileExists(*Current) ? Current : FString();
}
