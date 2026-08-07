// ROSE playable character — modular avatar + WASD movement + speed-driven anims.
#include "RoseCharacter.h"
#include "RoseBackend.h"
#include "RoseDragPan.h"
#include "RoseCart.h"

#include "RoseGameInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "SkeletalMergingLibrary.h"
// Merged-section -> material-slot lookup, so gear atlas cells are applied
// deterministically instead of by material-pointer guessing.
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "CapHairOffsets.h"
#include "RoseHairData.h"
#include "RoseWeaponData.h"
#include "RoseSkillMotionData.h"
#include "RoseItemTypes.h"
#include "RoseSkillComponent.h"
#include "RoseSoundData.h"
#include "RoseStatFormulas.h"
#include "RoseExpData.h"
#include "RoseUIChat.h"
#include "RoseTargetDummy.h"
#include "RoseBullet.h"
#include "RoseMonster.h"
#include "RoseNpcTypes.h"
#include "RoseMonsterSpawner.h"
#include "RoseNpc.h"             // town NPCs (click → dialog, not attack)
#include "RoseQuest.h"           // quest engine component
#include "Engine/DataTable.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/Light.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/LightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"   // gear atlas dynamic materials
#include "Engine/Texture.h"
#include "Misc/FileHelper.h"                      // gear_equip.json
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "RoseUIWindow.h"
#include "RoseUIManager.h"
#include "RoseModernWindow.h"
#include "RoseUIChat.h"          // FRoseChatLog (loot notices)
#include "RoseDrops.h"           // faithful ITEM_DROP.STB roll
#include "RoseGroundItem.h"      // dropped-loot actor (manual pickup)
#include "EngineUtils.h"
#include "Engine/Light.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/LightComponent.h"         // TActorIterator
#include "Net/UnrealNetwork.h"   // DOREPLIFETIME (replicated player state)

// Modern character-sheet + inventory content (RoseUICharSheet.cpp / RoseUIInventory.cpp).
TSharedRef<SWidget> RoseCharSheet_MakeContent(ARoseCharacter& Char);
TSharedRef<SWidget> RoseInventory_MakeContent(ARoseCharacter& Char);

ARoseCharacter::ARoseCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);

	// GetMesh() holds the SINGLE merged character mesh (ROSE: one model, one
	// skeleton, all parts merged in).  ROSE meshes have feet at origin, +Z up,
	// facing +Y — drop to capsule bottom and yaw to face actor forward (+X).
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocation(FVector(0.f, 0.f, -88.f));

	// The mesh transform DEPENDS ON THE ASSET ERA — see ApplyMeshEraTransform.
	// The era is not known until RefreshSharedSkeleton has picked a skeleton, so
	// this is only the legacy default; that call sets the real one.
	ApplyMeshEraTransform(/*bNative=*/false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 500.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 90.f);
	CameraBoom->bUsePawnControlRotation = false;
	// World-space rotation so the camera orbits independently of the character's
	// facing (ROSE camera does not spin when the character turns).
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 10.f;

	// NO collision probe.  The arm length is the player's zoom setting and
	// nothing else is allowed to change it.
	//
	// USpringArmComponent defaults bDoCollisionTest to TRUE, which sweeps from
	// the character to the camera and shortens the arm on any blocking hit —
	// including an NPC or monster capsule you happen to walk past.  The result
	// is the camera snapping into the character's back whenever a mob brushes
	// the line of sight, which reads as a bug rather than as occlusion handling.
	// ROSE's camera does not do this.
	CameraBoom->bDoCollisionTest = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom);

	// Held weapons: separate static-mesh components socket-attached to the hand bones
	// (not merged), so the grip is a tunable relative transform.  Bone attach happens
	// in UpdateWeapon (once the merged mesh + its bones exist).
	WeaponR = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponR"));
	WeaponR->SetupAttachment(GetMesh());
	WeaponR->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponL = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponL"));
	WeaponL->SetupAttachment(GetMesh());
	WeaponL->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Back item (wings/capes): same socket-attach pattern as weapons.  Attaches
	// to the chest bone in UpdateBack once the merged mesh + bones exist.
	BackItem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackItem"));
	BackItem->SetupAttachment(GetMesh());
	BackItem->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Sub-weapon (shield/off-hand): left-hand mirror of WeaponR/L.
	SubWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SubWeapon"));
	SubWeapon->SetupAttachment(GetMesh());
	SubWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// THE CHARACTER IS NOT A LIGHT.
	//
	// M_RoseChar is UNLIT, which in UE means its colour is output through
	// EMISSIVE — and Lumen (enabled on this project) treats emissive surfaces as
	// real light emitters.  So an unlit character silently becomes an area light
	// the colour of its own texture: skin tone pooling warm orange on the ground
	// around the player, brightest on dark or snowy terrain.
	//
	// bEmissiveLightSource=false keeps the character visible exactly as before
	// while removing it from Lumen's emitter list.  It is off for every unlit
	// ROSE surface for the same reason — see ARoseSkyDome and ARosePrecipitation,
	// where a full-screen dome and thousands of quads would be far worse.
	GetMesh()->SetEmissiveLightSource(false);
	WeaponR->SetEmissiveLightSource(false);
	WeaponL->SetEmissiveLightSource(false);
	BackItem->SetEmissiveLightSource(false);
	SubWeapon->SetEmissiveLightSource(false);

	// Job + skill runtime (tables, learning, hotbar casting, buffs/passives).
	Skills = CreateDefaultSubobject<URoseSkillComponent>(TEXT("Skills"));
	// Quest engine (QSD triggers + tagQuestData state — RoseQuest.h).
	Quests = CreateDefaultSubobject<URoseQuestComponent>(TEXT("Quests"));
	UI = CreateDefaultSubobject<URoseUIManager>(TEXT("UI"));

	// Live avatar portrait: a scene-capture of just this character's head → a
	// render target the HUD shows.  Head animation shows because it captures the
	// live mesh every frame.  Framing is tunable (PortraitCam* below).
	PortraitCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PortraitCapture"));
	PortraitCapture->SetupAttachment(RootComponent);
	PortraitCapture->bCaptureEveryFrame = true;
	PortraitCapture->bCaptureOnMovement = false;
	PortraitCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	PortraitCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// Full-body paperdoll for the inventory preview — captures only while the
	// inventory is open (SetPaperdollActive), so it costs nothing otherwise.
	PaperdollCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PaperdollCapture"));
	PaperdollCapture->SetupAttachment(RootComponent);
	PaperdollCapture->bCaptureEveryFrame = false;
	PaperdollCapture->bCaptureOnMovement = false;
	PaperdollCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	PaperdollCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 400.f;

	// ── Jump feel ────────────────────────────────────────────────────────────
	//
	// UE's defaults make a jump that reads as stiff: AirControl 0.05 means you
	// are committed the instant you leave the ground, and the default falling
	// braking bleeds the run speed away mid-air, so a running jump lands short
	// and feels like it snagged.  ROSE's own jump is worse still (a fixed hop
	// that stops you dead), and the ask is for something FLUID rather than
	// faithful, so this is deliberately nicer than the original.
	//
	//   AirControl 0.35            — steerable in the air without feeling floaty
	//   BrakingDecelerationFalling 0 — a running jump keeps its momentum
	//   JumpZVelocity 460          — clears ROSE's step heights comfortably
	//   JumpMaxHoldTime 0.18       — variable height: tap for a hop, hold for
	//                                the full jump, which is most of what makes
	//                                a jump feel responsive rather than canned
	GetCharacterMovement()->JumpZVelocity = 460.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->BrakingDecelerationFalling = 0.f;
	GetCharacterMovement()->GravityScale = 1.5f;   // snappier arc, less hang time
	JumpMaxHoldTime = 0.18f;

	// ── Networking ──────────────────────────────────────────────────────────
	// APawn's constructor already sets bReplicates; make movement replication
	// explicit and pick a rate suited to an MMO crowd (CharacterMovement's own
	// client-prediction + server-correction path does the heavy lifting).
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
	SetNetCullDistanceSquared(225000000.f);   // 150 m relevancy radius

	// MUST NOT auto-possess: with more than one ARoseCharacter in the world
	// (every other player's proxy is one) Player0 would grab whichever spawned
	// first.  The GameMode spawns and possesses this pawn per PlayerController.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

// ROSE hand-dummy indices.
//
// THE ZMD BONE NAMES ARE SWAPPED: the bone called b1_lhand is on the
// character's visual RIGHT, and b1_rhand is on the visual LEFT.  Confirmed by
// inspecting the skeleton in UE.  So the dummy that lands in the hand a weapon
// is actually held in is rose_dummy_1 (parented to b1_lhand), NOT dummy 0.
//
// This is NOT a mirror — the mesh imports at scale (1,1,1) on the native path
// ("mesh era=NATIVE" in the log).  The names really are the wrong way round in
// the data.
//
// The attack animations swing that same visual-right arm, so weapon and motion
// agree here.  Do not "correct" these back to 0/1 to match the bone names:
// that was tried and puts the sword in the empty hand.
static constexpr int32 kRoseDummyRightHand = 0;   // child of b1_rhand
static constexpr int32 kRoseDummyLeftHand  = 1;

void ARoseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone: what another player needs to draw you (look + level + vitals).
	DOREPLIFETIME(ARoseCharacter, RepGender);
	DOREPLIFETIME(ARoseCharacter, RepEquipped);
	DOREPLIFETIME(ARoseCharacter, Level);
	DOREPLIFETIME(ARoseCharacter, SummonUsedCapacity);
	DOREPLIFETIME(ARoseCharacter, CurrentHP);
	DOREPLIFETIME(ARoseCharacter, CurrentMP);
	DOREPLIFETIME(ARoseCharacter, Strength);
	DOREPLIFETIME(ARoseCharacter, Dexterity);
	DOREPLIFETIME(ARoseCharacter, Intelligence);
	DOREPLIFETIME(ARoseCharacter, Concentration);
	DOREPLIFETIME(ARoseCharacter, Charm);
	DOREPLIFETIME(ARoseCharacter, Sense);

	// Owner only: private state, and the anti-cheat boundary — a client is never
	// sent another player's inventory or progression.
	DOREPLIFETIME_CONDITION(ARoseCharacter, Exp,  COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ARoseCharacter, Zuly, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ARoseCharacter, Bag,  COND_OwnerOnly);
}

// Resolve one animation, native first.
//
//   native : /Game/Rose/Characters/<G>/Anims/A_<G>_<ZMO stem>   (RoseEditor)
//   legacy : /Game/Characters/Modular/<Gender>/base/base/SkeletalMeshes/<Name>
//
// `Name` is the LEGACY name and already carries a "base" prefix
// (baseEMPTY_STOP1_F1), because rose_combine_anims.py packed every clip into one
// GLB called `base` and Interchange named each clip <glb><clip>.  The ZMO stem —
// and so the native asset — is that name without the prefix.
// bNative MUST match the era of the skeleton the mesh is on.
//
// An animation is bound to ONE skeleton.  Native clips are built for
// SK_Rose_<G>_Skeleton and legacy clips for `base`; playing a native clip on a
// legacy-skinned character does not fail, it plays a garbage pose — the
// character folds into a crouch with limbs in the wrong places.  That is what
// preferring native here did once the loader went back to legacy parts.
static UAnimSequence* LoadAnim(const FString& Root, const TCHAR* GenderKey,
	const TCHAR* Name, bool bNative)
{
	if (bNative)
	{
		FString Stem(Name);
		Stem.RemoveFromStart(TEXT("base"), ESearchCase::CaseSensitive);

		const FString Native = FString::Printf(
			TEXT("/Game/Rose/Characters/%s/Anims/A_%s_%s.A_%s_%s"),
			GenderKey, GenderKey, *Stem, GenderKey, *Stem);
		if (UAnimSequence* A = LoadObject<UAnimSequence>(nullptr, *Native))
			return A;
	}

	const FString P = FString::Printf(
		TEXT("%s/base/base/SkeletalMeshes/%s.%s"), *Root, Name, Name);
	return LoadObject<UAnimSequence>(nullptr, *P);
}

// Load the anim for a weapon Motion Type + action (0=idle,1=walk,2=run), falling
// back to the bare set (motion type 0) when the weapon-specific anim isn't
// imported (e.g. the _M1 bow/gun idles not yet in the Female base).
UAnimSequence* ARoseCharacter::LoadWeaponAnim(int32 MotionType, int32 Action) const
{
	const bool bF = Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase);
	const FString Root = FString::Printf(TEXT("/Game/Characters/Modular/%s"), *Gender);
	for (int32 M : { MotionType, 0 })
	{
		const TCHAR* Name = RoseWeaponLocoAnim(M, Action, bF);
		if (Name && *Name)
			if (UAnimSequence* A = LoadAnim(Root, GenderKey(), Name, /*bNative*/ true))
				return A;
	}
	return nullptr;
}

void
ARoseCharacter::UpdateLocoSet() {
    // Check if player is riding a cart (CObjCART) or mount (CObjMOUNT) — the vehicle owns its own
    // motion set, so the avatar's anims are irrelevant.
    int32 Mt = 0; // Bare
    // Weapon's Motion Type comes from the DATA TABLE (weapons.csv), not code.
    if (const int32* W = Equipped.Find(TEXT("weapon")))
        if (*W >= 0)
            if (const FRoseWeaponRow* Row = GetWeaponRow(*W))
                Mt = Row->MotionType;
    WeaponMotionType = Mt;
    // Relaxed Stop1 idle (hand down → weapon points down); attacks straighten it.
    IdleAnim = LoadWeaponAnim(Mt, 0);
    WalkAnim = LoadWeaponAnim(Mt, 1);
    RunAnim = LoadWeaponAnim(Mt, 2);

    // Attack combo for this weapon (actions 4/5/6 = basic attack 01/02/03); keep
    // only the ones that resolve to a real anim so the combo cycles cleanly.
    AttackAnims.Reset();
    for (int32 A = 4; A <= 6; ++A)
        if (UAnimSequence* Anim = LoadWeaponAnim(Mt, A))
            AttackAnims.AddUnique(Anim);
    ComboIdx = 0;

    bAttacking = false; // an equip change cancels any in-progress swing
    AttackTimer = 0.f;
    CurrentLoco = nullptr; // force Tick to re-play the right loco for current speed
    
}

//void ARoseCharacter::Attack()
//{
//	// Register the hit first so it works even for weapons/poses without an attack anim.
//	DoMeleeHit();
//
//	if (AttackAnims.Num() == 0) return;
//	UAnimSequence* Anim = AttackAnims[ComboIdx % AttackAnims.Num()];
//	ComboIdx = (ComboIdx + 1) % AttackAnims.Num();   // advance the combo for next press
//	if (!Anim) return;
//	GetMesh()->PlayAnimation(Anim, /*bLooping*/ false);
//	bAttacking  = true;
//	AttackTimer = Anim->GetPlayLength();
//	CurrentLoco = nullptr;   // loco will be re-played when the attack ends
//}

//void ARoseCharacter::Attack() {
//    if (bAttacking) {
//        bAttackQueued = true;
//        return;
//    }
//
//    //DoMeleeHit();
//
//    if (AttackAnims.Num() == 0)
//        return;
//
//    UAnimSequence* Anim = AttackAnims[ComboIdx];
//
//    if (!Anim)
//        return;
//    GetMesh()->Stop();
//    GetMesh()->PlayAnimation(Anim, false);
//
//    bAttacking = true;
//    AttackTimer = Anim->GetPlayLength();
//}

void
ARoseCharacter::Attack() {
    if (bAttacking) {
        bAttackQueued = true;
        return;
    }

    if (AttackAnims.Num() == 0)
        return;

    UAnimSequence* Anim = AttackAnims[ComboIdx];
    if (!Anim)
        return;

    // CRITICAL: stop previous animation cleanly
    GetMesh()->Stop();
    GetMesh()->PlayAnimation(Anim, false);

    // ROSE attack speed (1500/(weaponSpd+5), 100 = normal) scales the swing anim.
    const float Rate = FMath::Clamp(Derived.AttackSpeed / 100.f, 0.4f, 2.5f);
    GetMesh()->SetPlayRate(Rate);

    // Swing-start sound (client action frame 31: the weapon's ATK_START row,
    // bare hands use LIST_WEAPON row 0's).
    {
        int32 StartSnd = ROSE_BAREHAND_ATK_START_SOUND;
        if (const int32* W = Equipped.Find(TEXT("weapon")))
            if (*W >= 0)
                if (const FRoseWeaponRow* R = GetWeaponRow(*W))
                    StartSnd = R->AtkStartSound;
        RosePlaySoundId(GetWorld(), StartSnd, GetActorLocation());
    }

    bAttacking = true;
    AttackTimer = Anim->GetPlayLength() / Rate;

    bHitThisSwing = false;   // new swing
    DoMeleeHit();            // reliable hit on the swing (notify, if placed, is a no-op via the guard)
}

float
ARoseCharacter::GetWeaponReach() const {
    float Range = 150.f;   // ROSE weapon Range (unarmed default)
    if (const int32* W = Equipped.Find(TEXT("weapon")))
        if (*W >= 0)
            if (const FRoseWeaponRow* R = GetWeaponRow(*W))
                Range = (float)R->Range;
    // Effective reach is centre-to-centre, so add room for both actors' radii +
    // a melee lunge; the raw ROSE Range (≈150 for a sword) is too short alone.
    return FMath::Max(300.f, Range) + 60.f;
}

void
ARoseCharacter::OnLeftClick() {
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    // Pick under the cursor on the Pawn channel — mob capsules block it, and a
    // nearer ground/terrain hit just means the click wasn't on a mob.
    //
    // A MANUAL trace, not GetHitResultUnderCursor, because that helper cannot be
    // told to ignore anything and always starts at the camera.  The player's own
    // capsule is a Pawn, so as soon as the camera came close to the character —
    // which the spring arm's collision probe did every time a mob walked past —
    // the very first blocking hit was the PLAYER, and every click silently
    // selected yourself instead of the NPC in front of you.
    FVector Origin, Dir;
    if (!PC->DeprojectMousePositionToWorld(Origin, Dir))
        return;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(RoseClick), /*bTraceComplex*/ false);
    Params.AddIgnoredActor(this);

    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(
        Hit, Origin, Origin + Dir * 100000.f, ECC_Pawn, Params);

    // Town NPCs first — ARoseNpc IS an ARoseMonster, so this branch must come
    // before the attack path.  Clicking one in talk range opens its CON dialog
    // (CObjMOB::Check_EVENT with nEventIDX -1).
    if (ARoseNpc* Npc = Cast<ARoseNpc>(Hit.GetActor())) {
        Target = nullptr;
        bAutoAttack = false;
        bAttackQueued = false;
        PendingCastSkill = -1;
        if (FVector::Dist2D(Npc->GetActorLocation(), GetActorLocation()) > kTalkRange) {
            // WALK to them, like the attack path pursues a mob.  Refusing with
            // "too far away" made the player do the pathing by hand for something
            // the click already expressed the intent of.
            PendingTalkNpc = Npc;
        } else if (UI) {
            // Face the NPC like the client does when a conversation opens.
            const FVector To = Npc->GetActorLocation() - GetActorLocation();
            SetActorRotation(FRotator(0.f, To.GetSafeNormal2D().Rotation().Yaw, 0.f));
            UI->OpenNpcDialog(Npc);
        }
        return;
    }

    ARoseMonster* Mob = Cast<ARoseMonster>(Hit.GetActor());
    if (Mob && !Mob->IsDead()) {
        Target = Mob;
        bAutoAttack = true;    // TickAutoAttack pursues + swings from here
    } else {
        // Empty ground (or a corpse): deselect and stand down.
        Target = nullptr;
        bAutoAttack = false;
        bAttackQueued = false;
        PendingCastSkill = -1;
    }
}

void
ARoseCharacter::TickAutoAttack(float DeltaSeconds) {
    if (!bAutoAttack) return;

    ARoseMonster* Mob = Target.Get();
    if (!Mob || Mob->IsDead()) {
        bAutoAttack = false;
        bAttackQueued = false;   // no swing at the corpse
        Target = nullptr;
        return;
    }

    const FVector To = Mob->GetActorLocation() - GetActorLocation();
    const float Dist = To.Size2D();
    // Stop a little inside DoMeleeHit's reach so the swing can't whiff on the edge.
    const bool bInRange = Dist <= GetWeaponReach() - 20.f;

    // Track the target's position while engaged (ROSE chars face their target).
    if (bInRange || bAttacking)
        SetActorRotation(FRotator(0.f, To.GetSafeNormal2D().Rotation().Yaw, 0.f));

    if (bAttacking) {
        // Chain the combo only while the target is still in reach; otherwise the
        // swing finishes and the pursuit below resumes next tick.
        bAttackQueued = bInRange;
        return;
    }

    if (bInRange)
        Attack();
    else
        AddMovementInput(To.GetSafeNormal2D());
}

void
ARoseCharacter::RequestSkillApproach(int32 SkillId, float Range) {
    PendingCastSkill = SkillId;
    PendingCastRange = FMath::Max(100.f, Range);
}

void
ARoseCharacter::TickSkillApproach(float DeltaSeconds) {
    if (PendingCastSkill <= 0) return;

    ARoseMonster* Mob = Target.Get();
    if (!Mob || Mob->IsDead()) {
        PendingCastSkill = -1;
        return;
    }
    if (bAttacking) return;   // let the current swing finish first

    const FVector To = Mob->GetActorLocation() - GetActorLocation();
    if (To.Size2D() <= PendingCastRange) {
        SetActorRotation(FRotator(0.f, To.GetSafeNormal2D().Rotation().Yaw, 0.f));
        const int32 Id = PendingCastSkill;
        PendingCastSkill = -1;
        if (Skills)
            Skills->CastSkill(Id);   // re-checks range/costs, then casts
    } else {
        AddMovementInput(To.GetSafeNormal2D());
    }
}

