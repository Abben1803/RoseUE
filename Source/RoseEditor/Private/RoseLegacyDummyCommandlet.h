#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseLegacyDummyCommandlet.generated.h"

// Add ROSE hand/attach dummies as SOCKETS to the LEGACY `base` skeleton.
//
// WHY
// ROSE applies no tuned weapon rotation.  zz_model::link_dummy makes the weapon
// a CHILD of the dummy node, so the weapon's placement is entirely
//     dummy world (parent bone x dummy local, from the ZMD) x weapon mesh local
// and the ZSC part transform is already baked into our weapon meshes.  Attach at
// a socket sitting on the dummy and the weapon lands correctly with no constants
// — which is exactly what the native path does.
//
// The legacy skeleton has no such sockets, so ARoseCharacter falls back to the
// bare hand BONE, whose orientation differs from the hand DUMMY.  A -5 degree
// grip nudge cannot make up that difference, which is why weapon grips read as
// completely wrong once weapons come from the native (ZSC) import.
//
// The legacy rig was built through the old glTF conversion, so its bone space is
// NOT the ROSE space the native importer assumes.  Rather than hard-code a basis
// (that conversion has been wrong twice), the mapping is DERIVED per bone:
//     M            = LegacyBoneWorld(B) * inverse(RoseBoneWorld(B))
//     DummyWorldUE = M * RoseDummyWorld
//     SocketLocal  = inverse(LegacyBoneWorld(B)) * DummyWorldUE
// which reads the basis off the skeleton instead of assuming it.
//
// usage: -run=RoseLegacyDummy [-assetroot=...] [-dryrun]
UCLASS()
class URoseLegacyDummyCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseLegacyDummyCommandlet();
	virtual int32 Main(const FString& Params) override;
};
