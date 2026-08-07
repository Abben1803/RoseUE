"""Is MP_WorldPositionOffset actually CONNECTED on M_RoseChar?

Parameters registering proves only that the nodes exist — an unconnected output
pin leaves them inert and the slider does nothing.  This walks the material's
expression graph looking for the chain that should feed WPO.
"""
import unreal

EAL = unreal.EditorAssetLibrary
mat = EAL.load_asset("/Game/Rose/Characters/M_RoseChar")

# Where the expression list lives moved between engine versions; try both.
exprs = None
for prop in ("expressions", "editor_only_data"):
    try:
        v = mat.get_editor_property(prop)
    except Exception as e:
        print(f"[wpo] no '{prop}': {e}")
        continue
    if prop == "expressions":
        exprs = list(v)
        break
    try:
        exprs = list(v.get_editor_property("expressions"))
        break
    except Exception as e:
        print(f"[wpo] editor_only_data has no expressions: {e}")

if exprs is None:
    print("[wpo] could not reach the expression list from Python")
else:
    print(f"[wpo] {len(exprs)} expressions in the graph")
    kinds = {}
    for e in exprs:
        n = type(e).__name__
        kinds[n] = kinds.get(n, 0) + 1
    for k in sorted(kinds):
        print(f"[wpo]    {k} x{kinds[k]}")

    wanted = ["MaterialExpressionVertexNormalWS", "MaterialExpressionDistance",
              "MaterialExpressionSaturate", "MaterialExpressionWorldPosition",
              "MaterialExpressionOneMinus", "MaterialExpressionDivide"]
    missing = [w for w in wanted if w not in kinds]
    print("[wpo] all bulge nodes present" if not missing
          else f"[wpo] MISSING nodes: {missing}")

# The decisive bit: does the compiled material actually use WPO?
try:
    stats = unreal.MaterialEditingLibrary.get_statistics(mat)
    print(f"[wpo] stats: {stats}")
except Exception as e:
    print(f"[wpo] no statistics API: {e}")
