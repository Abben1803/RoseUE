#include "RoseAttackHitNotify.h"

#include "Components/SkeletalMeshComponent.h"
#include "RoseCharacter.h"

void URoseAttackHitNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                  const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp) return;
	if (ARoseCharacter* C = Cast<ARoseCharacter>(MeshComp->GetOwner()))
		C->DoMeleeHit();
}

FString URoseAttackHitNotify::GetNotifyName_Implementation() const
{
	return TEXT("Rose Attack Hit");
}
