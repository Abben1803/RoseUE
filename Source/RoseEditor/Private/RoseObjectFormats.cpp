#include "RoseObjectFormats.h"

#include "RoseBinaryReader.h"
#include "RoseEditor.h"

namespace
{
	// STB and ZSC/ZMS use different string encodings; keep both explicit.
	FString ReadStringU16(FRoseBinaryReader& R)
	{
		const int32 Len = R.U16();
		return R.FixedStr(Len);
	}

	FString ReadNullStr(FRoseBinaryReader& R)
	{
		FString Out;
		while (!R.AtEnd())
		{
			const uint8 C = R.U8();
			if (C == 0) break;
			Out.AppendChar((TCHAR)C);
		}
		return Out;
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  STB
// ═══════════════════════════════════════════════════════════════════════════
int32 FRoseSTB::FindCol(const FString& HeaderName) const
{
	for (int32 c = 0; c < Headers.Num(); ++c)
		if (Headers[c].Equals(HeaderName, ESearchCase::IgnoreCase))
			return c;
	return INDEX_NONE;
}

bool FRoseSTB::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("STB missing: %s"), *Path);
		return false;
	}

	const FString Magic = R.FixedStr(4);
	if (!Magic.StartsWith(TEXT("STB")))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("STB %s: bad magic '%s'"), *Path, *Magic);
		return false;
	}

	const int32 DataOffset = (int32)R.U32();
	const int32 RowCount = (int32)R.U32();
	const int32 ColCount = (int32)R.U32();

	// Stored counts include the header row / root column.
	Rows = FMath::Max(0, RowCount - 1);
	Cols = FMath::Max(0, ColCount - 1);
	if (Rows <= 0 || Cols <= 0)
		return false;

	// Column names.  Layout is rose-tools/rose-lib/src/files/stb.rs:
	//   row_height u32 | root_col_width u16 | col_width u16 * col_count
	//   root_col_name str16 | col_name str16 * (col_count - 1)
	//   unknown str16 | row_name str16 * (row_count - 1)
	// then the data cells at data_offset.
	//
	// root_col_name names the ROW-NAME column, which Cells drops — so the
	// (col_count - 1) names that follow it align 1:1 with our columns.  Read
	// them, then jump to the data; the row names are of no use here.
	R.U32();                                  // row height
	R.U16();                                  // root column width
	for (int32 c = 0; c < ColCount; ++c)
		R.U16();                              // per-column widths
	ReadStringU16(R);                         // root column name (the row-name column)

	Headers.SetNum(Cols);
	for (int32 c = 0; c < Cols; ++c)
		Headers[c] = ReadStringU16(R);

	R.Seek(DataOffset);

	Cells.SetNum(Rows);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		Cells[Row].SetNum(Cols);
		for (int32 Col = 0; Col < Cols; ++Col)
			Cells[Row][Col] = ReadStringU16(R);
	}

	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ZMS
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
	int32 UvChannelCount(uint32 VF)
	{
		int32 N = 0;
		if (VF & ROSE_VF_UV0) ++N;
		if (VF & ROSE_VF_UV1) ++N;
		if (VF & ROSE_VF_UV2) ++N;
		if (VF & ROSE_VF_UV3) ++N;
		return N;
	}
}

