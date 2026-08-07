#include "RoseEffect.h"

#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  JSON → FRoseFXDef cache
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	FVector JVec(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		return (A.Num() >= 3)
			? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber())
			: FVector::ZeroVector;
	}

	// Engine coords (Z-up right-handed metres... already cm here) → UE:
	// negate Y (RH→LH), same as the mesh pipeline's mirror convention.
	FVector ToUE(const FVector& V) { return FVector(V.X, V.Y, V.Z); }

	bool ParseSequence(const TSharedPtr<FJsonObject>& J, FRoseFXSequence& S)
	{
		S.Name = J->GetStringField(TEXT("name"));
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (J->TryGetArrayField(TEXT("life"), A) && A->Num() == 2)
		{ S.LifeMin = (*A)[0]->AsNumber(); S.LifeMax = (*A)[1]->AsNumber(); }
		if (J->TryGetArrayField(TEXT("emit_rate"), A) && A->Num() == 2)
		{ S.RateMin = (*A)[0]->AsNumber(); S.RateMax = (*A)[1]->AsNumber(); }
		S.Loops = (int32)J->GetNumberField(TEXT("loops"));
		if (J->TryGetArrayField(TEXT("spawn_dir"), A) && A->Num() == 2)
		{ S.SpawnDirMin = JVec((*A)[0]->AsArray()); S.SpawnDirMax = JVec((*A)[1]->AsArray()); }
		if (J->TryGetArrayField(TEXT("emit_radius"), A) && A->Num() == 2)
		{ S.RadiusMin = JVec((*A)[0]->AsArray()); S.RadiusMax = JVec((*A)[1]->AsArray()); }
		if (J->TryGetArrayField(TEXT("gravity"), A) && A->Num() == 2)
		{ S.GravityMin = JVec((*A)[0]->AsArray()); S.GravityMax = JVec((*A)[1]->AsArray()); }
		S.Texture = J->GetStringField(TEXT("texture"));
		S.NumParticles = FMath::Clamp((int32)J->GetNumberField(TEXT("num_particles")), 1, 500);
		S.Align = (int32)J->GetNumberField(TEXT("align"));
		S.Coord = (int32)J->GetNumberField(TEXT("coord"));
		if (J->TryGetArrayField(TEXT("grid"), A) && A->Num() == 2)
		{
			S.GridW = FMath::Max(1, (int32)(*A)[0]->AsNumber());
			S.GridH = FMath::Max(1, (int32)(*A)[1]->AsNumber());
		}
		// D3DBLEND dest: 2 = ONE → additive; anything else (6 INVSRCALPHA) = alpha.
		// D3DBLEND dst: 2=ONE (additive) and 4=INVSRCCOLOR (screen) are both
		// black-safe add-style blends — route them to the Add material.  Only
		// 6=INVSRCALPHA is true alpha blending.  (dst=4 through the Alpha
		// material rendered opaque black boxes.)
		{
			const int32 Dst = (int32)J->GetNumberField(TEXT("blend_dst"));
			S.bAdditive = (Dst == 2 || Dst == 3 || Dst == 4);
		}

		if (J->TryGetArrayField(TEXT("events"), A))
			for (const TSharedPtr<FJsonValue>& EV : *A)
			{
				const TSharedPtr<FJsonObject> E = EV->AsObject();
				if (!E.IsValid()) continue;
				FRoseFXEvent Ev;
				Ev.Type = (int32)E->GetNumberField(TEXT("type"));
				Ev.T0 = E->GetNumberField(TEXT("t0"));
				Ev.T1 = E->GetNumberField(TEXT("t1"));
				Ev.bFade = ((int32)E->GetNumberField(TEXT("fade"))) != 0;
				const TArray<TSharedPtr<FJsonValue>>* V;
				if (E->TryGetArrayField(TEXT("v"), V))
					for (const TSharedPtr<FJsonValue>& F : *V)
						Ev.Vals.Add(F->AsNumber());
				S.Events.Add(MoveTemp(Ev));
			}
		return true;
	}
}

