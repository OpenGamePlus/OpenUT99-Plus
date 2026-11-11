"""Meshes: Mesh (vertex animation), LodMesh, SkeletalMesh, Animation. Spec: docs/formats/mesh.md"""
import struct

from upkg import Reader, lazy_bytes
from uobjects import register


def unpack_vert(v):
    """FMeshVert: X 11 bits, Y 11 bits, Z 10 bits, all signed."""
    x, y, z = v & 0x7FF, (v >> 11) & 0x7FF, v >> 22
    return (x - 0x800 if x & 0x400 else x, y - 0x800 if y & 0x400 else y, z - 0x400 if z & 0x200 else z)


def _unpack(r, fmt):
    v = struct.unpack_from("<" + fmt, r.data, r.pos)
    r.pos += struct.calcsize("<" + fmt)
    return v


def _array(r, item):
    return [item(r) for _ in range(r.index())]


def _fixed(r, fmt):
    return _array(r, lambda r: _unpack(r, fmt))


def _prim(r):
    box = _unpack(r, "6fB")
    return {"box": box, "sphere": _unpack(r, "4f")}


def _seq(r, names):
    s = {"name": names[r.index()], "group": names[r.index()]}
    s["start"], s["frames"] = _unpack(r, "ii")
    s["notifys"] = _array(r, lambda r: (_unpack(r, "f")[0], names[r.index()]))
    (s["rate"],) = _unpack(r, "f")
    return s


@register("Mesh")
def read_mesh(pkg, data, export, pos, props):
    r, ver, names = Reader(data, pos), pkg["version"], pkg["names"]
    m = {"primitive": _prim(r)}
    m["verts"] = [unpack_vert(v) for (v,) in struct.iter_unpack("<I", lazy_bytes(r, ver, 4))]  # anim_frames x frame_verts
    m["tris"] = list(struct.iter_unpack("<3H6BIi", lazy_bytes(r, ver, 20)))  # vertices, (u, v) x3, flags, texture index
    m["anim_seqs"] = _array(r, lambda r: _seq(r, names))
    m["connects"] = list(struct.iter_unpack("<ii", lazy_bytes(r, ver, 8)))
    m["bounds"] = _prim(r)
    m["vert_links"] = [v for (v,) in struct.iter_unpack("<i", lazy_bytes(r, ver, 4))]
    m["textures"] = _array(r, Reader.index)
    m["bounding_boxes"] = _fixed(r, "6fB")
    m["bounding_spheres"] = _fixed(r, "4f")
    m["frame_verts"], m["anim_frames"], m["and_flags"], m["or_flags"] = _unpack(r, "iiII")
    m["scale"], m["origin"], m["rot_origin"] = _unpack(r, "3f"), _unpack(r, "3f"), _unpack(r, "3i")
    m["cur_poly"], m["cur_vertex"] = _unpack(r, "ii")
    m["texture_lod"] = [v for (v,) in _fixed(r, "f")]
    return m, r.pos


def _lod(r, m):
    m["collapse_points"] = [v for (v,) in _fixed(r, "H")]
    m["face_level"] = [v for (v,) in _fixed(r, "H")]
    m["faces"] = _fixed(r, "4H")  # wedge x3, material index
    m["collapse_wedges"] = [v for (v,) in _fixed(r, "H")]
    m["wedges"] = _fixed(r, "HBB")  # vertex, u, v
    m["materials"] = _fixed(r, "Ii")  # poly flags, texture index
    m["special_faces"] = _fixed(r, "4H")
    (m["model_verts"], m["special_verts"], m["mesh_scale_max"], m["lod_hysteresis"], m["lod_strength"],
     m["lod_min_verts"], m["lod_morph"], m["lod_z_displace"], m["lod_high_quality"]) = _unpack(r, "iifffifff")
    m["remap_anim_verts"] = [v for (v,) in _fixed(r, "H")]


@register("LodMesh")
def read_lod_mesh(pkg, data, export, pos, props):
    m, end = read_mesh(pkg, data, export, pos, props)
    r = Reader(data, end)
    _lod(r, m)
    return m, r.pos


def _bone(r, names):
    b = {"name": names[r.index()]}
    (b["flags"],) = _unpack(r, "I")
    b["orientation"], b["position"] = _unpack(r, "4f"), _unpack(r, "3f")
    b["length"], b["size_x"], b["size_y"], b["size_z"], b["children"], b["parent"] = _unpack(r, "4fii")
    return b


@register("SkeletalMesh")
def read_skeletal_mesh(pkg, data, export, pos, props):
    m, end = read_lod_mesh(pkg, data, export, pos, props)
    r, names = Reader(data, end), pkg["names"]
    m["ext_wedges"] = _fixed(r, "HHff")  # vertex, flags, u, v
    m["points"] = _fixed(r, "3f")
    m["ref_skeleton"] = _array(r, lambda r: _bone(r, names))
    m["bone_weight_index"] = _fixed(r, "4H")
    m["bone_weights"] = _fixed(r, "HH")  # point, weight
    m["local_points"] = _fixed(r, "3f")
    (m["skeletal_depth"],) = _unpack(r, "i")
    m["default_animation"] = r.index()
    (m["weapon_bone"],) = _unpack(r, "i")
    m["weapon_adjust"] = _unpack(r, "12f")
    return m, r.pos


def _track(r):
    (flags,) = _unpack(r, "I")
    return {"flags": flags, "key_quat": _fixed(r, "4f"), "key_pos": _fixed(r, "3f"), "key_time": [t for (t,) in _fixed(r, "f")]}


def _move(r):
    mv = {"root_speed": _unpack(r, "3f")}
    mv["track_time"], mv["start_bone"], mv["flags"] = _unpack(r, "fiI")
    mv["bone_indices"] = [v for (v,) in _fixed(r, "i")]
    mv["tracks"] = _array(r, _track)
    mv["root_track"] = _track(r)
    return mv


@register("Animation")
def read_animation(pkg, data, export, pos, props):
    r, names = Reader(data, pos), pkg["names"]
    a = {"ref_bones": _array(r, lambda r: (names[r.index()],) + _unpack(r, "Ii"))}  # name, flags, parent
    a["moves"] = _array(r, _move)
    a["anim_seqs"] = _array(r, lambda r: _seq(r, names))
    return a, r.pos


def lod_mesh_obj(m, frame=0):
    """Wavefront OBJ text of one animation frame of a LodMesh."""
    n = m["frame_verts"]
    lines = ["v %d %d %d" % v for v in m["verts"][frame * n:(frame + 1) * n]]
    first = m["special_verts"] + 1  # special (weapon) verts come first in each frame; OBJ is 1-based
    lines += ["f %d %d %d" % tuple(m["wedges"][w][0] + first for w in face[:3]) for face in m["faces"]]
    return "\n".join(lines) + "\n"


def main(argv):
    import sys
    from upkg import parse
    from uobjects import read_object
    if len(argv) not in (5, 6) or argv[3] != "--obj":
        sys.exit("Usage: umesh.py FILE LODMESH --obj OUT.obj [FRAME]")
    with open(argv[1], "rb") as f:
        data = f.read()
    pkg = parse(data)
    e = next(e for e in pkg["exports"] if e["name"] == argv[2] and e["size"])
    with open(argv[4], "w") as f:
        f.write(lod_mesh_obj(read_object(pkg, data, e)["value"], int(argv[5]) if len(argv) == 6 else 0))


if __name__ == "__main__":
    import sys
    main(sys.argv)
