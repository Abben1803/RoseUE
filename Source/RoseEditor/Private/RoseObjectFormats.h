// STB / ZMS / ZSC / IFO — everything the map needs beyond terrain.
//
// Transcribed from tools/rose_parser/formats/{stb,zms,zsc,ifo}.py, which are
// validated against src/client/io_model.cpp, src/engine/src/zz_mesh_tool.cpp
// and rose-tools/rose-lib.  Local source is the authority (CLAUDE.md).
#pragma once

#include "CoreMinimal.h"

// ── STB: the game database ─────────────────────────────────────────────────
//   char[4] "STB1", u32 data_offset, u32 row_count(+1), u32 col_count(+1),
//   editor-hint gap, then string_u16 cells row-major at data_offset.
// Only cells are read.  NOTE the indexing convention: the game's column 0 is
// the first DATA column, i.e. the row-id column is not counted.
struct FRoseSTB
{
	int32 Rows = 0;
	int32 Cols = 0;
	TArray<TArray<FString>> Cells;   // [row][col], game-style columns

	// Column names, ALIGNED to Cells columns: Headers[c] names Cells[..][c].
	//
	// The file stores col_count headers, the first of which names the ROW-NAME
	// column that Cells deliberately drops — so the on-disk header k+1 names our
	// column k.  That shift is applied once here, at load, rather than at every
	// call site, because getting it wrong reads a neighbouring column and
	// silently produces plausible garbage.
	TArray<FString> Headers;

	const FString& Get(int32 Row, int32 Col) const
	{
		static const FString Empty;
		if (!Cells.IsValidIndex(Row) || !Cells[Row].IsValidIndex(Col)) return Empty;
		return Cells[Row][Col];
	}

	// Numeric accessors.  An empty cell is the DEFAULT, not 0 parsed from "" —
	// ROSE leaves unused cells blank and Atoi("") would be indistinguishable
	// from a real 0.
	int32 GetInt(int32 Row, int32 Col, int32 Default = 0) const
	{
		const FString& V = Get(Row, Col);
		return V.IsEmpty() ? Default : FCString::Atoi(*V);
	}

	float GetFloat(int32 Row, int32 Col, float Default = 0.f) const
	{
		const FString& V = Get(Row, Col);
		return V.IsEmpty() ? Default : FCString::Atof(*V);
	}

	// Column index for a header name (case-insensitive), or INDEX_NONE.
	//
	// ALWAYS prefer this over a hard-coded index.  The `rose/io/stb.h` macros are
	// written for the CLASSIC layout and are stale for Arua — PAT_ATTACH_NODE is
	// col 67 there, which in the Arua table is "HP Guage".  A wrong column does
	// not fail, it just yields the wrong number.
	int32 FindCol(const FString& HeaderName) const;

	const FString& GetByName(int32 Row, const FString& HeaderName) const
	{
		static const FString Empty;
		const int32 Col = FindCol(HeaderName);
		return Col == INDEX_NONE ? Empty : Get(Row, Col);
	}

	int32 GetIntByName(int32 Row, const FString& HeaderName, int32 Default = 0) const
	{
		const int32 Col = FindCol(HeaderName);
		return Col == INDEX_NONE ? Default : GetInt(Row, Col, Default);
	}

	float GetFloatByName(int32 Row, const FString& HeaderName, float Default = 0.f) const
	{
		const int32 Col = FindCol(HeaderName);
		return Col == INDEX_NONE ? Default : GetFloat(Row, Col, Default);
	}

	bool Load(const FString& Path);
};

// ── ZMS: mesh geometry ─────────────────────────────────────────────────────
// Vertex format flags (src/engine/include/zz_vertex_format.h).
enum : uint32
{
	ROSE_VF_POSITION     = 1u << 1,
	ROSE_VF_NORMAL       = 1u << 2,
	ROSE_VF_COLOR        = 1u << 3,
	ROSE_VF_BLEND_WEIGHT = 1u << 4,
	ROSE_VF_BLEND_INDEX  = 1u << 5,
	ROSE_VF_TANGENT      = 1u << 6,
	ROSE_VF_UV0          = 1u << 7,
	ROSE_VF_UV1          = 1u << 8,
	ROSE_VF_UV2          = 1u << 9,
	ROSE_VF_UV3          = 1u << 10,
};

// Up to four influences per vertex, the ROSE limit.
struct FRoseSkinVertex
{
	int32 Bones[4] = { 0, 0, 0, 0 };     // indices into FRoseZMS::BoneTable
	float Weights[4] = { 0.f, 0.f, 0.f, 0.f };
};