bool FRoseZMS::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
		return false;

	const FString Magic = ReadNullStr(R);
	bool bWide = false;      // v5/v6 prefix every record with a u32 index
	if (Magic == TEXT("ZMS0005") || Magic == TEXT("ZMS0006"))
	{
		Version = (Magic == TEXT("ZMS0005")) ? 5 : 6;
		bWide = true;
	}
	else if (Magic == TEXT("ZMS0007") || Magic == TEXT("ZMS0008"))
	{
		Version = (Magic == TEXT("ZMS0007")) ? 7 : 8;
	}
	else
	{
		UE_LOG(LogRoseImport, Warning, TEXT("ZMS %s: unknown magic '%s'"), *Path, *Magic);
		return false;
	}

	VertexFormat = R.U32();
	R.Vec3();      // bounds min
	R.Vec3();      // bounds max

	// The ZMS bone table: a vertex's blend index points HERE, and this maps it
	// to the real ZMD bone.
	const int32 NumBones = bWide ? (int32)R.U32() : (int32)R.U16();
	BoneTable.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		if (bWide)
		{
			R.U32();                                  // record index
			BoneTable.Add((int32)R.U32());
		}
		else
		{
			BoneTable.Add((int32)R.U16());
		}
	}

	const int32 NumVerts = bWide ? (int32)R.U32() : (int32)R.U16();
	if (NumVerts <= 0 || NumVerts > 1000000)
		return false;

	Positions.SetNumUninitialized(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i)
	{
		if (bWide) R.U32();
		Positions[i] = R.Vec3();
	}

	if (VertexFormat & ROSE_VF_NORMAL)
	{
		Normals.SetNumUninitialized(NumVerts);
		for (int32 i = 0; i < NumVerts; ++i)
		{
			if (bWide) R.U32();
			Normals[i] = R.Vec3();
		}
	}

	if (VertexFormat & ROSE_VF_COLOR)
	{
		for (int32 i = 0; i < NumVerts; ++i)
		{
			if (bWide) R.U32();
			R.F32(); R.F32(); R.F32(); R.F32();
		}
	}

	if ((VertexFormat & ROSE_VF_BLEND_WEIGHT) && (VertexFormat & ROSE_VF_BLEND_INDEX))
	{
		Skin.SetNum(NumVerts);
		for (int32 i = 0; i < NumVerts; ++i)
		{
			if (bWide) R.U32();
			FRoseSkinVertex& S = Skin[i];
			for (int32 w = 0; w < 4; ++w)
				S.Weights[w] = R.F32();
			for (int32 b = 0; b < 4; ++b)
				S.Bones[b] = bWide ? (int32)R.U32() : (int32)R.U16();
		}
	}

	if (VertexFormat & ROSE_VF_TANGENT)
	{
		for (int32 i = 0; i < NumVerts; ++i)
		{
			if (bWide) R.U32();
			R.Vec3();
		}
	}

	// UV channels are stored one WHOLE channel at a time, not interleaved.
	const int32 NumUV = UvChannelCount(VertexFormat);
	for (int32 Ch = 0; Ch < NumUV; ++Ch)
	{
		TArray<FVector2f>* Target = nullptr;
		if (Ch == 0) { UV0.SetNumUninitialized(NumVerts); Target = &UV0; }
		else if (Ch == 1) { UV1.SetNumUninitialized(NumVerts); Target = &UV1; }

		for (int32 i = 0; i < NumVerts; ++i)
		{
			if (bWide) R.U32();
			const float U = R.F32();
			const float V = R.F32();
			if (Target) (*Target)[i] = FVector2f(U, V);
		}
	}

	const int32 NumFaces = bWide ? (int32)R.U32() : (int32)R.U16();
	if (NumFaces < 0 || NumFaces > 2000000)
		return false;

	Faces.SetNumUninitialized(NumFaces);
	for (int32 i = 0; i < NumFaces; ++i)
	{
		if (bWide) R.U32();
		const int32 A = bWide ? (int32)R.U32() : (int32)R.U16();
		const int32 B = bWide ? (int32)R.U32() : (int32)R.U16();
		const int32 C = bWide ? (int32)R.U32() : (int32)R.U16();
		Faces[i] = FIntVector(A, B, C);
	}

	// ── material-id groups (this is what drives eye-blinking) ─────────────────
	//
	// After the face list the ZMS stores num_matids, then (mat_index, numfaces)
	// pairs.  zz_mesh_tool.cpp takes the LAST group's face count as
	// num_clip_faces, and the D3D renderer then draws a sub-range of the index
	// buffer:
	//   CLIP_FACE_FIRST (eyes OPEN)   start=3N, count-=N  -> skips the FIRST N
	//   CLIP_FACE_LAST  (eyes CLOSED) start=0,  count-=N  -> skips the LAST N
	// So a face mesh carries BOTH eye states: the first N triangles are the
	// closed eyes and the last N are the open ones.  Without this count the two
	// states render on top of each other and blinking is impossible.
	if (!R.HasOverrun())
	{
		const int32 NumMatIds = bWide ? (int32)R.U32() : (int32)R.U16();
		if (NumMatIds > 0 && NumMatIds < 4096)
		{
			int32 LastGroupFaces = 0;
			for (int32 i = 0; i < NumMatIds; ++i)
			{
				if (bWide) R.U32();                       // record index
				R.U32();                                  // material index
				LastGroupFaces = (int32)R.U32();          // faces in this group
			}
			if (!R.HasOverrun() && LastGroupFaces > 0 && LastGroupFaces * 2 <= Faces.Num())
				NumClipFaces = LastGroupFaces;
		}
	}

	// The matid table is optional/trailing — never let a short read invalidate
	// geometry that already parsed.
	return Positions.Num() > 0 && Faces.Num() > 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ZSC
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
	constexpr uint8 SWITCH_NULL = 0;
	constexpr uint8 SWITCH_POS = 1;
	constexpr uint8 SWITCH_ROT = 2;
	constexpr uint8 SWITCH_SCALE = 3;
	constexpr uint8 SWITCH_ROTAXIS = 4;
	constexpr uint8 SWITCH_BONEIDX = 5;
	constexpr uint8 SWITCH_DUMMYIDX = 6;
	constexpr uint8 SWITCH_PARENT = 7;
	constexpr uint8 SWITCH_ANI = 8;
	constexpr uint8 MAX_MESH_ANI_TYPE = 21;
	constexpr uint8 SWITCH_COLLISION = SWITCH_ANI + MAX_MESH_ANI_TYPE;   // 29
	constexpr uint8 SWITCH_CNST_ANI = SWITCH_COLLISION + 1;              // 30
	constexpr uint8 SWITCH_RANGE_SET = SWITCH_COLLISION + 2;             // 31
	constexpr uint8 SWITCH_BUSE_LIGHTMAP = SWITCH_COLLISION + 3;         // 32

	// The part payload is a tag/length/value stream terminated by tag 0.  Each
	// value is skipped by its declared LENGTH, so unknown tags cost nothing.
	FRoseZscPart ReadPartTags(FRoseBinaryReader& R)
	{
		FRoseZscPart Part;
		while (!R.AtEnd())
		{
			const uint8 Tag = R.U8();
			if (Tag == SWITCH_NULL)
				break;
			const uint8 Len = R.U8();
			const int64 End = R.Tell() + Len;

			switch (Tag)
			{
			case SWITCH_POS:
				Part.Position = R.Vec3();
				break;
			case SWITCH_ROT:
			{
				// ZSC quaternions are W FIRST.
				const float W = R.F32(), X = R.F32(), Y = R.F32(), Z = R.F32();
				Part.Rotation = FQuat4f(X, Y, Z, W);
				break;
			}
			case SWITCH_SCALE:
				Part.Scale = R.Vec3();
				break;
			case SWITCH_PARENT:
				Part.ParentIdx = (int32)R.I16() - 1;
				break;
			case SWITCH_BONEIDX:
				Part.BoneIdx = R.I16();
				break;
			case SWITCH_DUMMYIDX:
				Part.DummyIdx = R.I16();
				break;
			case SWITCH_COLLISION:
				// UNSIGNED: it is a packed flag word (zz_collision_level), and
				// bit 6 (NOTCAMERACOLLISION) upward would read negative as I16.
				Part.CollisionLevel = (int32)R.U16();
				break;
			case SWITCH_CNST_ANI:
				Part.CnstAnimFile = R.FixedStr(Len);
				break;
			case SWITCH_RANGE_SET:
				Part.RangeSet = R.I16();
				break;
			case SWITCH_BUSE_LIGHTMAP:
				Part.bUseLightmap = R.I16() != 0;
				break;
			default:
				break;
			}

			R.Seek(End);
		}
		return Part;
	}
}

