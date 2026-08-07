#include "RoseMonster.h"

#include "RoseCharacter.h"
#include "RoseEffect.h"          // skill cast/hit effects
#include "RoseMonsterAI.h"       // AIP interpreter
#include "RoseMonsterSpawner.h"
#include "RoseNpcTypes.h"
#include "RoseQuest.h"           // kill-trigger (Do_DeadEvent)
#include "RoseSkillComponent.h"  // GetStatusFx (debuff visuals)
#include "RoseSkillTypes.h"      // FRoseSkillRow (monster skill casts)
#include "RoseSoundData.h"
#include "RoseStatFormulas.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Net/UnrealNetwork.h"   // DOREPLIFETIME

ARoseMonster::ARoseMonster()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(40.f, 60.f);

	// ROSE meshes are feet-at-origin, +Z up, facing +Y (drop + yaw).
	//
	// NO negative X scale.  That was correct only for the glTF pipeline, whose
	// converter basis had determinant −1 and mirrored the model, so the flip
	// unmirrored it.  These meshes now come from -run=RoseImportNpc, which
	// converts IDENTITY (ROSE and UE are both left-handed Z-up, so no basis
	// change is needed) — the mesh arrives unmirrored and the flip therefore
	// MIRRORS it instead of correcting it.
	//
	// It is not only cosmetic: a negative scale is a determinant −1 transform,
	// which reverses triangle winding and inverts the shading normals.
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -60.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	GetMesh()->SetRelativeScale3D(FVector::OneVector);

	// Not a light — M_RoseChar is unlit, so its colour goes out through emissive
	// and Lumen would treat every NPC as an area light.  See RoseCharacter.
	GetMesh()->SetEmissiveLightSource(false);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);

	// CharacterMovement ignores AddMovementInput on an UNPOSSESSED character
	// (PerformMovement is gated on having a Controller) — runtime-spawned mobs
	// must auto-possess an AIController or they play the walk anim in place.
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	// Belt and braces: keep moving (and falling) even with no controller.
	GetCharacterMovement()->bRunPhysicsWithNoController = true;

	// ── Networking ──────────────────────────────────────────────────────────
	// Server-owned actor: the spawner creates it on the authority only, and
	// clients receive a movement-replicated copy.  10 Hz is plenty for a mob —
	// CharacterMovement smooths between updates.
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(2.f);
	SetNetCullDistanceSquared(100000000.f);   // 100 m — mobs stop replicating far away
}

void ARoseMonster::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARoseMonster, NpcId);
	DOREPLIFETIME(ARoseMonster, HP);
	DOREPLIFETIME(ARoseMonster, MaxHP);
	DOREPLIFETIME(ARoseMonster, RepState);
}

static UAnimSequence* LoadMobAnim(const FString& Root, int32 Id, const TCHAR* Slot)
{
	// ue5_import_monsters.py: anims land beside the mesh, named npc_<id>_<slot>.
	const FString P = FString::Printf(TEXT("%s/npc_%d/npc_%d/SkeletalMeshes/npc_%d_%s.npc_%d_%s"),
		*Root, Id, Id, Id, Slot, Id, Slot);
	return LoadObject<UAnimSequence>(nullptr, *P);
}

