#include "RoseMapAnimManager.h"

#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoseMapAnim, Log, All);

namespace
{
	// Same math as mapforge/export_map.py::compose — column-vector convention,
	// quat (x,y,z,w) inputs building a standard rotation matrix.
	FMatrix RoseCompose(const FVector3d& P, const FVector4d& QWXYZ, const FVector3d& S)
	{
		double qw = QWXYZ.X, qx = QWXYZ.Y, qy = QWXYZ.Z, qz = QWXYZ.W;
		const double N = FMath::Sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
		if (N > 1e-12) { qx /= N; qy /= N; qz /= N; qw /= N; }

		// Column-major math matrix A: columns are transformed basis vectors.
		double A[4][4] = {
			{1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw),     2 * (qx * qz + qy * qw),     0},
			{2 * (qx * qy + qz * qw),     1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw),     0},
			{2 * (qx * qz - qy * qw),     2 * (qy * qz + qx * qw),     1 - 2 * (qx * qx + qy * qy), 0},
			{0, 0, 0, 1}};
		for (int r = 0; r < 3; ++r)
		{
			A[r][0] *= S.X; A[r][1] *= S.Y; A[r][2] *= S.Z;
		}
		A[0][3] = P.X; A[1][3] = P.Y; A[2][3] = P.Z;

		// To UE FMatrix (row-vector convention): rows = images of the axes,
		// i.e. FMatrix[r][c] = A[c][r].
		FMatrix M;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				M.M[r][c] = A[c][r];
		return M;
	}

	// ROSE world (Z-up, cm, +Y as exported) -> UE world: mirror Y on both
	// sides. In row-vector FMatrix terms M_ue = S * M * S with S=diag(1,-1,1,1)
	// (S is symmetric so the convention flip does not matter).
	FMatrix RoseToUE(const FMatrix& M)
	{
		FMatrix R = M;
		for (int i = 0; i < 4; ++i)
		{
			R.M[1][i] = -R.M[1][i];
			R.M[i][1] = -R.M[i][1];
		}
		return R;
	}

	// Interchange baked the scene meshes' vertices at x100 in glTF axes and
	// compensates on every actor with scale 0.01 + a -90° X rotation (probe:
	// static actors sit at rot(-90,0,0), scale 0.01). The animated actor's
	// final transform must keep that mesh-space correction or parts render
	// 100x too big: A = MeshCorr * WorldUE  (row-vector; derivation:
	// C = 0.01 * S*P^-1*S = RotX(-90)*0.01 with P = rose->glTF axis map).
	const FMatrix& MeshCorrection()
	{
		static const FMatrix C(
			FPlane(0.01, 0, 0, 0),
			FPlane(0, 0, 0.01, 0),
			FPlane(0, -0.01, 0, 0),
			FPlane(0, 0, 0, 1));
		return C;
	}

	bool ParseMatrix16(const TArray<TSharedPtr<FJsonValue>>& Arr, FMatrix& Out)
	{
		if (Arr.Num() != 16)
		{
			return false;
		}
		// JSON stores column-major flatten (order='F') of the math matrix:
		// element k = A[k%4][k/4]  ->  FMatrix[r][c] = A[c][r] = flat[r*4+c]...
		// careful: flat[k] = A[row=k%4][col=k/4]; FMatrix[r][c] = A[c][r]
		// = flat[c%4 + r*4] with c as math-row: FMatrix[r][c] = flat[r*4 + c].
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				Out.M[r][c] = Arr[r * 4 + c]->AsNumber();
		return true;
	}
}

ARoseMapAnimManager::ARoseMapAnimManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