const FRoseFXDef* RoseFX_GetDef(int32 EffectId)
{
	static TMap<int32, TUniquePtr<FRoseFXDef>> Cache;
	if (EffectId <= 0)
		return nullptr;
	if (const TUniquePtr<FRoseFXDef>* Found = Cache.Find(EffectId))
		return Found->Get();

	const FString Path = FPaths::ProjectDir() / TEXT("Effects") /
		FString::Printf(TEXT("eft_%d.json"), EffectId);
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		Cache.Add(EffectId, nullptr);   // negative-cache the miss
		return nullptr;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseFX] bad effect json %s"), *Path);
		Cache.Add(EffectId, nullptr);
		return nullptr;
	}

	TUniquePtr<FRoseFXDef> Def = MakeUnique<FRoseFXDef>();
	Def->Id = EffectId;
	Def->Name = Root->GetStringField(TEXT("name"));
	const TArray<TSharedPtr<FJsonValue>>* Parts;
	if (Root->TryGetArrayField(TEXT("particles"), Parts))
		for (const TSharedPtr<FJsonValue>& PV : *Parts)
		{
			const TSharedPtr<FJsonObject> P = PV->AsObject();
			if (!P.IsValid()) continue;
			FRoseFXEmitterEntry Entry;
			const TArray<TSharedPtr<FJsonValue>>* A;
			if (P->TryGetArrayField(TEXT("pos_cm"), A))
				Entry.Pos = ToUE(JVec(*A));
			if (P->TryGetArrayField(TEXT("rot_deg"), A) && A->Num() >= 3)
				// D3D pitch/yaw/roll → UE (handedness flip on pitch+yaw).
				Entry.Rot = FRotator(-(*A)[0]->AsNumber(), -(*A)[1]->AsNumber(), (*A)[2]->AsNumber());
			Entry.DelaySec = P->GetNumberField(TEXT("delay_ms")) / 1000.f;
			Entry.bLink = ((int32)P->GetNumberField(TEXT("link"))) != 0;
			const TArray<TSharedPtr<FJsonValue>>* Seqs;
			if (P->TryGetArrayField(TEXT("sequences"), Seqs))
				for (const TSharedPtr<FJsonValue>& SV : *Seqs)
				{
					FRoseFXSequence S;
					if (SV->AsObject().IsValid() && ParseSequence(SV->AsObject(), S))
						Entry.Sequences.Add(MoveTemp(S));
				}
			Def->Emitters.Add(MoveTemp(Entry));
		}

	const FRoseFXDef* Ret = Def.Get();
	Cache.Add(EffectId, MoveTemp(Def));
	return Ret;
}

// ─────────────────────────────────────────────────────────────────────────────
//  URoseParticleSeqComponent — the emitter
// ─────────────────────────────────────────────────────────────────────────────

URoseParticleSeqComponent::URoseParticleSeqComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	NumCustomDataFloats = 5;   // R,G,B,A,Frame (M_RoseParticle_*)
}

void URoseParticleSeqComponent::Setup(const FRoseFXSequence& InSeq, float InDelaySec)
{
	Seq = InSeq;
	Delay = InDelaySec;

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
		Seq.bAdditive ? TEXT("/Game/Effects/M_RoseParticle_Add.M_RoseParticle_Add")
		              : TEXT("/Game/Effects/M_RoseParticle_Alpha.M_RoseParticle_Alpha"));
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr,
		*FString::Printf(TEXT("/Game/Effects/Particles/%s.%s"), *Seq.Texture, *Seq.Texture));
	if (!Plane || !Base || !Tex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseFX] seq '%s': missing %s"), *Seq.Name,
			!Plane ? TEXT("plane") : !Base ? TEXT("particle material (run ue5_make_fx_assets.py)")
			                               : *Seq.Texture);
		bFinished = true;
		return;
	}
	SetStaticMesh(Plane);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
	MID->SetTextureParameterValue(TEXT("Tex"), Tex);
	MID->SetScalarParameterValue(TEXT("GridW"), (float)Seq.GridW);
	MID->SetScalarParameterValue(TEXT("GridH"), (float)Seq.GridH);
	SetMaterial(0, MID);

	Parts.SetNum(Seq.NumParticles);
	// One instance per particle slot, parked at zero scale.
	for (int32 i = 0; i < Seq.NumParticles; ++i)
		AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)), /*bWorldSpace*/ true);
}

// Gather this channel's events (already file-ordered), nail random times/values.
void URoseParticleSeqComponent::NailChannel(TArray<FKey, TInlineAllocator<8>>& Out,
	const TArray<const FRoseFXEvent*>& Evs, int32 Dim, FRandomStream& Rand)
{
	for (const FRoseFXEvent* E : Evs)
	{
		FKey K;
		K.Time = Rand.FRandRange(E->T0, E->T1);
		K.bFade = E->bFade;
		// payload = (min0..minN, max0..maxN)
		for (int32 d = 0; d < Dim; ++d)
			K.Val[d] = Rand.FRandRange(E->Vals[d], E->Vals[Dim + d]);
		Out.Add(K);
	}
	Out.Sort([](const FKey& A, const FKey& B) { return A.Time < B.Time; });
}