bool FRoseZSC::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("ZSC missing: %s"), *Path);
		return false;
	}

	const int32 MeshCount = R.U16();
	MeshFiles.Reserve(MeshCount);
	for (int32 i = 0; i < MeshCount; ++i)
		MeshFiles.Add(ReadNullStr(R));

	const int32 MatCount = R.U16();
	Materials.Reserve(MatCount);
	for (int32 i = 0; i < MatCount; ++i)
	{
		FRoseZscMaterial M;
		M.TexturePath = ReadNullStr(R);
		M.bSkin = R.I16() != 0;
		M.bAlpha = R.I16() != 0;
		M.bTwoSided = R.I16() != 0;
		M.AlphaTest = R.I16();
		M.AlphaRef = R.I16();
		// z_write comes BEFORE z_test (rose-lib zsc.rs) — swapping them makes
		// every transparent object sort wrong.
		M.ZWrite = R.I16();
		M.ZTest = R.I16();
		M.BlendType = R.I16();
		M.Specular = R.I16();
		M.AlphaValue = R.F32();
		M.GlowType = R.I16();
		R.Vec3();      // glow colour
		Materials.Add(M);
	}

	const int32 EffectCount = R.U16();
	EffectFiles.SetNum(EffectCount);
	for (int32 i = 0; i < EffectCount; ++i)
		EffectFiles[i] = ReadNullStr(R);

	const int32 ObjectCount = R.U16();
	Objects.SetNum(ObjectCount);
	for (int32 i = 0; i < ObjectCount; ++i)
	{
		R.U32();        // cylinder radius
		R.I32();        // cylinder x
		R.I32();        // cylinder y

		const int32 PartCount = R.U16();
		// Empty objects stop here — no parts, no dummies, no bbox.
		if (PartCount == 0)
			continue;

		Objects[i].Parts.Reserve(PartCount);
		for (int32 p = 0; p < PartCount; ++p)
		{
			const int32 MeshId = R.U16();
			const int32 MatId = R.U16();
			FRoseZscPart Part = ReadPartTags(R);
			Part.MeshId = MeshId;
			Part.MaterialId = MatId;
			Objects[i].Parts.Add(Part);
		}

		// Effect sockets.  These used to be parsed and thrown away; they are how
		// ROSE marks a street lamp's bulb (type 1 DayNight / 2 LightContainer),
		// so the map importer can place real lights without guessing from names.
		// The tag block is the same one the parts use, so ReadPartTags gives us
		// the socket transform.
		const int32 ObjEffectCount = R.U16();
		for (int32 d = 0; d < ObjEffectCount; ++d)
		{
			FRoseZscEffect Fx;
			Fx.EffectId = (int32)R.U16();
			Fx.EffectType = (int32)R.U16();

			const FRoseZscPart Tags = ReadPartTags(R);
			Fx.Position = Tags.Position;
			Fx.Rotation = Tags.Rotation;
			Fx.Scale = Tags.Scale;
			Fx.ParentIdx = Tags.ParentIdx;

			Objects[i].Effects.Add(Fx);
		}

		R.Vec3();       // bbox min
		R.Vec3();       // bbox max
	}

	return MeshFiles.Num() > 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  IFO
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
	FString ReadPascalU8(FRoseBinaryReader& R)
	{
		const int32 Len = R.U8();
		return R.FixedStr(Len);
	}

	FRoseIfoObject ReadObjectData(FRoseBinaryReader& R)
	{
		FRoseIfoObject O;
		O.Name = ReadPascalU8(R);
		O.WarpId = R.I16();
		O.EventId = R.I16();
		O.ObjectType = R.I32();
		O.ObjectId = R.I32();
		O.MapX = R.I32();
		O.MapY = R.I32();
		// IFO quaternions are XYZW (NOT the ZSC W-first order).
		{
			const float X = R.F32(), Y = R.F32(), Z = R.F32(), W = R.F32();
			O.Rotation = FQuat4f(X, Y, Z, W);
		}
		{
			const FVector3f P = R.Vec3();
			// File positions are zone-centred; make them absolute world units.
			O.Position = FVector3f(P.X + kRoseCenterWorld, P.Y + kRoseCenterWorld, P.Z);
		}
		O.Scale = R.Vec3();
		return O;
	}
}