bool ARoseMonster::Initialize()
{
	if (!NpcTable)
		NpcTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/npcs.npcs"));
	if (!NpcTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] monster %d: npcs DataTable missing"), NpcId);
		return false;
	}
	const FRoseNpcRow* Row = NpcTable->FindRow<FRoseNpcRow>(
		*FString::Printf(TEXT("npc_%d"), NpcId), TEXT("RoseMonster"));
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] monster %d: no npcs row"), NpcId);
		return false;
	}

	// Two pipelines produce NPC meshes and they need DIFFERENT handling, so the
	// resolver records which one won.
	//
	//  1. glTF (tools/build_monsters.py -> ue5_import_monsters.py) into the
	//     current npc folder.  Preferred: this is the pipeline that renders
	//     correctly today.
	//  2. Native (-run=RoseImportNpc), /Game/Rose/Npcs/SK_NPC_<id>.
	//  3. Row->Mesh — the OLD /Game/Monsters glTF assets.  Arua-era content the
	//     QQ-iROSE tables no longer agree with, so a row id there resolves to a
	//     different creature; last resort only.
	//
	// The distinction that matters below is the BASIS: the glTF converter uses a
	// determinant −1 basis, which mirrors the model, while the native importer
	// converts identity.  Whether the X flip is a fix or a bug depends entirely
	// on which of these built the mesh.
	bool bGltfBasis = true;
	USkeletalMesh* SM = LoadObject<USkeletalMesh>(nullptr,
		*FString::Printf(TEXT("/Game/Rose/Npcs/npc_%d/npc_%d/SkeletalMeshes/npc_%d.npc_%d"),
			NpcId, NpcId, NpcId, NpcId));
	if (!SM)
	{
		SM = LoadObject<USkeletalMesh>(nullptr,
			*FString::Printf(TEXT("/Game/Rose/Npcs/SK_NPC_%d.SK_NPC_%d"), NpcId, NpcId));
		if (SM)
			bGltfBasis = false;
	}
	if (!SM)
	{
		SM = LoadObject<USkeletalMesh>(nullptr,
			*FString::Printf(TEXT("%s.npc_%d"), *Row->Mesh, NpcId));
	}
	if (!SM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] monster %d: mesh not imported (%s)"), NpcId, *Row->Mesh);
		return false;
	}
	GetMesh()->SetSkeletalMeshAsset(SM);
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	// NPC_SCALE is percent (client: m_fScale = NPC_SCALE/100), composed with the
	// unmirror flip ONLY for glTF-sourced meshes.
	//
	// The glTF converter's basis has determinant −1, so the model arrives
	// mirrored and the negative X puts it back.  The native importer converts
	// identity (ROSE and UE are both left-handed Z-up), so the same flip would
	// MIRROR that mesh instead of correcting it.  One flag, both pipelines.
	const float S = Row->Scale > 0 ? Row->Scale / 100.f : 1.f;
	GetMesh()->SetRelativeScale3D(bGltfBasis ? FVector(-S, S, S) : FVector(S));

	// Size the clickable capsule from the SCALED model bounds. The ctor's
	// fixed (40, 60) capsule made big models (Grunter King, bosses) mostly
	// unclickable — the cursor's ECC_Pawn trace only hits the capsule, and
	// the body extended metres beyond it.  Mesh local space is feet-at-origin
	// +Z-up, so model height = bounds origin Z + extent Z.
	{
		const FBoxSphereBounds B = SM->GetBounds();
		const float Height = FMath::Max(120.f, (B.Origin.Z + B.BoxExtent.Z) * S);
		const float HalfH  = FMath::Clamp(Height * 0.5f, 60.f, 450.f);
		const float Radius = FMath::Clamp(
			FMath::Max(B.BoxExtent.X, B.BoxExtent.Y) * S * 0.7f, 40.f, HalfH);
		const float OldHalf = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		GetCapsuleComponent()->SetCapsuleSize(Radius, HalfH);
		GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -HalfH));
		// Keep the feet where the spawner put them (the centre rises with size).
		AddActorWorldOffset(FVector(0.f, 0.f, HalfH - OldHalf));
	}

	// Faithful mob stats: MaxHP = HP_column * Level (client cobjchar.cpp).
	Level = FMath::Max(1, Row->Level);
	GiveExp = Row->GiveExp;
	DropType = Row->DropType;
	DropMoney = Row->DropMoney;
	DropItem = Row->DropItem;
	MaxHP = HP = (float)FMath::Max(1, Row->HP * Level);
	AttackPower = Row->Attack;
	HitRate = Row->Hit;
	Defense = Row->Defense;
	Resist = Row->Resist;
	Avoid = Row->Avoid;
	AtkSpeedStat = FMath::Clamp(Row->AttackSpeed, 50, 200);
	WalkSpeed = FMath::Max(50.f, (float)Row->WalkSpeed);
	RunSpeed = FMath::Max(WalkSpeed, (float)Row->RunSpeed);
	AttackRange = FMath::Max(150.f, (float)Row->AttackRange);
	bMagicDamage = Row->IsMagicDamage != 0;
	HitMaterial = Row->HitMaterial;
	AttackSoundId = Row->AttackSound;
	DieSoundId = Row->DieSound;
	QuestTrigger = Row->QuestTrigger;
	SellTabs[0] = Row->SellTab0; SellTabs[1] = Row->SellTab1;
	SellTabs[2] = Row->SellTab2; SellTabs[3] = Row->SellTab3;

	// Classic per-mob AI: LIST_NPC col 16 → FILE_AI row → parsed AIP script.
	// Falls back to the built-in FSM when the id has no parsed data.
	AiType = Row->AiType;
	if (AiType > 0 && !AIComp && UsesAip())
	{
		AIComp = NewObject<URoseMonsterAIComponent>(this, TEXT("AipAI"));
		AIComp->RegisterComponent();
		if (!AIComp->Setup(AiType))
		{
			AIComp->DestroyComponent();
			AIComp = nullptr;
		}
	}
	DisplayName = Row->DisplayName.IsEmpty()
		? FString::Printf(TEXT("npc %d"), NpcId) : Row->DisplayName;

	UE_LOG(LogTemp, Log, TEXT("[Rose] monster %d '%s' Lv%d HP %.0f ready"),
		NpcId, *DisplayName, Level, MaxHP);

	const FString Root = TEXT("/Game/Monsters");
	// Held weapon (guards' spears, armed bosses) — LIST_NPC col 5.
	AttachNpcWeapon(Row->RWeapon);

	AnimStop   = LoadMobAnim(Root, NpcId, TEXT("stop"));
	AnimMove   = LoadMobAnim(Root, NpcId, TEXT("move"));
	AnimRun    = LoadMobAnim(Root, NpcId, TEXT("run"));
	AnimAttack = LoadMobAnim(Root, NpcId, TEXT("attack"));
	AnimHit    = LoadMobAnim(Root, NpcId, TEXT("hit"));
	AnimDie    = LoadMobAnim(Root, NpcId, TEXT("die"));
	if (!AnimRun)  AnimRun = AnimMove;      // some mobs (butterflies) only walk
	if (!AnimMove) AnimMove = AnimRun;

	return true;
}