// Zone background music.
//
// The track is named by LIST_ZONE ("BGM Day"/"BGM Night") and resolved to an
// imported asset by tools/gen_zone_bgm.py.  Sound comes from the CLASSIC client
// tree — QQ-iROSE ships no audio at all — which is safe because a BGM track is
// not indexed by an STB row the way a mesh is, so it cannot resolve to the wrong
// asset across clients.
//
// Keyed on the zone so re-entering the same zone does NOT restart the track, and
// a warp to a new zone swaps it instead of layering a second one on top.
void ARoseCharacter::UpdateZoneBgm()
{
	UWorld* W = GetWorld();
	if (!W || IsDedicatedServerRole() || !IsLocallyControlled())
		return;   // one listener; a dedicated server has no audio device

	FString Map = FString(W->GetMapName());
	const FString Pie = TEXT("UEDPIE_");
	if (Map.StartsWith(Pie))
	{
		FString Rest = Map.RightChop(Pie.Len());
		int32 Under;
		if (Rest.FindChar(TEXT('_'), Under))
			Map = Rest.RightChop(Under + 1);
	}
	if (Map.StartsWith(TEXT("L_")))
		Map = Map.RightChop(2);
	const FString Zone = Map.ToUpper();

	if (Zone == BgmZone && BgmAudio && BgmAudio->IsPlaying())
		return;

	static TMap<FString, FString> Manifest;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Raw;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Sounds/zone_bgm.json");
		if (FFileHelper::LoadFileToString(Raw, *Path))
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
			if (FJsonSerializer::Deserialize(R, Root) && Root.IsValid())
				for (const auto& Pair : Root->Values)
				{
					const TSharedPtr<FJsonObject> O = Pair.Value->AsObject();
					if (O.IsValid())
						Manifest.Add(FString(Pair.Key).ToUpper(), O->GetStringField(TEXT("day")));
				}
		}
		UE_LOG(LogTemp, Log, TEXT("[Rose] zone BGM: %d zone(s)"), Manifest.Num());
	}

	const FString* Asset = Manifest.Find(Zone);
	BgmZone = Zone;

	if (BgmAudio)
	{
		BgmAudio->Stop();
		BgmAudio->DestroyComponent();
		BgmAudio = nullptr;
	}
	if (!Asset || Asset->IsEmpty())
		return;   // zone has no track (or the classic tree lacks it) — silence

	const FString Full = FString::Printf(TEXT("%s.%s"), **Asset,
		*FPaths::GetBaseFilename(*Asset));
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Full);
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] zone BGM missing: %s"), *Full);
		return;
	}
	// 2D, looping, and NOT tied to the pawn's position — it is music.
	BgmAudio = UGameplayStatics::SpawnSound2D(W, Sound, /*Volume*/ 0.5f,
		/*Pitch*/ 1.f, /*Start*/ 0.f, /*Concurrency*/ nullptr,
		/*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
	if (BgmAudio)
		BgmAudio->bIsUISound = true;   // keeps playing while the game is paused
}

// ROSE's own jump animation.
//
// ACharacter::Jump only moves the capsule; the motion is ours to play.  Taken
// from the Jump skill row (id 13, ActionMotion 15) so it follows the table like
// every other action animation rather than a hardcoded index.
void ARoseCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (IsDedicatedServerRole() || !Skills)
		return;
	if (const FRoseSkillRow* R = Skills->GetSkillRow(kSkillJump))
		PlaySkillMotion(R->ActionMotion, R->CastMotion, 100.f);
}

// Sit / stand, ROSE's pose (skill 11, ActionMotion 4).
//
// Standing up is just "stop the pose": the locomotion set takes over again on
// the next movement tick, so there is no separate stand animation to find.
void ARoseCharacter::ToggleSit()
{
	if (!Skills || IsDedicatedServerRole())
		return;

	if (bSitting)
	{
		bSitting = false;
		bAttacking = false;
		AttackTimer = 0.f;
		CurrentLoco = nullptr;      // force the loco set to re-apply
		return;
	}

	// Sitting while moving reads as a glitch; ROSE sits from a stand.
	if (GetVelocity().Size2D() > 10.f)
		return;

	if (const FRoseSkillRow* R = Skills->GetSkillRow(kSkillSit))
	{
		PlaySkillMotion(R->ActionMotion, R->CastMotion, 100.f);
		bSitting = true;
	}
}

// Walk to the drop a pick-up asked for, then take it.
//
// PickUpNearest used to demand you were already within 220 units and refuse
// otherwise.  The keypress expresses the intent; walking there is the game's job
// — same shape as the NPC talk approach and the attack pursuit.
void ARoseCharacter::TickPickUpApproach(float DeltaSeconds)
{
	ARoseGroundItem* Item = PendingPickUp.Get();
	if (!Item) { PendingPickUp = nullptr; return; }

	if (bAutoAttack || PendingCastSkill > 0 || bAttacking)
	{
		PendingPickUp = nullptr;   // a different intent won
		return;
	}

	const FVector To = Item->GetActorLocation() - GetActorLocation();
	if (To.Size2D() > 200.f)
	{
		AddMovementInput(To.GetSafeNormal2D());
		return;
	}

	PendingPickUp = nullptr;
	SetActorRotation(FRotator(0.f, To.GetSafeNormal2D().Rotation().Yaw, 0.f));
	DoPickUp(Item);
}

// Let the player walk THROUGH town NPCs without making them unclickable.
//
// The obvious fix — set the NPC capsule to overlap Pawn — also removes them from
// the Pawn TRACE, and OnLeftClick picks with GetHitResultUnderCursor(ECC_Pawn).
// That is one setting controlling two unrelated things, and turning it off to
// stop the blocking silently stopped the clicking.
//
// IgnoreActorWhenMoving separates them: the capsule still blocks the trace (so
// NPCs are clickable, and walk-to-talk can find them), while this character's
// movement passes through.  Applied on a throttle so NPCs that stream in later
// are picked up too.
void ARoseCharacter::RefreshNpcMoveIgnores()
{
	UWorld* W = GetWorld();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!W || !Capsule)
		return;

	for (TActorIterator<ARoseNpc> It(W); It; ++It)
	{
		ARoseNpc* Npc = *It;
		if (Npc && !IgnoredNpcs.Contains(Npc))
		{
			Capsule->IgnoreActorWhenMoving(Npc, true);
			IgnoredNpcs.Add(Npc);
		}
	}
}

// Walk to the NPC a click asked to talk to, then open the conversation.
//
// Same shape as TickAutoAttack/TickSkillApproach: steer each tick, act on
// arrival.  Cancelled by anything that expresses a different intent — attacking,
// casting, or the NPC going away — so it can never drag the player somewhere
// they have since changed their mind about.
void
ARoseCharacter::TickTalkApproach(float DeltaSeconds) {
    ARoseNpc* Npc = PendingTalkNpc.Get();
    if (!Npc) { PendingTalkNpc = nullptr; return; }

    if (bAutoAttack || PendingCastSkill > 0 || bAttacking) {
        PendingTalkNpc = nullptr;   // a different intent won
        return;
    }

    const FVector To = Npc->GetActorLocation() - GetActorLocation();
    if (To.Size2D() > kTalkRange) {
        AddMovementInput(To.GetSafeNormal2D());
        return;
    }

    PendingTalkNpc = nullptr;
    SetActorRotation(FRotator(0.f, To.GetSafeNormal2D().Rotation().Yaw, 0.f));
    if (UI)
        UI->OpenNpcDialog(Npc);
}

// Close the conversation once the player walks away from it.
//
// ROSE ends a conversation you step out of rather than tethering you to it; the
// window used to stay open at any distance, so you could be talking to someone
// on the other side of town.  Uses a LARGER radius than opening it, so standing
// at the edge of talk range does not flicker the window open and shut.
void
ARoseCharacter::TickTalkRange(float DeltaSeconds) {
    if (!UI)
        return;
    ARoseNpc* Npc = UI->GetOpenDialogNpc();
    if (!Npc)
        return;
    if (FVector::Dist2D(Npc->GetActorLocation(), GetActorLocation()) > kTalkCloseRange)
        UI->CloseNpcDialog();
}

// Damage target dummies in front within the equipped weapon's reach.  Range and
// AttackPower come from the weapon DATA TABLE (unarmed uses small defaults).
// dog shit ai generated lets fix it up
//void ARoseCharacter::DoMeleeHit()
//{
//	float Range = 180.f, Power = 5.f;
//	if (const int32* W = Equipped.Find(TEXT("weapon")))
//		if (*W >= 0)
//			if (const FRoseWeaponRow* R = GetWeaponRow(*W))
//			{
//				Range = FMath::Max(150.f, (float)R->Range);
//				Power = FMath::Max(1.f, (float)R->AttackPower);
//			}
//
//	const FVector Origin = GetActorLocation();
//	const FVector Fwd = GetActorForwardVector();
//	int32 Hits = 0;
//	for (TActorIterator<ARoseTargetDummy> It(GetWorld()); It; ++It)
//	{
//		ARoseTargetDummy* D = *It;
//		if (!D) continue;
//		const FVector To = D->GetActorLocation() - Origin;
//		// In reach (2D distance) and roughly in front (within ~75° of facing).
//		if (To.Size2D() <= Range && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) > 0.25f)
//		{
//			D->ApplyHit(Power, Fwd);
//			++Hits;
//		}
//	}
//	if (GEngine && Hits > 0)
//		GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Yellow,
//			FString::Printf(TEXT("Hit %d dummy(s)  dmg %.0f  range %.0f"), Hits, Power, Range));
//}

void
ARoseCharacter::DoMeleeHit() {
    // One hit per swing — whether it's triggered by the direct call in Attack()
    // or by the URoseAttackHitNotify on the anim, not both.
    if (bHitThisSwing) return;
    bHitThisSwing = true;

    // The swing animation and its sounds are local, but the DAMAGE belongs to
    // the server: it re-runs this same function with its own positions, stats
    // and RNG, so reach, facing and the combat roll are all validated there and
    // a client cannot hit what it could not reach.
    if (!HasAuthority())
    {
        if (IsLocallyControlled())
            Server_MeleeSwing(Target.Get());
        return;
    }

    const float Reach = GetWeaponReach();

    // Ranged weapons (wand/bow/gun — WEAPON_BULLET_EFFECT set): the contact
    // frame FIRES A BULLET at the click-target instead of applying instant
    // melee damage.  The combat roll happens NOW (like the server) and lands
    // when the bullet arrives, with the LIST_EFFECT hit .EFT + sound.
    if (const int32* W = Equipped.Find(TEXT("weapon")))
        if (*W >= 0)
            if (const FRoseWeaponRow* R = GetWeaponRow(*W))
                if (R->BulletEffect > 0)
                {
                    ARoseMonster* Tgt = Target.Get();
                    if (Tgt && !Tgt->IsDead()
                        && FVector::Dist2D(Tgt->GetActorLocation(), GetActorLocation()) <= Reach * 1.2f)
                    {
                        const int32 Suc = RoseStats::SuccessRate(Level, Tgt->Level, Derived.Hit, Tgt->Avoid);
                        const bool bCrit = Suc > 0 && RoseStats::RollCritical(Level, Derived.Crit);
                        const int32 Dmg = (Suc <= 0) ? -1
                            : RoseStats::BasicDamage(Derived.AttackPower, Tgt->Defense, Tgt->Avoid, Suc, bCrit);
                        const FVector Muzzle = (WeaponR && WeaponR->GetStaticMesh())
                            ? WeaponR->GetComponentLocation()
                            : GetActorLocation() + FVector(0, 0, 60.f);
                        if (R->AtkFireSound > 0)
                            RosePlaySoundId(GetWorld(), R->AtkFireSound, Muzzle);
                        ARoseBullet::Fire(GetWorld(), Muzzle, Tgt, R->BulletEffect, Dmg, bCrit);
                    }
                    return;   // ranged weapons never do the melee sweep
                }

    const FVector Origin = GetActorLocation();
    const FVector Fwd = GetActorForwardVector();

    int32 Hits = 0, Misses = 0, LastDmg = 0;
    float Nearest = -1.f;
    for (TActorIterator<ARoseTargetDummy> It(GetWorld()); It; ++It) {
        ARoseTargetDummy* D = *It;
        if (!D) continue;
        const FVector To = D->GetActorLocation() - Origin;
        const float Dist = To.Size2D();
        if (Nearest < 0.f || Dist < Nearest) Nearest = Dist;
        if (Dist <= Reach && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) > 0.25f) {
            // The ROSE combat roll (calculation.cpp): success vs the defender's
            // Avoid decides the dodge, then crit, then the damage formula.
            const int32 Suc = RoseStats::SuccessRate(Level, D->Level, Derived.Hit, D->Avoid);
            if (Suc <= 0) {
                D->ShowMiss();
                ++Misses;
                continue;
            }
            const bool bCrit = RoseStats::RollCritical(Level, Derived.Crit);
            const int32 Dmg =
                RoseStats::BasicDamage(Derived.AttackPower, D->Defense, D->Avoid, Suc, bCrit);
            D->ApplyHit((float)Dmg, Fwd, bCrit);
            LastDmg = Dmg;
            ++Hits;
        }
    }

    // Monsters take the identical roll (they expose the same defender stats).
    // Impact sound is the ROSE matrix: the weapon's hit-sound TYPE row x the
    // MOB's material column — hitting a Moldie sounds different from a Grunter.
    int32 HitSndType = ROSE_BAREHAND_HIT_TYPE;
    if (const int32* W = Equipped.Find(TEXT("weapon")))
        if (*W >= 0)
            if (const FRoseWeaponRow* R = GetWeaponRow(*W))
                HitSndType = R->AtkHitSoundType;
    // Click-to-attack: with a target selected the swing damages ONLY the target
    // (faithful — ROSE basic attacks are single-target); the cone sweep remains
    // for free swings with nothing targeted.
    ARoseMonster* const Tgt = Target.Get();
    TArray<ARoseMonster*> Candidates;
    if (Tgt) {
        if (!Tgt->IsDead())
            Candidates.Add(Tgt);
    } else {
        for (TActorIterator<ARoseMonster> It(GetWorld()); It; ++It)
            if (*It && !(*It)->IsDead())
                Candidates.Add(*It);
    }
    for (ARoseMonster* M : Candidates) {
        const FVector To = M->GetActorLocation() - Origin;
        const float Dist = To.Size2D();
        if (Nearest < 0.f || Dist < Nearest) Nearest = Dist;
        // Targeted swings skip the facing cone (TickAutoAttack already faces the
        // target) and get a little slack for a mob mid-step.
        const bool bInReach = Dist <= (Tgt ? Reach * 1.2f : Reach);
        const bool bFacing = Tgt || FVector::DotProduct(Fwd, To.GetSafeNormal2D()) > 0.25f;
        if (bInReach && bFacing) {
            const int32 Suc = RoseStats::SuccessRate(Level, M->Level, Derived.Hit, M->Avoid);
            if (Suc <= 0) {
                M->ShowMiss();
                ++Misses;
                continue;
            }
            const bool bCrit = RoseStats::RollCritical(Level, Derived.Crit);
            const int32 Dmg =
                RoseStats::BasicDamage(Derived.AttackPower, M->Defense, M->Avoid, Suc, bCrit);
            M->ApplyHit((float)Dmg, Fwd, bCrit);
            RosePlaySoundId(GetWorld(),
                RoseHitSound(HitSndType, M->HitMaterial), M->GetActorLocation());
            LastDmg = Dmg;
            ++Hits;
        }
    }

    if (GEngine) {
        GEngine->AddOnScreenDebugMessage(-1, 1.f, Hits > 0 ? FColor::Yellow : FColor::Red,
            (Hits + Misses) > 0
                ? FString::Printf(TEXT("hit %d  dodged %d  dmg %d  (ATK %d HIT %d)"),
                      Hits, Misses, LastDmg, Derived.AttackPower, Derived.Hit)
                : FString::Printf(TEXT("swing: no target (nearest %.0f, reach %.0f, facing matters)"), Nearest, Reach));
    }
}

void ARoseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Tuned weapon grips live in Content/DataTables/weapon_grips.json — load
	// them before anything equips, so the first weapon already sits right.
	LoadGripData();

	// Clean UI: the dev on-screen debug text (catalog browser, grip tuner, stat
	// dumps) sprays over the real HUD in the top-left — silence it.  Re-enable
	// from the console with `EnableAllScreenMessages` when debugging.
	if (GEngine)
		GEngine->bEnableOnScreenDebugMessages = false;

	// Portrait/paperdoll captures + the HUD belong to the LOCAL player's pawn
	// only (see EnsureLocalSetup — retried once the controller replicates).
	EnsureLocalSetup();

	const FString Root = FString::Printf(TEXT("/Game/Characters/Modular/%s"), *Gender);

	// The one shared skeleton every part is merged onto.
	RefreshSharedSkeleton();

	// Refine system: grade table (stats + the authentic glow RGB per grade).
	// The glow itself is M_RoseChar's RefineColor/RefineIntensity — there is no
	// separate overlay master any more.
	LoadRefineData();
	LoadJemOptions();   // bonus-option table (GEM_OP -> named stats)

	// Material used to hide the inactive eye-overlay section while blinking.
	HiddenMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Characters/Materials/M_Hidden.M_Hidden"));

	// Weapon data table (weapons.csv → DataTable).  Auto-load by path if not set in
	// the editor.  All weapon properties come from this table — no hardcoding.
	if (!WeaponTable)
		WeaponTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/weapons.weapons"));

	// Armor tables (body/arms/foot/cap) — defense/resist/requirements/names by id.
	if (!BodyTable) BodyTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/body.body"));
	if (!ArmsTable) ArmsTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/arms.arms"));
	if (!FootTable) FootTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/foot.foot"));
	if (!CapTable)  CapTable  = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/cap.cap"));
	if (!BackTable) BackTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/back.back"));
	if (!ConsumableTable) ConsumableTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/consumable.consumable"));
	if (!GemTable)        GemTable        = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/gem.gem"));
	if (!MaterialTable)   MaterialTable   = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/material.material"));
	if (!FaceItemTable)   FaceItemTable   = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/faceitem.faceitem"));
	if (!JewelTable)      JewelTable      = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/jewel.jewel"));
	if (!SubWpnTable)     SubWpnTable     = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/subwpn.subwpn"));
	if (!PatTable)        PatTable        = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/pat.pat"));
	// pat_parts / pat_motion were declared but never loaded, so GetPatPartRow()
	// always returned null on its `!PatPartTable` guard — which made
	// EquipRidePart() bail at its `!Row` validation and silently no-op, i.e. NO
	// pat part could ever be equipped, and ARoseCart::Build() got null tables.
	if (!PatPartTable)   PatPartTable   = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/pat_parts.pat_parts"));
	if (!PatMotionTable) PatMotionTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/pat_motion.pat_motion"));
	RideParts.Init(-1, 5);            // t_eRidePART: BODY/ENGINE/LEG/ABIL/ARMS
	AmmoSlots.SetNum(3);              // t_eSHOT: ARROW/BULLET/THROW
	for (FRoseItemStack& S : AmmoSlots) { S.Id = -1; S.Count = 0; }   // empty

	// Scan the imported catalog so the browser knows each slot's valid ids.
	// Purely a client-side authoring aid (the dev browser + the equip-slot
	// cycling) — a headless server has no reason to walk the asset registry.
	if (!IsDedicatedServerRole())
		BuildCatalog();

	// Default loadout (unarmed), then pick the loco set + build the merged mesh.
	Equipped.Add(TEXT("body"), 1);
	Equipped.Add(TEXT("arms"), 1);
	Equipped.Add(TEXT("foot"), 1);
	// Hair 110 was an ARUA id.  The classic tree's hair ids stop at 78, so it
	// resolved to nothing and every character spawned bald.  1 exists in both.
	Equipped.Add(TEXT("hair"), 1);
	Equipped.Add(TEXT("face"), 1);

	// New character chosen on the Character Select screen (no warp snapshot):
	// override the default look with the picked gender/hair/face before the
	// first mesh build.  A returning character (bHasSnapshot) is restored later.
	if (URoseGameInstance* GI = Cast<URoseGameInstance>(GetGameInstance()))
		if (GI->bHasPendingCreation && !GI->bHasSnapshot)
		{
			GI->bHasPendingCreation = false;
			if (!GI->PendingGender.IsEmpty())
				Gender = GI->PendingGender;
			Equipped.Add(TEXT("hair"), GI->PendingHair);
			Equipped.Add(TEXT("face"), GI->PendingFace);
		}

	for (const TPair<FString, int32>& P : Equipped)
		SetCursorToId(P.Key, P.Value);

	UpdateLocoSet();   // bare set (no weapon) — empty stop/walk/run
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	RebuildMesh();
	PlayLoco(IdleAnim);
	ApplyDerivedStats();   // stats → movement speed (tables are loaded by now)

	// Cross-level arrival: restore the snapshot taken before OpenLevel (warp
	// portal / quest teleport) — replaces the default loadout built above.
	if (URoseGameInstance* GI = Cast<URoseGameInstance>(GetGameInstance()))
		if (GI->bHasSnapshot)
			GI->Restore(this);

	// Warp arrival: a RoseWarpPortal passes the destination point as level URL
	// options ("WarpX=..?WarpY=..") — move there, ground-snapped.
	if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
	{
		const FString& Opts = GM->OptionsString;
		if (UGameplayStatics::HasOption(Opts, TEXT("WarpX")))
		{
			const float WX = FCString::Atof(*UGameplayStatics::ParseOption(Opts, TEXT("WarpX")));
			const float WY = FCString::Atof(*UGameplayStatics::ParseOption(Opts, TEXT("WarpY")));
			float WZ = 200.f;
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit,
					FVector(WX, WY, 100000.f), FVector(WX, WY, -100000.f), ECC_Visibility))
				WZ = Hit.ImpactPoint.Z + 100.f;
			SetActorLocation(FVector(WX, WY, WZ));
		}
	}

	// Publish the starting look so late-joining clients see it (no-op off the
	// server).  Everything after this point that changes the loadout only has to
	// call MarkAppearanceDirty().
	PushAppearance();

	// (Test target dummies + monster camps used to spawn in front of the player
	// here; removed — monsters come from the map's placed ARoseMonsterSpawners.)
}

// ═══════════════════════════════════════════════════════════════════════════
//  Networking
// ═══════════════════════════════════════════════════════════════════════════

void ARoseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	EnsureLocalSetup();       // listen host / standalone: possession is local
}

void ARoseCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	EnsureLocalSetup();       // client: the controller only just arrived
}

// Local-player-only presentation: the two scene captures (a dedicated server has
// no renderer; another player's proxy has no HUD to show them in) plus the UI
// manager.  Idempotent — whichever of BeginPlay / PossessedBy / OnRep_Controller
// lands last with a local PlayerController does the work, exactly once.
void ARoseCharacter::EnsureLocalSetup()
{
	if (bLocalSetupDone || IsDedicatedServerRole())
		return;
	if (!IsLocallyControlled() || !Cast<APlayerController>(GetController()))
		return;
	bLocalSetupDone = true;

	// Live portrait render target (head-and-shoulders of THIS character only).
	if (PortraitCapture)
	{
		PortraitCapture->SetRelativeLocation(PortraitCamLoc);
		PortraitCapture->SetRelativeRotation(PortraitCamRot);
		PortraitCapture->FOVAngle = PortraitFOV;
		PortraitRT = NewObject<UTextureRenderTarget2D>(this);
		PortraitRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		PortraitRT->ClearColor = FLinearColor(0.05f, 0.06f, 0.10f, 1.f);
		PortraitRT->InitAutoFormat(256, 256);
		PortraitRT->UpdateResourceImmediate(true);
		PortraitCapture->TextureTarget = PortraitRT;
		PortraitCapture->ShowOnlyActors.Empty();
		PortraitCapture->ShowOnlyActors.Add(this);
	}

	// Full-body paperdoll render target (portrait aspect; only captures when the
	// inventory is open — SetPaperdollActive).
	if (PaperdollCapture)
	{
		PaperdollCapture->SetRelativeLocation(PaperdollCamLoc);
		PaperdollCapture->SetRelativeRotation(PaperdollCamRot);
		PaperdollCapture->FOVAngle = PaperdollFOV;
		PaperdollRT = NewObject<UTextureRenderTarget2D>(this);
		PaperdollRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		PaperdollRT->ClearColor = FLinearColor(0.06f, 0.09f, 0.14f, 1.f);
		PaperdollRT->InitAutoFormat(256, 384);
		PaperdollRT->UpdateResourceImmediate(true);
		PaperdollCapture->TextureTarget = PaperdollRT;
		PaperdollCapture->ShowOnlyActors.Empty();
		PaperdollCapture->ShowOnlyActors.Add(this);
	}

	// The HUD/windows are built by the UI manager; on a client its own BeginPlay
	// ran before the controller replicated, so it needs the same second chance.
	if (UI)
		UI->EnsureInit();

	// Character Select runs on the CLIENT, so the look it picked lives in THIS
	// machine's GameInstance — the server has never seen it.  Hand it over; the
	// server applies it and replicates the result to everyone.  This is a
	// placeholder for the real login/roster service: once characters load from a
	// database the server owns the look and this call goes away.
	if (!HasAuthority())
		if (URoseGameInstance* GI = Cast<URoseGameInstance>(GetGameInstance()))
		{
			const FString WantGender = !GI->Gender.IsEmpty() ? GI->Gender
				: (!GI->PendingGender.IsEmpty() ? GI->PendingGender : Gender);
			const int32* H = GI->Equipped.Find(TEXT("hair"));
			const int32* F = GI->Equipped.Find(TEXT("face"));
			Server_SetInitialLook(WantGender,
				H ? *H : GI->PendingHair, F ? *F : GI->PendingFace);
		}
}

void ARoseCharacter::ApplyLook(const FString& InGender, int32 Hair, int32 Face)
{
	if (!InGender.IsEmpty() && !Gender.Equals(InGender, ESearchCase::IgnoreCase))
	{
		Gender = InGender;
		// Gender picks a different asset root: the shared skeleton, the loco set
		// and every merged part have to be resolved again.
		RefreshSharedSkeleton();
	}
	if (Hair > 0) Equipped.Add(TEXT("hair"), Hair);
	if (Face > 0) Equipped.Add(TEXT("face"), Face);

	if (!IsDedicatedServerRole())
	{
		UpdateLocoSet();
		RebuildMesh();
	}
	PushAppearance();
}

void ARoseCharacter::Server_SetInitialLook_Implementation(const FString& InGender,
	int32 Hair, int32 Face)
{
	ApplyLook(InGender, Hair, Face);
}

// Server: flatten the appearance maps into the replicated array.  Sorted so the
// comparison in ApplyReplicatedAppearance is order-independent and so an equip
// that changes nothing produces an identical array (no wasted replication).
void ARoseCharacter::PushAppearance()
{
	if (!HasAuthority())
		return;

	RepGender = Gender;
	RepEquipped.Reset(Equipped.Num());
	for (const TPair<FString, int32>& P : Equipped)
	{
		FRoseEquipRep E;
		E.Slot = FName(*P.Key);
		E.Id = P.Value;
		if (const int32* R = EquippedRefine.Find(P.Key))     E.Refine = *R;
		if (const int32* B = EquippedBonus.Find(P.Key))      E.Bonus = *B;
		if (const bool*  A = EquippedAppraised.Find(P.Key))  E.bAppraised = *A;
		RepEquipped.Add(E);
	}
	RepEquipped.Sort([](const FRoseEquipRep& A, const FRoseEquipRep& B)
	{
		return A.Slot.LexicalLess(B.Slot);
	});
}

void ARoseCharacter::OnRep_Appearance()
{
	ApplyReplicatedAppearance();
}

// Client: rebuild the appearance maps from the wire form and re-merge the mesh.
// Skips the (expensive) merge when nothing actually changed — OnRep fires for
// the owning client too, which already has the right look locally.
void ARoseCharacter::ApplyReplicatedAppearance()
{
	if (IsDedicatedServerRole())
		return;

	bool bChanged = false;

	if (!RepGender.IsEmpty() && !Gender.Equals(RepGender, ESearchCase::IgnoreCase))
	{
		Gender = RepGender;
		RefreshSharedSkeleton();
		bChanged = true;
	}

	// Same slot count + same (id, refine, bonus, appraised) per slot = no change.
	if (!bChanged)
	{
		if (RepEquipped.Num() != Equipped.Num())
		{
			bChanged = true;
		}
		else
		{
			for (const FRoseEquipRep& E : RepEquipped)
			{
				const FString Key = E.Slot.ToString();
				const int32* Have = Equipped.Find(Key);
				if (!Have || *Have != E.Id
					|| GetEquippedRefine(Key) != E.Refine
					|| GetEquippedBonus(Key) != E.Bonus
					|| GetEquippedAppraised(Key) != E.bAppraised)
				{
					bChanged = true;
					break;
				}
			}
		}
	}

	if (!bChanged)
		return;

	Equipped.Reset();
	EquippedRefine.Reset();
	EquippedBonus.Reset();
	EquippedAppraised.Reset();
	for (const FRoseEquipRep& E : RepEquipped)
	{
		const FString Key = E.Slot.ToString();
		Equipped.Add(Key, E.Id);
		if (E.Refine != 0) EquippedRefine.Add(Key, E.Refine);
		if (E.Bonus != 0)  EquippedBonus.Add(Key, E.Bonus);
		if (!E.bAppraised) EquippedAppraised.Add(Key, false);
	}

	UpdateLocoSet();       // the weapon may have changed → different loco set
	RebuildMesh();         // re-merge the body from the replicated parts
	ApplyDerivedStats();
	bInventoryDirty = true;
}

// ── Client → server intent ─────────────────────────────────────────────────
// Each _Implementation re-runs the SAME validated function the single-player
// path uses, so there is exactly one copy of the rules and a client cannot ask
// for anything it could not legitimately do.

void ARoseCharacter::Server_EquipItem_Implementation(const FString& Slot, int32 Id)
{
	EquipItem(Slot, Id);
}

void ARoseCharacter::Server_EquipFromBag_Implementation(const FString& Slot, int32 Id)
{
	FString Reason;
	TryEquipFromBag(Slot, Id, Reason);   // validates ownership + requirements
}

void ARoseCharacter::Server_UnequipToBag_Implementation(const FString& Slot)
{
	FString Reason;
	TryUnequipToBag(Slot, Reason);
}

// The client swung; run the authoritative roll here against the target it named
// (a NetGUID, resolved to the server's own monster actor).  DoMeleeHit does its
// own reach + facing test using the server's transforms, so the worst a lying
// client can do is ask to swing at something it will then miss.
void ARoseCharacter::Server_MeleeSwing_Implementation(ARoseMonster* TargetMob)
{
	Target = TargetMob;
	bHitThisSwing = false;
	DoMeleeHit();
}

// ── Test entry points ──────────────────────────────────────────────────────
// DEVELOPMENT ONLY.  ROSE is an MMO: the authority is a server WE run, and a
// shipped client must not be able to become one — a player-hostable client is
// an unauthenticated, unpoliced shard of the game.  RoseHost is therefore
// compiled out of Shipping builds; the real deployment is the RoseUEServer
// target (see docs/NETWORKING.md).  Until that target can be built (it needs a
// source engine), this makes the replication path reachable for two-instance
// testing:
//   host:   RoseHost            (re-opens the current zone as a listen server)
//   client: RoseJoin 127.0.0.1  (a second standalone instance)
void ARoseCharacter::RoseHost()
{
#if UE_BUILD_SHIPPING
	// Never in a shipped client.  See the note above.
	return;
#else
	UWorld* W = GetWorld();
	if (!W) return;
	const FString Map = W->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
	UE_LOG(LogTemp, Log, TEXT("[RoseNet] hosting %s as a listen server"), *Map);
	FRoseChatLog::Add(FRoseChatLog::EKind::System,
		FString::Printf(TEXT("Hosting %s (listen server, DEV ONLY) — others: RoseJoin <your-ip>"), *Map));
	W->ServerTravel(Map + TEXT("?listen"), /*bAbsolute*/ true);
#endif
}

void ARoseCharacter::RoseJoin(const FString& Address)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;
	const FString Addr = Address.IsEmpty() ? TEXT("127.0.0.1") : Address;
	UE_LOG(LogTemp, Log, TEXT("[RoseNet] joining %s"), *Addr);
	FRoseChatLog::Add(FRoseChatLog::EKind::System, FString::Printf(TEXT("Connecting to %s..."), *Addr));
	PC->ClientTravel(Addr, ETravelType::TRAVEL_Absolute);
}

// ── Summons ─────────────────────────────────────────────────────────────────
//
// CObjUSER::GetMax_SummonCNT (cuserdata.h:486): 50 + the passive.  The 50 is a
// hard base in the client, not a table value.
int32 ARoseCharacter::GetSummonMaxCapacity() const
{
	int32 Bonus = 0;
	if (const URoseSkillComponent* S = FindComponentByClass<URoseSkillComponent>())
		Bonus = S->PassiveFlat(62);   // AT_PSV_SUMMON_MOB_CNT (datatype.h:605)
	return 50 + Bonus;
}

