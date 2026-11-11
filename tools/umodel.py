"""Level geometry: Polys (editor brushes), Model (BSP, surfaces, lightmaps, zones), Level (actor list, URL, paths).

Spec: docs/formats/level.md
"""
import struct

from upkg import Reader
from uobjects import register


def _vec(r):
    v = struct.unpack_from("<3f", r.data, r.pos)
    r.pos += 12
    return v


def _box(r):
    lo, hi = _vec(r), _vec(r)
    valid = r.data[r.pos]
    r.pos += 1
    return {"min": lo, "max": hi, "valid": valid}


def _array(r, item):
    return [item(r) for _ in range(r.index())]


def _poly(r):
    n = r.index()
    p = {"base": _vec(r), "normal": _vec(r), "texture_u": _vec(r), "texture_v": _vec(r)}
    p["vertices"] = [_vec(r) for _ in range(n)]
    p["flags"] = r.u32()
    p["actor"], p["texture"], p["item_name"], p["link"], p["brush_poly"] = (r.index() for _ in range(5))
    p["pan"] = struct.unpack_from("<hh", r.data, r.pos)
    r.pos += 4
    return p


@register("Polys")
def read_polys(pkg, data, export, pos, props):
    r = Reader(data, pos)
    num = r.u32()
    r.u32()  # max (allocation size)
    return [_poly(r) for _ in range(num)], r.pos


def _node(r):
    plane = struct.unpack_from("<4f", r.data, r.pos)
    zone_mask, flags = struct.unpack_from("<QB", r.data, r.pos + 16)
    r.pos += 25
    n = {"plane": plane, "zone_mask": zone_mask, "flags": flags}
    for k in ("vert_pool", "surf", "back", "front", "plane_index", "collision_bound", "render_bound"):
        n[k] = r.index()
    n["zone"] = (r.data[r.pos], r.data[r.pos + 1])
    n["num_vertices"] = r.data[r.pos + 2]
    n["leaf"] = struct.unpack_from("<ii", r.data, r.pos + 3)
    r.pos += 11
    return n


def _surf(r):
    s = {"texture": r.index(), "flags": r.u32()}
    for k in ("base", "normal", "texture_u", "texture_v", "light_map", "brush_poly"):
        s[k] = r.index()  # base: index into points; normal, texture_u/v: into vectors; light_map: -1 = none
    s["pan"] = struct.unpack_from("<hh", r.data, r.pos)
    r.pos += 4
    s["actor"] = r.index()
    return s


def _lightmap(r):
    m = {"data_offset": r.i32(), "pan": _vec(r), "uclamp": r.index(), "vclamp": r.index()}
    m["uscale"], m["vscale"] = struct.unpack_from("<ff", r.data, r.pos)
    r.pos += 8
    m["light_actors"] = r.i32()
    return m


def _leaf(r):
    leaf = {"zone": r.index(), "permeating": r.index(), "volumetric": r.index()}
    (leaf["visible_zones"],) = struct.unpack_from("<Q", r.data, r.pos)
    r.pos += 8
    return leaf


@register("Model")
def read_model(pkg, data, export, pos, props):
    r = Reader(data, pos)
    m = {"bounding_box": _box(r), "bounding_sphere": struct.unpack_from("<4f", data, r.pos)}
    r.pos += 16
    m["vectors"] = _array(r, _vec)
    m["points"] = _array(r, _vec)
    m["nodes"] = _array(r, _node)
    m["surfs"] = _array(r, _surf)
    m["verts"] = _array(r, lambda r: (r.index(), r.index()))  # (point index, side)
    m["shared_sides"] = r.i32()
    m["zones"] = []
    for _ in range(r.i32()):
        zone_actor = r.index()
        connectivity, visibility = struct.unpack_from("<QQ", data, r.pos)
        r.pos += 16
        m["zones"].append({"actor": zone_actor, "connectivity": connectivity, "visibility": visibility})
    m["polys"] = r.index()
    m["lightmaps"] = _array(r, _lightmap)
    n = r.index()
    m["light_bits"] = data[r.pos:r.pos + n]
    r.pos += n
    m["bounds"] = _array(r, _box)
    m["leaf_hulls"] = _array(r, Reader.i32)
    m["leaves"] = _array(r, _leaf)
    m["lights"] = _array(r, Reader.index)
    m["root_outside"], m["linked"] = r.u32(), r.u32()
    return m, r.pos


def _reach_spec(r):
    s = {"distance": r.i32(), "start": r.index(), "end": r.index(),
         "collision_radius": r.i32(), "collision_height": r.i32(), "reach_flags": r.i32()}
    s["pruned"] = r.data[r.pos]
    r.pos += 1
    return s


@register("Level")
def read_level(pkg, data, export, pos, props):
    r = Reader(data, pos)
    num = r.u32()
    r.u32()  # max
    lv = {"actors": [r.index() for _ in range(num)]}  # 0 = deleted slot
    url = {k: r.string() for k in ("protocol", "host", "map", "portal")}
    url["options"] = [r.string() for _ in range(r.index())]
    url["port"], url["valid"] = r.i32(), r.i32()
    lv["url"] = url
    lv["model"] = r.index()
    lv["reach_specs"] = _array(r, _reach_spec)
    (lv["approx_time"],) = struct.unpack_from("<f", data, r.pos)
    r.pos += 4
    lv["first_deleted"] = r.index()
    lv["text_blocks"] = [r.index() for _ in range(16)]
    lv["travel_info"] = dict((r.string(), r.string()) for _ in range(r.index()))
    return lv, r.pos
