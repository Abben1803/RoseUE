#include "RoseStbAuditCommandlet.h"

#include "RoseEditor.h"
#include "RoseObjectFormats.h"
#include "RoseStbSchema.h"

#include "Misc/Paths.h"

URoseStbAuditCommandlet::URoseStbAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseStbAuditCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString AssetRoot = ParamsMap.Contains(TEXT("assetroot"))
		? ParamsMap[TEXT("assetroot")]
		: TEXT("C:/QQ-iROSE Online/extracted");

	UE_LOG(LogRoseImport, Display, TEXT("=== STB audit: %s ==="), *AssetRoot);

	struct FEntry
	{
		const TCHAR* File;
		const FRoseStbSchema& Schema;
	};
	const FEntry Entries[] =
	{
		{ TEXT("LIST_SKILL.STB"), RoseStb::SkillSchema() },
		{ TEXT("LIST_ZONE.STB"),  RoseStb::ZoneSchema()  },
	};

	int32 Problems = 0;

	for (const FEntry& E : Entries)
	{
		const FString Path = FPaths::Combine(AssetRoot, TEXT("3DDATA"), TEXT("STB"), E.File);

		FRoseSTB Table;
		if (!Table.Load(Path))
		{
			UE_LOG(LogRoseImport, Error, TEXT("%s: cannot load %s"), E.File, *Path);
			++Problems;
			continue;
		}

		const FRoseStbBinding Binding = RoseStb::Bind(Table, E.Schema);
		RoseStb::LogBindingReport(Binding, E.Schema);

		Problems += Binding.Unresolved.Num();
	}

	// Unresolved fields are not necessarily an error — a client may genuinely
	// lack a column, and those read as their default rather than as a
	// neighbouring column's value.  They are reported so the decision is a
	// human's.
	UE_LOG(LogRoseImport, Display,
		TEXT("=== STB audit done: %d field(s) with no column ==="), Problems);
	return 0;
}