bool ARoseCharacter::TrySummon(int32 NpcId)
{
	UWorld* World = GetWorld();
	if (!World || NpcId <= 0)
		return false;

	// Server owns summons; a client asking is a no-op (the spawn replicates).
	if (!HasAuthority())
		return false;

	// Cost is LIST_NPC col 21 — the same column as SellTab0.  A mob with no
	// cost is not summonable, which is how non-pet NPCs are excluded without a
	// separate flag.
	const FRoseNpcRow* Row = nullptr;
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/npcs.npcs")))
		Row = T->FindRow<FRoseNpcRow>(*FString::Printf(TEXT("npc_%d"), NpcId), TEXT("Summon"));
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] summon: no npcs row for %d"), NpcId);
		return false;
	}

	const int32 Cost = FMath::Max(0, Row->SellTab0);
	const int32 Max = GetSummonMaxCapacity();

	// skill.cpp:1189 — refuse when the budget cannot take it.
	if (Cost + SummonUsedCapacity > Max)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Rose] cannot summon %s: needs %d, %d/%d used"),
			*Row->DisplayName, Cost, SummonUsedCapacity, Max);
		return false;
	}

	// Beside the caster, facing the same way, on the ground.
	const FVector At = GetActorLocation()
		+ GetActorRotation().RotateVector(FVector(120.f, 60.f, 0.f));
	const FTransform T(GetActorRotation(), At);

	// Deferred: NpcId must be set before BeginPlay, which loads mesh/anims/stats.
	ARoseMonster* Mob = World->SpawnActorDeferred<ARoseMonster>(
		ARoseMonster::StaticClass(), T, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Mob)
		return false;

	Mob->NpcId = NpcId;
	// A summon is the player's, not a wild spawn: it must not aggro its owner.
	Mob->bAggressive = false;
	Mob->SummonOwner = this;
	Mob->FinishSpawning(T);

	ActiveSummons.Add(Mob);
	SummonUsedCapacity += Cost;

	UE_LOG(LogTemp, Log, TEXT("[Rose] summoned %s (npc %d) cost %d — %d/%d used"),
		*Row->DisplayName, NpcId, Cost, SummonUsedCapacity, Max);
	return true;
}

void ARoseCharacter::ReleaseSummon(ARoseMonster* Summon)
{
	if (!Summon)
		return;

	const int32 Idx = ActiveSummons.IndexOfByKey(Summon);
	if (Idx == INDEX_NONE)
		return;   // not ours, or already released

	// Give the capacity back from the SAME row the cost came from, rather than
	// remembering it — a table reload must not strand budget.
	int32 Cost = 0;
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/npcs.npcs")))
		if (const FRoseNpcRow* Row = T->FindRow<FRoseNpcRow>(
			*FString::Printf(TEXT("npc_%d"), Summon->NpcId), TEXT("Summon")))
			Cost = FMath::Max(0, Row->SellTab0);

	ActiveSummons.RemoveAt(Idx);
	SummonUsedCapacity = FMath::Max(0, SummonUsedCapacity - Cost);

	UE_LOG(LogTemp, Log, TEXT("[Rose] summon npc %d released, %d/%d used"),
		Summon->NpcId, SummonUsedCapacity, GetSummonMaxCapacity());
}

void ARoseCharacter::RoseDebugMat(int32 bOn)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || !Body->GetSkeletalMeshAsset())
		return;

	const int32 N = Body->GetSkeletalMeshAsset()->GetMaterials().Num();

	if (bOn)
	{
		// A stock LIT engine material.  If the character is still dark wearing
		// this, nothing about our materials is responsible and the fault is the
		// component or the scene.
		UMaterialInterface* Test = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
		if (!Test)
		{
			UE_LOG(LogTemp, Error, TEXT("[RoseDbg] WorldGridMaterial not found"));
			return;
		}
		for (int32 i = 0; i < N; ++i)
			Body->SetMaterial(i, Test);
		UE_LOG(LogTemp, Warning, TEXT("[RoseDbg] %d slots -> WorldGridMaterial"), N);
	}
	else
	{
		Body->EmptyOverrideMaterials();
		UE_LOG(LogTemp, Warning, TEXT("[RoseDbg] material overrides cleared"));
	}
}

void ARoseCharacter::RoseDebugLight()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
		return;

	// Everything that can stop a mesh receiving light while its neighbours are
	// lit normally.  Capsule/inset self-shadowing is the usual suspect for a
	// SKELETAL mesh going dark next to STATIC ones that are fine.
	UE_LOG(LogTemp, Warning,
		TEXT("[RoseDbg] visible=%d hiddenInGame=%d castShadow=%d insetShadow=%d "
			 "capsuleDirect=%d capsuleIndirect=%d selfShadowOnly=%d"),
		Body->IsVisible(), Body->bHiddenInGame, Body->CastShadow,
		Body->bCastInsetShadow, Body->bCastCapsuleDirectShadow,
		Body->bCastCapsuleIndirectShadow, Body->bSelfShadowOnly);

	UE_LOG(LogTemp, Warning,
		TEXT("[RoseDbg] mobility=%d lightingChannels=(%d,%d,%d) "
			 "bounds r=%.1f scale=%s"),
		(int32)Body->Mobility,
		Body->LightingChannels.bChannel0, Body->LightingChannels.bChannel1,
		Body->LightingChannels.bChannel2,
		Body->Bounds.SphereRadius,
		*Body->GetComponentScale().ToString());

	// THE LIGHTS IN THE LEVEL.
	//
	// A STATIC light only affects geometry with baked lighting.  A MOVABLE mesh
	// receives nothing from it and renders black, while the static floor beside
	// it looks perfectly lit — which is exactly the symptom, and it is invisible
	// from the character's own state (every component flag reads normal).
	//   mobility: 0 = Static, 1 = Stationary, 2 = Movable
	for (TActorIterator<ALight> It(GetWorld()); It; ++It)
	{
		ALight* L = *It;
		ULightComponent* C = L ? L->GetLightComponent() : nullptr;
		if (!C)
			continue;

		const int32 Mob = (int32)C->Mobility;
		UE_LOG(LogTemp, Warning,
			TEXT("[RoseDbg] light '%s' %s  mobility=%d(%s)  intensity=%.2f  "
				 "castShadow=%d  channels=(%d,%d,%d)  affectsWorld=%d"),
			*L->GetActorLabel(), *L->GetClass()->GetName(),
			Mob,
			Mob == 0 ? TEXT("STATIC - movable meshes get NOTHING from this")
					 : (Mob == 1 ? TEXT("Stationary") : TEXT("Movable")),
			C->Intensity, C->CastShadows ? 1 : 0,
			C->LightingChannels.bChannel0, C->LightingChannels.bChannel1,
			C->LightingChannels.bChannel2,
			C->bAffectsWorld ? 1 : 0);

		// Which way is the sun pointing, and is the character on its lit side?
		if (const ADirectionalLight* Dir = Cast<ADirectionalLight>(L))
		{
			const FVector SunDir = Dir->GetActorForwardVector();
			UE_LOG(LogTemp, Warning,
				TEXT("[RoseDbg]   sun direction %s (pitch %.0f) — surfaces facing "
					 "AWAY from this get only ambient"),
				*SunDir.ToString(), Dir->GetActorRotation().Pitch);
		}
	}

	// SKY LIGHT — the ambient fill, and the reason a shadowed side is dim
	// rather than PURE BLACK.  ASkyLight derives from AInfo, not ALight, so the
	// loop above cannot see it: its absence there proves nothing, which is
	// exactly why it needs its own pass.
	int32 SkyCount = 0;
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		ASkyLight* S = *It;
		USkyLightComponent* C = S ? S->GetLightComponent() : nullptr;
		if (!C)
			continue;
		++SkyCount;
		UE_LOG(LogTemp, Warning,
			TEXT("[RoseDbg] SKYLIGHT '%s' mobility=%d intensity=%.2f realtimeCapture=%d "
				 "lowerHemisphereBlack=%d affectsWorld=%d"),
			*S->GetActorLabel(), (int32)C->Mobility, C->Intensity,
			C->bRealTimeCapture ? 1 : 0, C->bLowerHemisphereIsBlack ? 1 : 0,
			C->bAffectsWorld ? 1 : 0);
	}
	if (SkyCount == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoseDbg] NO SKYLIGHT IN THIS LEVEL — anything facing away from "
				 "the sun receives ZERO ambient and renders BLACK, whatever its "
				 "material.  The floor stays lit because it faces the sun."));
	}
}

void ARoseCharacter::RoseNetInfo()
{
	const TCHAR* Mode = TEXT("?");
	switch (GetNetMode())
	{
	case NM_Standalone:      Mode = TEXT("Standalone");      break;
	case NM_ListenServer:    Mode = TEXT("ListenServer");    break;
	case NM_DedicatedServer: Mode = TEXT("DedicatedServer"); break;
	case NM_Client:          Mode = TEXT("Client");          break;
	default: break;
	}
	int32 Players = 0;
	for (TActorIterator<ARoseCharacter> It(GetWorld()); It; ++It)
		++Players;
	const FString Msg = FString::Printf(
		TEXT("[RoseNet] mode=%s authority=%d local=%d role=%d remoteRole=%d characters=%d"),
		Mode, HasAuthority() ? 1 : 0, IsLocallyControlled() ? 1 : 0,
		(int32)GetLocalRole(), (int32)GetRemoteRole(), Players);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	FRoseChatLog::Add(FRoseChatLog::EKind::System, Msg);
}

void
ARoseCharacter::BuildCatalog() {
    SlotOrder = {TEXT("body"),
        TEXT("arms"),
        TEXT("foot"),
        TEXT("cap"),
        TEXT("back"),
        TEXT("weapon"),
        TEXT("hair"),
        TEXT("face"),
        // Face items (masks/goggles/glasses) — BODY_PART_FACE_ITEM in the
        // original client (src/client/cjustmodelavt.h).  Like face/hair they are
        // rigid appearance parts merged from /Game/Characters/Modular/<Gender>/
        // (NOT gear-atlas items), so they need no entry in the gear loop below;
        // the asset-registry scan above picks up every imported faceitem_<id>.
        TEXT("faceitem")};

    for (const FString& S: SlotOrder) {
        SlotIds.Add(S);
        SlotCursor.Add(S, INDEX_NONE);
    }

    const FString Base = FString::Printf(TEXT("/Game/Characters/Modular/%s"), *Gender);

    const FString WeaponsBase = TEXT("/Game/Characters/Modular/WeaponsStatic");

    IAssetRegistry& AR =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    AR.ScanPathsSynchronous({Base, WeaponsBase}, true);

    TArray<FAssetData> Assets;

    // Gender-specific assets
    AR.GetAssetsByPath(FName(*Base), Assets, /*bRecursive*/ true);

    // Shared weapon assets
    TArray<FAssetData> WeaponAssets;
    AR.GetAssetsByPath(FName(*WeaponsBase), WeaponAssets, /*bRecursive*/ true);

    Assets.Append(WeaponAssets);

    for (const FAssetData& A: Assets) {
        // Only SkeletalMesh assets named "<slot>_<id>"
        FString Slot, Rest;
        const FString N = A.AssetName.ToString();

        if (!N.Split(TEXT("_"), &Slot, &Rest) || !SlotIds.Contains(Slot) || !Rest.IsNumeric()) {
            continue;
        }

        SlotIds[Slot].AddUnique(FCString::Atoi(*Rest));
    }

    // Gear slots (body/arms/foot/cap/back) browse the DATA TABLES, not the
    // legacy Modular folder scan — the tables carry the full Arua catalog.
    // Keep only ids the gear data can visually resolve (gear_equip.json).
    for (const TCHAR* GS : { TEXT("body"), TEXT("arms"), TEXT("foot"),
                             TEXT("cap"), TEXT("back") })
    {
        UDataTable* T = ArmorTableForSlot(GS);
        if (!T) continue;
        TArray<int32>& Ids = SlotIds[GS];
        for (const FName& RN : T->GetRowNames())
        {
            FString Rest;
            if (!RN.ToString().Split(TEXT("_"), nullptr, &Rest) || !Rest.IsNumeric())
                continue;
            const int32 Id = FCString::Atoi(*Rest);
            if (LoadPart(GS, Id))
                Ids.AddUnique(Id);
        }
    }

    for (const FString& S: SlotOrder) {
        SlotIds[S].Sort();
    }

    // Only base hairs are selectable.
    if (SlotIds.Contains(TEXT("hair"))) {
        SlotIds[TEXT("hair")].RemoveAll([](int32 Id) { return RoseHairBase(Id) != Id; });
    }

    int32 Total = 0;
    for (const FString& S: SlotOrder) {
        Total += SlotIds[S].Num();
    }

    UE_LOG(LogTemp, Log, TEXT("[Rose] catalog: %d items across %d slots"), Total, SlotOrder.Num());
}

void ARoseCharacter::SetCursorToId(const FString& Slot, int32 Id)
{
	if (!SlotIds.Contains(Slot)) return;
	const int32 idx = SlotIds[Slot].IndexOfByKey(Id);
	SlotCursor[Slot] = idx;   // INDEX_NONE if not found
}

void ARoseCharacter::BrowseId(int Dir)
{
	const FString Slot = SlotOrder[ActiveSlot];
	TArray<int32>& Ids = SlotIds[Slot];
	if (Ids.Num() == 0) return;
	int32& Cur = SlotCursor[Slot];
	Cur = (Cur == INDEX_NONE) ? (Dir > 0 ? 0 : Ids.Num() - 1) : Cur + Dir;
	if (Cur < 0)            Cur = Ids.Num() - 1;
	if (Cur >= Ids.Num())   Cur = 0;
	EquipItem(Slot, Ids[Cur]);
}

void ARoseCharacter::BrowseSlot(int Dir)
{
	ActiveSlot = (ActiveSlot + Dir + SlotOrder.Num()) % SlotOrder.Num();
}

void ARoseCharacter::UnequipActive()
{
	const FString Slot = SlotOrder[ActiveSlot];
	SlotCursor[Slot] = INDEX_NONE;
	EquipItem(Slot, -1);
}

void ARoseCharacter::ShowBrowserHUD()
{
	if (!GEngine || SlotOrder.Num() == 0) return;
	const FString Slot = SlotOrder[ActiveSlot];
	const int32 Cur = SlotCursor[Slot];
	const int32 Count = SlotIds[Slot].Num();
	const int32 ActiveId = (Cur == INDEX_NONE) ? -1 : SlotIds[Slot][Cur];
	const FString IdStr = (Cur == INDEX_NONE) ? TEXT("-") : FString::FromInt(ActiveId);
	GEngine->AddOnScreenDebugMessage(101, 0.f, FColor::Yellow,
		FString::Printf(TEXT(">> %s  id=%s  (%d/%d)  %s"),
			*Slot.ToUpper(), *IdStr, (Cur == INDEX_NONE ? 0 : Cur + 1), Count,
			*GetItemName(Slot, ActiveId)));
	FString Eq;
	for (const FString& S : SlotOrder)
	{
		const int32 c = SlotCursor[S];
		Eq += FString::Printf(TEXT("%s=%s  "), *S,
			(c == INDEX_NONE) ? TEXT("-") : *FString::FromInt(SlotIds[S][c]));
	}
	GEngine->AddOnScreenDebugMessage(102, 0.f, FColor::White, Eq);
	GEngine->AddOnScreenDebugMessage(103, 0.f, FColor::Silver,
		TEXT("Up/Down: slot   Left/Right: id   Del: unequip   LMB: attack"));
	// Diagnostic: show the weapon's resolved Motion Type + the loco anims chosen,
	// so we can tell at a glance whether the per-weapon set is being selected.
	GEngine->AddOnScreenDebugMessage(104, 0.f, FColor::Cyan,
		FString::Printf(TEXT("weapon MT=%d  idle=%s  walk=%s  run=%s"),
			WeaponMotionType,
			IdleAnim ? *IdleAnim->GetName() : TEXT("-"),
			WalkAnim ? *WalkAnim->GetName() : TEXT("-"),
			RunAnim  ? *RunAnim->GetName()  : TEXT("-")));

	// Data-table driven: the active item's equip-requirement check (red if unmet).
	if (ActiveId >= 0)
	{
		FString Reason;
		const bool bOK = MeetsRequirements(Slot, ActiveId, Reason);
		GEngine->AddOnScreenDebugMessage(105, 0.f, bOK ? FColor::Green : FColor::Red,
			bOK ? TEXT("equip: requirements met") : *Reason);
	}
	// Derived stats (ROSE formulas over base stats + loadout) — F2 opens sliders.
	GEngine->AddOnScreenDebugMessage(106, 0.f, FColor::Orange,
		FString::Printf(TEXT("SPD %.0f  AVOID %d  CRIT %d  HIT %d  DEF %d  RES %d  ATK %d  ASPD %d  |  Lv %d  (F2: sliders)"),
			Derived.RunSpeed * SpeedMultiplier, Derived.Avoid, Derived.Crit, Derived.Hit,
			Derived.Defense, Derived.Resist, Derived.AttackPower, Derived.AttackSpeed, Level));

	// Live weapon-grip tuning readout (shown while a weapon is equipped).
	const int32* W = Equipped.Find(TEXT("weapon"));
	if (W && *W >= 0)
	{
		// On the native path the grip is data-driven (ZSC part transform baked
		// into the mesh + the ZMD hand dummy socket) and these constants are
		// forced to identity, so say so rather than let someone spend an hour
		// nudging keys that cannot move anything.
		if (!RoseDummySocket(bTuneLeftHand ? kRoseDummyLeftHand
		                                   : kRoseDummyRightHand).IsNone())
		{
			GEngine->AddOnScreenDebugMessage(107, 0.f, FColor::Green,
				TEXT("GRIP: data-driven (ZSC part xform + ROSE hand dummy socket) — "
					 "nothing to tune here"));
			return;
		}

		const FRotator& GR = bTuneLeftHand ? GripRotL : GripRotR;
		const FVector&  GL = bTuneLeftHand ? GripLocL : GripLocR;
		GEngine->AddOnScreenDebugMessage(107, 0.f, FColor::Magenta, FString::Printf(
			TEXT("GRIP[%s] LEGACY  Rot(P %.0f Y %.0f R %.0f)  Loc(X %.1f Y %.1f Z %.1f)"),
			bTuneLeftHand ? TEXT("L") : TEXT("R"), GR.Pitch, GR.Yaw, GR.Roll, GL.X, GL.Y, GL.Z));
		GEngine->AddOnScreenDebugMessage(108, 0.f, FColor::Silver,
			TEXT("rot Home/End J/L U/O | loc F/H=X G/V=Y R/N=Z | T hand  Y reset  P save"));
	}
}

// "Female"/"Male" -> the "F"/"M" the native importer keys its folders on.
const TCHAR* ARoseCharacter::GenderKey() const
{
	return Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase) ? TEXT("F") : TEXT("M");
}

// The body transform is NOT the same for both asset eras, and getting it from
// the wrong one looks exactly like a handedness bug.
//
// LEGACY (Modular/, built through the glTF path) has the rig's Y negate
// diag(1,-1,1) BAKED INTO THE MESH.  On its own that is a reflection, so the
// character is mirrored — left and right hands swap.  The component cancels it
// with diag(-1,1,1): the product is diag(-1,-1,1), determinant +1, which is a
// 180 degree yaw and not a mirror at all.  Because those two reflections
// secretly contribute that 180, legacy needs yaw +90 (effective 270).
//
// NATIVE (RoseEditor) converts ROSE->UE with the IDENTITY — both are left-handed
// Z-up — so there is no baked reflection, the component must NOT add one, and
// the hidden 180 is gone with them, leaving yaw -90.
//
// Symptoms of using the wrong one: the character runs forward while facing
// backwards (the missing 180) and its hands are swapped (the uncancelled
// reflection).  Do NOT "fix" that by adding a negative scale on the NATIVE path
// — there it introduces a real mirror instead of cancelling one.
void ARoseCharacter::ApplyMeshEraTransform(bool bNative)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
		return;

	// Yaw +90 for BOTH eras.
	//
	// I reasoned this out as -90 for native (ROSE meshes face +Y, so rotate -90
	// to reach UE's +X forward) and that is simply wrong on the actual data:
	// at -90 the character runs forward while facing backwards.  Measured, both
	// rigs want +90.
	Body->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// BOTH eras mirror in X.
	//
	// Without it the native character comes out reversed against the real
	// client: the weapon sits in the left hand and the attack swings the left
	// arm — consistently, weapon and animation agreeing with each other and
	// disagreeing with ROSE.  The legacy path always applied this; native
	// skipped it because a negative scale flips the lighting normals and used
	// to render the character black.
	//
	// That objection is gone: M_RoseChar is UNLIT now (ROSE bakes its lighting
	// into the diffuse), so there are no normals left to flip and the mirror is
	// free.
	Body->SetRelativeScale3D(FVector(-1.f, 1.f, 1.f));

	// A NEGATIVE scale mirrors the lighting normals, so every surface faces away
	// from the light and the character renders BLACK while the asset itself is
	// perfect in the mesh editor.  Say which era won and what scale it left, so
	// that is one log line instead of an afternoon.
	UE_LOG(LogTemp, Warning, TEXT("[Rose] mesh era=%s  scale=%s  yaw=%.0f"),
		bNative ? TEXT("NATIVE") : TEXT("LEGACY"),
		*Body->GetRelativeScale3D().ToString(),
		Body->GetRelativeRotation().Yaw);
}

void ARoseCharacter::RefreshSharedSkeleton()
{
	// The native RoseEditor import is the ONLY character path.
	//
	// The legacy `base` GLB skeleton and the Modular/ + gear-atlas parts are
	// gone: two pipelines meant two of everything (two masters, two refine
	// formulas, two material paths) and the dead half kept being the thing that
	// broke — a refined weapon glowing by a different formula than refined
	// armour was the last of it.
	const FString Native = FString::Printf(
		TEXT("/Game/Rose/Characters/SK_Rose_%s_Skeleton.SK_Rose_%s_Skeleton"),
		GenderKey(), GenderKey());
	SharedSkeleton = LoadObject<USkeleton>(nullptr, *Native);
	ApplyMeshEraTransform(true);

	if (!SharedSkeleton)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Rose] no character skeleton at %s — run -run=RoseImportSkeletal"),
			*Native);
	}
}

USkeletalMesh* ARoseCharacter::LoadPart(const FString& Slot, int32 Id) const {
    // /Game/Rose/Characters/<G>/<SLOT>/SK_<G>_<SLOT>_<id> — the native import.
    //
    // FACEITEM is imported once under F: the mask sits on the head and is the
    // same mesh for both genders.
    const FString SlotUpper = Slot.ToUpper();
    const TCHAR* G = SlotUpper == TEXT("FACEITEM") ? TEXT("F") : GenderKey();
    const FString Asset = FString::Printf(TEXT("SK_%s_%s_%d"), G, *SlotUpper, Id);
    const FString Path = FString::Printf(TEXT("/Game/Rose/Characters/%s/%s/%s.%s"),
        G, *SlotUpper, *Asset, *Asset);
    return LoadObject<USkeletalMesh>(nullptr, *Path);
}

// ── gear (all-from-Arua) resolution ─────────────────────────────────────────

// ── Refine ("grade") data — DataTables/refine.json from LIST_GRADE.STB ──────────
void ARoseCharacter::LoadRefineData()
{
	if (RefineGrades.Num() > 0) return;
	const FString Path = FPaths::ProjectContentDir() / TEXT("DataTables/refine.json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] refine.json not found at %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] refine.json parse failed"));
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("grades"), Arr)) return;
	for (const TSharedPtr<FJsonValue>& GV : *Arr)
	{
		const TSharedPtr<FJsonObject> O = GV->AsObject();
		if (!O.IsValid()) continue;
		FRoseRefineGrade G;
		G.Atk   = (int32)O->GetNumberField(TEXT("atk"));
		G.Hit   = (int32)O->GetNumberField(TEXT("hit"));
		G.Def   = (int32)O->GetNumberField(TEXT("def"));
		G.Res   = (int32)O->GetNumberField(TEXT("res"));
		G.Avoid = (int32)O->GetNumberField(TEXT("avoid"));
		// STB stores 0..255; the glow master multiplies by GlowIntensity.
		G.Glow = FLinearColor(
			(float)O->GetNumberField(TEXT("r")) / 255.f,
			(float)O->GetNumberField(TEXT("g")) / 255.f,
			(float)O->GetNumberField(TEXT("b")) / 255.f, 1.f);
		RefineGrades.Add(G);
	}
	UE_LOG(LogTemp, Log, TEXT("[Rose] loaded %d refine grades"), RefineGrades.Num());
}

ARoseCharacter::FRoseRefineGrade ARoseCharacter::RefineBonus(const FString& Slot) const
{
	const int32 Grade = GetEquippedRefine(Slot);
	if (Grade <= 0 || Grade > RefineGrades.Num()) return FRoseRefineGrade();
	return RefineGrades[Grade - 1];
}

// ── Bonus options (GEM_OP) — DataTables/jem_options.json from LIST_JEMITEM ──────
void ARoseCharacter::LoadJemOptions()
{
	if (JemOptions.Num() > 0) return;
	const FString Path = FPaths::ProjectContentDir() / TEXT("DataTables/jem_options.json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] jem_options.json not found at %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid()) return;
	const TSharedPtr<FJsonObject>* Opts = nullptr;
	if (!Root->TryGetObjectField(TEXT("options"), Opts)) return;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*Opts)->Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pairs = nullptr;
		if (!It.Value->TryGetArray(Pairs)) continue;
		TArray<FIntPoint> Stats;
		for (const TSharedPtr<FJsonValue>& PV : *Pairs)
		{
			const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
			if (PV->TryGetArray(P) && P->Num() == 2)
				Stats.Add(FIntPoint((int32)(*P)[0]->AsNumber(), (int32)(*P)[1]->AsNumber()));
		}
		if (Stats.Num())
			JemOptions.Add(FCString::Atoi(*It.Key), MoveTemp(Stats));
	}
	UE_LOG(LogTemp, Log, TEXT("[Rose] loaded %d jem options"), JemOptions.Num());
}

// Defined below with the requirement labels (single source for ability names).
static FString RoseAbilityName(int32 A);

FString ARoseCharacter::BonusStatText(int32 GemOp) const
{
	const TArray<FIntPoint>* Stats = JemOptions.Find(GemOp);
	if (!Stats || Stats->Num() == 0) return FString();
	FString S;
	for (const FIntPoint& P : *Stats)
	{
		if (!S.IsEmpty()) S += TEXT(", ");
		// MP Consumption is a reduction; everything else is a plus.
		S += FString::Printf(TEXT("%s %s%d"), *RoseAbilityName(P.X),
			P.X == 29 ? TEXT("-") : TEXT("+"), P.Y);
	}
	return S;
}

void ARoseCharacter::SumEquipOptionStats(TMap<int32, int32>& Out) const
{
	// Classic Cal_AddAbility: an option counts ONLY once appraised.
	for (const TPair<FString, int32>& P : Equipped)
	{
		if (P.Value < 0) continue;
		if (!GetEquippedAppraised(P.Key)) continue;
		const TArray<FIntPoint>* Stats = JemOptions.Find(GetEquippedBonus(P.Key));
		if (!Stats) continue;
		for (const FIntPoint& St : *Stats)
			Out.FindOrAdd(St.X) += St.Y;
	}
}

void ARoseCharacter::RoseAppraiseAll()
{
	int32 N = 0;
	for (FRoseItemStack& S : Bag)
		if (!S.bAppraised) { S.bAppraised = true; ++N; }
	if (N) ++BagRevision;
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
			FString::Printf(TEXT("Appraised %d item(s)"), N));
}