void ARoseMonster::BeginPlay()
{
	Super::BeginPlay();
	SpawnLocation = GetActorLocation();
	bInitialized = Initialize();
	if (bInitialized)
	{
		EnterIdle();
		if (AIComp)
			AIComp->FireTrigger(EAipTrigger::Created);
	}
	else
	{
		// Assets for this NpcId aren't imported — release the spawner's live
		// slot and go away instead of standing around invisible.
		if (OwnerSpawner.IsValid())
			OwnerSpawner->NotifyDied();
		if (SummonOwner)
			SummonOwner->ReleaseSummon(this);
		Destroy();
	}
}

void ARoseMonster::Play(UAnimSequence* Anim, bool bLoop, float Rate)
{
	if (!Anim || (bLoop && Anim == CurrentAnim)) return;
	CurrentAnim = Anim;
	GetMesh()->Stop();
	GetMesh()->PlayAnimation(Anim, bLoop);
	GetMesh()->SetPlayRate(Rate);
}

ARoseCharacter* ARoseMonster::FindPlayer() const
{
	return Cast<ARoseCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
}

void ARoseMonster::FacePlayer(const AActor* Player)
{
	if (!Player) return;
	FRotator R = (Player->GetActorLocation() - GetActorLocation()).Rotation();
	R.Pitch = R.Roll = 0.f;
	SetActorRotation(R);
}

// ── state transitions ────────────────────────────────────────────────────────

void ARoseMonster::EnterIdle()
{
	State = EState::Idle;
	StateTimer = FMath::FRandRange(2.f, 6.f);
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	Play(AnimStop, true);}

void ARoseMonster::EnterWander()
{
	State = EState::Wander;
	const FVector2D Dir = FVector2D(FMath::VRand()).GetSafeNormal();
	WanderTarget = SpawnLocation + FVector(Dir.X, Dir.Y, 0.f) * FMath::FRandRange(150.f, WanderRadius);
	StateTimer = 10.f;               // give up if blocked
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	Play(AnimMove, true);
}

void ARoseMonster::EnterChase()
{
	State = EState::Chase;
	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	// Server CObjAI::Get_MoveAniSPEED: run anim rate = Cal_RunAniSPEED(speed)
	// = (cm/s + 180)/600; walking plays at 1.0.
	Play(AnimRun, true, RoseStats::RunAnimRate(RunSpeed));}

