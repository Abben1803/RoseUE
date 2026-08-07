// ROSE chat log — a process-global, game-thread-only ring buffer of chat lines
// that any system (chat input, combat, system messages) can append to via
// FRoseChatLog::Add.  The chat window's content widget (RoseUIChat.cpp) polls
// Revision() each Tick and rebuilds when it changes.  Colours approximate the
// classic client palette (src/client/interface/it_mgr.cpp:120-129).
#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"

struct ROSEUE_API FRoseChatLog
{
	// Kept EXACTLY as specified — other feature files call FRoseChatLog::Add.
	enum class EKind : uint8 { System, Say, Combat, Whisper };

	// A single stored line: its kind, text, and resolved display colour.
	struct FLine
	{
		EKind Kind = EKind::System;
		FString Text;
		FLinearColor Color = FLinearColor::White;
	};

	// Append a line (game thread only).  Bumps the revision counter.
	static void Add(EKind Kind, const FString& Text);

	// Monotonic counter — bumped on every Add.  The chat widget compares this
	// against its last-seen value in Tick to decide whether to rebuild.
	static int32 Revision();

	// Copy the current buffer (oldest → newest) into Out.
	static void GetLines(TArray<FLine>& Out);

	// The client palette colour for a kind (say=white, system=pale yellow,
	// combat=red-ish, whisper=pink).
	static FLinearColor ColorFor(EKind Kind);
};
