"""Measure the original engine with our probe (ADR 0008).

    python3 tools/probe/calibrate.py MAPGEN_DIR steps|ramps [--angles A,B,...] [--seconds S] [--no-build]

MAPGEN_DIR is a ut99-mapgen checkout set up as in DEVELOPMENT_GUIDE: ut/ holds a copy of the install with the
OldUnreal Linux patch that matches it (the server we measure); make-469e/ holds a System with the v469e Linux patch,
only to compile the probe (the 469f-rc5 Linux `ucc make` recurses forever into "Classes/./"). The runner compiles
tools/probe/OUProbe there (the package lands in ~/.utpg/System, where the server finds it), builds the calibration
map with mapgen, runs a dedicated server with the probe mutator and reads the probe lines from its log.

Each calibration is a map of lanes; a ProbePawn walks +x along each one toward an obstacle of growing rise. Reports,
per lane, the obstacle, its rise and how high the pawn got: a pawn that gets over it is lifted by its rise.
"steps": steps of growing height. "ramps": true slopes (wedges, 192 units long) of growing angle.
"""
import math
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEP_HEIGHTS = [16, 24, 28, 29, 30, 31, 32, 40]
RAMP_ANGLES = [30, 40, 44, 46, 50, 55, 60]
RAMP_RUN = 192
LANE_Y0, LANE_GAP = -1050, 300
FLOOR = 0
MAPS = {"steps": "OU-Steps", "ramps": "OU-Ramps"}
PROBE_RE = re.compile(r"PROBE (\S+) (" + r"\S+ " * 7 + r"\S+)")


def parse_log(text):
    """Probe lines -> {pawn name: [(t, x, y, z, physics, vx, vy, vz)]}, in log order."""
    tracks = {}
    for m in PROBE_RE.finditer(text):
        name, rest = m.group(1), m.group(2).split()
        if name == "spawnfail":
            continue
        values = [float(v) for v in rest]
        values[4] = int(values[4])
        tracks.setdefault(name, []).append(tuple(values))
    return tracks


def lane_of(y):
    """Lane index of a y coordinate (nearest lane centre)."""
    return round((y - LANE_Y0) / LANE_GAP)


def step_report(tracks, walk_from=0.5):
    """{lane: (rest z before walking, highest z while walking)} for the steps map."""
    out = {}
    for samples in tracks.values():
        before = [s for s in samples if s[0] <= walk_from]
        if not before:
            continue
        rest_z = before[-1][3]
        top = max([s[3] for s in samples if s[0] > walk_from] or [rest_z])
        out[lane_of(samples[0][2])] = (rest_z, top)
    return out