struct FRoseZMS
{
	int32 Version = 0;
	uint32 VertexFormat = 0;
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<FVector2f> UV0;
	TArray<FVector2f> UV1;      // lightmap channel, when present
	TArray<FIntVector> Faces;

	// ── skinning ──
	// The ZMS carries its OWN small bone table; a vertex's blend index is an
	// index into THIS table, not into the skeleton.  BoneTable maps it to the
	// real ZMD bone.  Skipping that indirection binds every vertex to the wrong
	// bone — and bind pose hides it completely, because the skin resolves to
	// identity until something animates.
	TArray<int32> BoneTable;
	TArray<FRoseSkinVertex> Skin;

	// Triangles in the LAST material-id group.  Non-zero only on meshes that
	// carry two alternates at the ends of the index buffer — in practice the
	// FACE meshes, whose first N triangles are the closed eyes and last N the
	// open ones (see the loader).  0 = no alternates.
	int32 NumClipFaces = 0;

	bool HasNormals() const { return (VertexFormat & ROSE_VF_NORMAL) != 0; }
	bool HasUV1() const { return (VertexFormat & ROSE_VF_UV1) != 0; }
	bool HasSkin() const { return Skin.Num() > 0; }

	bool Load(const FString& Path);
};

// ── ZSC: scene container (meshes + materials + object part lists) ──────────
struct FRoseZscMaterial
{
	FString TexturePath;
	bool bSkin = false;
	bool bAlpha = false;
	bool bTwoSided = false;
	int32 AlphaTest = 0;
	int32 AlphaRef = 0;
	int32 ZTest = 1;
	int32 ZWrite = 1;
	int32 BlendType = 0;      // 3 = Lighten -> additive
	int32 Specular = 0;
	float AlphaValue = 1.f;
	int32 GlowType = 0;
};

struct FRoseZscPart
{
	int32 MeshId = -1;
	int32 MaterialId = -1;
	FVector3f Position = FVector3f::ZeroVector;
	// ZSC stores the quaternion W-FIRST (w,x,y,z); IFO stores it xyzw.  Mixing
	// them up silently rotates every object in the zone.
	FQuat4f Rotation = FQuat4f::Identity;
	FVector3f Scale = FVector3f(1.f, 1.f, 1.f);
	int32 ParentIdx = -1;
	// Rigid appearance parts (hair, face, caps, masks) are NOT skinned: they are
	// authored in the LOCAL space of a bone or a dummy and pinned to it.
	// Cap attaches to head DUMMY 6 (the top of the head), not the head bone —
	// which sits at the neck.
	int32 BoneIdx = -1;
	int32 DummyIdx = -1;
	// zz_collision_level (zz_type.h:437) — a PACKED word, not a plain level:
	//
	//   bits 0-2  SHAPE   0 NONE | 1 SPHERE | 2 AABB | 3 OBB | 4 POLYGON
	//   bit  3    NOTMOVEABLE
	//   bit  4    NOTPICKABLE           (no mouse picking)
	//   bit  5    HEIGHTONLY            (contributes height only, not blocking)
	//   bit  6    NOTCAMERACOLLISION    (camera passes through)
	//
	// The engine ALWAYS masks the shape out before testing it
	// (ZZ_IS_POLYGON_LEVEL(L) is ((L) & 0x7) == ZZ_CL_POLYGON), so "does this
	// collide" is `(Level & 0x7) != 0` — NOT `Level != 0`.  Testing the whole
	// word gives every no-shape part that happens to carry a flag (0x20
	// height-only, 0x50 non-pickable + no-camera) full solid collision, which is
	// what makes the player and camera snag on bushes and grass.
	int32 CollisionLevel = 0;

	// Shape bits only — 0 means the part must not collide at all.
	int32 CollisionShape() const { return CollisionLevel & 0x7; }
	bool  HasCollision() const   { return CollisionShape() != 0; }
	bool  IsNotPickable() const  { return (CollisionLevel & 0x10) != 0; }
	bool  IsHeightOnly() const   { return (CollisionLevel & 0x20) != 0; }
	bool  IsNoCameraCollision() const { return (CollisionLevel & 0x40) != 0; }
	FString CnstAnimFile;    // SWITCH_CNST_ANI — a ZMO the part loops at runtime
	int32 RangeSet = 0;
	bool bUseLightmap = false;
};