void ARoseCharacter::RebuildMesh()
{
	// A dedicated server never renders: the capsule does the collision and the
	// movement component the physics, so skipping the merge (the single most
	// expensive thing an equip does) costs nothing and saves a lot of CPU with
	// hundreds of players on one zone.
	if (IsDedicatedServerRole())
		return;

	// Merge every equipped part into ONE skeletal mesh on the shared skeleton —
	// ROSE renders all parts as units of a single model on one skeleton.
	//
	// Every slot is a per-item mesh straight from the ZSC, carrying its own
	// materials — one path, no era branching.
	const bool bFemale = Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase);
	const int32* CapId = Equipped.Find(TEXT("cap"));

	// NOTE: "back" is deliberately NOT here — back items are socket-attached
	// static meshes (UpdateBack), not merged, so shared-mesh wings keep their own
	// textures.  Merging them shared one atlas material per mesh (the wing bug).
	static const TSet<FString> GearSlots = {
		TEXT("body"), TEXT("arms"), TEXT("foot"), TEXT("cap") };

	TArray<USkeletalMesh*> Parts;
	// Owning equip slot for EVERY merged part, in Parts order.  Refine glow is
	// per ITEM, and once merged the character is a single mesh — so the only way
	// to glow just the refined piece is to know which material slots came from
	// which part.  See ApplyMergedRefineGlow.
	TArray<FString> PartSlots;

	// 1. ARMOUR (body/arms/foot/cap).
	//
	// One skeletal mesh per (slot, id) straight from the ZSC, carrying its own
	// materials — no atlas, no cell lookup, no gear_equip.json, no per-section
	// MID.  Armour merges like any other part.
	for (const TPair<FString, int32>& Pair : Equipped)
	{
		if (Pair.Value < 0 || !GearSlots.Contains(Pair.Key.ToLower())) continue;
		if (USkeletalMesh* M = LoadPart(Pair.Key, Pair.Value))
		{
			Parts.Add(M);
			PartSlots.Add(Pair.Key.ToLower());
		}
	}

	// 2. appearance slots (hair with cap-clip logic, face, faceitem) via Modular
	//    LoadPart.  These are ROSE's rigid, non-skinned parts: the converter has
	//    already baked each one's attachment into the vertices (face/hair → head
	//    bone 4; cap → head dummy 6 DUMMY_IDX_CAP; faceitem → head dummy 4
	//    DUMMY_IDX_MOUSE, per src/client/io_basic.cpp:64), so the merge just
	//    needs the mesh — the loop below is deliberately slot-agnostic and any
	//    new appearance slot works by existing in Equipped + on disk.
	for (const TPair<FString, int32>& Pair : Equipped)
	{
		if (Pair.Value < 0) continue;
		if (Pair.Key == TEXT("weapon")) continue;             // socket-attached, not merged
		if (Pair.Key == TEXT("back")) continue;               // socket-attached (UpdateBack), not merged
		if (Pair.Key == TEXT("subwpn")) continue;             // socket-attached (UpdateSubWeapon), not merged
		if (GearSlots.Contains(Pair.Key.ToLower())) continue; // handled above
		int32 Id = Pair.Value;
		if (Pair.Key == TEXT("hair"))
		{
			const int32 Base = RoseHairBase(Id);
			const int32 Off  = (CapId && *CapId >= 0) ? RoseCapHairOffset(*CapId, bFemale) : 0;
			if (Off <= 0)
			{
				if (USkeletalMesh* M = LoadPart(TEXT("hair"), Base))
				{
					Parts.Add(M);
					PartSlots.Add(TEXT("hair"));
				}
			}
			else if (Off <= 3 && RoseHairBase(Base + Off) == Base)
			{
				if (USkeletalMesh* M = LoadPart(TEXT("hair"), Base + Off))
				{
					Parts.Add(M);
					PartSlots.Add(TEXT("hair"));
				}
			}
			continue;
		}
		if (USkeletalMesh* M = LoadPart(Pair.Key, Id))
		{
			Parts.Add(M);
			PartSlots.Add(Pair.Key.ToLower());
		}
	}
	if (Parts.Num() == 0) return;

	FSkeletalMeshMergeParams Params;
	Params.MeshesToMerge = Parts;
	Params.Skeleton = SharedSkeleton;
	// EVERY part gets explicit section ids.
	//
	// Refine glow has to address one item's material slots and leave the rest
	// alone, and without this the merged slot order is the merge's business:
	// deduping or reordering makes a computed "part -> slot range" correct for
	// the FIRST part and wrong for every part after it, which is exactly the
	// "body glows, the others do not" symptom.  Assigning ids here makes merged
	// slot == the id we chose, so the mapping is known rather than inferred.
	//
	// Each source section gets its OWN id (a face contributes three: base,
	// eyesopen, eyesclosed), so blinking keeps its separate sections.
	{
		TArray<FSkelMeshMergeSectionMapping> SecMap;
		SecMap.SetNum(Parts.Num());
		PartSlotRanges.Reset();
		int32 NextId = 0;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const int32 N = Parts[i] ? FMath::Max(1, Parts[i]->GetMaterials().Num()) : 1;
			SecMap[i].SectionIDs.Reset();
			for (int32 s = 0; s < N; ++s)
				SecMap[i].SectionIDs.Add(NextId + s);
			PartSlotRanges.Add(FIntPoint(NextId, N));
			NextId += N;
		}
		Params.MeshSectionMappings = SecMap;
	}

	if (USkeletalMesh* Merged = USkeletalMergingLibrary::MergeMeshes(Params))
	{
		GetMesh()->SetSkeletalMeshAsset(Merged);

		// Re-apply the era transform HERE, against the parts actually merged.
		//
		// It is also set in the constructor and in RefreshSharedSkeleton, and
		// those two can disagree: the constructor defaults to LEGACY, and if
		// RefreshSharedSkeleton does not run for a given character the component
		// keeps the legacy scale (-1,1,1) while displaying NATIVE meshes.
		//
		// That combination renders the character BLACK — a negative scale mirrors
		// the lighting normals, so every surface faces away from the light — while
		// the asset stays perfect in the mesh editor, which never applies the
		// component's scale.  Deriving it from what was merged makes the two
		// impossible to disagree.
		ApplyMeshEraTransform(true);

		// What the RUNTIME mesh actually ended up with.  Every stage of this is
		// correct on disk, so if the character still renders wrong the answer has
		// to be here — the merged mesh is built in memory and is the one thing no
		// asset audit can see.
		{
			const TArray<FSkeletalMaterial>& M = Merged->GetMaterials();
			UE_LOG(LogTemp, Warning,
				TEXT("[Rose] merged: %d parts -> %d mats"),
				Parts.Num(), M.Num());
			for (int32 i = 0; i < M.Num(); ++i)
			{
				UMaterialInterface* MI = M[i].MaterialInterface;
				UMaterial* Base = MI ? MI->GetMaterial() : nullptr;
				UE_LOG(LogTemp, Warning, TEXT("[Rose]   slot %d: %s (base %s)"),
					i, *GetNameSafe(MI), *GetNameSafe(Base));
			}
		}


		GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		if (CurrentLoco)
			GetMesh()->PlayAnimation(CurrentLoco, true);
		SetupBlink();    // re-locate eye sections in the freshly merged mesh

		// AFTER SetupBlink, never before: it calls EmptyOverrideMaterials() to
		// clear the previous merge's overrides, which would wipe these MIDs.
		MergedParts = Parts;
		MergedPartSlots = PartSlots;
		ApplyMergedRefineGlow(Parts, PartSlots);
		UpdateWeapon();  // re-attach the held weapon to the fresh mesh's hand bones
		UpdateBack();    // re-attach the back item to the fresh mesh's chest bone
		UpdateSubWeapon();   // re-attach the shield to the fresh mesh's left hand
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] mesh merge failed (%d parts)"), Parts.Num());
	}
}

// Look up a weapon's row in the data table by id (weapons.csv → DataTable).
const FRoseWeaponRow* ARoseCharacter::GetWeaponRow(int32 Id) const
{
	if (!WeaponTable || Id < 0) return nullptr;
	const FName RowName(*FString::Printf(TEXT("weapon_%d"), Id));
	return WeaponTable->FindRow<FRoseWeaponRow>(RowName, TEXT("GetWeaponRow"), false);
}

// Short label for a ROSE ability id (t_AbilityINDEX in src/common/shared/datatype.h).
static FString RoseAbilityName(int32 A)
{
	switch (A)
	{
	case 4:  return TEXT("Class");
	case 10: return TEXT("STR");
	case 11: return TEXT("DEX");
	case 12: return TEXT("INT");
	case 13: return TEXT("CON");
	case 14: return TEXT("CHA");
	case 15: return TEXT("SEN");
	// Derived stats — used by the bonus-option display (LIST_JEMITEM options).
	case 16: return TEXT("HP");
	case 17: return TEXT("MP");
	case 18: return TEXT("Attack Power");
	case 19: return TEXT("Defense");
	case 20: return TEXT("Hit Rate");
	case 21: return TEXT("Magic Resistance");
	case 22: return TEXT("Dodge Rate");
	case 23: return TEXT("Movement Speed");
	case 24: return TEXT("Attack Speed");
	case 25: return TEXT("Max Weight");
	case 26: return TEXT("Critical");
	case 27: return TEXT("HP Recovery");
	case 28: return TEXT("MP Recovery");
	case 29: return TEXT("MP Consumption");
	case 31: return TEXT("Level");
	case 38: return TEXT("Max HP");
	case 39: return TEXT("Max MP");
	case 40: return TEXT("Zuly");
	default: return FString::Printf(TEXT("Ability#%d"), A);
	}
}

UDataTable* ARoseCharacter::ArmorTableForSlot(const FString& Slot) const
{
	const FString S = Slot.ToLower();
	if (S == TEXT("body")) return BodyTable;
	if (S == TEXT("arms")) return ArmsTable;
	if (S == TEXT("foot")) return FootTable;
	if (S == TEXT("cap"))  return CapTable;
	if (S == TEXT("back")) return BackTable;   // back rows share FRoseArmorRow
	// Sub-weapons are defensive equipment (shields, magic tools) and use the
	// armour row too — they carry Defense/MagicResist/Durability, which the
	// simple-item row has no fields for.
	if (S == TEXT("subwpn")) return SubWpnTable;
	// Face items carry Defense/MagicResist/Durability; accessories carry the
	// bonus-stat pair.  Neither is a "simple" item — routing them through the
	// simple table dropped every stat they have.
	if (S == TEXT("faceitem")) return FaceItemTable;
	if (S == TEXT("jewel"))    return JewelTable;
	return nullptr;   // hair/face/weapon are not armor tables
}

// Look up an armor row by slot + id (body/arms/foot/cap.csv → DataTable).
const FRoseArmorRow* ARoseCharacter::GetArmorRow(const FString& Slot, int32 Id) const
{
	UDataTable* T = ArmorTableForSlot(Slot);
	if (!T || Id < 0) return nullptr;
	const FName RowName(*FString::Printf(TEXT("%s_%d"), *Slot.ToLower(), Id));
	return T->FindRow<FRoseArmorRow>(RowName, TEXT("GetArmorRow"), false);
}

// Non-equipment item row (consumable/gem/material.csv) — for bag drops with no
// equip model.  Row key is "<slot>_<id>" exactly like the equipment tables.
const FRoseSimpleItemRow* ARoseCharacter::GetSimpleItemRow(const FString& Slot, int32 Id) const
{
	if (Id < 0) return nullptr;
	const FString S = Slot.ToLower();
	UDataTable* T = (S == TEXT("consumable")) ? ConsumableTable
		: (S == TEXT("gem")) ? GemTable
		: (S == TEXT("material")) ? MaterialTable
		: (S == TEXT("faceitem")) ? FaceItemTable
		: (S == TEXT("jewel")) ? JewelTable
		: (S == TEXT("subwpn")) ? SubWpnTable
		: (S == TEXT("pat")) ? PatTable : nullptr;
	if (!T) return nullptr;
	const FName RowName(*FString::Printf(TEXT("%s_%d"), *S, Id));
	return T->FindRow<FRoseSimpleItemRow>(RowName, TEXT("GetSimpleItemRow"), false);
}

FString ARoseCharacter::GetItemName(const FString& Slot, int32 Id) const
{
	if (Id < 0) return TEXT("-");
	FString N;
	if (Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
	{
		if (const FRoseWeaponRow* R = GetWeaponRow(Id))
			N = R->DisplayName.IsEmpty() ? R->StlKey : R->DisplayName;
	}
	else if (const FRoseArmorRow* R = GetArmorRow(Slot, Id))
	{
		N = R->DisplayName.IsEmpty() ? R->StlKey : R->DisplayName;
	}
	else if (const FRoseSimpleItemRow* SR = GetSimpleItemRow(Slot, Id))
	{
		N = SR->DisplayName.IsEmpty() ? SR->StlKey : SR->DisplayName;
	}
	return N.IsEmpty() ? FString::Printf(TEXT("%s %d"), *Slot, Id) : N;
}

FString ARoseCharacter::GetItemDescription(const FString& Slot, int32 Id) const
{
	if (Id < 0) return FString();
	if (Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
	{
		if (const FRoseWeaponRow* R = GetWeaponRow(Id)) return R->Description;
	}
	else if (const FRoseArmorRow* R = GetArmorRow(Slot, Id))
		return R->Description;
	else if (const FRoseSimpleItemRow* SR = GetSimpleItemRow(Slot, Id))
		return SR->Description;
	return FString();
}

// Same per-slot table dispatch as GetItemName/GetItemDescription, for the icon.
// The value is ITEM_ICON_NO (STB col 9) used AS-IS — no era remapping, no STL
// lookup; build_item_icons.py slices the sheets by the same raw index.
int32 ARoseCharacter::GetItemFieldModel(const FString& Slot, int32 Id) const
{
	if (Id < 0) return 0;
	if (Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
	{
		if (const FRoseWeaponRow* R = GetWeaponRow(Id)) return R->FieldItemId;
	}
	else if (const FRoseArmorRow* R = GetArmorRow(Slot, Id))
		return R->FieldItemId;
	else if (const FRoseSimpleItemRow* SR = GetSimpleItemRow(Slot, Id))
		return SR->FieldItemId;
	return 0;
}

int32 ARoseCharacter::GetItemIconIdx(const FString& Slot, int32 Id) const
{
	if (Id < 0) return 0;
	if (Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
	{
		if (const FRoseWeaponRow* R = GetWeaponRow(Id)) return R->IconIdx;
	}
	else if (const FRoseArmorRow* R = GetArmorRow(Slot, Id))
		return R->IconIdx;
	else if (const FRoseSimpleItemRow* SR = GetSimpleItemRow(Slot, Id))
		return SR->IconIdx;
	return 0;
}

int32 ARoseCharacter::GetAbilityValue(int32 A) const
{
	switch (A)
	{
	case 4:  return Skills ? Skills->CurrentJob : 0;   // AT_CLASS
	case 10: return Strength;       // AT_STR
	case 11: return Dexterity;      // AT_DEX
	case 12: return Intelligence;   // AT_INT
	case 13: return Concentration;  // AT_CON
	case 14: return Charm;          // AT_CHARM
	case 15: return Sense;          // AT_SENSE
	// Derived/current values — buff amounts (Get_SkillAdjustVALUE) read these.
	case 16: return (int32)CurrentHP;        // AT_HP
	case 17: return (int32)CurrentMP;        // AT_MP
	case 18: return Derived.AttackPower;     // AT_ATK
	case 19: return Derived.Defense;         // AT_DEF
	case 20: return Derived.Hit;             // AT_HIT
	case 21: return Derived.Resist;          // AT_RES
	case 22: return Derived.Avoid;           // AT_AVOID
	case 23: return (int32)Derived.RunSpeed; // AT_SPEED
	case 24: return Derived.AttackSpeed;     // AT_ATK_SPD
	case 26: return Derived.Crit;            // AT_CRITICAL
	case 31: return Level;          // AT_LEVEL
	case 37: return Skills ? Skills->SkillPoints : 0;  // AT_SKILLPOINT
	case 38: return Derived.MaxHP;           // AT_MAX_HP
	case 39: return Derived.MaxMP;           // AT_MAX_MP
	default: return MAX_int32;      // union/etc. unmodeled → never blocks
	}
}

bool ARoseCharacter::MeetsRequirements(const FString& Slot, int32 Id, FString& OutReason) const
{
	// Pull the up-to-two (ability, amount) requirement pairs from the item's row.
	int32 Stats[2] = { 0, 0 };
	int32 Amts[2]  = { 0, 0 };
	if (Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
	{
		if (const FRoseWeaponRow* R = GetWeaponRow(Id))
		{ Stats[0] = R->ReqStat1; Amts[0] = R->ReqAmount1; Stats[1] = R->ReqStat2; Amts[1] = R->ReqAmount2; }
	}
	else if (const FRoseArmorRow* R = GetArmorRow(Slot, Id))
	{ Stats[0] = R->ReqStat1; Amts[0] = R->ReqAmount1; Stats[1] = R->ReqStat2; Amts[1] = R->ReqAmount2; }

	// ROSE Check_EquipCondition: each non-zero requirement needs char value ≥ amount.
	for (int32 k = 0; k < 2; ++k)
	{
		if (Stats[k] != 0 && GetAbilityValue(Stats[k]) < Amts[k])
		{
			OutReason = FString::Printf(TEXT("Requires %s %d"), *RoseAbilityName(Stats[k]), Amts[k]);
			return false;
		}
	}
	return true;
}

// ── Derived stats (formulas: RoseStatFormulas.h, inputs: DataTables) ─────────────

ARoseCharacter::FDerivedStats ARoseCharacter::ComputeDerivedStats() const
{
	const FLoadoutStats Loadout = ComputeLoadoutStats();

	// Boots move speed from the foot table; barefoot falls back to ROSE's
	// BOOTS_MOVE_SPEED(0) default (~65, the foot row-0 value).
	const int32 BootsSpeed = Loadout.MoveSpeed > 0 ? Loadout.MoveSpeed : 65;

	int32 WeaponAP = 0, WeaponDur = 0, WeaponQual = 0, WeaponType = 0;
	float WeaponAtkSpd = 0.f;
	bool bArmed = false;
	if (const int32* W = Equipped.Find(TEXT("weapon")))
		if (*W >= 0)
			if (const FRoseWeaponRow* R = GetWeaponRow(*W))
			{
				bArmed = true;
				// Refine grade ATK raises the weapon's own attack BEFORE the
				// stat formula (classic: grade applies to the item, not the char).
				WeaponAP = R->AttackPower + RefineBonus(TEXT("weapon")).Atk;
				WeaponDur = R->Durability;
				WeaponQual = R->Quality;
				WeaponAtkSpd = R->AttackSpeed;
				WeaponType = R->Type;
			}

	// Skill contributions: PASSIVE base-stat raises apply flat + rate% of the
	// base (Skill_LEARN passive branch, cuserdata.cpp:1560+), then feed every
	// formula below; running BUFFS add their ING_INC_*/DEC pair nets on top of
	// the derived values (IngSTATUS totals — datatype.h eING_TYPE).
	auto PsvStat = [&](int32 Base, int32 Ability) -> int32 {
		if (!Skills) return Base;
		return Base + Skills->PassiveFlat(Ability)
			+ (int32)(Base * Skills->PassiveRate(Ability) / 100.f);
	};
	auto Psv = [&](int32 Ability) { return Skills ? Skills->PassiveFlat(Ability) : 0; };
	auto PsvR = [&](int32 Ability) { return Skills ? Skills->PassiveRate(Ability) : 0; };
	auto Buff = [&](int32 IngIncType) { return Skills ? Skills->BuffNet(IngIncType) : 0; };

	// Appraised bonus options on equipped items (classic m_iAddValue): base-stat
	// options feed the formulas below, derived-stat options add flat after them.
	TMap<int32, int32> Opt;
	SumEquipOptionStats(Opt);
	auto O = [&Opt](int32 Type) { const int32* V = Opt.Find(Type); return V ? *V : 0; };

	const int32 EffStr = PsvStat(Strength, 10) + O(10);   // AT_STR..AT_SENSE = 10..15
	const int32 EffDex = PsvStat(Dexterity, 11) + O(11);
	const int32 EffInt = PsvStat(Intelligence, 12) + O(12);
	const int32 EffCon = PsvStat(Concentration, 13) + O(13);
	const int32 EffSen = PsvStat(Sense, 15) + O(15);

	FDerivedStats S;
	S.RunSpeed    = RoseStats::RunSpeed(EffDex, BootsSpeed) + O(23);
	// Refine grade bonuses ride on the Loadout (weapon grade -> Hit, armor ->
	// Avoid); appraised option stats add flat after each formula (classic
	// m_iAddValue semantics).
	S.Avoid       = RoseStats::Avoid(EffDex, Level) + Loadout.Avoid + O(22);
	S.Crit        = RoseStats::Critical(EffSen, EffCon) + O(26);
	S.Hit         = RoseStats::HitRate(EffCon, bArmed, WeaponDur, WeaponQual) + Loadout.Hit + O(20);
	S.Defense     = RoseStats::Defence(Loadout.Defense, EffStr, Level) + O(19);
	S.Resist      = RoseStats::Resist(Loadout.MagicResist, EffInt, Level) + O(21);
	S.MaxHP       = RoseStats::MaxHP(Level, EffStr) + O(16) + O(38);
	S.MaxMP       = RoseStats::MaxMP(Level, EffInt) + O(17) + O(39);
	S.MaxWeight   = RoseStats::MaxWeight(Level, EffStr) + O(25);
	S.AttackPower = (bArmed
		? RoseStats::AttackPower(WeaponType, EffStr, EffDex, EffInt, EffCon, EffSen,
			Level, WeaponAP, WeaponQual)
		: RoseStats::AttackPowerUnarmed(EffStr, EffDex, Level)) + O(18);
	S.AttackSpeed = (bArmed ? RoseStats::AttackSpeed(WeaponAtkSpd) : 100) + O(24);

	if (Skills)
	{
		// Passive derived-stat abilities (AT_PSV_*, datatype.h:578-649) +
		// weapon-class attack passives (cuserdata.cpp:1449/1491), then buffs
		// (ING_INC pairs: 4 maxHP, 6 movspd, 8 atkspd, 10 atk, 12 def, 14 res,
		// 16 hit, 18 crit, 20 avoid).
		S.MaxHP       += Psv(54) + (int32)(S.MaxHP * PsvR(54) / 100.f) + Buff(4);   // AT_PSV_MAX_HP
		S.MaxMP       += Psv(55) + (int32)(S.MaxMP * PsvR(55) / 100.f) + Buff(5);   // AT_PSV_MAX_MP
		S.RunSpeed    += Psv(52) + S.RunSpeed * PsvR(52) / 100.f + Buff(6);         // AT_PSV_MOV_SPD
		S.AttackSpeed += Skills->PassiveAttackSpeedBonus(S.AttackSpeed, WeaponType) + Buff(8);
		S.AttackPower += Skills->PassiveAttackPowerBonus(S.AttackPower, WeaponType) + Buff(10);
		S.Defense     += Psv(53) + (int32)(S.Defense * PsvR(53) / 100.f) + Buff(12); // AT_PSV_DEF_POW
		S.Resist      += Psv(98)  + Buff(14);   // AT_PSV_RES
		S.Hit         += Psv(99)  + Buff(16);   // AT_PSV_HIT
		S.Crit        += Psv(100) + Buff(18);   // AT_PSV_CRITICAL
		S.Avoid       += Psv(101) + Buff(20);   // AT_PSV_AVOID
	}
	return S;
}

int64 ARoseCharacter::GetNeedExp() const
{
	return RoseExp::NeedExp(Level);
}

float ARoseCharacter::GetExpFraction() const
{
	if (Level >= RoseExp::MaxLevel) return 1.f;
	const int64 Need = RoseExp::NeedExp(Level);
	return Need > 0 ? FMath::Clamp((float)((double)Exp / (double)Need), 0.f, 1.f) : 0.f;
}

void ARoseCharacter::GiveExp(int64 Amount)
{
	if (Amount <= 0 || Level >= RoseExp::MaxLevel)
		return;
	Exp += Amount;

	int32 Gained = 0;
	while (Level < RoseExp::MaxLevel && Exp >= RoseExp::NeedExp(Level))
	{
		Exp -= RoseExp::NeedExp(Level);
		++Level;
		++Gained;
	}
	if (Level >= RoseExp::MaxLevel)
		Exp = 0;   // cap: no overflow bar past max level

	if (Gained > 0)
	{
		// A level-up recomputes every derived stat and tops off HP/MP (faithful
		// to CObjAVT level-up: full heal + stat re-derive).
		ApplyDerivedStats();
		CurrentHP = (float)Derived.MaxHP;
		CurrentMP = (float)Derived.MaxMP;
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Level up!  You are now level %d."), Level));
	}
	++ExpRevision;
}

void ARoseCharacter::ApplyDerivedStats()
{
	Derived = ComputeDerivedStats();
	// ROSE speeds are cm/s — MaxWalkSpeed takes them unchanged; the slider
	// multiplier is the only non-faithful factor.
	GetCharacterMovement()->MaxWalkSpeed = Derived.RunSpeed * SpeedMultiplier;
	// Locomotion play rate tracks the effective speed (Cal_RunAniSPEED), so the
	// feet keep up when the slider changes; re-applied by PlayLoco on anim swaps.
	if (CurrentLoco && CurrentLoco == RunAnim)
		GetMesh()->SetPlayRate(RoseStats::RunAnimRate(Derived.RunSpeed * SpeedMultiplier));
	// First run (or a stat respec that raised MaxHP): fill up; otherwise clamp.
	if (CurrentHP <= 0.f || CurrentHP > Derived.MaxHP)
		CurrentHP = (float)Derived.MaxHP;
	if (CurrentMP <= 0.f || CurrentMP > Derived.MaxMP)
		CurrentMP = (float)Derived.MaxMP;
}

void ARoseCharacter::ReceiveMonsterMiss()
{
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 120.f),
		TEXT("MISS"), nullptr, FColor::Silver, 0.8f, true);
}

void ARoseCharacter::ReceiveMonsterHit(float Damage, bool bCritical)
{
	CurrentHP = FMath::Max(0.f, CurrentHP - Damage);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 120.f),
		FString::Printf(TEXT("-%d%s"), FMath::CeilToInt(Damage), bCritical ? TEXT("!!") : TEXT("")),
		nullptr, bCritical ? FColor::Red : FColor::Orange, 0.8f, true);
	if (CurrentHP <= 0.f)
	{
		// No death flow yet — dev character refills where it stands.
		CurrentHP = (float)Derived.MaxHP;
		if (GEngine)
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red,
				TEXT("You were defeated! (HP restored — no death flow yet)"));
	}
}

ARoseCharacter::FLoadoutStats ARoseCharacter::ComputeLoadoutStats() const
{
	FLoadoutStats S;
	for (const TPair<FString, int32>& P : Equipped)
	{
		if (P.Value < 0) continue;
		const FRoseRefineGrade G = RefineBonus(P.Key);   // zeroes when unrefined
		if (P.Key == TEXT("weapon"))
		{
			if (const FRoseWeaponRow* R = GetWeaponRow(P.Value)) S.Attack += R->AttackPower;
			// Weapon refine: grade ATK + HIT (LIST_GRADE.STB via refine.json).
			S.Attack += G.Atk;
			S.Hit    += G.Hit;
		}
		else if (const FRoseArmorRow* R = GetArmorRow(P.Key, P.Value))
		{
			S.Defense     += R->Defense;
			S.MagicResist += R->MagicResist;
			if (P.Key == TEXT("foot")) S.MoveSpeed = R->MoveSpeed;   // boots set base speed
			// Armor refine: grade DEF + RES + AVOID.
			S.Defense     += G.Def;
			S.MagicResist += G.Res;
			S.Avoid       += G.Avoid;
		}
		else if (P.Key == TEXT("faceitem") || P.Key == TEXT("subwpn"))
		{
			// Masks and shields have no armor row (simple-item tables) but DO
			// refine like armor: grade DEF + RES + AVOID.
			S.Defense     += G.Def;
			S.MagicResist += G.Res;
			S.Avoid       += G.Avoid;
		}
	}
	return S;
}

// ── Held weapons (socket-attached static meshes, live-tunable grip) ──────────────

