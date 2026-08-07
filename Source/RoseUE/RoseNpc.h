// A town NPC: same data-driven model/anim loading as ARoseMonster (LIST_NPC
// row → npc_<id> mesh + CHR anims) but no combat AI — it idles, faces its
// authored IFO yaw, shows a floating name, and clicking it starts its CON
// conversation (io_terrain.cpp LUMP_TERRAIN_MOB: the IFO carries the CON
// filename; ARoseCharacter::OnLeftClick routes here before the attack path).
#pragma once

#include "CoreMinimal.h"
#include "RoseMonster.h"
#include "RoseNpc.generated.h"

class UTextRenderComponent;

UCLASS()
class ROSEUE_API ARoseNpc : public ARoseMonster
{
	GENERATED_BODY()

public:
	ARoseNpc();

	// Dialog CON stem ("EM01-004") — empty = no conversation.
	UPROPERTY(EditAnywhere, Category = "Rose|Npc") FString ConStem;
	// LIST_EVENT.STB row matched from the CON filename (diagnostics).
	UPROPERTY(EditAnywhere, Category = "Rose|Npc") int32 EventId = 0;
	// IFO AI pattern id (unused for town NPCs so far).
	UPROPERTY(EditAnywhere, Category = "Rose|Npc") int32 AiId = 0;

	bool HasDialog() const { return !ConStem.IsEmpty(); }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	// No combat AI: stand at the authored spot playing the stop anim.
	virtual void TickAI(float Dt) override {}
	// Town NPCs don't run AIP scripts (no combat tick to drive them).
	virtual bool UsesAip() const override { return false; }

	UPROPERTY(VisibleAnywhere) UTextRenderComponent* NameLabel;
};
