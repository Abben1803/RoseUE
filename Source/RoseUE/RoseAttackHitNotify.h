// Anim notify that triggers the owning RoseCharacter's melee hit at the swing's
// contact frame.  This is a notify CLASS (not a named "AnimNotify_X"), so it fires
// even in single-node animation mode with no Anim Blueprint — the same mechanism
// the sound notifies use.  Place it on each attack AnimSequence at the impact frame.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "RoseAttackHitNotify.generated.h"

UCLASS(meta = (DisplayName = "Rose Attack Hit"))
class ROSEUE_API URoseAttackHitNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