// Resolve an equipment mesh, preferring the NATIVE import and falling back to
// the old glTF/Interchange assets.
//
//   native : /Game/Rose/Equipment/<Kind>/SM_<item>          (RoseEditor)
//   legacy : /Game/Characters/Modular/<Legacy>/<item>/<item>/StaticMeshes/<item>
//            (Interchange's nested folder shape)
//
// The fallback is what makes this safe to switch on before every pack has been
// re-imported: an item the native run has not produced yet still loads.
//
// `_off` is the dual-wield off-hand variant; the ZSC has no separate object for
// it, so it falls back to the main-hand mesh rather than vanishing.
static UStaticMesh* RoseLoadEquipmentMesh(const TCHAR* Kind, const FString& ItemName,
	const TCHAR* LegacyFolder)
{
	if (ItemName.IsEmpty())
		return nullptr;

	auto TryLoad = [](const FString& Path) -> UStaticMesh*
	{
		return LoadObject<UStaticMesh>(nullptr, *Path);
	};

	FString BaseName = ItemName;
	const bool bOffHand = BaseName.EndsWith(TEXT("_off"));
	if (bOffHand)
		BaseName.LeftChopInline(4);

	// native, exact name then the off-hand's main-hand equivalent
	for (const FString& Name : { ItemName, BaseName })
	{
		const FString Native = FString::Printf(
			TEXT("/Game/Rose/Equipment/%s/SM_%s.SM_%s"), Kind, *Name, *Name);
		if (UStaticMesh* Mesh = TryLoad(Native))
			return Mesh;
		if (Name == BaseName)
			break;   // both names identical when not an off-hand
	}

	// legacy
	for (const FString& Name : { ItemName, BaseName })
	{
		const FString Legacy = FString::Printf(
			TEXT("/Game/Characters/Modular/%s/%s/%s/StaticMeshes/%s.%s"),
			LegacyFolder, *Name, *Name, *Name, *Name);
		if (UStaticMesh* Mesh = TryLoad(Legacy))
			return Mesh;
		if (Name == BaseName)
			break;
	}

	return nullptr;
}

UStaticMesh* ARoseCharacter::LoadWeaponStatic(const FString& ItemName) const
{
	return RoseLoadEquipmentMesh(TEXT("Weapons"), ItemName, TEXT("WeaponsStatic"));
}

// Sign to apply to a socket-attached item's X scale so the body's mirror does
// not carry into it.  See ApplyWeaponGrips for the full reasoning: the legacy
// rig's baked Y negate is cancelled by a (-1,1,1) on the body, and anything
// parented to the body inherits that reflection uncancelled.
float ARoseCharacter::AttachedMirrorX() const
{
	const USkeletalMeshComponent* Body = GetMesh();
	return (Body && Body->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
}

int32 ARoseCharacter::GetTunedWeaponType() const
{
	// The off hand holds the SUB-weapon, so tuning the left hand keys off that
	// item rather than the main weapon.
	if (bTuneLeftHand)
	{
		// Sub-weapons are NOT in LIST_WEAPON — shields and magic tools live in
		// LIST_SUBWPN, with their class in Subtype.  Looking them up in the
		// weapon table returned nothing, so every off-hand item collapsed to
		// type 0 and shared one grip.
		const int32* SubId = Equipped.Find(TEXT("subwpn"));
		if (!SubId || *SubId < 0)
			return 0;
		if (const FRoseArmorRow* Row = GetArmorRow(TEXT("subwpn"), *SubId))
			return Row->Type;
		return 0;
	}

	const int32* Id = Equipped.Find(TEXT("weapon"));
	if (!Id || *Id < 0)
		return 0;
	if (const FRoseWeaponRow* Row = GetWeaponRow(*Id))
		return Row->Type;
	return 0;
}

// Find the tuned grip for a (type, hand) pair, or null when that combination
// has never been tuned — callers then fall back to the shared per-hand values.
static FRoseWeaponGrip* FindGrip(TArray<FRoseWeaponGrip>& Grips, int32 Type, bool bLeft)
{
	for (FRoseWeaponGrip& G : Grips)
		if (G.WeaponType == Type && G.bLeftHand == bLeft)
			return &G;
	return nullptr;
}

void ARoseCharacter::GetGrip(FVector& OutLoc, FRotator& OutRot, float& OutScale) const
{
	const int32 Type = GetTunedWeaponType();
	if (Type != 0)
	{
		if (const FRoseWeaponGrip* G =
			FindGrip(const_cast<TArray<FRoseWeaponGrip>&>(WeaponGrips), Type, bTuneLeftHand))
		{
			OutLoc = G->Loc; OutRot = G->Rot; OutScale = G->Scale;
			return;
		}
	}
	OutLoc   = bTuneLeftHand ? GripLocL   : GripLocR;
	OutRot   = bTuneLeftHand ? GripRotL   : GripRotR;
	OutScale = bTuneLeftHand ? GripScaleL : GripScaleR;
}

void ARoseCharacter::SetGrip(const FVector& Loc, const FRotator& Rot, float Scale)
{
	const int32 Type = GetTunedWeaponType();
	if (Type != 0)
	{
		FRoseWeaponGrip* G = FindGrip(WeaponGrips, Type, bTuneLeftHand);
		if (!G)
		{
			FRoseWeaponGrip New;
			New.WeaponType = Type;
			New.bLeftHand = bTuneLeftHand;
			G = &WeaponGrips[WeaponGrips.Add(New)];
		}
		G->Loc = Loc; G->Rot = Rot; G->Scale = Scale;
	}
	else
	{
		// Nothing equipped to key off — edit the shared fallback.
		(bTuneLeftHand ? GripLocL   : GripLocR)   = Loc;
		(bTuneLeftHand ? GripRotL   : GripRotR)   = Rot;
		(bTuneLeftHand ? GripScaleL : GripScaleR) = Scale;
	}
	// Push it straight onto the components so the viewport tracks the slider.
	//
	// The SUB-weapon's transform is applied by UpdateSubWeapon, not
	// ApplyWeaponGrips — without this call a shield only picked up a new grip
	// after a save-and-reequip, which read as the sliders not working at all.
	ApplyWeaponGrips();
	UpdateSubWeapon();
}

void ARoseCharacter::ApplyWeaponGrips()
{
	// On the NATIVE path the weapon is attached to a ROSE hand dummy and its own
	// grip rotation is baked into the mesh, so the relative transform must be
	// IDENTITY — applying the legacy tuned constants on top would rotate a
	// correctly-placed weapon back out of the hand.  The constants are kept for
	// the legacy fallback, where they still do the work.
	// These used to ask "does a rose_dummy socket exist?" and, if so, force the
	// relative transform to IDENTITY — on the theory that the grip was baked and
	// the dummy positioned it.  But weapons attach to the hand BONE now, not the
	// dummy, so the sockets still exist while nothing uses them: the tuner wrote
	// values that were then thrown away every frame, and the sliders did
	// nothing.  Attach point and grip source have to agree.
	const bool bNativeR = !RSocketInUse.IsNone();
	const bool bNativeL = !LSocketInUse.IsNone();

	// CANCEL THE BODY'S MIRROR ON THE WEAPON.
	//
	// The legacy rig carries a baked Y negate, which the body component cancels
	// with scale (-1,1,1) — two reflections whose product is a 180 yaw, so the
	// CHARACTER is correct.  But a weapon is attached to that component and
	// inherits the (-1,1,1) on its own, uncancelled: the sword renders mirrored,
	// which reads as its facing being completely wrong while its position looks
	// right.
	//
	// Negating the child's X as well makes the weapon's world scale +1 again.
	// It is derived from the body rather than hard-coded so it follows the era
	// automatically — the native rig has no mirror and this becomes a no-op.
	//
	// This is NOT something the tuned grip constants can fix: GripRotR is a -5
	// degree nudge, and no rotation can undo a reflection.

	// Resolve the RIGHT hand's grip from the equipped weapon's type, falling
	// back to the shared values when that type has not been tuned.
	FVector  RLoc = GripLocR;
	FRotator RRot = GripRotR;
	float    RScale = GripScaleR;
	if (const int32* WId = Equipped.Find(TEXT("weapon")))
		if (*WId >= 0)
			if (const FRoseWeaponRow* Row = GetWeaponRow(*WId))
				if (const FRoseWeaponGrip* G = FindGrip(WeaponGrips, Row->Type, false))
				{
					RLoc = G->Loc; RRot = G->Rot; RScale = G->Scale;
				}

	if (WeaponR)
	{
		WeaponR->SetRelativeLocation(bNativeR ? FVector::ZeroVector : RLoc);
		WeaponR->SetRelativeRotation(bNativeR ? FRotator::ZeroRotator : RRot);
		WeaponR->SetRelativeScale3D(FVector(RScale));
	}
	// The LEFT hand of a DUAL WIELD holds the same weapon type's off-hand
	// blade, so it takes that type's left-hand grip — not the shared fallback.
	// Without this a katar's second blade ignored every tuned value.
	FVector  LLoc = GripLocL;
	FRotator LRot = GripRotL;
	float    LScale = GripScaleL;
	if (const int32* WId = Equipped.Find(TEXT("weapon")))
		if (*WId >= 0)
			if (const FRoseWeaponRow* Row = GetWeaponRow(*WId))
				if (const FRoseWeaponGrip* G = FindGrip(WeaponGrips, Row->Type, true))
				{
					LLoc = G->Loc; LRot = G->Rot; LScale = G->Scale;
				}

	if (WeaponL)
	{
		WeaponL->SetRelativeLocation(bNativeL ? FVector::ZeroVector : LLoc);
		WeaponL->SetRelativeRotation(bNativeL ? FRotator::ZeroRotator : LRot);
		WeaponL->SetRelativeScale3D(FVector(LScale));
	}
}

FName ARoseCharacter::RoseDummySocket(int32 DummyIndex) const
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
		return NAME_None;

	const FName SocketName(*FString::Printf(TEXT("rose_dummy_%d"), DummyIndex));
	return MeshComp->DoesSocketExist(SocketName) ? SocketName : NAME_None;
}

void ARoseCharacter::UpdateWeapon()
{
	const int32* W = Equipped.Find(TEXT("weapon"));
	const int32 Id = (W && *W >= 0) ? *W : -1;

	// Hand routing from the weapons.csv Hand column ("Right"/"Left"/"Right+Left").
	FString Hand = TEXT("Right");
	if (Id >= 0)
		if (const FRoseWeaponRow* Row = GetWeaponRow(Id))
			if (!Row->Hand.IsEmpty()) Hand = Row->Hand;
	const bool bHasR = Hand.Contains(TEXT("Right"));
	const bool bHasL = Hand.Contains(TEXT("Left"));
	const bool bDual = bHasR && bHasL;

	// Dual-wield (katar/dual-sword/dual-gun): part 0 = weapon_<id> → right hand,
	// part 1 = weapon_<id>_off → left hand.  Single weapon: one mesh to its hand.
	UStaticMesh* RMesh = nullptr;
	UStaticMesh* LMesh = nullptr;
	if (Id >= 0)
	{
		const FString Base = FString::Printf(TEXT("weapon_%d"), Id);
		if (bDual)
		{
			RMesh = LoadWeaponStatic(Base);
			LMesh = LoadWeaponStatic(Base + TEXT("_off"));
		}
		else if (bHasL)               // left-only (guns)
			LMesh = LoadWeaponStatic(Base);
		else                          // right-hand (default)
			RMesh = LoadWeaponStatic(Base);
	}

	// Attach at the ROSE hand DUMMY when the skeleton has one (dummy 0 = R_HAND,
	// 1 = L_HAND).  The dummy carries the hand's offset AND orientation, and the
	// item's own grip rotation is already baked into the mesh from the ZSC part
	// — so between them the weapon lands correctly with no tuned constant.
	// Falling back to the bare hand bone is the legacy path, where the grip
	// values below still do the work.
	// Attach to the HAND BONE, not a rose_dummy socket.
	//
	// This is what the legacy avatar pipeline did (RightHandBone = b1_rhand)
	// and it holds the weapon in the correct hand.  The grip offset/rotation on
	// top is tuned by hand — see the grip tuning window.
	const FName RSocket = NAME_None;
	const FName LSocket = NAME_None;
	// Remember what we attached to, so ApplyWeaponGrips knows whether the grip
	// is already baked (dummy) or still needs the tuned transform (bone).
	RSocketInUse = RSocket;
	LSocketInUse = LSocket;

	if (WeaponR)
	{
		WeaponR->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform,
			RSocket.IsNone() ? RightHandBone : RSocket);
		WeaponR->SetStaticMesh(RMesh);
		WeaponR->SetVisibility(RMesh != nullptr);
	}
	if (WeaponL)
	{
		WeaponL->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform,
			LSocket.IsNone() ? LeftHandBone : LSocket);
		WeaponL->SetStaticMesh(LMesh);
		WeaponL->SetVisibility(LMesh != nullptr);
	}
	ApplyWeaponGrips();
	ApplyRefineGlow();
}

// ── Refine glow: additive overlay tinted with the grade's authentic RGB ─────────
void ARoseCharacter::ApplyRefineGlow()
{
	// Each hand carries a DIFFERENT item, so each takes its own grade: the right
	// hand is the weapon, the left is the sub-weapon.  Using the weapon's grade
	// for both made a refined sword light up an unrefined shield.
	GlowStaticItem(WeaponR, TEXT("weapon"));
	GlowStaticItem(WeaponL, TEXT("subwpn"));
	// BackItem too — it is glowed by UpdateBack on equip, but that only runs when
	// the back item CHANGES.  Left out here, the refine slider moved the grade
	// and nothing on the back updated until you re-equipped it.
	GlowStaticItem(BackItem, TEXT("back"));
}

// One glow for everything.
//
// This used to re-render the item additively through M_RoseRefineGlow
// (SetOverlayMaterial).  That is a DIFFERENT effect from the merged armour's,
// which drives M_RoseChar's RefineColor/RefineIntensity — the reference client's
// pow(albedo,GlowPower)*RefineColor*RefineIntensity — so a refined sword and
// refined armour did not look like the same game.  Equipment now parents to
// M_RoseChar too (RoseEquipmentImporter), so the identical parameters are
// available here and the overlay is gone.
void ARoseCharacter::GlowStaticItem(UStaticMeshComponent* Comp, const FString& Slot)
{
	if (!Comp)
		return;

	// Clear any overlay left by the previous implementation, or by a build that
	// still had it: otherwise both effects stack on the same item.
	Comp->SetOverlayMaterial(nullptr);

	UStaticMesh* SM = Comp->GetStaticMesh();
	if (!SM)
		return;

	const int32 Grade = GetEquippedRefine(Slot);
	const bool bGlow = Grade > 0 && Grade <= RefineGrades.Num();
	const FLinearColor Glow = bGlow ? RefineGrades[Grade - 1].Glow : FLinearColor::Black;

	const TArray<FStaticMaterial>& Mats = SM->GetStaticMaterials();
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		// From the ASSET's material, never the component's current one — that
		// would be the previous MID and each slider drag would stack another.
		UMaterialInterface* Base = Mats[i].MaterialInterface;
		if (!Base)
			continue;
		// Set unconditionally, intensity 0 included, so sliding back to +0
		// actually removes the glow instead of leaving it burnt in.
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
		Mid->SetVectorParameterValue(TEXT("RefineColor"), Glow);
		Mid->SetScalarParameterValue(TEXT("RefineIntensity"),
				bGlow ? RefineGlowScale : 0.f);
		Comp->SetMaterial(i, Mid);
	}
}

// ── Refine glow on the MERGED character (body / arms / foot / cap / faceitem) ──
//
// The socket-attached items (weapon, sub-weapon, back) each own a component, so
// they glow with SetOverlayMaterial.  Armour cannot: after
// USkeletalMergingLibrary::MergeMeshes the whole character is ONE component, and
// an overlay there would light up the entire body when a single piece is
// refined.
//
// So this drives M_RoseChar's own RefineColor / RefineIntensity — the parameters
// that reproduce the reference client's RefineGlow shader — on just the material
// slots belonging to the refined part.
//
// Mapping part -> material slots relies on the merge appending each part's
// materials in order.  That holds while the parts' materials are DISTINCT, which
// is the native path's normal state (each item has its own textures).  If the
// merge deduped, the running cursor would be wrong and the glow would land on
// the wrong body part — so the count is verified first and the whole pass is
// skipped when it disagrees.  Missing beats wrong.
void ARoseCharacter::RefreshMergedRefineGlow()
{
	ApplyMergedRefineGlow(MergedParts, MergedPartSlots);
}

void ARoseCharacter::ApplyMergedRefineGlow(const TArray<USkeletalMesh*>& Parts,
                                           const TArray<FString>& InPartSlots)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	USkeletalMesh* Merged = MeshComp ? MeshComp->GetSkeletalMeshAsset() : nullptr;
	if (!Merged || Parts.Num() == 0 || Parts.Num() != InPartSlots.Num())
		return;

	// Ranges come from the section ids we ASSIGNED at merge time, not from
	// re-deriving them here — see the MeshSectionMappings block in RebuildMesh.
	if (PartSlotRanges.Num() != Parts.Num())
		return;

	const TArray<FSkeletalMaterial>& MergedMats = Merged->GetMaterials();

	for (int32 p = 0; p < Parts.Num(); ++p)
	{
		const int32 Grade = GetEquippedRefine(InPartSlots[p]);
		const bool bGlow = Grade > 0 && Grade <= RefineGrades.Num();
		const FLinearColor Glow = bGlow ? RefineGrades[Grade - 1].Glow
		                                : FLinearColor::Black;

		const int32 First = PartSlotRanges[p].X;
		const int32 Count = PartSlotRanges[p].Y;
		for (int32 k = 0; k < Count; ++k)
		{
			const int32 SlotIdx = First + k;
			if (!MergedMats.IsValidIndex(SlotIdx))
				continue;

			// Build the MID from the ASSET's material, never from
			// MeshComp->GetMaterial(): once a glow has been applied that returns
			// the previous MID, so dragging the slider would stack a MID on a MID
			// on a MID and the parameter set would drift.
			UMaterialInterface* Base = MergedMats[SlotIdx].MaterialInterface;
			if (!Base)
				continue;

			// Applied UNCONDITIONALLY, including intensity 0.  Only setting it
			// when a grade exists means sliding back to +0 leaves the last glow
			// burnt in — which reads as "the slider does not work".
			UMaterialInstanceDynamic* Mid =
				UMaterialInstanceDynamic::Create(Base, this);
			Mid->SetVectorParameterValue(TEXT("RefineColor"), Glow);
			Mid->SetScalarParameterValue(TEXT("RefineIntensity"),
				bGlow ? RefineGlowScale : 0.f);
			MeshComp->SetMaterial(SlotIdx, Mid);
		}
	}
}

void ARoseCharacter::RoseGlowScale(float Scale)
{
	RefineGlowScale = FMath::Max(0.f, Scale);

	// Re-push to everything currently wearing a glow.  The scale multiplies the
	// grade's own colour, which is where the per-grade amount already lives.
	ApplyRefineGlow();
	RefreshMergedRefineGlow();

	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("[Rose] refine glow scale = %.2f"), RefineGlowScale));
}

void ARoseCharacter::RoseRefine(int32 Grade)
{
	RoseRefineSlot(TEXT("weapon"), Grade);
}

void ARoseCharacter::RoseRefineSlot(const FString& InSlot, int32 Grade)
{
	const FString Slot = InSlot.ToLower();
	// Fail LOUDLY — a silent no-op here is indistinguishable from "refine is
	// broken" and already cost a debugging round.
	if (RefineGrades.Num() == 0)
	{
		LoadRefineData();   // retry (BeginPlay may have run before the json existed)
		if (RefineGrades.Num() == 0)
		{
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red,
				TEXT("REFINE: refine.json missing/empty — run tools/gen_refine_table.py"));
			return;
		}
	}
	if (GetEquippedId(Slot) < 0)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red,
			FString::Printf(TEXT("REFINE: nothing equipped in '%s'"), *Slot));
		return;
	}
	Grade = FMath::Clamp(Grade, 0, RefineGrades.Num());
	EquippedRefine.Add(Slot, Grade);
	if (Slot == TEXT("weapon"))
		ApplyRefineGlow();
	else
		RebuildMesh();   // merged armor + back re-tint through their MIDs
	ApplyDerivedStats();
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow,
			FString::Printf(TEXT("%s refine -> +%d"), *Slot, Grade));
}

// ── Sub-weapon (shield/off-hand): left-hand socket attach + the 1H gate ─────────
UStaticMesh* ARoseCharacter::LoadSubWpnStatic(const FString& ItemName) const
{
	return RoseLoadEquipmentMesh(TEXT("SubWpn"), ItemName, TEXT("SubWpnStatic"));
}

bool ARoseCharacter::WeaponAllowsSubWeapon() const
{
	// Classic: the shield needs the LEFT hand free — only a plain one-handed
	// right weapon (Hand == "Right") qualifies.  Two-handers and duals occupy
	// both hands; left-hand guns occupy the shield hand itself.  Bare fists are
	// fine (shield alone is legal).
	const int32* W = Equipped.Find(TEXT("weapon"));
	if (!W || *W < 0) return true;
	if (const FRoseWeaponRow* R = GetWeaponRow(*W))
		return R->Hand.IsEmpty() || R->Hand == TEXT("Right");
	return true;
}

void ARoseCharacter::UpdateSubWeapon()
{
	if (!SubWeapon)
		return;
	const int32* S = Equipped.Find(TEXT("subwpn"));
	const int32 Id = (S && *S >= 0) ? *S : -1;
	UStaticMesh* Mesh = (Id >= 0 && WeaponAllowsSubWeapon())
		? LoadSubWpnStatic(FString::Printf(TEXT("subwpn_%d"), Id))
		: nullptr;
	// ROSE dummy 2 = L_SHIELD, which is where a sub-weapon actually hangs — not
	// the left hand bone.  Its own orientation is baked in from the ZSC part.
	// Attach to the LEFT HAND BONE, like the weapons attach to the right — the
	// dummy-socket path forced an identity transform, which meant the tuner
	// could not move a shield at all.
	SubWeapon->AttachToComponent(GetMesh(),
		FAttachmentTransformRules::KeepRelativeTransform, LeftHandBone);
	SubWeapon->SetStaticMesh(Mesh);
	SubWeapon->SetVisibility(Mesh != nullptr);

	// Per-TYPE grip: shields and magic tools sit in the hand differently, and
	// their class comes from LIST_SUBWPN's Subtype (they are not in
	// LIST_WEAPON).  Untuned types keep the shared SubGrip* fallback.
	FVector  SLoc = SubGripLoc;
	FRotator SRot = SubGripRot;
	float    SScale = SubGripScale;
	if (const int32* SubId = Equipped.Find(TEXT("subwpn")))
		if (*SubId >= 0)
			if (const FRoseArmorRow* Row = GetArmorRow(TEXT("subwpn"), *SubId))
				if (const FRoseWeaponGrip* G = FindGrip(WeaponGrips, Row->Type, true))
				{
					SLoc = G->Loc; SRot = G->Rot; SScale = G->Scale;
				}

	SubWeapon->SetRelativeLocation(SLoc);
	SubWeapon->SetRelativeRotation(SRot);
	SubWeapon->SetRelativeScale3D(FVector(SScale));
}

// ── Back item (socket-attached static mesh, live-tunable placement) ──────────────
UStaticMesh* ARoseCharacter::LoadBackStatic(const FString& ItemName) const
{
	// Interchange nesting mirrors weapons: BackStatic/<name>/<name>/StaticMeshes/<name>
	return RoseLoadEquipmentMesh(TEXT("Back"), ItemName, TEXT("BackStatic"));
}

void ARoseCharacter::UpdateBack()
{
	if (!BackItem)
		return;
	const int32* B = Equipped.Find(TEXT("back"));
	const int32 Id = (B && *B >= 0) ? *B : -1;

	UStaticMesh* Mesh = (Id >= 0)
		? LoadBackStatic(FString::Printf(TEXT("back_%d"), Id))
		: nullptr;

	// Dummy 3 IS the back attach point (DUMMY_IDX_BACK, parented to b1_chest).
	//
	// This used to attach to the b1_chest BONE with a hand-tuned BackLoc of
	// (7.42, -17.02, -0.55) — which is simply an eyeballed approximation of the
	// ZMD's dummy 3, measured at (7.0, -12.6, 0.0) female / (8.6, -12.4, 0.0)
	// male.  Use the real attachment instead of a guess at it: ROSE links back
	// items to that dummy exactly as it links weapons to the hand dummies.
	const FName BackSocket = RoseDummySocket(3);
	BackItem->AttachToComponent(GetMesh(),
		FAttachmentTransformRules::KeepRelativeTransform,
		BackSocket.IsNone() ? BackBone : BackSocket);
	BackItem->SetStaticMesh(Mesh);
	BackItem->SetVisibility(Mesh != nullptr);
	ApplyBackPlacement();

	// Refine glow — the same M_RoseChar parameter path as weapons and armour,
	// not the old additive overlay.
	GlowStaticItem(BackItem, TEXT("back"));
}

void ARoseCharacter::ApplyBackPlacement()
{
	if (!BackItem)
		return;

	// On the dummy, the transform is IDENTITY — the socket already carries the
	// attach point and the item's own orientation is baked into the mesh from
	// the ZSC part, exactly as with weapons.  BackLoc/BackRot are the legacy
	// fallback for a skeleton with no dummies, where they still do the work.
	const bool bOnDummy = !RoseDummySocket(3).IsNone();

	BackItem->SetRelativeLocation(bOnDummy ? FVector::ZeroVector : BackLoc);
	BackItem->SetRelativeRotation(bOnDummy ? FRotator::ZeroRotator : BackRot);
	// Plain uniform scale — NO mirror cancel.  Adding AttachedMirrorX() here
	// turned the back item upside down; it inherits the body's transform and
	// renders correctly that way.
	BackItem->SetRelativeScale3D(FVector(BackScale));
}

// Nudge the grip of the hand being tuned (Axis 0=Roll 1=Pitch 2=Yaw), live.

// Nudge the grip location of the hand being tuned (Axis 0=X 1=Y 2=Z), live.

void ARoseCharacter::ResetGrip()
{
	FRotator& R = bTuneLeftHand ? GripRotL : GripRotR;
	FVector&  L = bTuneLeftHand ? GripLocL : GripLocR;
	R = FRotator::ZeroRotator;
	L = FVector::ZeroVector;
	(bTuneLeftHand ? GripScaleL : GripScaleR) = 1.f;
	ApplyWeaponGrips();
}

// Where the tuned grips live: a plain JSON file beside the other generated
// data (item_drops.json, status_effects.json), loaded straight off disk.
//
// NOT SaveConfig().  That was the previous approach and it silently did
// nothing: UE writes config from the class-default object, so calling it on a
// spawned actor stored nothing at all, and every tuning session was lost on the
// next compile while still printing "saved".
//
// A data file also puts the grips where the rest of the game's data lives —
// they are per weapon TYPE, which is table data, not a user preference.
static FString RoseGripDataPath()
{
	return FPaths::ProjectContentDir() / TEXT("DataTables/weapon_grips.json");
}

void ARoseCharacter::SaveGripConfig()
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FRoseWeaponGrip& G : WeaponGrips)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("type"),  G.WeaponType);
		O->SetBoolField  (TEXT("left"),  G.bLeftHand);
		O->SetNumberField(TEXT("loc_x"), G.Loc.X);
		O->SetNumberField(TEXT("loc_y"), G.Loc.Y);
		O->SetNumberField(TEXT("loc_z"), G.Loc.Z);
		O->SetNumberField(TEXT("pitch"), G.Rot.Pitch);
		O->SetNumberField(TEXT("yaw"),   G.Rot.Yaw);
		O->SetNumberField(TEXT("roll"),  G.Rot.Roll);
		O->SetNumberField(TEXT("scale"), G.Scale);
		Rows.Add(MakeShared<FJsonValueObject>(O));
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Rows, W);

	const FString Path = RoseGripDataPath();
	const bool bOk = FFileHelper::SaveStringToFile(Out, *Path);

	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 4.f,
			bOk ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("[Rose] %s %d grip(s) -> %s"),
				bOk ? TEXT("saved") : TEXT("FAILED to save"),
				WeaponGrips.Num(), *Path));
}