void ARoseMonster::EnterAttack(ARoseCharacter* Player)
{
	State = EState::Attack;
	FacePlayer(Player);
	// Server: attack motion rate = attack_speed/100 (cobjchar.cpp
	// m_fCurAniSPEED) — the whole attack cycle shortens with the stat.
	const float Rate = AtkSpeedStat / 100.f;
	const float Len = AnimAttack ? AnimAttack->GetPlayLength() / Rate : 1.2f;
	Play(AnimAttack, false, Rate);
	AttackHitTimer = Len * 0.45f;    // contact point mid-swing
	StateTimer = Len + 0.3f;         // full swing + recovery, then re-evaluate
	// Swing-start sound (client action frame 31: NPC_ATTACK_SOUND for mobs).
	RosePlaySoundId(GetWorld(), AttackSoundId, GetActorLocation());}

void ARoseMonster::EnterReturn()
{
	State = EState::Return;
	HP = MaxHP;                      // ROSE mobs reset when they leash
	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	Play(AnimRun, true, RoseStats::RunAnimRate(RunSpeed));}

void ARoseMonster::Die(const FVector& FromDir)
{
	// AIP DEAD trigger BEFORE teardown (death shouts / on-death summons — the
	// movement-type actions no-op once State flips below).
	if (AIComp) AIComp->FireTrigger(EAipTrigger::Dead);
	State = EState::Dead;
	HP = 0.f;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	Play(AnimDie, false);
	RosePlaySoundId(GetWorld(), DieSoundId, GetActorLocation());
	StateTimer = (AnimDie ? AnimDie->GetPlayLength() : 1.f) + 3.f;   // corpse linger
	if (OwnerSpawner.IsValid())
		OwnerSpawner->NotifyDied();
		// A SUMMON holds capacity on its owner; hand it back or the owner's
		// summon budget leaks and they can never summon again.
		if (SummonOwner)
			SummonOwner->ReleaseSummon(this);

	// Roll the faithful ITEM_DROP.STB drop and drop it ON THE GROUND at the
	// corpse (the player picks it up manually with Spacebar).
	if (ARoseCharacter* Player = FindPlayer())
	{
		Player->SpawnMonsterLoot(GetActorLocation(), Level, DropType, DropItem, DropMoney);

		// Award experience (NPC_GIVE_EXP) — auto-levels the player.
		Player->GiveExp(GiveExp);

		// CObjMOB::Do_DeadEvent (cobjchar.cpp:4042): a kill fires the NPC
		// row's quest trigger (NPC_DESC) — kill-count quest vars advance here.
		if (!QuestTrigger.IsEmpty())
			if (URoseQuestComponent* Q = Player->FindComponentByClass<URoseQuestComponent>())
				Q->CheckQuestTrigger(QuestTrigger, /*bDoReward=*/true);
	}
}

// ── combat intake (the player's melee path) ──────────────────────────────────

void ARoseMonster::ShowMiss()
{
	if (IsDead()) return;
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 90.f),
		TEXT("MISS"), nullptr, FColor::Silver, 0.8f, true);
}

void ARoseMonster::ApplyHit(float Damage, const FVector& FromDir, bool bCritical)
{
	// Damage is the server's business.  A client that reaches here (a locally
	// spawned projectile, a stale call site) must not move the HP bar itself —
	// the authoritative HP replicates back.
	if (!HasAuthority()) return;
	if (IsDead() || !bInitialized) return;
	HP = FMath::Max(0.f, HP - Damage);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 90.f),
		FString::Printf(TEXT("-%d%s"), FMath::CeilToInt(Damage), bCritical ? TEXT("!!") : TEXT("")),
		nullptr, bCritical ? FColor::Orange : FColor::Yellow, 0.8f, true);

	if (HP <= 0.f)
	{
		Die(FromDir);
		return;
	}
	// Getting hit always aggroes (even passive mobs fight back), with a short
	// hit-react stun unless mid-swing.
	if (State != EState::Attack)
	{
		if (AnimHit)
		{
			Play(AnimHit, false);
			HitStunTimer = FMath::Min(AnimHit->GetPlayLength(), 0.6f);
		}
		if (State != EState::Chase)
			EnterChase();
	}
	// AIP DAMAGED trigger (%-gated when already fighting) — low-HP flees,
	// call-for-help, retaliation casts live here.
	if (AIComp) AIComp->OnDamaged();
}

// ── held weapon (LIST_NPC RWeapon on the CHR hand dummy) ─────────────────────
// npc_weapon_sockets.json: npc id -> { bone: "boneNN", pos: [x,y,z] cm } (the
// hand dummy's parent bone + local offset; gen_npc_weapon_sockets.py).
struct FNpcWeaponSocket { FString Bone; FVector Pos = FVector::ZeroVector; };

