// URoseCharSlotSave — the saved character roster shown on the Character Select
// screen.  One entry per created character (appearance only for now — no server,
// so progression isn't persisted yet).  Slot "RoseChars", user index 0.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RoseCharSlotSave.generated.h"

USTRUCT()
struct FRoseCharSlot
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FString Gender = TEXT("Female");
	// Classic hair ids stop at 78; 110 was Arua-only (bald character).
	UPROPERTY() int32 Hair = 1;
	UPROPERTY() int32 Face = 1;
	UPROPERTY() int32 Level = 1;
	// Backend character id (0 = offline / local-only).  When a backend is
	// configured the roster comes from the server and this is the id every API
	// call and world ticket is keyed on; the save game is then only a display
	// cache so the 3D avatar arc can be built before the fetch returns.
	UPROPERTY() int32 BackendId = 0;
};

UCLASS()
class ROSEUE_API URoseCharSlotSave : public USaveGame
{
	GENERATED_BODY()

public:
	static const TCHAR* SlotName() { return TEXT("RoseChars"); }

	UPROPERTY() TArray<FRoseCharSlot> Slots;
};
