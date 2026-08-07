// ROSE playable character — modular avatar + WASD movement + speed-driven anims.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "RosePatTypes.h"
#include "RoseCharacter.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UAnimSequence;
class UDataTable;
struct FRoseWeaponRow;
struct FRoseArmorRow;

// One stack in the player's bag.  Slot is the item's table key ("body","arms",
// "foot","cap","back","weapon","hair","face" for equipment; "consumable"/
// "material" for future non-equip items); Id is the row id; Count stacks
// identical entries.
USTRUCT()
struct FRoseItemStack
{
	GENERATED_BODY()

	UPROPERTY() FString Slot;
	UPROPERTY() int32 Id = 0;
	UPROPERTY() int32 Count = 1;
	// Rolled drop stats (CCal::Get_DropITEM): Bonus = the random GEM_OP option
	// magnitude (0 = none / unappraised), bAppraised = identified.
	UPROPERTY() int32 Bonus = 0;
	UPROPERTY() bool bAppraised = true;
	// Refine grade 0..9 (LIST_GRADE.STB row): per-INSTANCE, survives bag<->equip.
	UPROPERTY() int32 Refine = 0;
};

// One equipped slot as it goes over the wire.  The authoritative state lives in
// the Equipped/EquippedRefine/EquippedBonus/EquippedAppraised TMaps, but UE
// property replication does NOT support TMap — the server flattens them into
// this array (ARoseCharacter::PushAppearance) and clients rebuild the maps in
// OnRep_Appearance before re-merging the mesh.
USTRUCT()
struct FRoseEquipRep
{
	GENERATED_BODY()

	UPROPERTY() FName Slot;
	UPROPERTY() int32 Id = -1;
	UPROPERTY() int32 Refine = 0;
	UPROPERTY() int32 Bonus = 0;
	UPROPERTY() bool  bAppraised = true;

	bool operator==(const FRoseEquipRep& O) const
	{
		return Slot == O.Slot && Id == O.Id && Refine == O.Refine
			&& Bonus == O.Bonus && bAppraised == O.bAppraised;
	}
};

// config=Game so the grip transforms you tune live in PIE persist to DefaultGame.ini.

/** A tuned grip for ONE weapon type.
 *
 *  The grip is not a single value: a bow, a staff and a one-handed sword all
 *  sit in the hand differently, so a transform that suits one skews the others.
 *  Keyed by LIST_WEAPON's ITEM_TYPE (FRoseWeaponRow::Type), which is also what
 *  drives the animation set — weapons that animate alike hold alike. */
USTRUCT()
struct FRoseWeaponGrip
{
	GENERATED_BODY()

	UPROPERTY(config) int32    WeaponType = 0;
	UPROPERTY(config) FVector  Loc = FVector::ZeroVector;
	UPROPERTY(config) FRotator Rot = FRotator::ZeroRotator;
	UPROPERTY(config) float    Scale = 1.f;
	/** Right hand (false) or left/off hand (true). */
	UPROPERTY(config) bool     bLeftHand = false;
};

UCLASS(config=Game)
class ROSEUE_API ARoseCharacter : public ACharacter
{
	GENERATED_BODY()

	// Cross-level state snapshot (warp portals / quest teleports) reads and
	// writes the full private state.
	friend class URoseGameInstance;

public:
	ARoseCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;

	// ── Networking ──────────────────────────────────────────────────────────
	// One codebase, two roles.  The server (dedicated or listen host) owns all
	// gameplay state; clients own only their view of it.  Three predicates
	// decide who runs what:
	//   HasAuthority()        — the server: run gameplay, mutate state
	//   IsLocallyControlled() — this machine's own pawn: input, camera, UI
	//   neither               — a simulated proxy: replicated transform + anims
	// Everything cosmetic (scene captures, HUD, cursor) is local-only, and the
	// dedicated server skips the mesh merge entirely (it never renders).

	// True when this build is a headless server (no merge, no captures, no UI).
	bool IsDedicatedServerRole() const { return GetNetMode() == NM_DedicatedServer; }

	// One-time setup that needs a local controller (portrait/paperdoll render
	// targets + the UI manager).  Called from BeginPlay, PossessedBy and
	// OnRep_Controller — whichever happens last wins; it runs exactly once.
	void EnsureLocalSetup();

	// Stamp a chosen appearance (gender + hair + face) onto whatever this
	// character is currently wearing, WITHOUT touching the rest of the loadout.
	// Used for a brand-new character, where the only stored data is the look
	// picked at Character Select and the starting kit comes from BeginPlay.
	// Ids <= 0 leave that slot alone.
	void ApplyLook(const FString& InGender, int32 Hair, int32 Face);

	// Server: flatten Equipped/Refine/Bonus/Appraised into RepEquipped (+ gender)
	// so every client can rebuild this character's look.  Cheap; call it
	// whenever the loadout changes (Tick coalesces it via bAppearanceDirty).
	void PushAppearance();
	// Mark the loadout dirty so the next server Tick republishes it.
	void MarkAppearanceDirty() { bAppearanceDirty = true; }

	// Console helpers for two-player testing without a packaged server:
	//   RoseHost            — reopen the current level as a listen server
	//   RoseJoin 127.0.0.1  — travel to a host
	UFUNCTION(Exec) void RoseHost();
	UFUNCTION(Exec) void RoseJoin(const FString& Address);
	UFUNCTION(Exec) void RoseNetInfo();

	// Diagnostics for "the character renders dark while static meshes next to it
	// do not".  Every offline check (assets, materials, master wiring, normals,
	// merge, transform) passes, so the answer is runtime state.
	//
	//   RoseDebugMat 1  — replace every character material with a stock engine
	//                     material.  Still dark => NOT the material; it is the
	//                     component or the scene.  Lights up => the material.
	//   RoseDebugMat 0  — restore.
	//   RoseDebugLight  — dump the mesh component's lighting-relevant state.
	UFUNCTION(Exec) void RoseDebugMat(int32 bOn);
	UFUNCTION(Exec) void RoseDebugLight();

	// "Female" or "Male"
	UPROPERTY(EditAnywhere, Category = "Rose")
	FString Gender = TEXT("Female");
	UPROPERTY(VisibleAnywhere, Category = "Rose") USpringArmComponent* CameraBoom;
	UPROPERTY(VisibleAnywhere, Category = "Rose") UCameraComponent* FollowCamera;

	// ── Live avatar portrait (HUD) — a scene-capture of this character's head
	//    rendered to PortraitRT every frame, so head animation shows live. ──
	UPROPERTY(VisibleAnywhere, Category = "Rose|UI") class USceneCaptureComponent2D* PortraitCapture;
	UPROPERTY() class UTextureRenderTarget2D* PortraitRT = nullptr;
	class UTextureRenderTarget2D* GetPortraitRT() const { return PortraitRT; }
	// Framing, editor-adjustable + persisted (tune to fill the face).  The mesh
	// sits at capsule-relative Z=-88 (feet at the capsule bottom) and the char is
	// ~176cm, so the head is around Z≈+70..+88.  The camera looks in level at
	// Z≈74 (face centre) from ~0.75m in front, FOV 38 → a head-and-shoulders crop
	// (covers Z≈48..100).  Was Z=55 (neck) looking level → the head clipped off
	// the top of the frame.
	UPROPERTY(EditAnywhere, config, Category = "Rose|UI") FVector  PortraitCamLoc = FVector(75.f, 0.f, 74.f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|UI") FRotator PortraitCamRot = FRotator(0.f, 180.f, 0.f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|UI") float    PortraitFOV = 38.f;

