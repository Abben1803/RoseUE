"""ue5_probe_gear_master.py — dump the BUILT M_RoseGearBlend graph.

The runtime readback proves the MID is correct (right slot, CellOffset param
exists, correct value stored) yet all wings render identically.  That isolates
the fault to the master's UV math not actually consuming CellOffset.  This
prints the real, saved graph connectivity so we stop guessing from the build
script and see what was actually compiled.

Headless, editor CLOSED, -nullrhi fine.
"""
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary


def log(m):
    unreal.log(f"[probe] {m}")


for name in ("M_RoseGearBlend", "M_RoseGear"):
    path = f"/Game/Characters/Gear/{name}"
    m = EAL.load_asset(path)
    if not m:
        log(f"{name}: MISSING")
        continue
    log(f"===== {name} =====")

    # Parameters actually exposed (what a MID can override).
    try:
        names_scalar = ME.get_scalar_parameter_names(m)
        names_vector = ME.get_vector_parameter_names(m)
        names_tex = ME.get_texture_parameter_names(m)
        log(f"scalar params : {names_scalar}")
        log(f"vector params : {names_vector}")
        log(f"texture params: {names_tex}")
    except Exception as e:
        log(f"param name query failed: {e}")

    # Expression list + each node's inputs (what feeds what).  In UE5 the graph
    # lives in editor-only data, and Material.Expressions is protected.
    exprs = []
    try:
        eod = m.get_editor_only_data()
        exprs = list(eod.get_editor_property("expression_collection").get_editor_property("expressions"))
    except Exception as e1:
        try:
            exprs = list(m.get_editor_only_data().get_editor_property("expressions"))
        except Exception as e2:
            log(f"could not read expressions: {e1} / {e2}")
    log(f"expression count: {len(exprs)}")
    for e in exprs:
        cls = type(e).__name__
        extra = ""
        if isinstance(e, unreal.MaterialExpressionScalarParameter):
            extra = f" name={e.get_editor_property('parameter_name')} default={e.get_editor_property('default_value')}"
        elif isinstance(e, unreal.MaterialExpressionVectorParameter):
            extra = f" name={e.get_editor_property('parameter_name')} default={e.get_editor_property('default_value')}"
        elif isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            t = e.get_editor_property("texture")
            extra = f" name={e.get_editor_property('parameter_name')} tex={unreal.SystemLibrary.get_object_name(t) if t else None}"
        elif isinstance(e, unreal.MaterialExpressionConstant2Vector):
            extra = f" ({e.get_editor_property('r')},{e.get_editor_property('g')})"
        elif isinstance(e, unreal.MaterialExpressionComponentMask):
            extra = f" R={e.get_editor_property('r')} G={e.get_editor_property('g')} B={e.get_editor_property('b')} A={e.get_editor_property('a')}"
        log(f"  <{cls}>{extra}")

    # What is connected to the texture sampler's UV input?  Walk inputs via the
    # generic get_inputs / get_input API where available.
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            try:
                coord = e.get_editor_property("coordinates")
                src = coord.get_editor_property("expression")
                log(f"  AtlasTex.UVs <- {type(src).__name__ if src else 'NOTHING (uses mesh UV0 directly)'}")
            except Exception as ex:
                log(f"  UV-input probe failed: {ex}")

log("[probe] DONE")