def wedge(x0, x1, y0, y1, z0, z1, texture, name):
    """A mapgen brush rising along +x from (x0, z0) to (x1, z1): a true slope, not stairs."""
    from t3d import Brush, Poly
    a, b, c, d = (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)
    e, f = (x1, y0, z1), (x1, y1, z1)
    polys = []
    for verts in ([a, b, c, d], [c, f, e, d], [a, e, f, b], [a, d, e], [b, f, c]):
        u = [verts[1][k] - verts[0][k] for k in range(3)]
        v = [verts[2][k] - verts[1][k] for k in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        length = math.sqrt(sum(x * x for x in n))
        n = tuple(x / length for x in n)  # outward, by the winding (mapgen's box convention)
        tu = (0, 1, 0) if abs(n[0]) > 0.5 else (1, 0, 0)
        tv = tuple(n[(k + 1) % 3] * tu[(k + 2) % 3] - n[(k + 2) % 3] * tu[(k + 1) % 3] for k in range(3))
        polys.append(Poly(verts=verts, normal=n, texture_u=tu, texture_v=tv, texture=texture))
    return Brush(polys=polys, name=name, csg="CSG_Add")


def rises(kind):
    if kind == "steps":
        return [(f"{h}", h) for h in STEP_HEIGHTS]
    return [(f"{a} deg", RAMP_RUN * math.tan(math.radians(a))) for a in RAMP_ANGLES]


def build_lanes(mapgen, kind):
    sys.path.insert(0, os.path.join(mapgen, "tools"))
    from build import Map
    from t3d import box

    m = Map(MAPS[kind])
    lanes = rises(kind)
    y_top = LANE_Y0 + LANE_GAP * len(lanes)
    m.add(box(-512, LANE_Y0 - 350, FLOOR, 2048, y_top + 100, FLOOR + 512,
              floor="UTtech1.bmFloor", walls="UTtech1.bmwall3", ceiling="UTtech1.bmFloor"))
    for i, (_, rise) in enumerate(lanes):
        y = LANE_Y0 + i * LANE_GAP
        if kind == "steps":
            m.add(box(256, y - 100, FLOOR, 1024, y + 100, FLOOR + rise, texture="UTtech1.bmTrim", csg="CSG_Add",
                      name=f"Lane{i}"))
        else:
            m.add(wedge(256, 256 + RAMP_RUN, y - 100, y + 100, FLOOR, FLOOR + rise, "UTtech1.bmTrim", f"Lane{i}"))
        m.actor("PathNode", 0, y, FLOOR + 60)  # the probe spawns here and walks +x
    m.player_start(-300, y_top - 20, floor_z=FLOOR)
    m.actor("Light", 768, 0, FLOOR + 450, LightBrightness=200, LightRadius=64)
    m.build(validate=False)


def compile_probe(mapgen):
    make = os.path.join(mapgen, "make-469e")
    system = os.path.join(make, "System64")
    dest = os.path.join(make, "OUProbe")
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, "OUProbe"), dest)
    ini = os.path.join(system, "Probe.ini")
    if not os.path.exists(ini):  # the game's ini with only the packages the probe needs
        text = open(os.path.join(make, "System", "UnrealTournament.ini"), encoding="latin-1").read()
        text = re.sub(r"(?m)^EditPackages=.*\r?\n", "", text)
        packages = "".join(f"EditPackages={p}\r\n" for p in ("Core", "Engine", "OUProbe"))
        text = re.sub(r"(?m)^\[Editor\.EditorEngine\]\r?\n", lambda m: m.group(0) + packages, text, count=1)
        open(ini, "w", encoding="latin-1", newline="").write(text)
    out = os.path.expanduser("~/.utpg/System/OUProbe.u")
    if os.path.exists(out):
        os.remove(out)
    log = subprocess.run(["./ucc-bin-amd64", "make", "-ini=Probe.ini"], cwd=system, capture_output=True, text=True,
                         timeout=600, stdin=subprocess.DEVNULL)
    if "Success - 0 error(s)" not in log.stdout or not os.path.exists(out):
        raise SystemExit("ucc make failed:\n" + "\n".join((log.stdout + log.stderr).splitlines()[-20:]))


def run_server(game, map_name, seconds, log_path):
    url = f"{map_name}?game=Botpack.DeathMatchPlus?mutator=OUProbe.ProbeMutator"
    server = subprocess.Popen(["./ucc-bin-amd64", "server", url, f"-log={log_path}"], cwd=os.path.join(game, "System"),
                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        server.wait(timeout=seconds)
    except subprocess.TimeoutExpired:  # a dedicated server runs until stopped; SIGTERM lets it flush its log
        server.terminate()
        server.wait(timeout=30)
    return open(log_path, encoding="latin-1").read()


def main(argv):
    if len(argv) < 3 or argv[2] not in MAPS:
        raise SystemExit(__doc__)
    mapgen, kind = os.path.abspath(argv[1]), argv[2]
    game = os.path.join(mapgen, "ut")
    seconds = float(argv[argv.index("--seconds") + 1]) if "--seconds" in argv else 12
    if "--angles" in argv:
        RAMP_ANGLES[:] = [float(a) for a in argv[argv.index("--angles") + 1].split(",")]
    if "--no-build" not in argv:
        compile_probe(mapgen)
        build_lanes(mapgen, kind)
    log_path = os.path.join(mapgen, "work", f"probe-{kind}.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    tracks = parse_log(run_server(game, MAPS[kind], seconds, log_path))
    if not tracks:
        raise SystemExit(f"no probe lines in {log_path}")
    lanes = rises(kind)
    print("lane      rise  rest z  top z  over")
    for lane, (rest_z, top) in sorted(step_report(tracks).items()):
        label, rise = lanes[lane] if 0 <= lane < len(lanes) else ("?", 0)
        print(f"{label:>7} {rise:6.1f}  {rest_z:6.1f}  {top:5.1f}  {'yes' if rise and top - rest_z > rise - 2 else 'no'}")


if __name__ == "__main__":
    main(sys.argv)