FVector4 URoseParticleSeqComponent::SampleChannel(const TArray<FKey, TInlineAllocator<8>>& K,
	float Age, const FVector4& Default, bool* bFound)
{
	if (bFound) *bFound = K.Num() > 0;
	if (K.Num() == 0)
		return Default;
	if (Age <= K[0].Time)
		return K[0].Val;
	for (int32 i = 1; i < K.Num(); ++i)
		if (Age < K[i].Time)
		{
			if (!K[i].bFade)
				return K[i - 1].Val;   // step until the next key fires
			const float T = (Age - K[i - 1].Time) / FMath::Max(0.001f, K[i].Time - K[i - 1].Time);
			FVector4 R;
			for (int32 d = 0; d < 4; ++d)
				R[d] = FMath::Lerp(K[i - 1].Val[d], K[i].Val[d], T);
			return R;
		}
	return K.Last().Val;
}

void URoseParticleSeqComponent::SpawnParticle(FPart& P)
{
	static FRandomStream Rand(20260711);
	auto RandVec = [&](const FVector& Mn, const FVector& Mx) {
		return FVector(Rand.FRandRange(Mn.X, Mx.X), Rand.FRandRange(Mn.Y, Mx.Y),
		               Rand.FRandRange(Mn.Z, Mx.Z));
	};

	P.bAlive = true;
	P.Age = 0.f;
	P.Life = Rand.FRandRange(Seq.LifeMin, Seq.LifeMax);
	// Engine axes → UE: negate Y (same convention as the def loader).
	auto ToUEv = [](const FVector& V) { return FVector(V.X, -V.Y, V.Z); };
	P.Pos = ToUEv(RandVec(Seq.RadiusMin, Seq.RadiusMax));
	P.Vel = ToUEv(RandVec(Seq.SpawnDirMin, Seq.SpawnDirMax));
	P.Gravity = ToUEv(RandVec(Seq.GravityMin, Seq.GravityMax));
	if (Seq.Coord == 0)   // world-space particles spawn at the emitter's world pos
		P.Pos += GetComponentLocation();
	P.Size = FVector2D(10.f, 10.f);
	P.Color = FLinearColor::White;
	P.Frame = 0.f;
	P.AngleDeg = 0.f;
	P.bUseAngle = false;

	// Nail this particle's keyframes.  Channels: size(2) / color(4, merging
	// COLOR + the RED/GREEN/BLUE/ALPHA scalars) / velocity(3, merging VELOCITY
	// + per-axis) / frame(1) / rotation(1).
	P.KSize.Reset(); P.KColor.Reset(); P.KVel.Reset(); P.KFrame.Reset(); P.KAngle.Reset();
	TArray<const FRoseFXEvent*> Size, Color, Vel, Frame, Angle;
	for (const FRoseFXEvent& E : Seq.Events)
	{
		switch (E.Type)
		{
		case 1: Size.Add(&E); break;
		case 3: case 4: case 5: case 6: case 7: Color.Add(&E); break;
		case 8: case 9: case 10: case 11: Vel.Add(&E); break;
		case 12: Frame.Add(&E); break;
		case 13: Angle.Add(&E); break;
		default: break;   // 2 = event-timer re-randomize: unmodeled
		}
	}
	NailChannel(P.KSize, Size, 2, Rand);
	NailChannel(P.KFrame, Frame, 1, Rand);
	NailChannel(P.KAngle, Angle, 1, Rand);
	P.bUseAngle = P.KAngle.Num() > 0;

	// Color: scalar events touch one component; expand them to rgba keys that
	// keep the other components at their previous value (sampled lazily — we
	// approximate by carrying white/1 defaults, which matches the authored
	// data: scalar color events are used alone or with a full COLOR at t=0).
	{
		TArray<FKey, TInlineAllocator<8>> Keys;
		FRandomStream& R = Rand;
		FLinearColor Carry = FLinearColor::White;
		// build in file order (already time-sorted per authoring convention)
		TArray<const FRoseFXEvent*> Sorted = Color;
		Sorted.Sort([](const FRoseFXEvent& A, const FRoseFXEvent& B) { return A.T0 < B.T0; });
		for (const FRoseFXEvent* E : Sorted)
		{
			FKey K;
			K.Time = R.FRandRange(E->T0, E->T1);
			K.bFade = E->bFade;
			FLinearColor C = Carry;
			switch (E->Type)
			{
			case 7:   // full RGBA: (minR,minG,minB,minA, maxR..)
				C.R = R.FRandRange(E->Vals[0], E->Vals[4]);
				C.G = R.FRandRange(E->Vals[1], E->Vals[5]);
				C.B = R.FRandRange(E->Vals[2], E->Vals[6]);
				C.A = R.FRandRange(E->Vals[3], E->Vals[7]);
				break;
			case 3: C.R = R.FRandRange(E->Vals[0], E->Vals[1]); break;
			case 4: C.G = R.FRandRange(E->Vals[0], E->Vals[1]); break;
			case 5: C.B = R.FRandRange(E->Vals[0], E->Vals[1]); break;
			case 6: C.A = R.FRandRange(E->Vals[0], E->Vals[1]); break;
			}
			Carry = C;
			K.Val = FVector4(C.R, C.G, C.B, C.A);
			Keys.Add(K);
		}
		Keys.Sort([](const FKey& A, const FKey& B) { return A.Time < B.Time; });
		P.KColor = Keys;
	}

	// Velocity: same merge for the per-axis scalars (engine axis Y → UE -Y).
	{
		TArray<FKey, TInlineAllocator<8>> Keys;
		FVector Carry = P.Vel;
		TArray<const FRoseFXEvent*> Sorted = Vel;
		Sorted.Sort([](const FRoseFXEvent& A, const FRoseFXEvent& B) { return A.T0 < B.T0; });
		for (const FRoseFXEvent* E : Sorted)
		{
			FKey K;
			K.Time = Rand.FRandRange(E->T0, E->T1);
			K.bFade = E->bFade;
			FVector V = Carry;
			switch (E->Type)
			{
			case 11:
				V.X = Rand.FRandRange(E->Vals[0], E->Vals[3]);
				V.Y = -Rand.FRandRange(E->Vals[1], E->Vals[4]);
				V.Z = Rand.FRandRange(E->Vals[2], E->Vals[5]);
				break;
			case 8:  V.X = Rand.FRandRange(E->Vals[0], E->Vals[1]); break;
			case 9:  V.Y = -Rand.FRandRange(E->Vals[0], E->Vals[1]); break;
			case 10: V.Z = Rand.FRandRange(E->Vals[0], E->Vals[1]); break;
			}
			Carry = V;
			K.Val = FVector4(V.X, V.Y, V.Z, 0.f);
			Keys.Add(K);
		}
		Keys.Sort([](const FKey& A, const FKey& B) { return A.Time < B.Time; });
		P.KVel = Keys;
	}
	++TotalSpawned;
}