void ARoseCharacter::LoadGripData()
{
	WeaponGrips.Reset();

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *RoseGripDataPath()))
		return;   // nothing tuned yet — the shared fallbacks apply

	TArray<TSharedPtr<FJsonValue>> Rows;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Rows))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] weapon_grips.json is malformed"));
		return;
	}

	for (const TSharedPtr<FJsonValue>& V : Rows)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
			continue;
		FRoseWeaponGrip G;
		G.WeaponType = (int32)O->GetNumberField(TEXT("type"));
		G.bLeftHand  = O->GetBoolField(TEXT("left"));
		G.Loc = FVector(O->GetNumberField(TEXT("loc_x")),
		                O->GetNumberField(TEXT("loc_y")),
		                O->GetNumberField(TEXT("loc_z")));
		G.Rot = FRotator(O->GetNumberField(TEXT("pitch")),
		                 O->GetNumberField(TEXT("yaw")),
		                 O->GetNumberField(TEXT("roll")));
		G.Scale = (float)O->GetNumberField(TEXT("scale"));
		WeaponGrips.Add(G);
	}

	UE_LOG(LogTemp, Log, TEXT("[Rose] loaded %d weapon grip(s)"), WeaponGrips.Num());
}

void ARoseCharacter::SetupBlink()
{
	OpenEyeSlot = ClosedEyeSlot = -1;
	OpenEyeMat = ClosedEyeMat = nullptr;

	OpenEyeSection = ClosedEyeSection = -1;

	// Clear any per-index material overrides left from the PREVIOUS merge — after
	// a re-merge the section→material-index mapping changes, so a stale M_Hidden
	// override would hide the wrong section (e.g. hair/face vanishing on equip).
	GetMesh()->EmptyOverrideMaterials();
	// Same reasoning for section visibility: indices shift on a re-merge, so a
	// hide left over from the previous mesh would blank an unrelated section.
	GetMesh()->ShowAllMaterialSections(0);

	USkeletalMesh* SM = GetMesh()->GetSkeletalMeshAsset();
	if (!SM) return;

	// The face imports as distinct material slots named "...eyeopen"/"...eyeclosed".
	const TArray<FSkeletalMaterial>& Mats = SM->GetMaterials();
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		UMaterialInterface* MI = Mats[i].MaterialInterface;
		if (!MI) continue;

		// Match the SLOT name, not the material's.  The importer splits the face
		// mesh's eye alternates into their own sections named "…_eyesopen" /
		// "…_eyesclosed", but all three sections deliberately share ONE material
		// instance (same texture) — so MaterialInterface->GetName() returns the
		// same string for all of them and never matches.  The distinguishing
		// name lives on FSkeletalMaterial::MaterialSlotName.
		const FString N = Mats[i].MaterialSlotName.ToString();
		if (N.Contains(TEXT("eyesopen")))        { OpenEyeSlot = i;   OpenEyeMat = MI; }
		else if (N.Contains(TEXT("eyesclosed"))) { ClosedEyeSlot = i; ClosedEyeMat = MI; }
	}

	// Map each eye material slot to its RENDER SECTION.
	//
	// Blinking used to swap the inactive overlay's material for M_Hidden, and an
	// invisible material still SUBMITS its geometry: the open and closed eyes are
	// alternates occupying the same place on the face, so the hidden one kept
	// occluding the visible one and the eyes clipped through the face.  Hiding
	// the SECTION removes it from the draw entirely, which is what was wanted.
	if (const FSkeletalMeshRenderData* RD = SM->GetResourceForRendering())
	{
		if (RD->LODRenderData.Num() > 0)
		{
			const TArray<FSkelMeshRenderSection>& Sections = RD->LODRenderData[0].RenderSections;
			for (int32 s = 0; s < Sections.Num(); ++s)
			{
				if (Sections[s].MaterialIndex == OpenEyeSlot)   OpenEyeSection = s;
				if (Sections[s].MaterialIndex == ClosedEyeSlot) ClosedEyeSection = s;
			}
		}
	}

	// Start with eyes open: hide the closed-eye overlay.
	SetEyeSectionVisible(/*bOpen*/ true, true);
	SetEyeSectionVisible(/*bOpen*/ false, false);
	bEyesClosed = false;
	BlinkTimer = 0.f;
	// zz_model::update_blink — open for rand()%3000+100 ms, closed for only
	// rand()%100+10 ms.  A long open phase and a brief flick shut.
	NextBlinkAt = FMath::FRandRange(0.1f, 3.1f);
}

/** Show or hide one eye overlay by SECTION, so the inactive one is not drawn at
 *  all.  Falls back to the old material swap only if the section could not be
 *  resolved, so a mesh whose render data is unavailable still blinks. */
void ARoseCharacter::SetEyeSectionVisible(bool bOpenEye, bool bVisible)
{
	const int32 Slot    = bOpenEye ? OpenEyeSlot    : ClosedEyeSlot;
	const int32 Section = bOpenEye ? OpenEyeSection : ClosedEyeSection;
	if (Slot < 0)
		return;

	if (Section >= 0)
	{
		GetMesh()->ShowMaterialSection(Slot, Section, bVisible, 0);
		return;
	}
	if (HiddenMaterial)
	{
		UMaterialInterface* Real = bOpenEye ? OpenEyeMat : ClosedEyeMat;
		GetMesh()->SetMaterial(Slot, bVisible ? Real : HiddenMaterial);
	}
}

void ARoseCharacter::UpdateBlink(float Dt)
{
	// No HiddenMaterial requirement any more — section hiding does not need it.
	if (OpenEyeSlot < 0 || ClosedEyeSlot < 0) return;

	BlinkTimer += Dt;
	if (!bEyesClosed)
	{
		if (BlinkTimer >= NextBlinkAt)   // time to close
		{
			SetEyeSectionVisible(true, false);
			SetEyeSectionVisible(false, true);
			bEyesClosed = true;
			BlinkTimer = 0.f;
		}
	}
	else if (BlinkTimer >= ClosedFor)
	{
		SetEyeSectionVisible(true, true);
		SetEyeSectionVisible(false, false);
		bEyesClosed = false;
		BlinkTimer = 0.f;
		// zz_model::update_blink: open rand()%3000+100 ms, shut rand()%100+10 ms.
		NextBlinkAt = FMath::FRandRange(0.1f, 3.1f);
		ClosedFor   = FMath::FRandRange(0.01f, 0.11f);
	}
}

void ARoseCharacter::EquipItem(const FString& Slot, int32 Id)
{
	// A client asks; the server decides.  (Standalone and the listen host are
	// already the authority, so this is a no-op for single player.)
	if (!HasAuthority())
	{
		if (IsLocallyControlled())
			Server_EquipItem(Slot, Id);
		return;
	}

	// Unequipping a BODY slot falls back to the naked mesh, it does not remove
	// the slot.
	//
	// body/arms/foot each have an "empty" item at id 1 — the bare skin the
	// character is created with (see the defaults in BeginPlay).  Removing the
	// entry instead left those slots with no mesh at all, so taking off armour
	// deleted the torso/hands/feet and the character rendered as a floating
	// head.  Slots with no naked equivalent (cap, back, weapon, faceitem, ...)
	// are genuinely absent when empty and still remove.
	const FString Key = Slot.ToLower();
	if (Id < 0)
	{
		const int32 Naked = NakedDefaultForSlot(Key);
		if (Naked >= 0)
			Equipped.Add(Key, Naked);
		else
			Equipped.Remove(Key);
	}
	else
		Equipped.Add(Key, Id);
	// Refine/bonus belong to the item INSTANCE, not the slot: any id change
	// resets them.  TryEquipFromBag re-applies the incoming instance's right after.
	EquippedRefine.Remove(Slot.ToLower());
	EquippedBonus.Remove(Slot.ToLower());
	EquippedAppraised.Remove(Slot.ToLower());
	UpdateLocoSet();   // a weapon change swaps the stop/walk/run set
	RebuildMesh();
	ApplyDerivedStats();   // boots/weapon changes shift speed + combat stats
	bInventoryDirty = true;   // refresh the equip-slot icons (deferred to Tick —
	                          // the click that got us here lives in that widget)
	// Republish the look to every client.  Deferred to Tick so the refine/bonus
	// that TryEquipFromBag applies right after this call is included in the same
	// update instead of being sent as a second one.
	MarkAppearanceDirty();
}

void ARoseCharacter::CycleBody()
{
	BodyTestId = BodyTestId >= 4 ? 1 : BodyTestId + 1;
	EquipItem(TEXT("body"), BodyTestId);
}

void ARoseCharacter::CycleCap()
{
	CapTestId = CapTestId >= 3 ? 0 : CapTestId + 1;
	EquipItem(TEXT("cap"), CapTestId <= 1 ? -1 : CapTestId); // 0/1 = none
}

// Feed the material's chest-bulge region from the live skeleton.
//
// The offset is done in the MATERIAL (M_RoseChar, MP_WorldPositionOffset)
// because the two obvious tools are both unavailable here:
//   * morph targets — the character is one mesh from
//     USkeletalMergingLibrary::MergeMeshes, and the merge does not carry morph
//     targets across at all, so a morph authored on the body part never reaches
//     the screen;
//   * bone scaling — FEMALE.ZMD has no chest-specific bone; b1_chest parents the
//     neck and both clavicles, so scaling it takes the arms with it.
//
// WPO runs AFTER skinning, so the region has to be told where the chest went
// this frame — otherwise it stays put in space and the body animates through
// it.  Pushing the bone's world position each frame is what makes it follow.
void ARoseCharacter::UpdateBodyShape()
{
	USkeletalMeshComponent* M = GetMesh();
	if (!M || BodyChestBulge <= 0.f)
		return;

	static const FName ChestBone(TEXT("b1_chest"));
	if (M->GetBoneIndex(ChestBone) == INDEX_NONE)
		return;   // rig without the bone — leave the body alone

	const FVector Chest = M->GetBoneLocation(ChestBone, EBoneSpaces::WorldSpace);
	M->SetVectorParameterValueOnMaterials(TEXT("ChestCenter"), FVector(Chest));
	M->SetScalarParameterValueOnMaterials(TEXT("ChestBulge"), BodyChestBulge);
	M->SetScalarParameterValueOnMaterials(TEXT("ChestRadius"), BodyChestRadius);
}

void ARoseCharacter::RoseBodyShape(float Bulge, float Radius)
{
	if (Radius > 0.f)
		BodyChestRadius = Radius;
	SetBodyChestBulge(Bulge);

	// Report what actually happened.  Without this, "no visible change" could
	// equally mean the bone was missing, the material has no such parameter, or
	// the offset simply is not connected — and they need different fixes.
	USkeletalMeshComponent* M = GetMesh();
	const bool bHasBone = M && M->GetBoneIndex(FName(TEXT("b1_chest"))) != INDEX_NONE;
	const FVector Chest = bHasBone
		? M->GetBoneLocation(FName(TEXT("b1_chest")), EBoneSpaces::WorldSpace)
		: FVector::ZeroVector;

	const FString Msg = bHasBone
		? FString::Printf(TEXT("[Rose] body: bulge=%.1f radius=%.1f  b1_chest=(%.0f %.0f %.0f)"),
			BodyChestBulge, BodyChestRadius, Chest.X, Chest.Y, Chest.Z)
		: FString::Printf(TEXT("[Rose] body: bulge=%.1f but b1_chest NOT FOUND on this mesh"),
			BodyChestBulge);

	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 5.f,
			bHasBone ? FColor::Green : FColor::Red, Msg);
}

void ARoseCharacter::SetBodyChestBulge(float Amount)
{
	BodyChestBulge = FMath::Clamp(Amount, 0.f, 12.f);
	// Push it straight away so a slider drags live instead of waiting a frame.
	UpdateBodyShape();
	if (BodyChestBulge <= 0.f)
		if (USkeletalMeshComponent* M = GetMesh())
			M->SetScalarParameterValueOnMaterials(TEXT("ChestBulge"), 0.f);
}

void ARoseCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Three roles share this function (see the note in RoseCharacter.h):
	//   bServer — owns state; runs the simulation
	//   bLocal  — this machine's pawn; owns input, camera and HUD
	//   neither — another player's proxy: replicated transform + locomotion anim
	const bool bServer = HasAuthority();
	const bool bLocal  = IsLocallyControlled();
	const bool bRender = !IsDedicatedServerRole();

	if (bRender)
		UpdateBodyShape();

	// Coalesced appearance publish: many things touch the loadout in one frame
	// (equip → refine → bonus), so send the result once, at the end.
	if (bServer && bAppearanceDirty)
	{
		bAppearanceDirty = false;
		PushAppearance();
	}

	if (bServer || bLocal)
	{
		TickRide(DeltaSeconds);   // fuel drain / auto-dismount while mounted
		TickPotionRegens(DeltaSeconds);   // over-time HP/MP potions
	}

	// Orbit camera (world-space, independent of character facing) + smooth zoom.
	// Only the local pawn has a camera worth driving.
	if (bLocal && CameraBoom)
	{
		CameraBoom->SetWorldRotation(FRotator(CamPitch, CamYaw, 0.f));
		CameraBoom->TargetArmLength =
			FMath::FInterpTo(CameraBoom->TargetArmLength, ZoomTarget, DeltaSeconds, 10.f);
	}

	// ROSE cursor feedback (CCursor::SetCursorType): the classic .cur shapes
	// registered by the UI manager — attack over a live mob, talk over an NPC,
	// pickup over ground loot, default elsewhere.
	if (APlayerController* CursorPC = Cast<APlayerController>(GetController());
		CursorPC && IsLocallyControlled())
	{
		EMouseCursor::Type Want = EMouseCursor::Default;
		FHitResult Hover;
		CursorPC->GetHitResultUnderCursor(ECC_Pawn, false, Hover);
		if (Cast<ARoseNpc>(Hover.GetActor()))
			Want = EMouseCursor::Hand;                       // CURSOR_NPC
		else if (ARoseMonster* HoverMob = Cast<ARoseMonster>(Hover.GetActor()))
			Want = HoverMob->IsDead() ? EMouseCursor::Default
			                          : EMouseCursor::Crosshairs;   // CURSOR_ATTACK
		else if (Cast<ARoseGroundItem>(Hover.GetActor()))
			Want = EMouseCursor::GrabHand;                   // CURSOR_ITEM_PICK
		CursorPC->CurrentMouseCursor = Want;
	}

	// An attack plays once and suppresses locomotion until it finishes.
	if (bAttacking)
	{
		AttackTimer -= DeltaSeconds;
		// ai dog shit
		//if (AttackTimer <= 0.f)
		//{
		//	bAttacking = false;
		//	CurrentLoco = nullptr;   // resume loco below / next frame
		//}

		if (AttackTimer <= 0.f) {
            bAttacking = false;

            if (bAttackQueued) {
                bAttackQueued = false;

                ComboIdx = (ComboIdx + 1) % AttackAnims.Num();

                UAnimSequence* NextAnim = AttackAnims[ComboIdx];

                if (NextAnim) {
                    GetMesh()->PlayAnimation(NextAnim, false);

                    const float Rate = FMath::Clamp(Derived.AttackSpeed / 100.f, 0.4f, 2.5f);
                    GetMesh()->SetPlayRate(Rate);

                    // Every swing announces (client action frame 31), not just the first.
                    {
                        int32 StartSnd = ROSE_BAREHAND_ATK_START_SOUND;
                        if (const int32* W = Equipped.Find(TEXT("weapon")))
                            if (*W >= 0)
                                if (const FRoseWeaponRow* R = GetWeaponRow(*W))
                                    StartSnd = R->AtkStartSound;
                        RosePlaySoundId(GetWorld(), StartSnd, GetActorLocation());
                    }

                    bAttacking = true;
                    AttackTimer = NextAnim->GetPlayLength() / Rate;

                    bHitThisSwing = false;   // new combo swing
                    DoMeleeHit();            // reliable hit (guarded against double)
                }
            } else {
                ComboIdx = 0; // reset combo
                CurrentLoco = nullptr;
            }
        }
	}

	// Click-to-attack: pursue/face/swing at the current target.  Runs after the
	// attack-state update so a finished swing chains without a gap, and before
	// locomotion so pursuit movement picks the run anim this frame.  A pending
	// skill approach takes precedence over the basic-attack pursuit.
	// Only the player driving this pawn pursues and swings.  (Damage is still
	// applied locally — making combat server-authoritative is the next step.)
	if (bLocal)
	{
		TickSkillApproach(DeltaSeconds);
		TickTalkApproach(DeltaSeconds);
		// Cheap: only NPCs not already ignored are touched, and a 2s sweep picks
		// up any that streamed in since.
		if (GetWorld() && GetWorld()->GetTimeSeconds() >= NextNpcIgnoreSweep)
		{
			NextNpcIgnoreSweep = GetWorld()->GetTimeSeconds() + 2.0;
			RefreshNpcMoveIgnores();
		}
		TickTalkRange(DeltaSeconds);
		TickPickUpApproach(DeltaSeconds);
		// Zone music: cheap when unchanged (it compares the zone key and returns),
		// and this way a warp swaps the track without needing a level-load hook.
		UpdateZoneBgm();
		if (PendingCastSkill <= 0)
			TickAutoAttack(DeltaSeconds);
	}

	// On-foot locomotion — NEVER while riding: TickRide owns both halves of the
	// ride animation (vehicle clip + rider seat clip), and this block re-playing
	// walk/run every tick was exactly the "character walks inside the castle
	// gear" bug (it stomped the ride anim one frame after SetRideAnim set it).
	// Runs for EVERY role that renders, including other players' proxies: their
	// velocity is replicated, so idle/walk/run picks itself with no extra data
	// on the wire.
	if (bRender && !bAttacking && !IsRiding())
	{
		const float Speed = GetVelocity().Size2D();
		UAnimSequence* Want = IdleAnim;
		if (Speed > 220.f)      Want = RunAnim;
		else if (Speed > 10.f)  Want = WalkAnim;

		if (Want && Want != CurrentLoco)
			PlayLoco(Want);
	}

	// Deferred inventory refresh after an equip change (safe here — the click
	// handler that triggered it has fully unwound).
	if (bLocal && bInventoryDirty)
	{
		bInventoryDirty = false;
		if (bInventoryVisible && InventoryWindow.IsValid())
		{
			const FVector2D Pos = InventoryWindow->Position;
			ToggleInventory();               // off (drops old brushes)
			ToggleInventory();               // on (fresh icons)
			if (InventoryWindow.IsValid())
				InventoryWindow->Position = Pos;
		}
	}

	if (bRender)
		UpdateBlink(DeltaSeconds);
	if (bLocal)
		ShowBrowserHUD();
}

void ARoseCharacter::PlayLoco(UAnimSequence* Anim)
{
	if (!Anim) return;
	CurrentLoco = Anim;
	GetMesh()->PlayAnimation(Anim, true);
	// Run tracks the effective speed (Cal_RunAniSPEED); idle/walk at 1x.
	GetMesh()->SetPlayRate(Anim == RunAnim
		? RoseStats::RunAnimRate(Derived.RunSpeed * SpeedMultiplier) : 1.f);
}

void ARoseCharacter::MoveForward(float Value)
{
	if (Value != 0.f)
	{
		// Manual movement overrides the auto-attack pursuit and any pending
		// skill approach (the target stays selected — click to re-engage).
		bAutoAttack = false;
		bAttackQueued = false;
		PendingCastSkill = -1;
		// Move relative to the camera's yaw (ROSE: forward = into the screen)
		const FRotator Yaw(0.f, CamYaw, 0.f);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Value);
	}
}

void ARoseCharacter::MoveRight(float Value)
{
	if (Value != 0.f)
	{
		bAutoAttack = false;
		bAttackQueued = false;
		PendingCastSkill = -1;
		const FRotator Yaw(0.f, CamYaw, 0.f);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Value);
	}
}

// Mouse only orbits the camera while the right button is held (ROSE-style).
void ARoseCharacter::TurnInput(float Value)
{
	if (bRotatingCamera) { CamYaw += Value; }
}

void ARoseCharacter::LookUpInput(float Value)
{
	if (bRotatingCamera) { CamPitch = FMath::Clamp(CamPitch + Value, -80.f, -5.f); }
}

void ARoseCharacter::OnRotateCameraPressed()  { bRotatingCamera = true; }
void ARoseCharacter::OnRotateCameraReleased() { bRotatingCamera = false; }

void ARoseCharacter::ZoomCamera(float Value)
{
	if (Value != 0.f)
		ZoomTarget = FMath::Clamp(ZoomTarget - Value * 60.f, 150.f, 1200.f);
}

void ARoseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("MoveForward", this, &ARoseCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ARoseCharacter::MoveRight);
	PlayerInputComponent->BindAxis("Turn", this, &ARoseCharacter::TurnInput);
	PlayerInputComponent->BindAxis("LookUp", this, &ARoseCharacter::LookUpInput);
	PlayerInputComponent->BindAxis("Zoom", this, &ARoseCharacter::ZoomCamera);

	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ARoseCharacter::OnRotateCameraPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ARoseCharacter::OnRotateCameraReleased);

	// Left mouse = click-to-attack (ROSE point-and-click): click a monster to
	// target it and auto-attack; click empty ground to deselect.
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ARoseCharacter::OnLeftClick);

	// Skill quickbar: keys 1-9, 0, -, = cast hotbar slots 1-12 (assign by
	// drag-drop from the skill window or `RoseHotbar <slot> <id>`).
	PlayerInputComponent->BindKey(EKeys::One,    IE_Pressed, this, &ARoseCharacter::CastHotbar1);
	PlayerInputComponent->BindKey(EKeys::Two,    IE_Pressed, this, &ARoseCharacter::CastHotbar2);
	PlayerInputComponent->BindKey(EKeys::Three,  IE_Pressed, this, &ARoseCharacter::CastHotbar3);
	PlayerInputComponent->BindKey(EKeys::Four,   IE_Pressed, this, &ARoseCharacter::CastHotbar4);
	PlayerInputComponent->BindKey(EKeys::Five,   IE_Pressed, this, &ARoseCharacter::CastHotbar5);
	PlayerInputComponent->BindKey(EKeys::Six,    IE_Pressed, this, &ARoseCharacter::CastHotbar6);
	PlayerInputComponent->BindKey(EKeys::Seven,  IE_Pressed, this, &ARoseCharacter::CastHotbar7);
	PlayerInputComponent->BindKey(EKeys::Eight,  IE_Pressed, this, &ARoseCharacter::CastHotbar8);
	PlayerInputComponent->BindKey(EKeys::Nine,   IE_Pressed, this, &ARoseCharacter::CastHotbar9);
	PlayerInputComponent->BindKey(EKeys::Zero,   IE_Pressed, this, &ARoseCharacter::CastHotbar10);
	PlayerInputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &ARoseCharacter::CastHotbar11);
	PlayerInputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &ARoseCharacter::CastHotbar12);

	// Bar 2 = the SAME keys with ALT held.  BindKey's member-function overload
	// cannot carry the slot index, so each chord gets a lambda through a
	// manually-built FInputKeyBinding — the supported way to bind a lambda to a
	// key.  Alt+<key> can never collide with bar 1's bare <key>.
	{
		static const FKey kBarKeys[URoseSkillComponent::SlotsPerBar] = {
			EKeys::One,  EKeys::Two,  EKeys::Three,  EKeys::Four,
			EKeys::Five, EKeys::Six,  EKeys::Seven,  EKeys::Eight,
			EKeys::Nine, EKeys::Zero, EKeys::Hyphen, EKeys::Equals };

		for (int32 i = 0; i < URoseSkillComponent::SlotsPerBar; ++i)
		{
			const int32 Slot = URoseSkillComponent::SlotsPerBar + i;   // bar 2
			FInputKeyBinding Binding(
				FInputChord(kBarKeys[i], /*bShift*/ false, /*bCtrl*/ false,
				            /*bAlt*/ true, /*bCmd*/ false),
				IE_Pressed);
			Binding.KeyDelegate.GetDelegateForManualSet().BindLambda(
				[this, Slot]() { if (Skills) Skills->CastHotbar(Slot); });
			PlayerInputComponent->KeyBindings.Emplace(MoveTemp(Binding));
		}
	}

	// F9 = stats panel (speed multiplier + base-stat sliders, derived readout).
	// (Moved off `5`, which now casts hotbar slot 5.)
	PlayerInputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ARoseCharacter::ToggleStatsPanel);
	PlayerInputComponent->BindKey(EKeys::F12, IE_Pressed, this, &ARoseCharacter::ToggleBackTunePanel);
	PlayerInputComponent->BindKey(EKeys::F10, IE_Pressed, this, &ARoseCharacter::ToggleDevSpawn);
	// F9 = weapon grip tuner (sliders + numeric entry, live on the held weapon).
	PlayerInputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ARoseCharacter::ToggleGripTuner);
	PlayerInputComponent->BindKey(EKeys::F7, IE_Pressed, this, &ARoseCharacter::ToggleRide);

	// C = ROSE character sheet (dlgavata: stats + the + buttons).  The old
	// C=CycleCap test is superseded by the browser + the inventory slots.
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &ARoseCharacter::ToggleCharacterSheet);
	// Test equipment cycling: B = next body armor.
	PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &ARoseCharacter::CycleBody);

	// Catalog browser
	PlayerInputComponent->BindKey(EKeys::Up,    IE_Pressed, this, &ARoseCharacter::BrowseSlotPrev);
	PlayerInputComponent->BindKey(EKeys::Down,  IE_Pressed, this, &ARoseCharacter::BrowseSlotNext);
	PlayerInputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ARoseCharacter::BrowseIdNext);
	PlayerInputComponent->BindKey(EKeys::Left,  IE_Pressed, this, &ARoseCharacter::BrowseIdPrev);
	PlayerInputComponent->BindKey(EKeys::Delete, IE_Pressed, this, &ARoseCharacter::UnequipActive);

	// I = ROSE inventory window (Arua DlgBag skin).
	PlayerInputComponent->BindKey(EKeys::I, IE_Pressed, this, &ARoseCharacter::ToggleInventory);

	// K = modern skills window, X = modern skill tree.  Bound directly here (like
	// C) because they are viewport overlays, not registered dialog defs — the
	// manager's key registry path did not fire for the K binding.
	PlayerInputComponent->BindKey(EKeys::K, IE_Pressed, this, &ARoseCharacter::ToggleSkills);
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &ARoseCharacter::ToggleSkillTree);

	// Enter = focus the always-on modern chat input (fires only when chat isn't
	// already focused — a focused editbox consumes Enter to commit).
	PlayerInputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ARoseCharacter::FocusChat);
	// M = toggle the modern minimap, Z = modern options/system window,
	// Q = modern quest journal.
	PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &ARoseCharacter::ToggleMinimapKey);
	PlayerInputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ARoseCharacter::ToggleOptionsKey);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ARoseCharacter::ToggleQuestKey);
	// Space = JUMP.  Pressed/Released, not just Pressed: releasing early is what
	// gives the variable jump height JumpMaxHoldTime enables, and without the
	// Released binding every jump is the full-height one.
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed,  this, &ACharacter::Jump);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ACharacter::StopJumping);

	// E = pick up the nearest dropped item (ROSE loot is manual).  Moved off
	// Space, which now jumps; Z was already the options window.
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ARoseCharacter::PickUpNearest);
	// G = sit / stand (ROSE's own pose, skill 11).  C is the character sheet;
	// G is one of the keys freed by deleting the weapon-grip nudge binds.
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &ARoseCharacter::ToggleSit);

	// Weapon-grip tuning has NO key bindings.
	//
	// It used to own fourteen letter keys for 1cm/1degree nudges (F/H/G/V/R/N,
	// Home/End, J/L, U/O, T, Y, P).  The F9 tuner replaced that workflow entirely
	// — sliders with numeric entry, per weapon type, persisted to
	// weapon_grips.json — so the keys were a dev tool holding gameplay letters
	// hostage.  F in particular is the conventional interact key.

	// Classic-UI windows (HUD/minimap/skills/chat/misc) bind their own toggle
	// keys through the manager's registry.
	if (UI)
		UI->SetupInput(PlayerInputComponent);
}

