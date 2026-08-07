// RoseUITheme — the single source of truth for the modern ROSE UI palette.
// Every modern UI file (HUD, windows, chat, minimap, inventory, options, …)
// pulls its colours from here, so re-theming is a one-file change.
//
// Current theme: DARK GREY / LIGHT GREY (easier on the eyes than the old dark
// blue).  Accent is a neutral light grey rather than a saturated hue.
#pragma once

#include "Math/Color.h"

namespace RoseTheme
{
	// Surfaces (dark greys).
	inline const FLinearColor Panel   (0.105f, 0.107f, 0.115f, 0.93f);  // window background
	inline const FLinearColor PanelHi (0.170f, 0.175f, 0.190f, 0.96f);  // raised panel / badge
	inline const FLinearColor Title   (0.145f, 0.150f, 0.160f, 0.98f);  // title bar
	inline const FLinearColor Row     (0.150f, 0.155f, 0.170f, 0.85f);  // list row
	inline const FLinearColor Track   (0.050f, 0.052f, 0.058f, 0.90f);  // gauge track / dark inset
	inline const FLinearColor Slot    (0.085f, 0.088f, 0.095f, 0.92f);  // item/equip slot
	inline const FLinearColor Preview (0.070f, 0.072f, 0.080f, 1.00f);  // paperdoll backdrop

	// Tabs.
	inline const FLinearColor TabOn   (0.320f, 0.330f, 0.365f, 0.98f);  // selected (medium grey)
	inline const FLinearColor TabOff  (0.140f, 0.145f, 0.160f, 0.85f);  // unselected

	// Accent + text (light greys).
	inline const FLinearColor Accent  (0.560f, 0.575f, 0.615f, 1.00f);  // borders / highlights
	inline const FLinearColor Text    (0.910f, 0.910f, 0.925f, 1.00f);  // primary
	inline const FLinearColor TextDim (0.590f, 0.600f, 0.635f, 1.00f);  // secondary
	inline const FLinearColor Dim     (0.400f, 0.410f, 0.440f, 1.00f);  // disabled

	// Semantic / status.
	inline const FLinearColor HP      (0.780f, 0.255f, 0.280f, 1.00f);
	inline const FLinearColor MP      (0.340f, 0.480f, 0.660f, 1.00f);  // muted steel blue
	inline const FLinearColor EXP     (0.830f, 0.700f, 0.320f, 1.00f);
	inline const FLinearColor Gold    (0.850f, 0.720f, 0.320f, 1.00f);
	inline const FLinearColor Green   (0.360f, 0.660f, 0.420f, 1.00f);  // learn / ON
	inline const FLinearColor Off     (0.300f, 0.310f, 0.340f, 1.00f);  // OFF pill
	inline const FLinearColor Button  (0.200f, 0.210f, 0.240f, 0.95f);  // action button
	inline const FLinearColor Danger  (0.520f, 0.200f, 0.220f, 0.95f);  // exit / destructive
}