void URoseParticleSeqComponent::TickComponent(float Dt, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(Dt, TickType, ThisTickFunction);
	if (bFinished)
		return;

	if (Delay > 0.f)
	{
		Delay -= Dt;
		if (Delay > 0.f)
			return;
	}

	// ── Emission (zz_particle_event_sequence::update): rate*dt with carry,
	//    capped by live slots and Loops*Num total lives. ──
	int32 LiveCount = 0;
	for (const FPart& P : Parts)
		if (P.bAlive) ++LiveCount;

	if (bEmitting)
	{
		static FRandomStream Rand(1337);
		EmitCarry += Rand.FRandRange(Seq.RateMin, Seq.RateMax) * Dt;
		int32 NumNew = (int32)EmitCarry;
		EmitCarry -= NumNew;
		if (Seq.Loops > 0)
			NumNew = FMath::Min(NumNew, Seq.Loops * Seq.NumParticles - TotalSpawned);
		for (FPart& P : Parts)
		{
			if (NumNew <= 0) break;
			if (P.bAlive) continue;
			SpawnParticle(P);
			--NumNew;
			++LiveCount;
		}
		if (Seq.Loops > 0 && TotalSpawned >= Seq.Loops * Seq.NumParticles)
			bEmitting = false;
	}

	// ── Simulate ──
	for (FPart& P : Parts)
	{
		if (!P.bAlive) continue;
		P.Age += Dt;
		if (P.Age >= P.Life) { P.bAlive = false; --LiveCount; continue; }

		const FVector4 Sz = SampleChannel(P.KSize, P.Age, FVector4(P.Size.X, P.Size.Y, 0, 0));
		P.Size = FVector2D(Sz.X, Sz.Y);
		const FVector4 Col = SampleChannel(P.KColor, P.Age, FVector4(1, 1, 1, 1));
		P.Color = FLinearColor(Col.X, Col.Y, Col.Z, Col.W);
		bool bHasVelKeys = false;
		const FVector4 V = SampleChannel(P.KVel, P.Age,
			FVector4(P.Vel.X, P.Vel.Y, P.Vel.Z, 0), &bHasVelKeys);
		if (bHasVelKeys)
			P.Vel = FVector(V.X, V.Y, V.Z);
		P.Frame = SampleChannel(P.KFrame, P.Age, FVector4(P.Frame, 0, 0, 0)).X;
		if (P.bUseAngle)
			P.AngleDeg = SampleChannel(P.KAngle, P.Age, FVector4(0, 0, 0, 0)).X;

		P.Vel += P.Gravity * Dt;
		P.Pos += P.Vel * Dt;
	}

	WriteInstances();

	if (!bEmitting && LiveCount == 0)
	{
		bFinished = true;
		SetComponentTickEnabled(false);
	}
}