static const FNpcWeaponSocket* FindNpcWeaponSocket(int32 NpcId)
{
	static TMap<int32, FNpcWeaponSocket> Table;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Raw;
		if (FFileHelper::LoadFileToString(Raw,
				*(FPaths::ProjectContentDir() / TEXT("DataTables/npc_weapon_sockets.json"))))
		{
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
			if (FJsonSerializer::Deserialize(R, Root) && Root.IsValid())
				for (const TPair<FString, TSharedPtr<FJsonValue>>& It : Root->Values)
					if (const TSharedPtr<FJsonObject> O = It.Value->AsObject())
					{
						FNpcWeaponSocket S;
						S.Bone = O->GetStringField(TEXT("bone"));
						const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
						if (O->TryGetArrayField(TEXT("pos"), P) && P->Num() == 3)
							S.Pos = FVector((*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber());
						if (!S.Bone.IsEmpty())
							Table.Add(FCString::Atoi(*It.Key), MoveTemp(S));
					}
			UE_LOG(LogTemp, Log, TEXT("[Rose] %d npc weapon sockets loaded"), Table.Num());
		}
	}
	return Table.Find(NpcId);
}

void ARoseMonster::AttachNpcWeapon(int32 RWeaponId)
{
	if (RWeaponId <= 0 || WeaponComp) return;
	const FNpcWeaponSocket* S = FindNpcWeaponSocket(NpcId);
	if (!S) return;
	if (GetMesh()->GetBoneIndex(FName(*S->Bone)) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("[Rose] npc %d: weapon bone %s missing"), NpcId, *S->Bone);
		return;
	}
	const FString Name = FString::Printf(TEXT("weapon_%d"), RWeaponId);
	UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *FString::Printf(
		TEXT("/Game/Characters/Modular/WeaponsStatic/%s/%s/StaticMeshes/%s.%s"),
		*Name, *Name, *Name, *Name));
	if (!SM)
	{
		UE_LOG(LogTemp, Log, TEXT("[Rose] npc %d: weapon mesh %s not imported"), NpcId, *Name);
		return;
	}
	WeaponComp = NewObject<UStaticMeshComponent>(this, TEXT("NpcWeapon"));
	WeaponComp->SetStaticMesh(SM);
	WeaponComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponComp->RegisterComponent();
	WeaponComp->AttachToComponent(GetMesh(),
		FAttachmentTransformRules::KeepRelativeTransform, FName(*S->Bone));
	// Same hand-grip convention as the player's tuned right-hand attach
	// (GripRotR/GripLocR defaults) — NPC humanoids share the b1_* bone space.
	WeaponComp->SetRelativeLocation(S->Pos + FVector(8.f, 0.f, 0.f));
	WeaponComp->SetRelativeRotation(FRotator(-5.f, 0.f, 0.f));
}

// ── AIP-facing steering (called by URoseMonsterAIComponent actions) ──────────

void ARoseMonster::AIStop()
{
	if (State == EState::Dead || State == EState::Attack) return;
	EnterIdle();
}

void ARoseMonster::AIWander(bool bAroundSpawn, float Dist, bool bRun)
{
	if (State == EState::Dead || State == EState::Attack) return;
	State = EState::Wander;
	const FVector2D Dir = FVector2D(FMath::VRand()).GetSafeNormal();
	const FVector Center = bAroundSpawn ? SpawnLocation : GetActorLocation();
	WanderTarget = Center + FVector(Dir.X, Dir.Y, 0.f) * FMath::FRandRange(Dist * 0.3f, Dist);
	StateTimer = 10.f;
	GetCharacterMovement()->MaxWalkSpeed = bRun ? RunSpeed : WalkSpeed;
	Play(bRun ? AnimRun : AnimMove, true, bRun ? RoseStats::RunAnimRate(RunSpeed) : 1.f);
}