// `RoseUI <dialog>` — toggle any converted layout (Content/UI/Layouts) as a
// bare draggable window; registered windows keep their wired content.
void ARoseCharacter::RoseUI(const FString& Dialog)
{
	if (UI)
		UI->ToggleGeneric(Dialog);
}


void ARoseCharacter::AnimNotify_AttackHit() {
    DoMeleeHit();
}

// ── Job + skills (URoseSkillComponent glue) ───────────────────────────────────

int32 ARoseCharacter::GetEquippedWeaponType() const
{
	if (const int32* W = Equipped.Find(TEXT("weapon")))
		if (*W >= 0)
			if (const FRoseWeaponRow* R = GetWeaponRow(*W))
				return R->Type;
	return 0;   // bare hands
}

// Skill cast motion: reuses the attack machinery (single-node PlayAnimation —
// no AnimInstance, so no montages; loco resumes when AttackTimer runs out in
// Tick).  SpeedPct = SKILL_ANI_ACTION_SPEED (percent, io_skill.h:169).
UAnimSequence* ARoseCharacter::LoadSkillAnim(int32 ActionRow) const
{
	if (ActionRow <= 0)
		return nullptr;
	const bool bF = Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase);
	const FString Root = FString::Printf(TEXT("/Game/Characters/Modular/%s"), *Gender);
	// The weapon Motion Type selects the column; fall back to bare (0) like loco.
	for (int32 M : { WeaponMotionType, 0 })
	{
		const TCHAR* Name = RoseSkillActionAnim(M, ActionRow, bF);
		if (Name && *Name)
			if (UAnimSequence* A = LoadAnim(Root, GenderKey(), Name, /*bNative*/ true))
				return A;
	}
	return nullptr;
}

float ARoseCharacter::PlaySkillMotion(int32 ActionMotion, int32 CastMotion, float SpeedPct)
{
	// The skill's own release motion, then its casting pose, then the basic
	// swing so a cast is never animation-less (RoseSkillMotionData.h).
	UAnimSequence* Anim = LoadSkillAnim(ActionMotion);
	if (!Anim)
		Anim = LoadSkillAnim(CastMotion);
	if (!Anim)
		Anim = AttackAnims.Num() > 0 ? AttackAnims[ComboIdx % AttackAnims.Num()] : nullptr;
	if (!Anim)
		return 0.f;
	GetMesh()->Stop();
	GetMesh()->PlayAnimation(Anim, false);
	const float Rate = FMath::Clamp(SpeedPct / 100.f, 0.4f, 2.5f);
	GetMesh()->SetPlayRate(Rate);
	bAttacking = true;
	bHitThisSwing = true;   // the skill applies the damage — mute the basic hit
	AttackTimer = Anim->GetPlayLength() / Rate;
	CurrentLoco = nullptr;
	return AttackTimer;
}

void ARoseCharacter::CastHotbar1() { if (Skills) Skills->CastHotbar(0); }
void ARoseCharacter::CastHotbar2() { if (Skills) Skills->CastHotbar(1); }
void ARoseCharacter::CastHotbar3() { if (Skills) Skills->CastHotbar(2); }
void ARoseCharacter::CastHotbar4() { if (Skills) Skills->CastHotbar(3); }
void ARoseCharacter::CastHotbar5() { if (Skills) Skills->CastHotbar(4); }
void ARoseCharacter::CastHotbar6() { if (Skills) Skills->CastHotbar(5); }
void ARoseCharacter::CastHotbar7() { if (Skills) Skills->CastHotbar(6); }
void ARoseCharacter::CastHotbar8() { if (Skills) Skills->CastHotbar(7); }
void ARoseCharacter::CastHotbar9() { if (Skills) Skills->CastHotbar(8); }
void ARoseCharacter::CastHotbar10() { if (Skills) Skills->CastHotbar(9); }
void ARoseCharacter::CastHotbar11() { if (Skills) Skills->CastHotbar(10); }
void ARoseCharacter::CastHotbar12() { if (Skills) Skills->CastHotbar(11); }

void ARoseCharacter::RoseSetJob(int32 JobId)       { if (Skills) Skills->SetJob(JobId); }
void ARoseCharacter::RoseLearnSkill(int32 SkillId) { if (Skills) Skills->LearnSkill(SkillId); }
void ARoseCharacter::RoseAddSkillPoints(int32 Points)
{
	if (Skills)
	{
		Skills->SkillPoints += Points;
		if (GEngine)
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow,
				FString::Printf(TEXT("skill points: %d"), Skills->SkillPoints));
	}
}
void ARoseCharacter::RoseHotbar(int32 Slot, int32 SkillId)
{
	if (Skills) Skills->SetHotbarSlot(Slot - 1, SkillId);   // user-facing slots are 1-4
}
void ARoseCharacter::RoseSkillInfo()
{
	if (!Skills) return;
	const FString S = Skills->DescribeState();
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, S);
	UE_LOG(LogTemp, Log, TEXT("[Rose] skill state:\n%s"), *S);
}

void ARoseCharacter::RoseQuestTrigger(const FString& Name)
{
	if (!Quests) return;
	const ERoseQuestResult R = Quests->CheckQuestTrigger(Name, /*bDoReward=*/true);
	FRoseChatLog::Add(FRoseChatLog::EKind::System,
		FString::Printf(TEXT("Trigger %s: %s"), *Name,
			R == ERoseQuestResult::Success ? TEXT("SUCCESS") : TEXT("failed")));
}

void ARoseCharacter::RoseQuestInfo()
{
	if (!Quests) return;
	const FString S = Quests->DescribeState();
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, S);
	UE_LOG(LogTemp, Log, TEXT("[Rose] quest state:\n%s"), *S);
}

// ── Stats debug panel (F2): sliders for speed + base stats, live readout ─────────

TSharedRef<SWidget> ARoseCharacter::BuildStatsPanel()
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	// One labelled slider row; Get/Set close over the backing field, every change
	// re-runs ApplyDerivedStats so movement/combat react immediately.
	auto AddSlider = [this, &Rows](const FString& Label, float Min, float Max, int32 Decimals,
	                               TFunction<float()> Get, TFunction<void(float)> Set)
	{
		Rows->AddSlot().AutoHeight().Padding(6.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Label, Get, Decimals] {
					return FText::FromString(FString::Printf(TEXT("%s  %.*f"), *Label, Decimals, Get()));
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Get, Min, Max] { return (Get() - Min) / (Max - Min); })
				.OnValueChanged_Lambda([this, Set, Min, Max](float V) {
					Set(Min + V * (Max - Min));
					ApplyDerivedStats();
				})
			]
		];
	};

	Rows->AddSlot().AutoHeight().Padding(6.f, 4.f)
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("ROSE STATS  (F2 to close)")))
	];

	AddSlider(TEXT("Speed x"), 0.1f, 5.f, 2,
		[this] { return SpeedMultiplier; }, [this](float V) { SpeedMultiplier = V; });
	AddSlider(TEXT("Level"), 1.f, 250.f, 0,
		[this] { return (float)Level; }, [this](float V) { Level = (int32)V; });
	AddSlider(TEXT("STR"), 1.f, 400.f, 0,
		[this] { return (float)Strength; }, [this](float V) { Strength = (int32)V; });
	AddSlider(TEXT("DEX"), 1.f, 400.f, 0,
		[this] { return (float)Dexterity; }, [this](float V) { Dexterity = (int32)V; });
	AddSlider(TEXT("INT"), 1.f, 400.f, 0,
		[this] { return (float)Intelligence; }, [this](float V) { Intelligence = (int32)V; });
	AddSlider(TEXT("CON"), 1.f, 400.f, 0,
		[this] { return (float)Concentration; }, [this](float V) { Concentration = (int32)V; });
	AddSlider(TEXT("SEN"), 1.f, 400.f, 0,
		[this] { return (float)Sense; }, [this](float V) { Sense = (int32)V; });
	// Refine grade per slot (LIST_GRADE.STB).  Stats come via ApplyDerivedStats,
	// which AddSlider calls after every set.
	//
	// Every refinable slot gets its own slider, not just the weapon: armour
	// refine feeds Def/Res/Avoid through ComputeLoadoutStats exactly as the
	// weapon's feeds Atk/Hit, and there was no way to exercise it.
	//
	// "back" is included because it is refinable in ROSE even though it is
	// socket-attached rather than merged.  "subwpn" likewise — a shield's refine
	// is armour-side (Def/Res), which is why it is not special-cased here.
	{
		// "faceitem" is the mask slot — refinable like the rest, and merged into
		// the body rather than socket-attached, so it needs the merged path.
		// Global glow strength, above the per-slot grades.  The grade's own RGB
		// carries the per-grade amount (LIST_GRADE scales it), so this is one
		// scale over all of them — the knob to reach for when refined gear reads
		// too hot or too dim overall.
		AddSlider(TEXT("Glow x"), 0.f, 10.f, 2,
			[this] { return RefineGlowScale; },
			[this](float V) {
				RefineGlowScale = FMath::Max(0.f, V);
				ApplyRefineGlow();
				RefreshMergedRefineGlow();
			});

		static const TCHAR* const kRefinableSlots[] = {
			TEXT("weapon"), TEXT("subwpn"), TEXT("body"),
			TEXT("arms"),   TEXT("foot"),   TEXT("cap"), TEXT("back"),
			TEXT("faceitem")
		};

		for (const TCHAR* Slot : kRefinableSlots)
		{
			const FString SlotName(Slot);
			// Capitalised label: "Weapon +N", "Body +N", ...
			FString Label = SlotName;
			Label[0] = FChar::ToUpper(Label[0]);
			Label += TEXT(" +N");

			AddSlider(Label, 0.f, 9.f, 0,
				[this, SlotName] { return (float)GetEquippedRefine(SlotName); },
				[this, SlotName](float V) {
					EquippedRefine.Add(SlotName,
						FMath::Clamp((int32)FMath::RoundToInt(V), 0, RefineGrades.Num()));
					// Both halves update live now.
					//
					// ApplyRefineGlow does the socket-attached items (weapon,
					// sub-weapon, back) via their overlay material;
					// RefreshMergedRefineGlow does the merged armour by setting
					// RefineColor/RefineIntensity on the already-merged material
					// slots.  Armour used to wait for the next equip because the
					// glow was baked during the merge and re-merging on every
					// slider tick would be unusable — driving material parameters
					// costs nothing, so that restriction is gone.
					ApplyRefineGlow();
					RefreshMergedRefineGlow();
				});
		}
	}

	// Live derived-stat readout — the formulas' output for the current sliders.
	Rows->AddSlot().AutoHeight().Padding(6.f, 6.f)
	[
		SNew(STextBlock)
		.Text_Lambda([this] {
			return FText::FromString(FString::Printf(
				TEXT("run %.0f cm/s  (x%.2f = %.0f)\navoid %d   crit %d   hit %d\ndef %d   res %d   atk %d   aspd %d\nHP %d   MP %d   weight %d"),
				Derived.RunSpeed, SpeedMultiplier, Derived.RunSpeed * SpeedMultiplier,
				Derived.Avoid, Derived.Crit, Derived.Hit,
				Derived.Defense, Derived.Resist, Derived.AttackPower, Derived.AttackSpeed,
				Derived.MaxHP, Derived.MaxMP, Derived.MaxWeight));
		})
	];

	return SNew(SBox).WidthOverride(400.f)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			Rows
		]
	];
}

void ARoseCharacter::ToggleStatsPanel()
{
	if (!GEngine || !GEngine->GameViewport) return;

	if (bStatsPanelVisible)
	{
		if (StatsPanel.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(StatsPanel.ToSharedRef());
		StatsPanel.Reset();
		bStatsPanelVisible = false;
		UpdateUIInputMode();   // keep the cursor if the inventory is still open
		return;
	}

	StatsPanel = SNew(SBox)
		.HAlign(HAlign_Right).VAlign(VAlign_Top)
		.Padding(FMargin(0.f, 40.f, 20.f, 0.f))
		[
			BuildStatsPanel()
		];
	GEngine->GameViewport->AddViewportWidgetContent(StatsPanel.ToSharedRef(), 10);
	bStatsPanelVisible = true;
	UpdateUIInputMode();   // sliders need the cursor
}

// ── Back-placement tuning panel (F8): drag sliders, wing moves live ──────────────

TSharedRef<SWidget> ARoseCharacter::BuildBackTunePanel()
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	// Each row: label+value on the left, slider on the right; every change re-applies
	// the placement to the live BackItem so the wing moves as you drag.
	auto AddSlider = [this, &Rows](const FString& Label, float Min, float Max, int32 Decimals,
	                               TFunction<float()> Get, TFunction<void(float)> Set)
	{
		Rows->AddSlot().AutoHeight().Padding(6.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Label, Get, Decimals] {
					return FText::FromString(FString::Printf(TEXT("%s  %.*f"), *Label, Decimals, Get()));
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Get, Min, Max] { return (Get() - Min) / (Max - Min); })
				.OnValueChanged_Lambda([this, Set, Min, Max](float V) {
					Set(Min + V * (Max - Min));
					ApplyBackPlacement();
				})
			]
		];
	};

	Rows->AddSlot().AutoHeight().Padding(6.f, 4.f)
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("BACK PLACEMENT  (F8 to close)")))
	];

	AddSlider(TEXT("Loc X"), -60.f, 60.f, 2, [this]{ return BackLoc.X; }, [this](float V){ BackLoc.X = V; });
	AddSlider(TEXT("Loc Y"), -60.f, 60.f, 2, [this]{ return BackLoc.Y; }, [this](float V){ BackLoc.Y = V; });
	AddSlider(TEXT("Loc Z"), -60.f, 60.f, 2, [this]{ return BackLoc.Z; }, [this](float V){ BackLoc.Z = V; });
	AddSlider(TEXT("Rot Pitch"), -180.f, 180.f, 1, [this]{ return BackRot.Pitch; }, [this](float V){ BackRot.Pitch = V; });
	AddSlider(TEXT("Rot Yaw"),   -180.f, 180.f, 1, [this]{ return BackRot.Yaw; },   [this](float V){ BackRot.Yaw = V; });
	AddSlider(TEXT("Rot Roll"),  -180.f, 180.f, 1, [this]{ return BackRot.Roll; },  [this](float V){ BackRot.Roll = V; });
	AddSlider(TEXT("Scale"), 0.1f, 3.f, 2, [this]{ return BackScale; }, [this](float V){ BackScale = V; });

	// Exact numbers to paste into the BackLoc/BackRot defaults.
	Rows->AddSlot().AutoHeight().Padding(6.f, 6.f)
	[
		SNew(STextBlock)
		.Text_Lambda([this] {
			return FText::FromString(FString::Printf(
				TEXT("BackLoc = (%.2f, %.2f, %.2f)\nBackRot = (P %.1f, Y %.1f, R %.1f)\nBackScale = %.2f"),
				BackLoc.X, BackLoc.Y, BackLoc.Z,
				BackRot.Pitch, BackRot.Yaw, BackRot.Roll, BackScale));
		})
	];

	// Save button: writes these to DefaultGame.ini (config) AND logs them.
	Rows->AddSlot().AutoHeight().Padding(6.f, 4.f)
	[
		SNew(SButton)
		.Text(FText::FromString(TEXT("Save to config + log")))
		.OnClicked_Lambda([this]() -> FReply {
			SaveConfig();
			UE_LOG(LogTemp, Warning,
				TEXT("[BackTune] BackLoc=(%.3f,%.3f,%.3f) BackRot=(%.2f,%.2f,%.2f) BackScale=%.3f"),
				BackLoc.X, BackLoc.Y, BackLoc.Z, BackRot.Pitch, BackRot.Yaw, BackRot.Roll, BackScale);
			return FReply::Handled();
		})
	];

	return SNew(SBox).WidthOverride(420.f)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			Rows
		]
	];
}

void ARoseCharacter::ToggleBackTunePanel()
{
	if (!GEngine || !GEngine->GameViewport) return;

	if (bBackTuneVisible)
	{
		if (BackTunePanel.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(BackTunePanel.ToSharedRef());
		BackTunePanel.Reset();
		bBackTuneVisible = false;
		UpdateUIInputMode();
		return;
	}

	BackTunePanel = SNew(SBox)
		.HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(FMargin(20.f, 40.f, 0.f, 0.f))
		[
			BuildBackTunePanel()
		];
	GEngine->GameViewport->AddViewportWidgetContent(BackTunePanel.ToSharedRef(), 10);
	bBackTuneVisible = true;
	UpdateUIInputMode();
}

// ── ROSE inventory window (I): DlgBag frame + the equipped loadout ─────────────

void ARoseCharacter::UpdateUIInputMode()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
		return;
	// ROSE is a point-and-click MMO — once the HUD registers, the cursor stays
	// on (UI->WantsCursor()); camera drag already hides nothing (RMB capture
	// keeps the cursor visible).  Without the HUD the old per-window rule holds.
	if (bStatsPanelVisible || bBackTuneVisible || bInventoryVisible || bSheetVisible
		|| (UI && UI->WantsCursor()))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ARoseCharacter::CycleSlotItem(const FString& Slot, int32 Dir)
{
	const TArray<int32>* Ids = SlotIds.Find(Slot);
	if (!Ids || Ids->Num() == 0)
		return;
	const int32* Cur = Equipped.Find(Slot);
	int32 Idx = (Cur && *Cur >= 0) ? Ids->IndexOfByKey(*Cur) : INDEX_NONE;
	Idx = (Idx == INDEX_NONE) ? (Dir > 0 ? 0 : Ids->Num() - 1)
	                          : (Idx + Dir + Ids->Num()) % Ids->Num();
	SetCursorToId(Slot, (*Ids)[Idx]);
	EquipItem(Slot, (*Ids)[Idx]);
}

// Currently-equipped item id in a slot (-1 = none) — for the inventory UI.
int32 ARoseCharacter::GetEquippedId(const FString& Slot) const
{
	const int32* Id = Equipped.Find(Slot);
	return (Id && *Id >= 0) ? *Id : -1;
}

FString ARoseCharacter::GetDisplayName() const
{
	// The chosen character's name lives on the backend session (char-select
	// wrote it there); fall back to the actor label so the panel is never blank.
	if (const UGameInstance* GI = GetGameInstance())
		if (const URoseBackend* B = GI->GetSubsystem<URoseBackend>())
			if (!B->GetCharacterName().IsEmpty())
				return B->GetCharacterName();
	return GetName();
}

FString ARoseCharacter::GetJobName() const
{
	// ROSE starts everyone as VISITOR (job 0) — the jobs table has no row for
	// it, so an empty lookup is the normal case, not a failure.
	if (!Skills || Skills->CurrentJob == 0)
		return TEXT("Visitor");
	if (const FRoseJobRow* Row = Skills->GetJobRow(Skills->CurrentJob))
		if (!Row->JobName.IsEmpty())
			return Row->JobName;
	return TEXT("Visitor");
}

int32 ARoseCharacter::GetItemWeight(const FString& Slot, int32 Id) const
{
	// Same three-table lookup the inventory tooltip uses: weapons, armour, then
	// the simple-item tables (consumable/gem/material/...).
	if (Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
	{
		if (const FRoseWeaponRow* R = GetWeaponRow(Id))
			return R->Weight;
	}
	if (const FRoseArmorRow* R = GetArmorRow(Slot, Id))
		return R->Weight;
	if (const FRoseSimpleItemRow* SR = GetSimpleItemRow(Slot, Id))
		return SR->Weight;
	return 0;
}

int32 ARoseCharacter::GetCarriedWeight() const
{
	// Sum the bag's item weights.  Equipment weight is not counted here: ROSE
	// charges carried weight for what is IN the bag.
	int32 Total = 0;
	for (const FRoseItemStack& It : Bag)
	{
		if (It.Id < 0 || It.Count <= 0)
			continue;
		Total += GetItemWeight(It.Slot, It.Id) * It.Count;
	}
	return Total;
}

float ARoseCharacter::GetCarriedWeightFraction() const
{
	const int32 Max = Derived.MaxWeight;
	return Max > 0 ? FMath::Clamp((float)GetCarriedWeight() / (float)Max, 0.f, 1.f) : 0.f;
}

void ARoseCharacter::AddZuly(int32 Amount)
{
	if (Amount == 0)
		return;
	Zuly = FMath::Max(0, Zuly + Amount);
	++BagRevision;
}

void ARoseCharacter::AddItemToBag(const FString& Slot, int32 Id, int32 Count, int32 Bonus, bool bAppraised, int32 Refine)
{
	if (Id < 0 || Count <= 0)
		return;
	// Only stackable items (consumables/materials/gems/quest) merge; each piece of
	// equipment is a UNIQUE instance (with its own rolled stats) and gets its own entry.
	if (RoseIsStackableSlot(Slot))
	{
		for (FRoseItemStack& S : Bag)
			if (S.Slot == Slot && S.Id == Id) { S.Count += Count; ++BagRevision; return; }
		FRoseItemStack NewStack; NewStack.Slot = Slot; NewStack.Id = Id; NewStack.Count = Count;
		Bag.Add(NewStack);
	}
	else
	{
		for (int32 n = 0; n < Count; ++n)
		{
			FRoseItemStack NewStack; NewStack.Slot = Slot; NewStack.Id = Id; NewStack.Count = 1;
			NewStack.Bonus = Bonus; NewStack.bAppraised = bAppraised;
			NewStack.Refine = Refine;   // instance grade rides with the item
			Bag.Add(NewStack);
		}
	}
	++BagRevision;
}

void ARoseCharacter::ConsumeBagItem(const FString& Slot, int32 Id, int32 Count)
{
	for (int32 i = 0; i < Bag.Num(); ++i)
		if (Bag[i].Slot == Slot && Bag[i].Id == Id)
		{
			Bag[i].Count -= Count;
			if (Bag[i].Count <= 0)
				Bag.RemoveAt(i);
			++BagRevision;
			return;
		}
}

// ── DEV / cheat commands ────────────────────────────────────────────────────

void ARoseCharacter::RoseGive(const FString& Slot, int32 Id, int32 Count)
{
	const FString S = Slot.ToLower();
	// validate against the tables so typos don't create ghost items
	const bool bKnown =
		(S == TEXT("weapon") && GetWeaponRow(Id)) ||
		(GetArmorRow(S, Id) != nullptr) ||
		(GetSimpleItemRow(S, Id) != nullptr);
	if (!bKnown)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseGive] unknown item %s %d"), *S, Id);
		return;
	}
	AddItemToBag(S, Id, FMath::Max(1, Count));
	UE_LOG(LogTemp, Log, TEXT("[RoseGive] %s %d x%d -> bag"), *S, Id, Count);
}

void ARoseCharacter::RoseZuly(int32 Amount)
{
	AddZuly(Amount);
}

void ARoseCharacter::RoseDev()
{
	ToggleDevSpawn();
}

void ARoseCharacter::ToggleGripTuner()
{
	if (UI)
		UI->ToggleGripTuner();
}

void ARoseCharacter::ToggleDevSpawn()
{
	if (UI)
		UI->ToggleDevSpawn();
}