bool FRoseIFO::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
		return false;

	const int32 BlockCount = (int32)R.U32();
	if (BlockCount <= 0 || BlockCount > 64)
		return false;

	TArray<TPair<int32, int32>> BlockList;
	BlockList.Reserve(BlockCount);
	for (int32 i = 0; i < BlockCount; ++i)
	{
		const int32 Type = (int32)R.U32();
		const int32 Offset = (int32)R.U32();
		BlockList.Emplace(Type, Offset);
	}

	for (const TPair<int32, int32>& Block : BlockList)
	{
		R.Seek(Block.Value);
		const int32 Type = Block.Key;

		if (Type == ROSE_IFO_MAPINFO)
		{
			continue;   // map/zone indices, unused here
		}

		if (Type == ROSE_IFO_OCEAN)
		{
			R.F32();                             // patch size
			const int32 Count = (int32)R.U32();
			for (int32 i = 0; i < Count && i < 4096; ++i)
			{
				// Ocean corners are stored (x, HEIGHT, y) — NOT the (x, y, height)
				// that ObjectData above uses.  Same exception the ZON event
				// objects have.  Measured: across JPT01's 45 patches the MIDDLE
				// component is the constant one (-800 everywhere) while the first
				// and third vary and form the rectangles.
				//
				// Reading it as (x, y, height) like an object makes every patch
				// zero-height in plan view — start and end share the same middle
				// value — so the rectangle collapses, is rejected as degenerate,
				// and the zone silently gets no water at all.
				FRoseOceanBlock B;
				const FVector3f S = R.Vec3();
				const FVector3f E = R.Vec3();
				B.Start = FVector3f(S.X + kRoseCenterWorld, S.Z + kRoseCenterWorld, S.Y);
				B.End = FVector3f(E.X + kRoseCenterWorld, E.Z + kRoseCenterWorld, E.Y);
				Ocean.Add(B);
			}
			continue;
		}

		if (Type == ROSE_IFO_WATER)
		{
			// Per-cell water grid — not used; the OCEAN block carries the
			// surfaces we actually draw.
			const int32 Count = (int32)R.U32();
			for (int32 i = 0; i < Count; ++i)
			{
				const int32 W = (int32)R.U32();
				const int32 H = (int32)R.U32();
				for (int32 c = 0; c < W * H; ++c) { R.U8(); R.F32(); }
			}
			continue;
		}

		const int32 Count = (int32)R.U32();
		if (Count < 0 || Count > 65536)
			continue;

		TArray<FRoseIfoObject>& Out = Blocks.FindOrAdd(Type);
		Out.Reserve(Out.Num() + Count);

		for (int32 i = 0; i < Count; ++i)
		{
			FRoseIfoObject O = ReadObjectData(R);

			switch (Type)
			{
			case ROSE_IFO_NPC:
				O.NpcAi = R.I32();
				O.NpcCon = ReadPascalU8(R);
				break;

			case ROSE_IFO_SOUND:
				ReadPascalU8(R);   // file
				R.I32();           // range
				R.I32();           // interval
				break;

			case ROSE_IFO_EFFECT:
				ReadPascalU8(R);   // effect file
				break;

			case ROSE_IFO_MONSTER:
			{
				O.Name = ReadPascalU8(R);     // spawn point name
				const int32 BasicCount = (int32)R.U32();
				for (int32 b = 0; b < BasicCount && b < 64; ++b)
				{
					FRoseIfoSpawnEntry E;
					E.Name = ReadPascalU8(R);
					E.NpcId = (int32)R.U32();
					E.Count = (int32)R.U32();
					O.SpawnBasic.Add(E);
				}
				const int32 TacticCount = (int32)R.U32();
				for (int32 t = 0; t < TacticCount && t < 64; ++t)
				{
					FRoseIfoSpawnEntry E;
					E.Name = ReadPascalU8(R);
					E.NpcId = (int32)R.U32();
					E.Count = (int32)R.U32();
					O.SpawnTactic.Add(E);
				}
				O.SpawnInterval = (int32)R.U32();       // seconds
				O.SpawnLimit = (int32)R.U32();
				O.SpawnRange = (int32)R.U32();          // metres
				O.SpawnTacticPoints = (int32)R.U32();
				break;
			}

			case ROSE_IFO_EVENT_OBJECT:
				ReadPascalU8(R);   // function
				ReadPascalU8(R);   // file
				break;

			default:
				break;
			}

			Out.Add(MoveTemp(O));
		}
	}

	return true;
}