void ARoseMonster::AIFlee(float Dist)
{
	if (State == EState::Dead) return;
	// Run away from the player (or from spawn if no player) by Dist.
	FVector Away = FVector::ForwardVector;
	if (ARoseCharacter* P = FindPlayer())
		Away = (GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D();
	State = EState::Wander;                  // reuse wander locomotion to a point
	WanderTarget = GetActorLocation() + Away * Dist;
	StateTimer = 8.f;
	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	Play(AnimRun, true, RoseStats::RunAnimRate(RunSpeed));
}

void ARoseMonster::AIAggroPlayer()
{
	if (State == EState::Dead || State == EState::Chase || State == EState::Attack) return;
	EnterChase();
}

static const FRoseSkillRow* MonsterSkillRow(int32 SkillId)
{
	static UDataTable* Table = nullptr;
	if (!Table)
		Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/skills.skills"));
	if (!Table || SkillId <= 0) return nullptr;
	return Table->FindRow<FRoseSkillRow>(
		FName(*FString::Printf(TEXT("skill_%d"), SkillId)), TEXT("RoseMonsterAI"), false);
}

void ARoseMonster::AICastSkill(int32 SkillId, int32 /*MotionId*/, AActor* Target, bool bSelf)
{
	if (State == EState::Dead || PendingSkillTimer > 0.f) return;
	const FRoseSkillRow* Row = MonsterSkillRow(SkillId);
	if (!Row) return;

	if (!bSelf && Target)
		FacePlayer(Target);

	// Cast motion: the mob attack anim at the attack-speed rate (mob anim sets
	// have no dedicated cast slot; classic nMotion indexes CHR motions — v1
	// approximation).  Damage/heal applies at the contact point.
	const float Rate = AtkSpeedStat / 100.f;
	const float Len = AnimAttack ? AnimAttack->GetPlayLength() / Rate : 1.2f;
	Play(AnimAttack, false, Rate);
	State = EState::Attack;
	AttackHitTimer = -1.f;                  // no basic-attack contact — the skill is the payload
	StateTimer = Len + 0.3f;
	PendingSkillId = SkillId;
	bPendingSkillSelf = bSelf;
	PendingSkillTarget = Target;
	PendingSkillTimer = Len * 0.45f;

	RosePlaySoundId(GetWorld(), Row->FireSound, GetActorLocation());
	if (Row->CastEffect > 0)
		ARoseEffect::SpawnById(GetWorld(), Row->CastEffect, GetActorLocation(), this);

	UE_LOG(LogTemp, Log, TEXT("[RoseAI] %s casts skill %d (%s)"),
		*DisplayName, SkillId, bSelf ? TEXT("self") : TEXT("target"));
}

void ARoseMonster::ApplyPendingSkill()
{
	const int32 SkillId = PendingSkillId;
	PendingSkillId = -1;
	const FRoseSkillRow* Row = MonsterSkillRow(SkillId);
	if (!Row || IsDead()) return;

	AActor* Target = PendingSkillTarget.Get();

	// ── self / ally support skills: HP heal (AT_HP=16), like ApplySelfStatus ──
	if (bPendingSkillSelf || Cast<ARoseMonster>(Target))
	{
		ARoseMonster* Who = bPendingSkillSelf ? this : Cast<ARoseMonster>(Target);
		const int32 IncA[2] = { Row->IncAbility1, Row->IncAbility2 };
		const int32 IncV[2] = { Row->IncValue1,  Row->IncValue2 };
		const int32 IncR[2] = { Row->IncRate1,   Row->IncRate2 };
		for (int32 k = 0; k < 2; ++k)
		{
			if (IncA[k] == 16 && Who && !Who->IsDead())   // AT_HP heal
			{
				const int32 Adj = RoseStats::SkillAdjustValue(
					(int32)Who->HP, IncR[k], IncV[k], /*Int*/ 0);
				Who->HP = FMath::Min(Who->MaxHP, Who->HP + (float)Adj);
				DrawDebugString(GetWorld(), Who->GetActorLocation() + FVector(0, 0, 90.f),
					FString::Printf(TEXT("+%d"), Adj), nullptr, FColor::Green, 0.8f, true);
			}
		}
		if (Row->HitEffect > 0 && Who)
			ARoseEffect::SpawnById(GetWorld(), Row->HitEffect, Who->GetActorLocation(), Who);
		return;
	}

	// ── offensive skill vs the player ─────────────────────────────────────────
	ARoseCharacter* P = Cast<ARoseCharacter>(Target);
	if (!P) P = FindPlayer();
	if (!P || P->CurrentHP <= 0.f) return;
	// Generous range gate: skill range + melee reach margin (casts started in
	// range shouldn't whiff because the player stepped back a little).
	const float MaxR = FMath::Max((float)Row->Range, AttackRange) + 400.f;
	if (FVector::Dist2D(GetActorLocation(), P->GetActorLocation()) > MaxR) return;

	RoseStats::FSkillAttacker Atk;
	Atk.Level = Level;
	Atk.AttackPower = AttackPower;
	Atk.HitRate = HitRate;
	RoseStats::FSkillDefender Def;
	Def.Level = P->Level;
	Def.Defense = P->GetDefenseStat();
	Def.Resist = P->GetResistStat();
	Def.Avoid = P->GetAvoidStat();

	const int32 Dmg = RoseStats::SkillDamage(
		Row->Power, Row->DamageType, Atk, Def, FMath::Max(1, Row->HitCount));
	if (Dmg <= 0)
		P->ReceiveMonsterMiss();
	else
		P->ReceiveMonsterHit((float)Dmg, false);
	if (Row->HitEffect > 0)
		ARoseEffect::SpawnById(GetWorld(), Row->HitEffect, P->GetActorLocation(), P);

	// Debuff visuals on the player (LIST_STATUS STATE_STEP_EFFECT loops for the
	// skill's duration).  The stat side of monster debuffs is still not modeled
	// — this is the visual layer.
	if (Dmg > 0)
		for (const int32 StatusId : { Row->Status1, Row->Status2 })
			if (StatusId > 0)
				if (const int32 Fx = URoseSkillComponent::GetStatusFx(StatusId))
				{
					const FVector Feet = P->GetActorLocation()
						- FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
					ARoseEffect::SpawnById(GetWorld(), Fx, Feet, P,
						Row->Duration > 0 ? (float)Row->Duration : 6.f, /*bForceLink*/ true);
				}
}

// ── AI ───────────────────────────────────────────────────────────────────────

void ARoseMonster::TickAI(float Dt)
{
	ARoseCharacter* Player = FindPlayer();
	const FVector Loc = GetActorLocation();
	const float ToPlayer = Player ? FVector::Dist2D(Loc, Player->GetActorLocation()) : TNumericLimits<float>::Max();
	const float FromSpawn = FVector::Dist2D(Loc, SpawnLocation);
	// Reach is centre-to-centre: add both capsules like the player's melee does.
	const float Reach = AttackRange + 80.f;

	switch (State)
	{
	case EState::Idle:
		if (bAggressive && Player && ToPlayer < AggroRange) { EnterChase(); break; }
		// AIP STOP trigger (throttled by the script's header interval) — drives
		// idle behaviour (wander patterns, aggro conditions, idle self-buffs).
		if (AIComp) AIComp->TickIdle(Dt);
		StateTimer -= Dt;
		if (StateTimer <= 0.f && !AIComp)
			EnterWander();               // scripted mobs wander via their AIP
		break;

	case EState::Wander:
	{
		if (bAggressive && Player && ToPlayer < AggroRange) { EnterChase(); break; }
		StateTimer -= Dt;
		const FVector To = WanderTarget - Loc;
		if (To.Size2D() < 60.f)
		{
			// Positive proof of locomotion for headless log checks.
			UE_LOG(LogTemp, Log, TEXT("[Rose] %s npc %d reached wander target"), *GetName(), NpcId);
			EnterIdle(); break;
		}
		if (StateTimer <= 0.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Rose] %s npc %d wander TIMED OUT still %.0fcm away (stuck/not moving?)"),
				*GetName(), NpcId, To.Size2D());
			EnterIdle(); break;
		}
		AddMovementInput(To.GetSafeNormal2D());
		break;
	}

	case EState::Chase:
	{
		if (!Player || FromSpawn > LeashRange) { EnterReturn(); break; }
		if (AIComp) AIComp->TickChase(Dt);   // AIP ATTACKMOVE trigger (ranged casts)
		if (State != EState::Chase) break;   // the trigger may have started a cast
		if (ToPlayer <= Reach) { EnterAttack(Player); break; }
		AddMovementInput((Player->GetActorLocation() - Loc).GetSafeNormal2D());
		break;
	}

	case EState::Attack:
	{
		FacePlayer(Player);
		// Contact frame: roll the ROSE combat formulas against the player.
		if (AttackHitTimer > 0.f)
		{
			AttackHitTimer -= Dt;
			if (AttackHitTimer <= 0.f && Player && ToPlayer <= Reach * 1.3f)
			{
				const int32 Suc = RoseStats::SuccessRate(Level, Player->Level, HitRate, Player->GetAvoidStat());
				if (Suc <= 0)
					Player->ReceiveMonsterMiss();
				else
				{
					const bool bCrit = RoseStats::RollCritical(Level, 0);
					// Magic-damage mobs are resisted by RES, not DEF.
					const int32 Mitigation = bMagicDamage
						? Player->GetResistStat() : Player->GetDefenseStat();
					const int32 Dmg = RoseStats::BasicDamage(
						AttackPower, Mitigation, Player->GetAvoidStat(), Suc, bCrit);
					Player->ReceiveMonsterHit((float)Dmg, bCrit);
					// Impact sound: bare-hand hit type vs a player = material 1
					// (client cobjchar_actionframe.cpp case 21).
					RosePlaySoundId(GetWorld(),
						RoseHitSound(ROSE_BAREHAND_HIT_TYPE, 1), Player->GetActorLocation());
				}
			}
		}
		StateTimer -= Dt;
		if (StateTimer <= 0.f)
		{
			if (!Player || FromSpawn > LeashRange) EnterReturn();
			else if (ToPlayer <= Reach)            EnterAttack(Player);
			else                                   EnterChase();
		}
		break;
	}

	case EState::Return:
	{
		const FVector To = SpawnLocation - Loc;
		if (To.Size2D() < 100.f) { EnterIdle(); break; }
		AddMovementInput(To.GetSafeNormal2D());
		break;
	}

	case EState::Dead:
		StateTimer -= Dt;
		if (StateTimer <= 0.f)
			Destroy();
		break;
	}
}