void ARoseCharacter::GetAllItemIdsForDev(TArray<FRoseDevItem>& Out) const
{
	struct FT { const TCHAR* Slot; UDataTable* T; };
	const FT Tables[] = {
		{ TEXT("weapon"), WeaponTable }, { TEXT("body"), BodyTable },
		{ TEXT("arms"), ArmsTable }, { TEXT("foot"), FootTable },
		{ TEXT("cap"), CapTable }, { TEXT("back"), BackTable },
		{ TEXT("consumable"), ConsumableTable }, { TEXT("gem"), GemTable },
		{ TEXT("material"), MaterialTable }, { TEXT("faceitem"), FaceItemTable },
		{ TEXT("jewel"), JewelTable }, { TEXT("subwpn"), SubWpnTable },
		{ TEXT("pat"), PatTable },
	};
	for (const FT& E : Tables)
	{
		if (!E.T) continue;
		for (const FName& RowName : E.T->GetRowNames())
		{
			FString Tail;
			if (RowName.ToString().Split(TEXT("_"), nullptr, &Tail, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				FRoseDevItem It;
				It.Slot = E.Slot;
				It.Id = FCString::Atoi(*Tail);
				Out.Add(MoveTemp(It));
			}
		}
	}
}

// ── PAT (cart / castle gear) + ammo + fuel ──────────────────────────────────

// datatype.h t_eItemCLASS → t_eRidePART slot: 51x body, 52x engine, 53x leg,
// 541 ability, 55x weapon (ARMS).
int32 ARoseCharacter::RidePartForClass(int32 ItemClass)
{
	switch (ItemClass / 10)
	{
	case 51: return 0;    // RIDE_PART_BODY   (511 cart / 512 cg / 513 mount)
	case 52: return 1;    // RIDE_PART_ENGINE (521 / 522)
	case 53: return 2;    // RIDE_PART_LEG    (531 / 532)
	case 54: return 3;    // RIDE_PART_ABIL   (541)
	case 55: return 4;    // RIDE_PART_ARMS   (551 / 552)
	default: return -1;
	}
}

int32 ARoseCharacter::ShotTypeForClass(int32 ItemClass)
{
	// citem.cpp GetNaturalBulletType: 431 arrow, 432 bullet, 433/421-423 throw.
	switch (ItemClass)
	{
	case 431: return 0;
	case 432: return 1;
	case 433: case 421: case 422: case 423: return 2;
	default: return -1;
	}
}

// Mount / dismount. Faithful to CObjCART: the vehicle is the thing that moves
// and the avatar is LINKED to it (CObjCART::Create does
// LinkDummy(avatarNode, PAT_ATTACH_NODE(body))), so the rider cannot turn
// independently — it inherits the vehicle's facing outright. Here the capsule
// still drives movement (it owns the CharacterMovementComponent and the input),
// the vehicle is attached to it, and the character MESH is re-parented onto the
// vehicle so the avatar rides the vehicle's transform rather than its own.
void ARoseCharacter::ToggleRide()
{
	if (RideCart)
	{
		// dismount — put the avatar back on its own capsule
		GetMesh()->AttachToComponent(GetCapsuleComponent(),
			FAttachmentTransformRules::KeepRelativeTransform);
		GetMesh()->SetRelativeLocation(RideMeshRestLocation);
		GetMesh()->SetRelativeRotation(RideMeshRestRotation);
		GetMesh()->SetVisibility(true, true);

		RideCart->Destroy();
		RideCart = nullptr;
		if (BaseWalkSpeed > 0.f)
			GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
		// hand rotation control back to the movement component
		GetCharacterMovement()->bOrientRotationToMovement = true;
		CurrentLoco = nullptr;     // force the locomotion state to re-evaluate
		return;
	}
	if (RideParts.Num() != MAX_RIDING_PART || RideParts[RIDE_PART_BODY] < 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoseRide] no PAT body equipped (PAT tab)"));
		return;
	}

	ARoseCart* Cart = GetWorld()->SpawnActor<ARoseCart>(GetActorLocation(), GetActorRotation());
	//if (!Cart || !Cart->Build(RideParts, PatPartTable, PatMotionTable))
	//{
	//	if (Cart) Cart->Destroy();
	//	UE_LOG(LogTemp, Warning, TEXT("[RoseRide] PAT build failed"));
	//	return;
	//}
	// Only carts and castle gears burn fuel — a mount never does.
	if (Cart->UsesFuel())
	{
		if (Cart->GetMaxFuel() > 0 && Fuel <= 0.f)
		{
			Cart->Destroy();
			UE_LOG(LogTemp, Log, TEXT("[RoseRide] out of fuel"));
			return;
		}
	}

	Cart->AttachToComponent(GetRootComponent(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Cart->SetActorRelativeLocation(FVector(0.f, 0.f, -RideCapsuleDrop));
    Cart->SetActorRelativeRotation(FRotator(0.f, 90.f, 0.f));
	RideCart = Cart;

	// Link the avatar to the vehicle. Once attached, the mesh has no rotation
	// of its own — this is the rotation lock, and it is why the rider stays
	// square in the seat while the vehicle turns.
	RideMeshRestLocation = GetMesh()->GetRelativeLocation();
	RideMeshRestRotation = GetMesh()->GetRelativeRotation();
	GetMesh()->AttachToComponent(Cart->GetMesh(),
		FAttachmentTransformRules::KeepWorldTransform);
	GetMesh()->SetRelativeLocation(Cart->GetSeatOffset());
	//GetMesh()->SetRelativeRotation(Cart->GetSeatRotation());
	// Morph-style mounts replace the avatar rather than carry it.
	GetMesh()->SetVisibility(!Cart->ShouldHideRider(), true);

	// The vehicle owns facing now, so stop the movement component from also
	// yawing the capsule's mesh child on its own schedule.
	GetCharacterMovement()->bOrientRotationToMovement = true;

	BaseWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	GetCharacterMovement()->MaxWalkSpeed = RideWalkSpeed(Cart);

	SetRideAnim(ERosePatAnim::Stop1);
	UE_LOG(LogTemp, Log, TEXT("[RoseRide] mounted (%s), speed %.0f, fuel %.0f"),
		*UEnum::GetValueAsString(Cart->GetPatClass()),
		GetCharacterMovement()->MaxWalkSpeed, Fuel);
}

// Cart/castle-gear part speeds are percentages of the on-foot speed; a mount's
// LIST_PAT "Movement Speed" is an absolute figure in ROSE units.
float ARoseCharacter::RideWalkSpeed(const ARoseCart* Cart) const
{
	const int32 Spd = Cart->GetMoveSpeed();
	if (Cart->GetPatClass() == ERosePatClass::Mount)
		return Spd > 0 ? Spd * MountSpeedScale : BaseWalkSpeed * RideSpeedMult;
	return BaseWalkSpeed * (Spd > 0 ? Spd / 100.f : RideSpeedMult);
}

// Drive both halves of the ride from one action: the vehicle plays its clip
// (CObjCART::GetANI_*) and the rider plays the matching avatar-motion clip
// (CObjCART::GetRideAniPos() + PETMODE_AVATAR_ANI_*).
void ARoseCharacter::SetRideAnim(ERosePatAnim Action)
{
	if (!RideCart)
		return;
	if (RideAnim == Action)
		return;
	RideAnim = Action;

	RideCart->PlayPatAnim(Action);

	if (RideCart->ShouldHideRider())
		return;
	const FString SkeletonPath = FString::Printf(
		TEXT("/Game/Characters/Modular/%s/base/base/SkeletalMeshes"), *Gender);
	if (UAnimSequence* Anim = RideCart->ResolveRiderAnim(Action, SkeletonPath))
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		GetMesh()->PlayAnimation(Anim, true);
		CurrentLoco = Anim;
	}
}

void ARoseCharacter::TickRide(float DeltaSeconds)
{
	if (!RideCart)
		return;

	// Stop / move is the vehicle's whole locomotion state (CART_STATE_*).
	const bool bMoving = GetVelocity().SizeSquared2D() > 25.f;
	SetRideAnim(bMoving ? ERosePatAnim::Move : ERosePatAnim::Stop1);

	if (!RideCart->UsesFuel())
		return;   // mounts never burn fuel
	// Fuel Rate is the engine's consumption per unit of travel; only a moving
	// vehicle burns any.
	if (!bMoving)
		return;
	const float Rate = RideCart->GetFuelRate() > 0
		? RideCart->GetFuelRate() * FuelDrainScale : FuelDrainPerSec;
	Fuel = FMath::Max(0.f, Fuel - Rate * DeltaSeconds);
	if (Fuel <= 0.f)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoseRide] fuel empty — dismounting"));
		ToggleRide();
	}
}

const FRosePatPartRow* ARoseCharacter::GetPatPartRow(int32 PatId) const
{
	if (PatId < 0 || !PatPartTable) return nullptr;
	return PatPartTable->FindRow<FRosePatPartRow>(
		FName(*FString::Printf(TEXT("pat_%d"), PatId)), TEXT("GetPatPartRow"), false);
}

void ARoseCharacter::EquipRidePart(int32 PartIdx, int32 PatId)
{
	if (RideParts.Num() != MAX_RIDING_PART) RideParts.Init(-1, MAX_RIDING_PART);
	if (PartIdx < 0 || PartIdx >= MAX_RIDING_PART) return;

	const FRosePatPartRow* Row = GetPatPartRow(PatId);
	if (PatId >= 0)
	{
		// The item's own slot must match where it is being put (server-shaped
		// validation). pat_parts.csv already forces a mount into BODY.
		if (!Row || Row->PartSlot != PartIdx) return;
	}

	// A mount is standalone — it has no parts to combine with, so equipping one
	// clears the assembled vehicle, and vice versa.
	const bool bMount = Row && Row->PatClass == static_cast<int32>(ERosePatClass::Mount);
	const bool bWasMount = [&]
	{
		const FRosePatPartRow* B = GetPatPartRow(GetRidePart(RIDE_PART_BODY));
		return B && B->PatClass == static_cast<int32>(ERosePatClass::Mount);
	}();
	if (PatId >= 0 && (bMount || (bWasMount && PartIdx != RIDE_PART_BODY)))
	{
		for (int32 i = 0; i < RideParts.Num(); ++i)
		{
			if (i == PartIdx || RideParts[i] < 0) continue;
			AddItemToBag(TEXT("pat"), RideParts[i], 1);
			RideParts[i] = -1;
		}
	}

	if (PatId >= 0)
		ConsumeBagItem(TEXT("pat"), PatId, 1);
	const int32 Old = RideParts[PartIdx];
	RideParts[PartIdx] = PatId;
	if (Old >= 0)
		AddItemToBag(TEXT("pat"), Old, 1);
	++BagRevision;
}

int32 ARoseCharacter::GetRidePart(int32 PartIdx) const
{
	return RideParts.IsValidIndex(PartIdx) ? RideParts[PartIdx] : -1;
}

void ARoseCharacter::UnequipAmmo(int32 ShotIdx)
{
	if (!AmmoSlots.IsValidIndex(ShotIdx)) return;
	FRoseItemStack S = AmmoSlots[ShotIdx];
	if (S.Id < 0 || S.Count <= 0) return;
	AmmoSlots[ShotIdx].Id = -1;
	AmmoSlots[ShotIdx].Count = 0;
	AddItemToBag(S.Slot, S.Id, S.Count);
	++BagRevision;
}

void ARoseCharacter::UseFuelItem(int32 ConsumableId)
{
	const FRoseSimpleItemRow* Row = GetSimpleItemRow(TEXT("consumable"), ConsumableId);
	if (!Row || Row->Subtype != 317) return;      // USE_ITEM_FUEL only
	bool bOwned = false;
	for (const FRoseItemStack& S : Bag)
		if (S.Slot == TEXT("consumable") && S.Id == ConsumableId && S.Count > 0)
		{ bOwned = true; break; }
	if (!bOwned) return;
	ConsumeBagItem(TEXT("consumable"), ConsumableId, 1);
	Fuel = MaxFuel;
	++BagRevision;
}

// ── Consumables ─────────────────────────────────────────────────────────────
// The instant branch of CUserDATA::Use_ITEM (cuserdata.cpp:933):
// Add_AbilityValue(ADD_DATA_TYPE, ADD_DATA_VALUE).  Only the abilities this
// port actually models are honoured; anything else is a no-op rather than a
// silent wrong effect (coupons, loot bags and Lua-script items live here).
void ARoseCharacter::AddAbilityFromItem(int32 Ability, int32 Amount)
{
	if (Amount == 0) return;
	switch (Ability)
	{
	case 16:   // AT_HP
		CurrentHP = FMath::Clamp(CurrentHP + Amount, 0.f, (float)Derived.MaxHP);
		break;
	case 17:   // AT_MP
		CurrentMP = FMath::Clamp(CurrentMP + Amount, 0.f, (float)Derived.MaxMP);
		break;
	case 30:   // AT_EXP
		GiveExp(Amount);
		break;
	case 40:   // AT_MONEY
		AddZuly(Amount);
		break;
	case 77:   // AT_FUEL
		Fuel = FMath::Clamp(Fuel + Amount, 0.f, MaxFuel);
		break;
	default:
		break;
	}
}

void ARoseCharacter::TickPotionRegens(float DeltaSeconds)
{
	for (int32 i = PotionRegens.Num() - 1; i >= 0; --i)
	{
		FRosePotionRegen& R = PotionRegens[i];
		const float Step = FMath::Min(R.PerSec * DeltaSeconds, R.Remaining);
		R.Remaining -= Step;
		if (R.IngType == 1)                    // ING_INC_HP
			CurrentHP = FMath::Min((float)Derived.MaxHP, CurrentHP + Step);
		else if (R.IngType == 2)               // ING_INC_MP
			CurrentMP = FMath::Min((float)Derived.MaxMP, CurrentMP + Step);
		if (R.Remaining <= KINDA_SMALL_NUMBER)
			PotionRegens.RemoveAt(i);
	}
}

float ARoseCharacter::GetConsumableCooldown(int32 ConsumableId) const
{
	const FRoseSimpleItemRow* Row = GetSimpleItemRow(TEXT("consumable"), ConsumableId);
	const UWorld* W = GetWorld();
	if (!Row || !W || Row->CooldownSec <= 0) return 0.f;
	const float* End = ConsumableCooldownEnd.Find(Row->CooldownType);
	return End ? FMath::Max(0.f, *End - W->GetTimeSeconds()) : 0.f;
}

bool ARoseCharacter::UseConsumableItem(int32 ConsumableId)
{
	FString Unused;
	return TryUseConsumableItem(ConsumableId, Unused);
}

bool ARoseCharacter::TryUseConsumableItem(int32 ConsumableId, FString& OutReason)
{
	const FRoseSimpleItemRow* Row = GetSimpleItemRow(TEXT("consumable"), ConsumableId);
	if (!Row)
	{ OutReason = TEXT("unknown item"); return false; }

	// Ownership is the authority, exactly as in TryEquipFromBag.
	bool bOwned = false;
	for (const FRoseItemStack& S : Bag)
		if (S.Slot == TEXT("consumable") && S.Id == ConsumableId && S.Count > 0)
		{ bOwned = true; break; }
	if (!bOwned)
	{ OutReason = TEXT("item not in bag"); return false; }

	// Cart / castle-gear fuel keeps its own path (USE_ITEM_FUEL, gs_user.cpp:1013).
	if (Row->Subtype == 317)
	{
		UseFuelItem(ConsumableId);
		return true;
	}

	// USEITEM_NEED_DATA_TYPE/VALUE gate (gs_user.cpp:940).  Abilities we don't
	// model return MAX_int32 from GetAbilityValue, so they never block.
	if (Row->NeedAbility != 0 && GetAbilityValue(Row->NeedAbility) < Row->NeedValue)
	{ OutReason = TEXT("requirements not met"); return false; }

	const UWorld* W = GetWorld();
	const float Now = W ? W->GetTimeSeconds() : 0.f;
	if (Row->CooldownSec > 0)
		if (const float* End = ConsumableCooldownEnd.Find(Row->CooldownType))
			if (Now < *End)
			{
				OutReason = FString::Printf(TEXT("cooling down (%.0fs)"), *End - Now);
				return false;
			}

	if (Row->StatusId != 0 && Row->StatusPerSec > 0 &&
		(Row->StatusType == 1 || Row->StatusType == 2))
	{
		// Over-time potion: AddValue is the TOTAL, StatusPerSec the per-second
		// rate (classUSER::Use_pITEM -> UpdateIngPOTION, gs_user.cpp:894).
		// Re-using the same kind refreshes rather than stacking, matching
		// IsEnableApplayITEM's replace-the-running-instance behaviour.
		for (int32 i = PotionRegens.Num() - 1; i >= 0; --i)
			if (PotionRegens[i].IngType == Row->StatusType)
				PotionRegens.RemoveAt(i);
		FRosePotionRegen R;
		R.IngType = Row->StatusType;
		R.PerSec = (float)Row->StatusPerSec;
		R.Remaining = (float)Row->AddValue;
		PotionRegens.Add(R);
	}
	else
	{
		AddAbilityFromItem(Row->AddAbility, Row->AddValue);
	}

	if (Row->CooldownSec > 0)
		ConsumableCooldownEnd.Add(Row->CooldownType, Now + (float)Row->CooldownSec);

	ConsumeBagItem(TEXT("consumable"), ConsumableId, 1);
	return true;
}

void ARoseCharacter::EquipAmmoFromBag(int32 BagIndex)
{
	if (AmmoSlots.Num() != 3) AmmoSlots.SetNum(3);
	if (!Bag.IsValidIndex(BagIndex)) return;
	const FRoseItemStack S = Bag[BagIndex];
	if (S.Slot != TEXT("material")) return;
	const FRoseSimpleItemRow* Row = GetSimpleItemRow(TEXT("material"), S.Id);
	if (!Row) return;
	const int32 Shot = ShotTypeForClass(Row->Subtype);
	if (Shot < 0) return;
	// swap the whole stack with whatever occupied the slot
	FRoseItemStack Prev = AmmoSlots[Shot];
	AmmoSlots[Shot] = S;
	Bag.RemoveAt(BagIndex);
	if (Prev.Id >= 0 && Prev.Count > 0)
		AddItemToBag(Prev.Slot, Prev.Id, Prev.Count);
	++BagRevision;
}

bool ARoseCharacter::TryEquipFromBag(const FString& Slot, int32 Id, FString& OutReason)
{
	if (Id < 0)
	{ OutReason = TEXT("invalid item"); return false; }

	// Ownership: the item must actually be in the bag.  A server must never
	// trust the client's (slot,id) — this check IS the authority.  Capture the
	// instance's refine grade from the same stack ConsumeBagItem will take
	// (both scan first-match, so they agree).
	bool bOwned = false;
	int32 BagRefine = 0, BagBonus = 0;
	bool BagAppraised = true;
	for (const FRoseItemStack& S : Bag)
		if (S.Slot == Slot && S.Id == Id && S.Count > 0)
		{ bOwned = true; BagRefine = S.Refine; BagBonus = S.Bonus; BagAppraised = S.bAppraised; break; }
	if (!bOwned)
	{ OutReason = TEXT("item not in bag"); return false; }

	// Faithful equip gate (CUserDATA::Check_EquipCondition) — enforced here,
	// not just displayed by the UI.
	if (!MeetsRequirements(Slot, Id, OutReason))
		return false;

	// Classic hand rules: a sub-weapon (shield/off-hand) needs the LEFT hand
	// free — only a plain one-handed right weapon (or bare fists) allows it.
	if (Slot == TEXT("subwpn") && !WeaponAllowsSubWeapon())
	{ OutReason = TEXT("requires a one-handed weapon"); return false; }

	// Atomic swap: bag → hand → old piece back to the bag.  Instance data
	// (refine grade + bonus option + appraisal state) rides both directions.
	const int32 Old = GetEquippedId(Slot);
	const int32 OldRefine = GetEquippedRefine(Slot);
	const int32 OldBonus = GetEquippedBonus(Slot);
	const bool  OldAppraised = GetEquippedAppraised(Slot);
	ConsumeBagItem(Slot, Id, 1);
	EquipItem(Slot, Id);
	EquippedRefine.Add(Slot, BagRefine);
	EquippedBonus.Add(Slot, BagBonus);
	EquippedAppraised.Add(Slot, BagAppraised);
	ApplyRefineGlow();
	ApplyDerivedStats();
	if (Old >= 0 && Old != Id)
		AddItemToBag(Slot, Old, 1, OldBonus, OldAppraised, OldRefine);

	// Equipping a weapon that occupies both hands (2H/dual/left-hand gun)
	// evicts the shield back to the bag — the classic auto-unequip.
	if (Slot == TEXT("weapon") && !WeaponAllowsSubWeapon()
		&& GetEquippedId(TEXT("subwpn")) >= 0)
	{
		FString Dummy;
		TryUnequipToBag(TEXT("subwpn"), Dummy);
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			TEXT("Sub-weapon unequipped (two-handed weapon)"));
	}
	return true;
}

bool ARoseCharacter::TryUnequipToBag(const FString& Slot, FString& OutReason)
{
	const int32 Id = GetEquippedId(Slot);
	if (Id < 0)
	{ OutReason = TEXT("nothing equipped there"); return false; }
	// (Bag capacity is unmodeled — when MaxWeight/slots exist, gate here.)
	// Instance data goes back with the item.
	const int32 Refine = GetEquippedRefine(Slot);
	const int32 Bonus = GetEquippedBonus(Slot);
	const bool  bApp = GetEquippedAppraised(Slot);
	EquipItem(Slot, -1);
	AddItemToBag(Slot, Id, 1, Bonus, bApp, Refine);
	return true;
}

bool ARoseCharacter::TryPickUp(ARoseGroundItem* Item, FString& OutReason)
{
	if (!IsValid(Item))
	{ OutReason = TEXT("it's gone"); return false; }
	// Reach: must be standing near the drop (the server-side range check).
	if (FVector::DistSquared2D(Item->GetActorLocation(), GetActorLocation())
		> FMath::Square(220.f))
	{ OutReason = TEXT("too far away"); return false; }

	if (Item->bIsMoney)
	{
		AddZuly(Item->Zuly);
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Picked up %d zuly"), Item->Zuly));
	}
	else
	{
		AddItemToBag(Item->Slot, Item->ItemId, Item->Count, Item->Bonus, Item->bAppraised);
		// The bonus is an OPTION ROW, not a "+N" — and an unappraised drop keeps
		// its option hidden until appraisal (that's the whole appraise loop).
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Picked up %s%s"), *Item->DisplayName,
				!Item->bAppraised ? TEXT(" [Unappraised]") : TEXT("")));
	}
	Item->Destroy();
	return true;
}

void ARoseCharacter::EquipFromBag(const FString& Slot, int32 Id)
{
	// UI entry point.  Off the server this is an intent, not an action: the
	// result comes back as replicated Bag + RepEquipped.
	if (!HasAuthority())
	{
		if (IsLocallyControlled())
			Server_EquipFromBag(Slot, Id);
		return;
	}
	FString Reason;
	if (!TryEquipFromBag(Slot, Id, Reason))
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Cannot equip: %s"), *Reason));
}

void ARoseCharacter::UnequipToBag(const FString& Slot)
{
	if (!HasAuthority())
	{
		if (IsLocallyControlled())
			Server_UnequipToBag(Slot);
		return;
	}
	FString Reason;
	if (!TryUnequipToBag(Slot, Reason))
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Cannot unequip: %s"), *Reason));
}

void ARoseCharacter::SpawnMonsterLoot(const FVector& DropLoc, int32 MobLevel, int32 DropType, int32 DropItem, int32 DropMoney)
{
	// Faithful ROSE drop roll (CCal::Get_DropITEM): one roll → money OR one item.
	FRoseDropResult Drop;
	if (!RoseRollDrop(MobLevel, Level, DropType, DropItem, DropMoney, Charm, Drop))
		return;

	UWorld* W = GetWorld();
	if (!W)
		return;

	// Scatter the drop a little around the corpse and snap it to the ground.
	FVector Loc = DropLoc + FVector(FMath::RandRange(-40.f, 40.f), FMath::RandRange(-40.f, 40.f), 20.f);
	FHitResult Hit;
	if (W->LineTraceSingleByChannel(Hit, Loc + FVector(0, 0, 100), Loc - FVector(0, 0, 300), ECC_Visibility))
		Loc.Z = Hit.ImpactPoint.Z + 14.f;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARoseGroundItem* GI = W->SpawnActor<ARoseGroundItem>(ARoseGroundItem::StaticClass(), Loc, FRotator::ZeroRotator, P);
	if (!GI)
		return;

	if (Drop.bIsMoney)
		GI->InitMoney(Drop.Money);
	else
	{
		const FString Slot = RoseItemTypeToSlot(Drop.ItemType);
		GI->InitItem(Slot, Drop.ItemNo, FMath::Max(1, Drop.Quantity), Drop.Bonus, Drop.bAppraised,
			GetItemName(Slot, Drop.ItemNo), GetItemFieldModel(Slot, Drop.ItemNo));
	}
}

void ARoseCharacter::PickUpNearest()
{
	UWorld* W = GetWorld();
	if (!W)
		return;
	// SEEK wide, PICK UP close.  The old radius was the pickup reach itself, so
	// a drop two steps away simply reported "nothing nearby".
	ARoseGroundItem* Best = nullptr;
	float BestSq = FMath::Square(kPickUpSeekRange);
	for (TActorIterator<ARoseGroundItem> It(W); It; ++It)
	{
		const float D = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (D <= BestSq) { BestSq = D; Best = *It; }
	}
	if (!Best)
	{
		FRoseChatLog::Add(FRoseChatLog::EKind::System, TEXT("Nothing to pick up nearby."));
		return;
	}

	// Out of arm's reach: walk there and pick it up on arrival.
	if (BestSq > FMath::Square(200.f))
	{
		PendingPickUp = Best;
		return;
	}

	DoPickUp(Best);
}

// The pick-up itself: ROSE's own motion, its sound, then the transfer.
void ARoseCharacter::DoPickUp(ARoseGroundItem* Best)
{
	if (!Best)
		return;

	// Animation + sound are LOCAL feedback, so they run on whoever is looking —
	// not gated on authority like the transfer below.
	if (!IsDedicatedServerRole())
	{
		// Motion comes from the Pick Up skill row (id 12, ActionMotion 17)
		// rather than a hardcoded index, so it follows the table like every
		// other action animation.
		if (Skills)
			if (const FRoseSkillRow* R = Skills->GetSkillRow(kSkillPickUp))
				PlaySkillMotion(R->ActionMotion, R->CastMotion, 100.f);

		if (UWorld* SW = GetWorld())
			if (USoundBase* S = LoadObject<USoundBase>(nullptr,
				TEXT("/Game/Sounds/item/getitem_001.getitem_001")))
				UGameplayStatics::PlaySoundAtLocation(SW, S, GetActorLocation());
	}

	// The client picks the target (it can see the drop); the server re-checks
	// reach and does the actual transfer — TryPickUp is written to validate, so
	// it is safe to hand it a client-chosen actor.
	if (!HasAuthority())
	{
		Server_PickUp(Best);
		return;
	}

	FString Reason;
	if (!TryPickUp(Best, Reason))
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Cannot pick up: %s"), *Reason));
}

void ARoseCharacter::Server_PickUp_Implementation(ARoseGroundItem* Item)
{
	FString Reason;
	TryPickUp(Item, Reason);
}

// Enable per-frame paperdoll capture only while the inventory is on screen.
void ARoseCharacter::SetPaperdollActive(bool bActive)
{
	if (PaperdollCapture)
		PaperdollCapture->bCaptureEveryFrame = bActive;
}

// ── Inventory (I): MODERN window — paperdoll + equip slots + bag grid + zuly
//    (RoseUIInventory.cpp), in a draggable chrome ────────────────────────────
void ARoseCharacter::ToggleInventory()
{
	if (!GEngine || !GEngine->GameViewport)
		return;

	if (bInventoryVisible)
	{
		if (InventoryRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(InventoryRoot.ToSharedRef());
		InventoryRoot.Reset();
		InventoryWindow.Reset();
		bInventoryVisible = false;
		SetPaperdollActive(false);
		UpdateUIInputMode();
		return;
	}

	SetPaperdollActive(true);
	// NO SRoseModernWindow here: the inventory now IS the client's dlgitem
	// layout, frame and caption included, so wrapping it in our own chrome
	// would draw two windows.  It drags itself by its caption bar.
	TSharedRef<SWidget> Panel = RoseInventory_MakeContent(*this);
	InventoryWindow.Reset();

	InventoryRoot = SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Offset(FMargin(320.f, 120.f, 0.f, 0.f))
		.Alignment(FVector2D(0, 0))
		.AutoSize(true)
		[ Panel ];

	GEngine->GameViewport->AddViewportWidgetContent(InventoryRoot.ToSharedRef(), 9);
	bInventoryVisible = true;
	UpdateUIInputMode();
}

// ── Character sheet (C): MODERN panel — HP/MP/Exp bars, base stats with "+",
//    derived stats, SP footer (RoseUICharSheet.cpp), in a draggable chrome ──────

void ARoseCharacter::RaiseStat(const FString& Which)
{
	if      (Which == TEXT("STR"))   ++Strength;
	else if (Which == TEXT("DEX"))   ++Dexterity;
	else if (Which == TEXT("INT"))   ++Intelligence;
	else if (Which == TEXT("CON"))   ++Concentration;
	else if (Which == TEXT("CHARM")) ++Charm;
	else if (Which == TEXT("SENSE")) ++Sense;
	else
		return;
	ApplyDerivedStats();
}

void ARoseCharacter::ToggleSkills()
{
	if (UI)
		UI->ToggleSkillPanel();
}

void ARoseCharacter::ToggleSkillTree()
{
	if (UI)
		UI->ToggleSkillTree();
}

void ARoseCharacter::FocusChat()
{
	if (UI)
		UI->FocusChat();
}

void ARoseCharacter::ToggleMinimapKey()
{
	if (UI)
		UI->ToggleMinimap();
}

void ARoseCharacter::ToggleOptionsKey()
{
	if (UI)
		UI->ToggleOptions();
}

void ARoseCharacter::ToggleQuestKey()
{
	if (UI)
		UI->ToggleQuestJournal();
}

void ARoseCharacter::ToggleCharacterSheet()
{
	if (!GEngine || !GEngine->GameViewport)
		return;

	if (bSheetVisible)
	{
		if (SheetRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(SheetRoot.ToSharedRef());
		SheetRoot.Reset();
		SheetModernWindow.Reset();
		bSheetVisible = false;
		UpdateUIInputMode();
		return;
	}

	// Like the inventory: the sheet IS the client's dlgavata, frame and caption
	// included, so our own chrome would draw a second window around it.  It
	// drags itself by its caption bar.
	TSharedRef<SWidget> Panel = RoseCharSheet_MakeContent(*this);
	SheetModernWindow.Reset();

	SheetRoot = SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Offset(FMargin(60.f, 150.f, 0.f, 0.f))
		.Alignment(FVector2D(0, 0))
		.AutoSize(true)
		[ Panel ];

	GEngine->GameViewport->AddViewportWidgetContent(SheetRoot.ToSharedRef(), 9);
	bSheetVisible = true;
	UpdateUIInputMode();
}

void ARoseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The panels' lambdas capture `this` — tear them down before the actor goes.
	if (GEngine && GEngine->GameViewport)
	{
		if (bStatsPanelVisible && StatsPanel.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(StatsPanel.ToSharedRef());
		if (bInventoryVisible && InventoryRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(InventoryRoot.ToSharedRef());
		if (bSheetVisible && SheetRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(SheetRoot.ToSharedRef());
	}
	StatsPanel.Reset();
	InventoryRoot.Reset();
	InventoryWindow.Reset();
	SheetRoot.Reset();
	SheetModernWindow.Reset();
	bStatsPanelVisible = false;
	bInventoryVisible = false;
	bSheetVisible = false;
	Super::EndPlay(EndPlayReason);
}