bool ARoseMapAnimManager::LoadZoneJson()
{
	FString Zone = ZoneKey;
	if (Zone.IsEmpty())
	{
		Zone = GetWorld()->GetMapName();
		Zone.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
		Zone.RemoveFromStart(TEXT("L_"));
	}
	const FString Path = FPaths::ProjectContentDir() / TEXT("MapAnims") / Zone + TEXT(".json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogRoseMapAnim, Log, TEXT("no anim json for zone %s (%s)"), *Zone, *Path);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Root) || !Root.IsValid())
	{
		UE_LOG(LogRoseMapAnim, Warning, TEXT("bad anim json %s"), *Path);
		return false;
	}

	const TSharedPtr<FJsonObject>* Anims;
	if (Root->TryGetObjectField(TEXT("anims"), Anims))
	{
		for (const auto& Pair : (*Anims)->Values)
		{
			const TSharedPtr<FJsonObject> A = Pair.Value->AsObject();
			if (!A.IsValid())
			{
				continue;
			}
			FRoseZmoClip Clip;
			Clip.Fps = A->GetNumberField(TEXT("fps"));
			Clip.Frames = (int32)A->GetNumberField(TEXT("frames"));
			const TArray<TSharedPtr<FJsonValue>>* Arr;
			if (A->TryGetArrayField(TEXT("pos"), Arr))
			{
				for (const auto& V : *Arr)
				{
					const auto& F = V->AsArray();
					if (F.Num() == 3)
					{
						Clip.Pos.Emplace(F[0]->AsNumber(), F[1]->AsNumber(), F[2]->AsNumber());
					}
				}
			}
			if (A->TryGetArrayField(TEXT("rot"), Arr))
			{
				for (const auto& V : *Arr)
				{
					const auto& F = V->AsArray();
					if (F.Num() == 4)
					{
						Clip.Rot.Emplace(F[0]->AsNumber(), F[1]->AsNumber(), F[2]->AsNumber(), F[3]->AsNumber());
					}
				}
			}
			if (A->TryGetArrayField(TEXT("scale"), Arr))
			{
				for (const auto& V : *Arr)
				{
					Clip.Scale.Add(V->AsNumber());
				}
			}
			if (Clip.Frames > 0)
			{
				const int32 ClipIdx = Clips.Add(MoveTemp(Clip));
				ClipIndexByPath.Add(FString(Pair.Key), ClipIdx);
			}
		}
	}

	// tag -> binding template
	TMap<FString, FRoseAnimBinding> ByTag;
	const TArray<TSharedPtr<FJsonValue>>* Actors;
	if (Root->TryGetArrayField(TEXT("actors"), Actors))
	{
		for (const auto& V : *Actors)
		{
			const TSharedPtr<FJsonObject> A = V->AsObject();
			if (!A.IsValid())
			{
				continue;
			}
			FRoseAnimBinding B;
			const int32* Ci = ClipIndexByPath.Find(A->GetStringField(TEXT("anim")));
			if (!Ci)
			{
				continue;
			}
			B.ClipIndex = *Ci;
			const TArray<TSharedPtr<FJsonValue>>* Arr;
			if (!A->TryGetArrayField(TEXT("parent"), Arr) || !ParseMatrix16(*Arr, B.ParentRose))
			{
				continue;
			}
			if (A->TryGetArrayField(TEXT("pos"), Arr) && Arr->Num() == 3)
			{
				B.StaticPos = FVector3d((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
			}
			if (A->TryGetArrayField(TEXT("rot"), Arr) && Arr->Num() == 4)
			{
				B.StaticRot = FVector4d((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber(), (*Arr)[3]->AsNumber());
			}
			if (A->TryGetArrayField(TEXT("scl"), Arr) && Arr->Num() == 3)
			{
				B.StaticScl = FVector3d((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
			}
			ByTag.Add(A->GetStringField(TEXT("tag")), B);
		}
	}
	if (ByTag.IsEmpty())
	{
		return false;
	}

	int32 Found = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (const FRoseAnimBinding* Tmpl = ByTag.Find(Tag.ToString()))
			{
				FRoseAnimBinding B = *Tmpl;
				B.Actor = *It;
				if (USceneComponent* RootC = It->GetRootComponent())
				{
					RootC->SetMobility(EComponentMobility::Movable);
				}
				Bindings.Add(B);
				++Found;
				break;
			}
		}
	}
	UE_LOG(LogRoseMapAnim, Log, TEXT("zone %s: %d clips, bound %d/%d tagged actors"),
		*Zone, Clips.Num(), Found, ByTag.Num());

	// Math self-check: the frame-0 pose should land close to the actor's
	// editor-saved static placement (exactly equal when the ZMO's first frame
	// matches the ZSC static local transform).
	for (int32 i = 0; i < Bindings.Num() && i < 3; ++i)
	{
		const FRoseAnimBinding& B = Bindings[i];
		const FRoseZmoClip& Clip = Clips[B.ClipIndex];
		FVector3d P = (Clip.Pos.Num() == Clip.Frames && Clip.Frames > 0) ? Clip.Pos[0] : B.StaticPos;
		FVector4d Q = (Clip.Rot.Num() == Clip.Frames && Clip.Frames > 0) ? Clip.Rot[0] : B.StaticRot;
		FVector3d S = (Clip.Scale.Num() == Clip.Frames && Clip.Frames > 0)
			? FVector3d(Clip.Scale[0], Clip.Scale[0], Clip.Scale[0]) : B.StaticScl;
		const FTransform Pred(MeshCorrection() * RoseToUE(RoseCompose(P, Q, S) * B.ParentRose));
		const FTransform Cur = B.Actor->GetActorTransform();
		const double DLoc = FVector::Dist(Pred.GetLocation(), Cur.GetLocation());
		const double DRot = Pred.GetRotation().AngularDistance(Cur.GetRotation()) * 180.0 / PI;
		const double DScl = (Pred.GetScale3D() - Cur.GetScale3D()).GetAbsMax();
		UE_LOG(LogRoseMapAnim, Log, TEXT("selfcheck %s: frame0 dLoc=%.2f dRot=%.1fdeg dScale=%.4f (pred scale %.4f, placed %.4f)"),
			*B.Actor->GetName(), DLoc, DRot, DScl, Pred.GetScale3D().X, Cur.GetScale3D().X);
	}
	return Found > 0;
}

void ARoseMapAnimManager::BeginPlay()
{
	Super::BeginPlay();
	if (!LoadZoneJson())
	{
		SetActorTickEnabled(false);
	}
}

void ARoseMapAnimManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PlayTime += DeltaSeconds;

	// movement proof in logs: first binding's location, every ~2 s for 10 s
	if (PlayTime < 10.0 && Bindings.Num() > 0 && Bindings[0].Actor.IsValid())
	{
		static double NextLog = 0.0;
		if (PlayTime >= NextLog)
		{
			NextLog = PlayTime + 2.0;
			const FVector L = Bindings[0].Actor->GetActorLocation();
			UE_LOG(LogRoseMapAnim, Log, TEXT("t=%.1f %s at (%.1f %.1f %.1f)"),
				PlayTime, *Bindings[0].Actor->GetName(), L.X, L.Y, L.Z);
		}
	}

	for (const FRoseAnimBinding& B : Bindings)
	{
		AActor* Actor = B.Actor.Get();
		if (!Actor || !Clips.IsValidIndex(B.ClipIndex))
		{
			continue;
		}
		const FRoseZmoClip& Clip = Clips[B.ClipIndex];
		const double F = FMath::Fmod(PlayTime * Clip.Fps, (double)Clip.Frames);
		const int32 F0 = (int32)F;
		const int32 F1 = (F0 + 1) % Clip.Frames;
		const double A = F - F0;

		FVector3d P = B.StaticPos;
		if (Clip.Pos.Num() == Clip.Frames)
		{
			P = FMath::Lerp(Clip.Pos[F0], Clip.Pos[F1], A);
		}
		FVector4d Q = B.StaticRot;
		if (Clip.Rot.Num() == Clip.Frames)
		{
			// slerp via FQuat (FQuat is (x,y,z,w); ours is (w,x,y,z))
			const FVector4d& Ra = Clip.Rot[F0];
			const FVector4d& Rb = Clip.Rot[F1];
			FQuat Qa(Ra.Y, Ra.Z, Ra.W, Ra.X);
			FQuat Qb(Rb.Y, Rb.Z, Rb.W, Rb.X);
			Qa.Normalize(); Qb.Normalize();
			const FQuat Qi = FQuat::Slerp(Qa, Qb, A);
			Q = FVector4d(Qi.W, Qi.X, Qi.Y, Qi.Z);
		}
		FVector3d S = B.StaticScl;
		if (Clip.Scale.Num() == Clip.Frames)
		{
			const double U = FMath::Lerp(Clip.Scale[F0], Clip.Scale[F1], A);
			S = FVector3d(U, U, U);
		}

		const FMatrix LocalRose = RoseCompose(P, Q, S);
		// Row-vector convention: world = local * parent.
		const FMatrix WorldUE = MeshCorrection() * RoseToUE(LocalRose * B.ParentRose);
		Actor->SetActorTransform(FTransform(WorldUE), false, nullptr, ETeleportType::TeleportPhysics);
	}
}