// Client: pick the locomotion clip from the replicated velocity, exactly like
// the player proxies do.  Nothing about the AI state has to cross the wire for
// this — a mob that the server made run simply arrives moving fast.
void ARoseMonster::TickRemoteAnim(float Dt)
{
	if (State == EState::Dead)
		return;                       // OnRep_MobState played the death clip

	const float Speed = GetVelocity().Size2D();
	const float RunThreshold = FMath::Max(WalkSpeed + 20.f, RunSpeed * 0.6f);
	UAnimSequence* Want = Speed > RunThreshold ? AnimRun
		: (Speed > 10.f ? AnimMove : AnimStop);
	if (Want && Want != CurrentAnim)
		Play(Want, true, Want == AnimRun ? RoseStats::RunAnimRate(RunSpeed) : 1.f);
}

// Client: the server's AI state changed.  Only death needs acting on — it is
// the one transition with a non-looping animation and a collision change.
void ARoseMonster::OnRep_MobState()
{
	const EState S = static_cast<EState>(RepState);
	if (S == EState::Dead && State != EState::Dead)
	{
		State = EState::Dead;
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GetCharacterMovement()->StopMovementImmediately();
		if (AnimDie)
			Play(AnimDie, false);
	}
	else if (S != EState::Dead)
	{
		State = S;
	}
}

