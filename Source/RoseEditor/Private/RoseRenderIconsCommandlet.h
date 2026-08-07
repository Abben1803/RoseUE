#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseRenderIconsCommandlet.generated.h"

// Render item icons from the imported 3D models.
//
// usage: -run=RoseRenderIcons [-only=weapon,cap] [-max=N] [-size=128] [-force]
//
// WHY: the loose client build in client/3ddata ships NO CONTROL/RES/ICON*.DDS,
// so the icon sheets we slice come out of client/rose.vfs — a different, much
// smaller build (its LIST_MBODY.ZSC is 192 KB against the loose 517 KB).  Sheets
// 1-13 happen to coincide, which is why low IconIdx values look right and higher
// ones land on another item's art.  Half the named items do not exist in the VFS
// build at all, so no sheet anywhere holds their icon.
//
// The MESHES, though, come from the same build as the tables — so rendering the
// item itself is correct by construction and needs no era guesswork.  This
// covers the 4,411 worn items (74% of items with an icon).
//
// It does NOT cover jewel/useitem/gem/material: those are not worn and have no
// per-item model.  Their LIST_* row does carry a "Field Item" id, but those
// ground-drop models are far too coarse to identify an item — 206 jewels share
// just 2 of them.  Those slots keep the sheet icons.
UCLASS()
class URoseRenderIconsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseRenderIconsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