// An effect socket on a ZSC object — a lamp flame, a torch, a glow.
//
// ROSE tells us directly which of these are lights and which are night-only,
// so street lamps do NOT have to be recognised by name:
//     Type 0 = Normal          always-on effect
//     Type 1 = DayNight        only shows when it is dark
//     Type 2 = LightContainer  a light source
// (zsc.rs SceneEffectType).  The transform is the socket's local placement on
// the object, i.e. exactly where the bulb sits.
enum : int32
{
	ROSE_ZSC_EFFECT_NORMAL = 0,
	ROSE_ZSC_EFFECT_DAYNIGHT = 1,
	ROSE_ZSC_EFFECT_LIGHTCONTAINER = 2,
};

struct FRoseZscEffect
{
	int32 EffectId = -1;        // index into FRoseZSC::EffectFiles
	int32 EffectType = 0;       // ROSE_ZSC_EFFECT_*
	int32 ParentIdx = -1;

	FVector3f Position = FVector3f::ZeroVector;
	FQuat4f Rotation = FQuat4f::Identity;
	FVector3f Scale = FVector3f::OneVector;

	// A light for our purposes: an explicit light container, or a night-only
	// effect (which in practice is what ROSE hangs on street lamps).
	bool IsLight() const
	{
		return EffectType == ROSE_ZSC_EFFECT_LIGHTCONTAINER
			|| EffectType == ROSE_ZSC_EFFECT_DAYNIGHT;
	}

	bool IsNightOnly() const { return EffectType == ROSE_ZSC_EFFECT_DAYNIGHT; }
};

struct FRoseZscObject
{
	TArray<FRoseZscPart> Parts;
	TArray<FRoseZscEffect> Effects;
};

struct FRoseZSC
{
	TArray<FString> MeshFiles;
	TArray<FRoseZscMaterial> Materials;
	TArray<FString> EffectFiles;
	TArray<FRoseZscObject> Objects;

	bool Load(const FString& Path);
};

// ── IFO: per-chunk placement ───────────────────────────────────────────────
enum : int32
{
	ROSE_IFO_MAPINFO = 0,
	ROSE_IFO_OBJECT = 1,       // deco
	ROSE_IFO_NPC = 2,
	ROSE_IFO_BUILDING = 3,     // construction
	ROSE_IFO_SOUND = 4,
	ROSE_IFO_EFFECT = 5,
	ROSE_IFO_ANIMATION = 6,
	ROSE_IFO_WATER = 7,
	ROSE_IFO_MONSTER = 8,
	ROSE_IFO_OCEAN = 9,
	ROSE_IFO_WARP = 10,
	ROSE_IFO_COLLISION = 11,
	ROSE_IFO_EVENT_OBJECT = 12,
};

struct FRoseIfoSpawnEntry
{
	FString Name;
	int32 NpcId = 0;
	int32 Count = 0;
};

struct FRoseIfoObject
{
	FString Name;
	int32 WarpId = 0;
	int32 EventId = 0;
	int32 ObjectType = 0;
	int32 ObjectId = 0;
	int32 MapX = 0;
	int32 MapY = 0;
	FQuat4f Rotation = FQuat4f::Identity;   // file order is XYZW
	// Absolute ROSE world units: the file stores zone-centred values and
	// +520000 is added on both X and Y at read time (mapforge/rose_ifo.py).
	FVector3f Position = FVector3f::ZeroVector;
	FVector3f Scale = FVector3f(1.f, 1.f, 1.f);

	// NPC payload
	int32 NpcAi = 0;
	FString NpcCon;

	// MonsterSpawn payload (CRegenPOINT::Load order)
	TArray<FRoseIfoSpawnEntry> SpawnBasic;
	TArray<FRoseIfoSpawnEntry> SpawnTactic;
	int32 SpawnInterval = 0;     // seconds
	int32 SpawnLimit = 0;
	int32 SpawnRange = 0;        // metres
	int32 SpawnTacticPoints = 0;
};

struct FRoseOceanBlock
{
	FVector3f Start = FVector3f::ZeroVector;
	FVector3f End = FVector3f::ZeroVector;
};

struct FRoseIFO
{
	TMap<int32, TArray<FRoseIfoObject>> Blocks;
	TArray<FRoseOceanBlock> Ocean;

	const TArray<FRoseIfoObject>* Get(int32 BlockType) const { return Blocks.Find(BlockType); }

	bool Load(const FString& Path);
};

// World centring: 32 * 16000 + 8000.  IFO/ocean positions are stored relative
// to the zone centre.
constexpr float kRoseCenterWorld = 520000.f;