void ARoseMonster::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bInitialized) return;

	// Everything below — AI, casts, hit-stun, death — is simulation, and the
	// server owns all of it.  A client only animates what it is told.
	if (!HasAuthority())
	{
		TickRemoteAnim(Dt);
		return;
	}

	// Publish the AI state for the clients' death/animation handling.
	RepState = static_cast<uint8>(State);

	// In-flight AIP skill cast: apply at the motion's contact point (not
	// cancelled by hit-stun — classic casts complete once started).
	if (PendingSkillTimer > 0.f && State != EState::Dead)
	{
		PendingSkillTimer -= Dt;
		if (PendingSkillTimer <= 0.f)
			ApplyPendingSkill();
	}

	if (HitStunTimer > 0.f)
	{
		HitStunTimer -= Dt;
		if (HitStunTimer <= 0.f && State != EState::Dead && State != EState::Attack)
		{
			// Resume the state's locomotion anim after the hit react.
			CurrentAnim = nullptr;
			const bool bRunning = State == EState::Chase || State == EState::Return;
			Play(bRunning ? AnimRun : State == EState::Wander ? AnimMove : AnimStop,
				true, bRunning ? RoseStats::RunAnimRate(RunSpeed) : 1.f);
		}
	}
	else
	{
		TickAI(Dt);
	}
}