	// ── Full-body paperdoll (inventory preview) — a second scene-capture framed
	//    on the whole character; only captures while the inventory is open. ──
	UPROPERTY(VisibleAnywhere, Category = "Rose|UI") class USceneCaptureComponent2D* PaperdollCapture;
	UPROPERTY() class UTextureRenderTarget2D* PaperdollRT = nullptr;
	class UTextureRenderTarget2D* GetPaperdollRT() const { return PaperdollRT; }
	void SetPaperdollActive(bool bActive);   // enable per-frame capture only when shown
	UPROPERTY(EditAnywhere, config, Category = "Rose|UI") FVector  PaperdollCamLoc = FVector(300.f, 0.f, 2.f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|UI") FRotator PaperdollCamRot = FRotator(0.f, 180.f, 0.f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|UI") float    PaperdollFOV = 35.f;

	// Weapon data table (RoseUE/DataTables/weapons.csv → a UE DataTable of
	// FRoseWeaponRow).  Read by id — NO hardcoded per-weapon code.  Auto-loaded by
	// path in BeginPlay if left unset.
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* WeaponTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<USoundBase> HitSound;
	const FRoseWeaponRow* GetWeaponRow(int32 Id) const;

	// Armor data tables (RoseUE/DataTables/{body,arms,foot,cap}.csv → DataTables of
	// FRoseArmorRow).  Defense/resist/requirements/names all come from these, read
	// by id — never hardcoded.  Auto-loaded by path in BeginPlay if left unset.
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* BodyTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* ArmsTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* FootTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* CapTable  = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* BackTable = nullptr;
	// Non-equipment item tables (consumable/gem/material.csv → FRoseSimpleItemRow)
	// — for bag drops that have no equip model; auto-loaded by path in BeginPlay.
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* ConsumableTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* GemTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* MaterialTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* FaceItemTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* JewelTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* SubWpnTable = nullptr;
	// LIST_PAT.STB — cart/castle-gear/mount parts (ITEM_TYPE_RIDE_PART=14).
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* PatTable = nullptr;
	// Armor row for a slot ("body"/"arms"/"foot"/"cap"); null for non-armor slots.
	const FRoseArmorRow* GetArmorRow(const FString& Slot, int32 Id) const;
	// Simple (non-equipment) item row for slot "consumable"/"gem"/"material"; null otherwise.
	const struct FRoseSimpleItemRow* GetSimpleItemRow(const FString& Slot, int32 Id) const;

	// Localized display name of an equipped item (weapon or armor) from its table;
	// falls back to "<slot> <id>" when the table/row/name is absent.
	UFUNCTION(BlueprintCallable, Category = "Rose")
	FString GetItemName(const FString& Slot, int32 Id) const;
	// Localised item description (weapon/armor/consumable/…); empty if none.
	FString GetItemDescription(const FString& Slot, int32 Id) const;
	// ITEM_ICON_NO (STB col 9) for any (slot,id), whichever table owns it; 0 = none.
	// Feed to RoseUI::MakeIconBrush — see the icon rule: col 9 is used AS-IS.
	int32 GetItemIconIdx(const FString& Slot, int32 Id) const;
	/** LIST_FIELDITEM row for this item's ground-drop model, or 0. */
	int32 GetItemFieldModel(const FString& Slot, int32 Id) const;

	// ── Body shape ───────────────────────────────────────────────────────────
	// Chest bulge, in centimetres of outward push along the vertex normal.
	// Driven through the material's world-position offset rather than a morph
	// target — see UpdateBodyShape for why neither morphs nor bone scaling work
	// on this rig.  0 = the untouched mesh.
	UPROPERTY(EditAnywhere, Category = "Rose|Body") float BodyChestBulge = 0.f;
	UPROPERTY(EditAnywhere, Category = "Rose|Body") float BodyChestRadius = 18.f;
	void  SetBodyChestBulge(float Amount);
	// RoseBodyShape 6      — try a bulge of 6cm
	// RoseBodyShape 6 24   — ...with a wider region
	// RoseBodyShape 0      — back to the untouched mesh
	// Reports the resolved b1_chest position, so a silent no-op is
	// distinguishable from "the value went in and nothing moved".
	UFUNCTION(Exec) void RoseBodyShape(float Bulge = 6.f, float Radius = -1.f);
	float GetBodyChestBulge() const { return BodyChestBulge; }
	/** Push the chest region to the material; called every render frame. */
	void  UpdateBodyShape();

	// ── Held weapons — socket-attached, NOT merged (so the grip is tunable) ──────
	// The weapon static mesh (origin-authored by build_weapons_static.py) is attached
	// to a hand bone of the merged body; its placement is the relative transform
	// below, which you tune LIVE in PIE (keys) and persist (P → SaveConfig).  ROSE
	// shares one grip per hand, so GripRotR fixes ALL right-hand weapons and GripRotL
	// all left-hand ones (guns/dual off-hand).
	UPROPERTY(VisibleAnywhere,  Category = "Rose|Weapon") UStaticMeshComponent* WeaponR;
	UPROPERTY(VisibleAnywhere,  Category = "Rose|Weapon") UStaticMeshComponent* WeaponL;

	// Hand bones on the shared skeleton (b1_rhand=bone 12, b1_lhand=bone 8).
	// Used only as the FALLBACK when the skeleton has no ROSE dummy sockets
	// (i.e. legacy assets) — see RoseWeaponSocket below.
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") FName RightHandBone = TEXT("b1_rhand");
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") FName LeftHandBone  = TEXT("b1_lhand");

	// Grip placement relative to the attach point.
	//
	// IDENTITY by default now, and that is the point: weapon orientation is
	// DATA, not a tuned constant.  It composes from two sources the importer
	// preserves — the ZSC per-part transform (per ITEM, baked into the mesh) and
	// the ZMD hand dummy (per GENDER, a socket on the skeleton).  A single
	// global rotation could never be right for both a dagger and a bow, and had
	// to be re-derived every time the rig space changed.
	//
	// These keep their hand-tuned LEGACY values and are applied ONLY on the
	// legacy fallback path (no dummy socket).  On the native path they are
	// forced to identity — see ApplyWeaponGrips.  Zeroing the defaults instead
	// would have silently broken legacy weapon placement.
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") FRotator GripRotR = FRotator(-5.f, 0.f, 0.f);  // legacy tuned
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") FVector  GripLocR = FVector(8.f, 0.f, 0.f);    // legacy tuned
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") float    GripScaleR = 1.f;
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") FRotator GripRotL = FRotator::ZeroRotator;     // legacy tuned
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") FVector  GripLocL = FVector(9.f, 0.f, 0.f);    // legacy tuned
	UPROPERTY(EditAnywhere, config, Category = "Rose|Weapon") float    GripScaleL = 1.f;

	// PER-WEAPON-TYPE grips.  GripLoc/Rot/Scale above stay as the fallback for a
	// type that has never been tuned, so nothing regresses for weapons nobody
	// has touched yet.
	UPROPERTY(config) TArray<FRoseWeaponGrip> WeaponGrips;

	// What UpdateWeapon actually attached to this time.  ApplyWeaponGrips needs
	// it to tell a baked grip (dummy socket) from a tuned one (hand bone).
	FName RSocketInUse, LSocketInUse;

	// ROSE dummy sockets the native skeletal importer writes onto the skeleton:
	//   0 = R_HAND   1 = L_HAND   2 = L_SHIELD   3 = back
	//   4 = DUMMY_IDX_MOUSE (face item)          6 = DUMMY_IDX_CAP
	// Returns the socket name if this character's skeleton has it, or NAME_None
	// to fall back to the bone + tuned grip.
	FName RoseDummySocket(int32 DummyIndex) const;

	// Set the held weapon meshes from the equipped "weapon" id + apply the grips.
	void UpdateWeapon();
	// Push GripRot/Loc/Scale onto the live components (call after a nudge).
	void ApplyWeaponGrips();

