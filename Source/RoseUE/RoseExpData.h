// RoseExpData — the ROSE experience curve.  NeedExp(L) is the XP required to
// advance FROM level L to L+1, transcribed from CCal::Get_NeedRawEXP
// (src/common/calculation.cpp).  MaxLevel = GameStaticConfig::MAX_LEVEL (88),
// so only the first three bands are reachable — the higher bands are kept for
// faithfulness / future cap raises.
#pragma once

#include "CoreMinimal.h"

namespace RoseExp
{
	inline constexpr int32 MaxLevel = 250;

	inline int64 NeedExp(int32 Level)
	{
		if (Level < 1) Level = 1;
		if (Level > MaxLevel) Level = MaxLevel;
		const int64 L = Level;
		if (Level <= 15)  return (int64)((L + 3) * (L + 5) * (L + 10) * 0.7f);
		if (Level <= 60)  return (int64)((L - 5) * (L + 2) * (L + 2) * 2.2f);
		if (Level <= 113) return (int64)((L - 11) * (L) * (L + 4) * 2.5f);
		if (Level <= 150) return (int64)((L - 31) * (L - 20) * (L + 4) * 3.8f);
		if (Level <= 189) return (int64)((L - 67) * (L - 20) * (L - 10) * 6.f);
		return (L - 90) * (L - 120) * (L - 60) * (L - 170) * (L - 188);
	}
}