void URoseParticleSeqComponent::WriteInstances()
{
	// Camera basis for billboarding.
	FVector CamFwd = FVector(1, 0, 0);
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager)
			CamFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();

	const FTransform CompTM = GetComponentTransform();
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		const FPart& P = Parts[i];
		if (!P.bAlive)
		{
			UpdateInstanceTransform(i, FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)),
				/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
			continue;
		}

		// World position: local particles ride the component (follow), world
		// particles stay put; align-1 quads also honor the emitter's rotation.
		const FVector WPos = (Seq.Coord == 0) ? P.Pos : CompTM.TransformPosition(P.Pos);

		FQuat Face;
		switch (Seq.Align)
		{
		case 1:   // world-aligned: flat quad (engine identity = facing up)
			Face = CompTM.GetRotation();
			break;
		case 2:   // billboard around Z only
		{
			FVector Flat = -CamFwd; Flat.Z = 0.f;
			Face = FRotationMatrix::MakeFromZ(Flat.IsNearlyZero() ? FVector(1, 0, 0) : Flat).ToQuat();
			break;
		}
		default:  // full camera billboard: plane +Z looks at the camera
			Face = FRotationMatrix::MakeFromZ(-CamFwd).ToQuat();
			break;
		}
		if (P.bUseAngle && P.AngleDeg != 0.f)
			Face = Face * FQuat(FVector::UpVector, FMath::DegreesToRadians(P.AngleDeg));

		// Plane.Plane is 100x100 cm; particle Size is HALF extents.
		const FVector Scale(P.Size.X * 2.f / 100.f, P.Size.Y * 2.f / 100.f, 1.f);
		UpdateInstanceTransform(i, FTransform(Face, WPos, Scale),
			/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);

		SetCustomDataValue(i, 0, P.Color.R, false);
		SetCustomDataValue(i, 1, P.Color.G, false);
		SetCustomDataValue(i, 2, P.Color.B, false);
		SetCustomDataValue(i, 3, P.Color.A, false);
		SetCustomDataValue(i, 4, FMath::Floor(P.Frame), false);
	}
	MarkRenderStateDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  ARoseEffect
// ─────────────────────────────────────────────────────────────────────────────

ARoseEffect::ARoseEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

ARoseEffect* ARoseEffect::SpawnById(UWorld* World, int32 EffectId,
	const FVector& Location, AActor* Follow, float MaxLifeSeconds, bool bForceLink)
{
	const FRoseFXDef* Def = RoseFX_GetDef(EffectId);
	if (!World || !Def || Def->Emitters.Num() == 0)
		return nullptr;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARoseEffect* FX = World->SpawnActor<ARoseEffect>(
		ARoseEffect::StaticClass(), Location, FRotator::ZeroRotator, SP);
	if (!FX)
		return nullptr;
	FX->MaxLife = MaxLifeSeconds;

	if (Follow)
		FX->AttachToActor(Follow, FAttachmentTransformRules::KeepWorldTransform);

	for (const FRoseFXEmitterEntry& E : Def->Emitters)
		for (const FRoseFXSequence& S : E.Sequences)
		{
			URoseParticleSeqComponent* C = NewObject<URoseParticleSeqComponent>(FX);
			C->SetupAttachment(FX->GetRootComponent());
			C->SetRelativeLocation(E.Pos);
			C->SetRelativeRotation(E.Rot);
			C->RegisterComponent();
			// Unlinked emitters stay where the effect was spawned even if the
			// effect root follows an actor.  (bForceLink overrides — status
			// loops must move with the character.)
			if (!E.bLink && !bForceLink)
			{
				C->SetUsingAbsoluteLocation(true);
				C->SetUsingAbsoluteRotation(true);
				C->SetWorldLocation(Location + E.Pos);
			}
			C->Setup(S, E.DelaySec);
			FX->Emitters.Add(C);
		}
	return FX;
}

void ARoseEffect::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	bool bAllDone = true;
	for (const URoseParticleSeqComponent* C : Emitters)
		if (C && !C->IsFinished()) { bAllDone = false; break; }
	// MaxLife is the failsafe for endless-loop sequences (auras etc.).
	if (bAllDone || Age >= MaxLife)
		Destroy();
}