	// ── Back item (wings/capes) — socket-attached like weapons, NOT merged ──────
	// The atlas + skeletal-merge gear path shares one material per mesh, so back
	// items that share a mesh (every Astarot wing = mesh_5175) could not carry
	// distinct textures.  Back is now its own static mesh with its own embedded
	// material (BackStatic/back_<id>), attached to the chest and placed by a
	// live-tuned transform — faithful to ROSE (back LINKS to the back dummy).
	UPROPERTY(VisibleAnywhere, Category = "Rose|Back") UStaticMeshComponent* BackItem;
	// Back dummy (ZMD dummy p_03) parents to the chest (b1_chest = bone 2).  Its
	// offset from the chest is the SAME for every back item (BACK has no
	// per-part xform), so this one transform restores the placement the old
	// skeletal-merge baked in.  Default = M·dummy_local·M^-1 of the ZMD dummy
	// (ROSE (0.070,-0.126,0) m → ~(7,12.5,0) cm).  Live-tunable to dial signs.
	UPROPERTY(EditAnywhere, config, Category = "Rose|Back") FName    BackBone  = TEXT("b1_chest");
	UPROPERTY(EditAnywhere, config, Category = "Rose|Back") FRotator BackRot   = FRotator::ZeroRotator; // tuned
	// Tuned live in the F8 back-placement panel (2026-07-21): sits the wing on the
	// upper back, behind the character.  One shared transform for all back items.
	UPROPERTY(EditAnywhere, config, Category = "Rose|Back") FVector  BackLoc   = FVector(7.42f, -17.02f, -0.55f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|Back") float    BackScale = 1.f;
	// Set the back mesh from the equipped "back" id + apply the placement.
	void UpdateBack();
	// Push BackLoc/Rot/Scale onto the live BackItem (call after a slider nudge).
	void ApplyBackPlacement();
	class UStaticMesh* LoadBackStatic(const FString& ItemName) const;

	// ── Sub-weapon (shield/off-hand) — socket-attached to the LEFT hand ────────
	// Classic gate: a sub-weapon needs a ONE-HANDED right weapon; equipping a
	// two-hander/dual/left-hand weapon auto-returns the shield to the bag.
	UPROPERTY(VisibleAnywhere, Category = "Rose|SubWeapon") UStaticMeshComponent* SubWeapon;
	UPROPERTY(EditAnywhere, config, Category = "Rose|SubWeapon") FRotator SubGripRot = FRotator(0.0f, -4.0,  -65.f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|SubWeapon") FVector  SubGripLoc = FVector(-14.f, -7.0f,  0.0f);
	UPROPERTY(EditAnywhere, config, Category = "Rose|SubWeapon") float    SubGripScale = 1.f;
	void UpdateSubWeapon();
	class UStaticMesh* LoadSubWpnStatic(const FString& ItemName) const;
	// True when the CURRENT weapon leaves the left hand free (Hand == "Right").
	bool WeaponAllowsSubWeapon() const;

	// ── Character base stats (ROSE abilities) — drive equip-requirement checks ──
	// Faithful to CUserDATA::Check_EquipCondition: an item lists up to two
	// (ability, amount) requirements and is equippable when the character's value
	// for each ability ≥ amount.
	// Replicated to everyone: a remote client needs them to derive the MaxHP/level
	// its nameplate and HP bar show for other players.
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Strength = 15;
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Dexterity = 15;
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Intelligence = 15;
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Concentration = 15;
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Charm = 15;
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Sense = 15;
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Stats") int32 Level = 1;

	// Experience toward the next level (RoseExpData.h curve).  GiveExp adds and
	// auto-levels; ExpRevision bumps so the HUD gauge can refresh lazily.
	// Owner-only: nobody else's client has any use for your XP.
	UPROPERTY(Replicated) int64 Exp = 0;
	UPROPERTY() int32 ExpRevision = 0;

	// Grant experience (monster kills / quest rewards): adds Exp and levels up
	// while the threshold is met, restoring HP/MP and recomputing stats.
	void GiveExp(int64 Amount);
	int64 GetExp() const { return Exp; }
	int64 GetNeedExp() const;         // XP to reach the next level
	float GetExpFraction() const;     // 0..1 for the HUD bar
	int32 GetExpRevision() const { return ExpRevision; }

	// ── Derived stats (RoseStatFormulas.h — transcribed from the ROSE server) ──
	// Base stats + the equipped loadout produce every derived value; movement,
	// anim rates and combat all read from here.  SpeedMultiplier is a dev knob
	// (the F9 slider) layered on top of the faithful formula.
	UPROPERTY(EditAnywhere, Category = "Rose|Stats", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SpeedMultiplier = 1.f;

	struct FDerivedStats
	{
		float RunSpeed = 425.f;   // cm/s (== UE MaxWalkSpeed)
		int32 Avoid = 0, Crit = 0, Hit = 0, Defense = 0, Resist = 0;
		int32 MaxHP = 0, MaxMP = 0, MaxWeight = 0;
		int32 AttackPower = 0, AttackSpeed = 100;   // 100 = normal anim rate
	};
	FDerivedStats ComputeDerivedStats() const;
	// Recompute + push onto the movement component; call after any stat/equip change.
	void ApplyDerivedStats();

protected:
	FDerivedStats Derived;

	// ── Stats debug panel (F9): sliders for speed multiplier + base stats ──────
	TSharedPtr<class SWidget> StatsPanel;
	bool bStatsPanelVisible = false;
	void ToggleStatsPanel();
	TSharedRef<class SWidget> BuildStatsPanel();

	// ── Back-placement tuning panel (F8): live sliders for BackLoc/Rot/Scale ───
	// Drag to move the equipped wing/cape live; the readout prints the exact
	// numbers to paste into the BackLoc/BackRot defaults (or press Save to config).
	TSharedPtr<class SWidget> BackTunePanel;
	bool bBackTuneVisible = false;
	void ToggleBackTunePanel();
	TSharedRef<class SWidget> BuildBackTunePanel();

	// ── ROSE UI: classic windows (SRoseUIWindow, Content/UI/Layouts/*.json) ────
	// I = dlgitem (inventory: clickable equip slots at the client's authored
	// positions — click cycles the slot's items, Shift+click unequips).
	// C = dlgavata (character sheet: live stats; the + buttons raise stats).
	TSharedPtr<class SWidget> InventoryRoot;      // viewport overlay canvas
	TSharedPtr<class SRoseModernWindow> InventoryWindow;
	TSharedPtr<class SWidget> SheetRoot;
	TSharedPtr<class SRoseModernWindow> SheetModernWindow;
	TArray<TSharedPtr<struct FSlateBrush>> InventoryBrushes;   // icon brushes
	UPROPERTY() TArray<class UTexture2D*> InventoryIconTextures;   // keep icons from GC
	bool bInventoryVisible = false;
	bool bSheetVisible = false;
	bool bInventoryDirty = false;                 // equip changed → rebuild icons

public:
	// Public so the HUD menu bar (RoseUIHud.cpp) can drive the same toggles.
	void ToggleInventory();
	void ToggleCharacterSheet();
	void ToggleSkills();       // K / hamburger "Skill" → modern skill panel
	void ToggleSkillTree();    // X / hamburger "Tree"  → modern skill tree
	void FocusChat();          // Enter → focus the modern chat input
	void ToggleMinimapKey();   // M → toggle the modern minimap
	void ToggleOptionsKey();   // Z → toggle the modern options window
	void ToggleQuestKey();     // Q → toggle the modern quest journal
	void UpdateUIInputMode();                     // cursor policy (see UI manager)
	// Equip the next/previous valid item in a slot (the equip-slot click).
	void CycleSlotItem(const FString& Slot, int32 Dir);

	// ── Classic-UI window manager (RoseUIManager.h): HUD + minimap + skills +
	//    chat + misc windows; `RoseUI <dialog>` opens ANY converted layout. ──
	UPROPERTY(VisibleAnywhere, Category = "Rose|UI")
	class URoseUIManager* UI = nullptr;
	UFUNCTION(Exec) void RoseUI(const FString& Dialog);

	// For Sounds
    //UPROPERTY(EditAnywhere, Category = "Rose|Audio")
    //USoundBase* HitSound = nullptr;

	// Character's value for a ROSE ability id (AT_STR=10..AT_SENSE=15, AT_LEVEL=31).
	// Abilities we don't model return MAX_int32 so they never block a dev equip.
	int32 GetAbilityValue(int32 AbilityType) const;

	// True if the character meets item (slot,id)'s equip requirements; on false,
	// OutReason describes the first unmet one (e.g. "Requires STR 16").
	UFUNCTION(BlueprintCallable, Category = "Rose")
	bool MeetsRequirements(const FString& Slot, int32 Id, FString& OutReason) const;

	// ── Defender interface for monster attacks (ARoseMonster rolls the ROSE
	//    combat formulas against these, mirroring the player's melee path) ────
	int32 GetAvoidStat()   const { return Derived.Avoid; }
	int32 GetDefenseStat() const { return Derived.Defense; }
	int32 GetResistStat()  const { return Derived.Resist; }   // vs magic-damage mobs
	// Attacker-side stats for the skill component's damage rolls.
	int32 GetAttackPowerStat() const { return Derived.AttackPower; }
	int32 GetHitStat()  const { return Derived.Hit; }
	int32 GetCritStat() const { return Derived.Crit; }
	int32 GetMaxHPStat() const { return Derived.MaxHP; }

	// ── Summons (SKILL_ACTION_SUMMON_PET = 14) ──────────────────────────────
	//
	// ROSE gates summons on a CAPACITY budget, not a count or a timer:
	//   max   = 50 + passive AT_PSV_SUMMON_MOB_CNT (62)   cuserdata.h:486
	//   cost  = LIST_NPC col 21 per summoned mob          NPC_NEED_SUMMON_CNT
	//   cast is refused when cost + used > max            skill.cpp:1189
	// Measured costs: ButterFly 30, Bonfire 37, Beast 50 — so the base budget
	// holds one Beast or one butterfly, and the passive is what buys more.
	//
	// col 21 is DUAL PURPOSE — it is also NPC_SELL_TAB0, which is why the row
	// field is called SellTab0 and the STB header reads "sell tab1".  Same
	// column, different meaning depending on whether the NPC is a shop or a
	// summonable mob.
	int32 GetSummonMaxCapacity() const;
	int32 GetSummonUsedCapacity() const { return SummonUsedCapacity; }

	// Summon a mob if the budget allows.  Returns false (and messages the
	// player, as the client does) when it does not.
	bool TrySummon(int32 NpcId);

	// Called when a summon dies or is dismissed, to give its capacity back.
	void ReleaseSummon(class ARoseMonster* Summon);

	// Live summons owned by this character, parallel to the capacity they hold.
	UPROPERTY() TArray<TObjectPtr<class ARoseMonster>> ActiveSummons;
	int32 GetMaxMPStat() const { return Derived.MaxMP; }
	int32 GetAttackSpeedStat() const { return Derived.AttackSpeed; }   // 100 = 1.00 atk/s
	float GetRunSpeedStat()    const { return Derived.RunSpeed; }      // cm/s (pre-multiplier)
	float GetSpeedMultiplier() const { return SpeedMultiplier; }
	// Raise a base stat by name ("STR"/"DEX"/"INT"/"CON"/"CHARM"/"SENSE") and
	// re-derive — the character-sheet "+" buttons (dev: no stat-point currency yet).
	void RaiseStat(const FString& Which);
	// Incoming monster hit/miss: floating text + HP; at 0 HP the dev character
	// just refills (no death flow yet).
	void ReceiveMonsterHit(float Damage, bool bCritical);
	void ReceiveMonsterMiss();
	// Vitals replicate to everyone (other players' HP bars); the server owns them.
	UPROPERTY(Replicated) float CurrentHP = 0.f;   // set from Derived.MaxHP on the first ApplyDerivedStats
	UPROPERTY(Replicated) float CurrentMP = 0.f;   // mirrors CurrentHP; skills spend it (AT_MP costs)

	// ── Job + skills (URoseSkillComponent — DataTables skills/jobs/skill_points;
	//    formulas in RoseStatFormulas.h; server logic cited in the component) ──
	UPROPERTY(VisibleAnywhere, Category = "Rose|Skills")
	class URoseSkillComponent* Skills = nullptr;

	// ── Quests (URoseQuestComponent — the QSD trigger engine over
	//    Content/Quests/quests.json + tagQuestData state; RoseQuest.h) ──
	UPROPERTY(VisibleAnywhere, Category = "Rose|Quests")
	class URoseQuestComponent* Quests = nullptr;

	// Equipped weapon's STB Type (211 = 1H sword ... 271 = crossbow; 0 = bare).
	// Skills gate on it (SKILL_NEED_WEAPON) and passives select by it.
	int32 GetEquippedWeaponType() const;

	// Play a skill's own cast/action motion once (SKILL_ANI_ACTION_TYPE /
	// SKILL_ANI_CASTING rows resolved against the equipped weapon's Motion Type
	// — RoseSkillMotionData.h).  ActionMotion is preferred; CastMotion is the
	// fallback pose; if neither is imported, the basic-attack swing is used so
	// the cast is never silent.  SpeedPct = SKILL_ANI_ACTION_SPEED (percent).
	// Reuses the attack state so locomotion resumes and the basic hit is muted;
	// returns the scaled duration (0 = nothing to play).
	float PlaySkillMotion(int32 ActionMotion, int32 CastMotion, float SpeedPct);

	// AnimSequence for a skill action row (TYPE_MOTION row) played with the
	// current weapon Motion Type, gender-aware with motion-type-0 fallback;
	// null when that row's ZMO isn't imported into base.glb.
	UAnimSequence* LoadSkillAnim(int32 ActionRow) const;

	// Dev console commands (possessed-pawn Exec): job/skill state without UI.
	//   RoseSetJob 111 | RoseLearnSkill 321 | RoseAddSkillPoints 50
	//   RoseHotbar 1 321 (slot 1-4) | RoseSkillInfo
	UFUNCTION(Exec) void RoseSetJob(int32 JobId);
	UFUNCTION(Exec) void RoseLearnSkill(int32 SkillId);
	UFUNCTION(Exec) void RoseAddSkillPoints(int32 Points);
	UFUNCTION(Exec) void RoseHotbar(int32 Slot, int32 SkillId);
	UFUNCTION(Exec) void RoseSkillInfo();
	// Quest diagnostics: fire a QSD trigger by name / dump quest state.
	UFUNCTION(Exec) void RoseQuestTrigger(const FString& Name);
	UFUNCTION(Exec) void RoseQuestInfo();

	// Aggregate stats of everything equipped: armor Defense/MagicResist summed,
	// weapon AttackPower, and the equipped boots' MoveSpeed.  Hit/Avoid carry the
	// REFINE grade bonuses (weapon grade -> Attack+Hit, armor grade ->
	// Defense+MagicResist+Avoid, per cuserdata.cpp / LIST_GRADE.STB).
	struct FLoadoutStats { int32 Defense = 0, MagicResist = 0, Attack = 0, MoveSpeed = 0,
	                       Hit = 0, Avoid = 0; };
	FLoadoutStats ComputeLoadoutStats() const;

	// ── Refine ("grade") system — LIST_GRADE.STB via DataTables/refine.json ──
	// One entry per grade 1..9: stat bonuses + the authentic glow RGB the classic
	// client tints refined items with (grade 1 faint green ... grade 9 white-gold).
	struct FRoseRefineGrade { int32 Atk = 0, Hit = 0, Def = 0, Res = 0, Avoid = 0;
	                          FLinearColor Glow = FLinearColor::Black; };
	TArray<FRoseRefineGrade> RefineGrades;   // index 0 = grade 1
	void LoadRefineData();
	// Refine grade per equipped slot (0 = unrefined).  Kept beside Equipped and
	// snapshotted with it; bag instances carry theirs in FRoseItemStack::Refine.
	UPROPERTY() TMap<FString, int32> EquippedRefine;
	int32 GetEquippedRefine(const FString& Slot) const
	{ const int32* R = EquippedRefine.Find(Slot); return R ? *R : 0; }

	// ── Bonus options (classic GEM_OP / LIST_JEMITEM) ─────────────────────────
	// A dropped item can roll an OPTION: a LIST_JEMITEM row granting up to two
	// named stats (e.g. "STR +1, Max HP +5").  The item drops UNAPPRAISED (red
	// in the tooltip, stats hidden+inactive); appraising reveals and activates
	// them (blue).  Instance data rides bag<->equip like Refine.
	TMap<int32, TArray<FIntPoint>> JemOptions;   // option row -> (AT type, value)*
	void LoadJemOptions();
	// "STR +1, Max HP +5" — display text for an option row ("" if none/unknown).
	FString BonusStatText(int32 GemOp) const;
	UPROPERTY() TMap<FString, int32> EquippedBonus;      // option row per slot
	UPROPERTY() TMap<FString, bool>  EquippedAppraised;  // option revealed+active?
	int32 GetEquippedBonus(const FString& Slot) const
	{ const int32* B = EquippedBonus.Find(Slot); return B ? *B : 0; }
	bool GetEquippedAppraised(const FString& Slot) const
	{ const bool* A = EquippedAppraised.Find(Slot); return A ? *A : true; }
	// Sum of every APPRAISED equipped option's stats, keyed by t_AbilityINDEX.
	void SumEquipOptionStats(TMap<int32, int32>& Out) const;
	// Dev: appraise everything in the bag (until the NPC/hammer flow exists).
	UFUNCTION(Exec) void RoseAppraiseAll();
	// Grade bonuses for one equipped slot (zeroes when unrefined/unknown).
	FRoseRefineGrade RefineBonus(const FString& Slot) const;
	// Tint the weapon glow overlays from the equipped weapon's grade.
	// Global multiplier on the refine glow — the shader's _RefineIntensity.
	//
	// The PER-GRADE amount already comes from the grade's own RGB (LIST_GRADE
	// scales it: +1 is (30,50,30), +9 is (255,255,140)), so this is a single
	// scale over all of them rather than anything per-item.  1.0 = the table's
	// authored strength; the old gear-atlas path effectively used 0.35.
	UPROPERTY(EditAnywhere, Category = "Rose|Refine") float RefineGlowScale = 1.f;
	/** RoseGlowScale 0.35 — rescale every refine glow live. */
	UFUNCTION(Exec) void RoseGlowScale(float Scale);

	/** The bare-skin item id a slot falls back to when unequipped, or -1 when the
	 *  slot is simply empty.  body/arms/foot each have an "empty" mesh at id 1 —
	 *  the same ids a new character is created with. */
	static int32 NakedDefaultForSlot(const FString& Slot)
	{
		return (Slot == TEXT("body") || Slot == TEXT("arms") || Slot == TEXT("foot"))
			? 1 : -1;
	}

	// ── Zone music ───────────────────────────────────────────────────────────
	// One looping track per zone, from LIST_ZONE's "BGM Day"/"BGM Night" via
	// Content/Sounds/zone_bgm.json (tools/gen_zone_bgm.py).  Held as a component
	// so it can be stopped/swapped on a zone change rather than layering.
	UPROPERTY() class UAudioComponent* BgmAudio = nullptr;
	FString BgmZone;                      // zone the current track belongs to
	void UpdateZoneBgm();

	// ── Pick-up ──────────────────────────────────────────────────────────────
	// A click/keypress targets the nearest drop within this radius and WALKS to
	// it; the actual transfer still requires being inside TryPickUp's reach.
	static constexpr float kPickUpSeekRange = 2500.f;
	TWeakObjectPtr<class ARoseGroundItem> PendingPickUp;
	void TickPickUpApproach(float DeltaSeconds);
	void DoPickUp(class ARoseGroundItem* Item);
	// ROSE's basic-skill rows: motions and behaviour come from the table.
	static constexpr int32 kSkillSit    = 11;
	static constexpr int32 kSkillPickUp = 12;
	static constexpr int32 kSkillJump   = 13;
	/** Toggle ROSE's sit pose (skill 11, ActionMotion 4). */
	virtual void OnJumped_Implementation() override;
	void ToggleSit();
	bool bSitting = false;

	// ── NPC interaction ──────────────────────────────────────────────────────
	// Opening range, and the (larger) range at which an open conversation closes.
	// The gap is hysteresis: equal values would flicker the window at the edge.
	static constexpr float kTalkRange = 600.f;
	static constexpr float kTalkCloseRange = 900.f;
	TWeakObjectPtr<class ARoseNpc> PendingTalkNpc;   // clicked, walking to them
	// NPCs already added to the capsule's move-ignore list.  They stay
	// clickable (the capsule still blocks the Pawn TRACE) while this
	// character walks through them — see RefreshNpcMoveIgnores.
	TSet<TWeakObjectPtr<class ARoseNpc>> IgnoredNpcs;
	double NextNpcIgnoreSweep = 0.0;
	void RefreshNpcMoveIgnores();
	void TickTalkApproach(float DeltaSeconds);
	void TickTalkRange(float DeltaSeconds);

	void ApplyRefineGlow();
	/** Refine glow on one socket-attached item (weapon / sub-weapon / back),
	 *  using the same M_RoseChar parameters the merged armour uses. */
	void GlowStaticItem(class UStaticMeshComponent* Comp, const FString& Slot);
	/** Refine glow on the merged armour (body/arms/foot/cap/faceitem/hair).
	 *  Socket-attached items use an overlay; the merged body cannot, so this
	 *  drives M_RoseChar's RefineColor/RefineIntensity on the refined part's own
	 *  material slots. */
	void ApplyMergedRefineGlow(const TArray<class USkeletalMesh*>& Parts,
	                           const TArray<FString>& InPartSlots);
	/** Re-apply armour glow from the LAST merge, without re-merging. */
	void RefreshMergedRefineGlow();

	// What the last merge was built from, kept so the refine sliders can restyle
	// armour live: setting RefineColor/RefineIntensity on existing material slots
	// costs nothing, where re-merging per slider tick would be unusable.
	UPROPERTY() TArray<class USkeletalMesh*> MergedParts;
	TArray<FString> MergedPartSlots;
	// (first merged material slot, count) per merged part — the section ids the
	// merge was TOLD to use, so the glow addresses known slots instead of ones
	// inferred from material counts.
	TArray<FIntPoint> PartSlotRanges;
	// Dev: set an equipped slot's refine grade 0..9 (stats + glow react).
	// Slots: weapon, cap, body, foot, arms, back, faceitem.  RoseRefine 5 alone
	// refines the weapon.
	UFUNCTION(Exec) void RoseRefine(int32 Grade);
	UFUNCTION(Exec) void RoseRefineSlot(const FString& Slot, int32 Grade);

	// Equip an item into a slot: "body","arms","foot","cap","back","hair","face",
	// "faceitem" (masks/goggles — BODY_PART_FACE_ITEM), "weapon".
	// Id < 0 clears (unequips) the slot.  Re-merges the character mesh.
	UFUNCTION(BlueprintCallable, Category = "Rose")
	void EquipItem(const FString& Slot, int32 Id);

	// ── Client → server intent ──────────────────────────────────────────────
	// A client never mutates its own loadout: it asks, the server runs the SAME
	// validated function (TryEquipFromBag / TryUnequipToBag / EquipItem), and the
	// result arrives as replicated Bag + RepEquipped.  On a listen host / in
	// standalone HasAuthority() is already true, so these are never sent and the
	// single-player path is byte-for-byte what it was.
	UFUNCTION(Server, Reliable) void Server_EquipItem(const FString& Slot, int32 Id);
	UFUNCTION(Server, Reliable) void Server_EquipFromBag(const FString& Slot, int32 Id);
	UFUNCTION(Server, Reliable) void Server_UnequipToBag(const FString& Slot);
	// Character Select ran on the client, so only the client knows the chosen
	// look until it tells the server.  Placeholder for a real roster service.
	UFUNCTION(Server, Reliable) void Server_SetInitialLook(const FString& InGender, int32 Hair, int32 Face);
	// A basic-attack swing: the client plays the animation, the server rolls the
	// damage (DoMeleeHit re-runs there with authoritative positions and stats).
	UFUNCTION(Server, Reliable) void Server_MeleeSwing(class ARoseMonster* TargetMob);
	// Ground loot: the client names a drop it can see, the server re-checks reach
	// (TryPickUp) and moves it into the bag.
	UFUNCTION(Server, Reliable) void Server_PickUp(class ARoseGroundItem* Item);

	// Currently-equipped item id in a slot (-1 = none) — for the inventory UI.
	int32 GetEquippedId(const FString& Slot) const;

	// ── Bag + money (drops feed these; the inventory UI reads them) ──────────
	// Owner-only (COND_OwnerOnly): your bag is nobody else's business, and not
	// sending it to every client is also the anti-cheat boundary.
	UPROPERTY(ReplicatedUsing = OnRep_Bag) int32 Zuly = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Bag) TArray<FRoseItemStack> Bag;
	UPROPERTY() int32 BagRevision = 0;      // bumped on any bag/zuly change (UI refresh)
	UFUNCTION() void OnRep_Bag() { ++BagRevision; }
	int32 GetZuly() const { return Zuly; }

	// ── dlginfo's readouts ────────────────────────────────────────────────────
	// The player panel needs a name, a job and a carried-weight fraction; none
	// of these had an accessor because the old hand-built HUD hardcoded them.
	/** Character name from the backend session, or the actor's as a fallback. */
	FString GetDisplayName() const;
	/** Job name from the jobs DataTable (skills component holds CurrentJob). */
	FString GetJobName() const;
	/** An item's unit weight, from whichever table owns that slot. */
	int32 GetItemWeight(const FString& Slot, int32 Id) const;
	/** Carried weight, and it over MaxWeight (0..1) — the bag's item weights. */
	int32 GetCarriedWeight() const;
	float GetCarriedWeightFraction() const;
	const TArray<FRoseItemStack>& GetBag() const { return Bag; }
	int32 GetBagRevision() const { return BagRevision; }
	void AddZuly(int32 Amount);
	// Add Count of (Slot,Id) to the bag; stackables merge, equipment stays unique.
	// Bonus/bAppraised are the rolled drop stats (equipment instances).
	void AddItemToBag(const FString& Slot, int32 Id, int32 Count = 1, int32 Bonus = 0, bool bAppraised = true, int32 Refine = 0);

	// ── PAT (cart / castle gear) + ammo + fuel — datatype.h faithful ────────
	// 5 ride part slots (t_eRidePART: BODY/ENGINE/LEG/ABIL/ARMS), item ids
	// into pat.csv (LIST_PAT.STB); -1 = empty.
	UPROPERTY() TArray<int32> RideParts;
	// 3 shot slots (t_eSHOT: ARROW/BULLET/THROW), each an item id into
	// material.csv (LIST_NATURAL class 431/432/433,421-423) + count.
	UPROPERTY() TArray<FRoseItemStack> AmmoSlots;
	// Cart fuel (AT_FUEL): refilled by USE items of class 317 (USE_ITEM_FUEL).
	UPROPERTY() float Fuel = 0.f;
	UPROPERTY() float MaxFuel = 1000.f;

	UFUNCTION(BlueprintCallable, Category = "Rose|Pat")
	void EquipRidePart(int32 PartIdx, int32 PatId);      // -1 clears
	int32 GetRidePart(int32 PartIdx) const;
	// Equip an ammo stack from the bag into its shot slot (t_eSHOT by class).
	UFUNCTION(BlueprintCallable, Category = "Rose|Pat")
	void EquipAmmoFromBag(int32 BagIndex);
	// Return a loaded shot slot's stack to the bag.
	UFUNCTION(BlueprintCallable, Category = "Rose|Pat")
	void UnequipAmmo(int32 ShotIdx);
	// Shot type (0..2) for a material.csv Subtype/class, or -1 (citem.cpp
	// GetNaturalBulletType: 431 arrow, 432 bullet, 433/421-423 throw).
	static int32 ShotTypeForClass(int32 ItemClass);

	// ── DEV / cheat commands (console ` in dev builds, and the F10 spawner) ──
	// RoseGive weapon 128 [count] — spawn any item by slot+id into the bag.
	UFUNCTION(Exec) void RoseGive(const FString& Slot, int32 Id, int32 Count = 1);
	UFUNCTION(Exec) void RoseZuly(int32 Amount);
	UFUNCTION(Exec) void RoseDev();                 // console alias for the window
	void ToggleDevSpawn();                          // F10 — the dev spawn window
	// Every (slot,id) known to the item tables — feeds the dev spawner list.
	struct FRoseDevItem { FString Slot; int32 Id = 0; };
	void GetAllItemIdsForDev(TArray<FRoseDevItem>& Out) const;
	// t_eRidePART slot (0..4) for a pat.csv Subtype/class, or -1.
	static int32 RidePartForClass(int32 ItemClass);
	// Consume one class-317 fuel consumable from the bag -> full tank.
	UFUNCTION(BlueprintCallable, Category = "Rose|Pat")
	void UseFuelItem(int32 ConsumableId);

	// ── Consumables (LIST_USEITEM effect block on FRoseSimpleItemRow) ────────
	// Use one consumable from the bag, faithful to classUSER::Use_InventoryITEM
	// (gs_user.cpp:915) + CUserDATA::Use_ITEM (cuserdata.cpp:921):
	//   1. requirement check   GetAbilityValue(NeedAbility) >= NeedValue
	//   2. cooldown check      per CooldownType group, CooldownSec seconds
	//   3. StatusId != 0 -> a regen ticking StatusPerSec/s until AddValue is
	//      delivered;  StatusId == 0 -> Add_AbilityValue instantly
	//   4. one is removed from the stack
	// Class 317 (USE_ITEM_FUEL) still routes to UseFuelItem.  Returns false and
	// fills OutReason when the item can't be used right now.
	UFUNCTION(BlueprintCallable, Category = "Rose|Items")
	bool UseConsumableItem(int32 ConsumableId);
	// Same, with the refusal reason (matches TryEquipFromBag's convention; a
	// separate name because UHT does not handle overloading a UFUNCTION).
	bool TryUseConsumableItem(int32 ConsumableId, FString& OutReason);
	// Seconds left on a consumable's cooldown group (0 = ready) — for the UI.
	float GetConsumableCooldown(int32 ConsumableId) const;

protected:
	// Cooldown group (USEITEM_COOLTIME_TYPE) -> world time it frees up.
	TMap<int32, float> ConsumableCooldownEnd;
	// A potion delivering AddValue over time at StatusPerSec per second.
	struct FRosePotionRegen
	{
		int32 IngType = 0;      // eING_TYPE: 1 = HP, 2 = MP
		float PerSec = 0.f;
		float Remaining = 0.f;  // total left to deliver
	};
	TArray<FRosePotionRegen> PotionRegens;
	void TickPotionRegens(float DeltaSeconds);
	// Instant Add_AbilityValue for a use item (cuserdata.cpp:933).
	void AddAbilityFromItem(int32 Ability, int32 Amount);

public:
	float GetFuel() const { return Fuel; }
	float GetMaxFuel() const { return MaxFuel; }

	// Mount / dismount whatever is assembled in the PAT slots — a cart, a
	// castle gear, or a standalone mount. The vehicle attaches under the
	// character and the character MESH re-parents onto the vehicle, so the
	// rider inherits its facing (CObjCART links the avatar to a cart dummy).
	// Cart/CG burn fuel while moving; an empty tank auto-dismounts. Mounts
	// need neither parts nor fuel.
	UFUNCTION(BlueprintCallable, Category = "Rose|Pat")
	void ToggleRide();
	bool IsRiding() const { return RideCart != nullptr; }
	// Fallbacks for items whose LIST_PAT speed/fuel columns are blank.
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") float RideSpeedMult = 1.8f;
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") float FuelDrainPerSec = 0.6f;
	// LIST_PAT "Fuel Rate" -> fuel per second while moving.
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") float FuelDrainScale = 0.3f;
	// A mount's "Movement Speed" is absolute in ROSE units -> cm/s.
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") float MountSpeedScale = 0.5f;
	// How far the vehicle sits below the capsule origin.
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") float RideCapsuleDrop = 90.f;

	// pat_parts.csv / pat_motion.csv (tools/gen_pat_tables.py).
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") UDataTable* PatPartTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") UDataTable* PatMotionTable = nullptr;
	const struct FRosePatPartRow* GetPatPartRow(int32 PatId) const;

private:
	UPROPERTY() class ARoseCart* RideCart = nullptr;
	float BaseWalkSpeed = 0.f;
	// Where the mesh sat on the capsule before it was parented to the vehicle.
	FVector RideMeshRestLocation = FVector::ZeroVector;
	FRotator RideMeshRestRotation = FRotator::ZeroRotator;
	ERosePatAnim RideAnim = static_cast<ERosePatAnim>(255);   // 255 = none yet
	void TickRide(float DeltaSeconds);
	void SetRideAnim(ERosePatAnim Action);
	float RideWalkSpeed(const class ARoseCart* Cart) const;
public:
	// Remove Count of (Slot,Id) from the bag (e.g. after equipping it).
	void ConsumeBagItem(const FString& Slot, int32 Id, int32 Count = 1);
	// ── Authoritative item ops (server-shaped: validate → apply → revision) ──
	// THE mutation points for player-driven item actions.  Each one validates
	// against the character's own state (ownership, equip requirements, reach)
	// and only then applies — never trusting the caller's arguments — so when
	// networking lands they become the bodies of Server_* RPCs verbatim and
	// the UI keeps calling the same wrappers below.  On failure OutReason says
	// why (the wrappers echo it to the chat log).
	bool TryEquipFromBag(const FString& Slot, int32 Id, FString& OutReason);
	bool TryUnequipToBag(const FString& Slot, FString& OutReason);
	bool TryPickUp(class ARoseGroundItem* Item, FString& OutReason);

	// Drag-and-drop equip: equip a bag item, consuming it, and return the
	// previously-equipped item (if any) to the bag (a swap).  Thin wrapper over
	// TryEquipFromBag that reports failures to chat.
	void EquipFromBag(const FString& Slot, int32 Id);
	// Drag-and-drop unequip: clear a slot and drop the item into the bag.
	void UnequipToBag(const FString& Slot);
	// Roll a killed monster's loot (RoseDrops.h / ITEM_DROP.STB) and SPAWN it on
	// the ground at DropLoc (ROSE loot is picked up manually).  Called by
	// ARoseMonster::Die with the corpse location + the NPC's drop columns.
	void SpawnMonsterLoot(const FVector& DropLoc, int32 MobLevel, int32 DropType, int32 DropItem, int32 DropMoney);
	// Manual pickup (bound to a key): collect the nearest ground item in reach.
	void PickUpNearest();

protected:


	// Currently equipped item per slot (ROSE: one model, all parts merged onto
	// ONE skeleton — GetMesh() holds the single merged body).
	UPROPERTY() TMap<FString, int32> Equipped;
	UPROPERTY() class USkeleton* SharedSkeleton = nullptr;

	// Resolve SharedSkeleton for the current Gender, NATIVE first, and record
	// which asset era won.  Call this anywhere Gender changes.
	void RefreshSharedSkeleton();

	// Body yaw + mirror for the asset era in use.  Legacy meshes carry a baked
	// Y negate that the component must cancel; native ones do not.  Using the
	// wrong era's transform makes the character face backwards while moving
	// forward, with its hands swapped.
	void ApplyMeshEraTransform(bool bNative);

	// -1 when the body component is mirrored (legacy rig), else +1.  Anything
	// attached to the body inherits that reflection, so socket-attached items
	// must negate their own X to cancel it or they render mirrored.
	float AttachedMirrorX() const;

	// True when the native (RoseEditor) character assets are present.
	//
	// This is deliberately ALL-OR-NOTHING per character.  Native parts are built
	// on SK_Rose_<G>_Skeleton and legacy parts on base_Skeleton; a merge can only
	// target one skeleton, and feeding it meshes from both eras produces a
	// character assembled from mismatched rigs rather than an honest failure.
	// So the skeleton decides the era, and every part follows it.


	// ── Replicated appearance (the wire form of the maps above) ─────────────
	// Gender + loadout are the only things a remote client needs to reproduce
	// this character's model, so they replicate to EVERYONE (not just the
	// owner).  Both share one OnRep so a gender+loadout change rebuilds once.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance) FString RepGender;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance) TArray<FRoseEquipRep> RepEquipped;
	UFUNCTION() void OnRep_Appearance();
	// Client: RepGender/RepEquipped → the Equipped* maps → re-merge the mesh.
	void ApplyReplicatedAppearance();
	bool bAppearanceDirty = false;

	// Capacity currently held by live summons (CObjUSER::m_iSummonMobCapacity).
	// Replicated so a remote client's gauge matches the server's truth.
	UPROPERTY(Replicated) int32 SummonUsedCapacity = 0;
	bool bLocalSetupDone = false;

	// ── all-gear-from-Arua (body/arms/foot/cap/back) ──────────────────────────
	// One visual part of an equipped gear item, resolved from gear_equip.json:
	// which deduped mesh (/Game/Characters/Gear/mesh_<Mesh>) + its atlas cell.
	// ZSC-faithful gear masters (zz_material): opaque default, masked when
	// alpha&alpha_test, translucent when alpha only. MIDs can't change blend
	// mode, hence three parents sharing the same parameter names.

	// Rebuild GetMesh() by merging every equipped part onto the shared skeleton.
	// (Weapons are NOT merged — they are socket-attached via UpdateWeapon.)
	void RebuildMesh();
	USkeletalMesh* LoadPart(const FString& Slot, int32 Id) const;
	// "Female"/"Male" -> the "F"/"M" the native importer keys its folders on.
	const TCHAR* GenderKey() const;
	// Which armor DataTable backs a slot ("body"/"arms"/"foot"/"cap"), or null.
	UDataTable* ArmorTableForSlot(const FString& Slot) const;
	// Static weapon mesh by asset name, e.g. "weapon_400" or "weapon_400_off"
	// (/Game/Characters/Modular/WeaponsStatic/<name>/<name>/StaticMeshes/<name>).
	class UStaticMesh* LoadWeaponStatic(const FString& ItemName) const;

	// ── Live grip tuning (PIE) ──────────────────────────────────────────────────
	// Nudge the grip of the hand you're tuning, then read the values off the HUD and
	// press P to persist (SaveConfig).  bTuneLeftHand picks which hand the keys move.
	bool bTuneLeftHand = false;
	float GripStepDeg = 5.f;
	float GripLocStep = 1.f;                   // cm per location nudge
	/** F9 — open/close the grip tuner window. */
	void ToggleGripTuner();

public:
	// ── Grip tuner (RoseUIGripTuner.cpp) ──────────────────────────────────────
	// One shared grip per hand, edited live.  Reads/writes whichever hand
	// bTuneLeftHand selects so the UI does not branch on it everywhere.
	// PUBLIC: the tuner widget is a free function in another translation unit.
	bool IsTuningLeftHand() const { return bTuneLeftHand; }
	void SetTuningLeftHand(bool bLeft) { bTuneLeftHand = bLeft; }
	void GetGrip(FVector& OutLoc, FRotator& OutRot, float& OutScale) const;
	void SetGrip(const FVector& Loc, const FRotator& Rot, float Scale);
	void ResetGrip();
	/** Persist the tuned values (SaveConfig) so they survive a restart. */
	void SaveGripConfig();
	/** Read Content/DataTables/weapon_grips.json into WeaponGrips. */
	void LoadGripData();

	/** ITEM_TYPE of the weapon currently held in the hand being tuned, or 0. */
	int32 GetTunedWeaponType() const;

protected:

	// ── Eye blinking ────────────────────────────────────────────────────────
	// ROSE's face mesh carries open-eye + closed-eye overlay sections; blinking
	// swaps which one is drawn.  We import them as distinct sections
	// ("...eyesopen" / "...eyesclosed") and hide the inactive SECTION.
	//
	// Not a material swap: an invisible material still submits its geometry, and
	// the two overlays occupy the same place on the face, so the hidden one went
	// on occluding the visible one — the eyes clipped through the face.
	// HiddenMaterial survives only as a fallback for a mesh whose render data
	// cannot be read.
	UPROPERTY() class UMaterialInterface* HiddenMaterial = nullptr;  // /Game/Characters/Materials/M_Hidden
	int32 OpenEyeSlot = -1;
	int32 ClosedEyeSlot = -1;
	int32 OpenEyeSection = -1;      // render-section index of the open-eye slot
	int32 ClosedEyeSection = -1;
	void  SetEyeSectionVisible(bool bOpenEye, bool bVisible);
	UPROPERTY() class UMaterialInterface* OpenEyeMat = nullptr;
	UPROPERTY() class UMaterialInterface* ClosedEyeMat = nullptr;
	bool bEyesClosed = false;
	float BlinkTimer = 0.f;
	float NextBlinkAt = 3.f;
	// How long THIS blink stays shut.  The client uses rand()%100+10 ms — a
	// flick, not a hold (zz_model::update_blink).
	float ClosedFor = 0.06f;
	void SetupBlink();           // locate eye sections after a (re)merge
	void UpdateBlink(float Dt);  // drive the blink timer

	// ── Catalog browser ───────────────────────────────────────────────────────
	// Up/Down = active slot, Left/Right = step that slot's valid ids, Del = off.
	TArray<FString> SlotOrder;
	int32 ActiveSlot = 0;
	TMap<FString, TArray<int32>> SlotIds;   // sorted valid ids per slot
	TMap<FString, int32> SlotCursor;        // index into SlotIds[slot], -1 = none
	void BuildCatalog();
	void SetCursorToId(const FString& Slot, int32 Id);
	void BrowseId(int Dir);
	void BrowseSlot(int Dir);
	void UnequipActive();
	void ShowBrowserHUD();
	void BrowseIdNext()   { BrowseId(+1); }
	void BrowseIdPrev()   { BrowseId(-1); }
	void BrowseSlotNext() { BrowseSlot(+1); }
	void BrowseSlotPrev() { BrowseSlot(-1); }
    bool bAttackQueued = false;

	// Simple test cycling so equipment can be tried with keys
	int32 BodyTestId = 1;
	int32 CapTestId  = 0;
	void CycleBody();
	void CycleCap();
	// Locomotion set for the CURRENTLY equipped weapon type (ROSE plays a
	// different stop/walk/run per weapon class — see TYPE_MOTION/FILE_MOTION).
	UPROPERTY() UAnimSequence* IdleAnim = nullptr;
	UPROPERTY() UAnimSequence* WalkAnim = nullptr;
	UPROPERTY() UAnimSequence* RunAnim  = nullptr;

	UAnimSequence* CurrentLoco = nullptr;
	int32 WeaponMotionType = 0;    // current equipped weapon's Motion Type (HUD/diag)

	// Re-pick IdleAnim/WalkAnim/RunAnim + the attack combo from the equipped
	// weapon's Motion Type (RoseWeaponData.h), falling back to the bare set when
	// an anim is absent.
	void UpdateLocoSet();
	UAnimSequence* LoadWeaponAnim(int32 MotionType, int32 Action) const;

	// ── Attack ────────────────────────────────────────────────────────────────
	// A basic attack plays the equipped weapon's swing (TYPE_MOTION rows 8-10,
	// the 1-3 combo) once; locomotion resumes when it ends.  Bare-handed too.
	UPROPERTY() TArray<UAnimSequence*> AttackAnims;   // valid attack01/02/03 for the weapon
	int32 ComboIdx = 0;            // next attack in the combo to play
	bool  bAttacking = false;
	float AttackTimer = 0.f;       // seconds left in the current attack
	bool  bHitThisSwing = false;   // ensures one hit per swing (direct call OR notify)
	void Attack();

	// ── Click-to-attack (faithful ROSE point-and-click combat) ────────────────
	// Left mouse picks under the cursor: a live monster becomes the Target and
	// auto-attack starts — run into weapon reach, then swing on repeat (the
	// combo chains, attack-speed scaled) until it dies.  WASD movement or a
	// click on empty ground cancels.  Targeted swings damage ONLY the target
	// (ROSE basic attacks are single-target).
	TWeakObjectPtr<class ARoseMonster> Target;
	bool bAutoAttack = false;
	void OnLeftClick();
	void TickAutoAttack(float DeltaSeconds);

	// ── Skill approach: a targeted skill cast out of range runs at the target
	// until within PendingCastRange, then re-issues the cast (set by
	// URoseSkillComponent::CastSkill; cancelled by WASD / ground click / death).
	int32 PendingCastSkill = -1;
	float PendingCastRange = 0.f;
	void TickSkillApproach(float DeltaSeconds);
public:
	// Current click-target (null = none) — ARoseMobHUD highlights its plate.
	class ARoseMonster* GetTargetMonster() const { return Target.Get(); }
	// Melee reach in cm, centre-to-centre (weapon Range + capsule/lunge slack) —
	// shared by the pursuit, DoMeleeHit and melee skill ranges so they agree.
	float GetWeaponReach() const;
	void RequestSkillApproach(int32 SkillId, float Range);
protected:

	// Skill hotbar keys 1-9, 0, -, = (slots 1-12) → URoseSkillComponent::CastHotbar.
	void CastHotbar1();
	void CastHotbar2();
	void CastHotbar3();
	void CastHotbar4();
	void CastHotbar5();
	void CastHotbar6();
	void CastHotbar7();
	void CastHotbar8();
	void CastHotbar9();
	void CastHotbar10();
	void CastHotbar11();
	void CastHotbar12();
public:
	// Melee hit: damage target dummies within the equipped weapon's Range (data
	// table) in front of the character, for AttackPower damage.  Public so the
	// attack-hit AnimNotify (URoseAttackHitNotify) can trigger it at the swing's
	// contact frame.
	void DoMeleeHit();
protected:

	void MoveForward(float Value);
	void MoveRight(float Value);
	void TurnInput(float Value);
	void LookUpInput(float Value);

	// ROSE-style orbit camera: rotate only while right mouse held; wheel zooms.
	bool bRotatingCamera = false;
	float CamYaw = 0.f;
	float CamPitch = -35.f;     // elevated, looking down
	float ZoomTarget = 500.f;
	void OnRotateCameraPressed();
	void OnRotateCameraReleased();
	void ZoomCamera(float Value);

	void PlayLoco(UAnimSequence* Anim);

	// Sounds for the character's hit
    UFUNCTION() void AnimNotify_AttackHit();
};
