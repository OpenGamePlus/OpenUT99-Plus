"""Measure the original engine with our probe (ADR 0008).

    python3 tools/probe/calibrate.py MAPGEN_DIR steps|ramps|air|slide|entry|ledge|wall [--angles A,B,...] [--seconds S] [--no-build] [--ours UT_SCRIPT]

MAPGEN_DIR is a ut99-mapgen checkout set up as in DEVELOPMENT_GUIDE: ut/ holds a copy of the install with the
OldUnreal Linux patch that matches it (the server we measure); make-469e/ holds a System with the v469e Linux patch,
only to compile the probe (the 469f-rc5 Linux `ucc make` recurses forever into "Classes/./"). The runner compiles
tools/probe/OUProbe there (the package lands in ~/.utpg/System, where the server finds it), builds the calibration
map with mapgen, runs a dedicated server with the probe mutator and reads the probe lines from its log.
--ours runs the same probe in our engine too (`ut_script ut/ --map MAP?mutator=... S` with UTVM_LOG=1) and reports
both side by side.

Each calibration is a map of lanes; a ProbePawn walks +x along each one toward an obstacle of growing rise. Reports,
per lane, the obstacle, its rise and how high the pawn got: a pawn that gets over it is lifted by its rise.
"steps": steps of growing height. "ramps": true slopes (wedges, 192 units long) of growing angle.
"air": a ProbeFaller per lane, spawned 1,800 up with growing AirControl, falls while MoveTo pulls it forward;
reports its mean horizontal acceleration while falling and its top horizontal speed.
"slide": a ProbeDropper per lane dropped on a slope too steep to walk (wedges 400 high, --angles applies); reports
its steady acceleration along the slope while it slides down, against 950 sin(angle) (all of gravity along it).
"entry": ProbePawns walk into slopes too steep to walk (wedges 300 high) at several GroundSpeeds and angles; reports
the speed along the slope on the first ticks after the switch to falling and how high each gets (--angles: lanes
at those angles, all at 400).
"ledge": like steps, but too high to step, with bCanJump and the MoveTo goal on top of the ledge; reports whether
the pawn jumped, how high it got and where it ended up (on top or not). Lanes are (ledge height, JumpZ).
"wall": a wall 200 high and 32 thick across each lane (lanes are its width and the offset of its centre), the goal
straight behind it; reports what
PickWallAdjust (called from HitWall) returns, the Destination it leaves, and whether the pawn got past the wall.
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
AIR_CONTROLS = [0, 0.05, 0.2, 0.35, 0.6, 1.0]
AIR_Z = 1800
SLIDE_ANGLES = [46, 50, 60, 70, 80]
SLIDE_RISE = 400
GRAVITY = 950
ENTRY_LANES = [(46, 100), (46, 200), (46, 300), (46, 400), (46, 600), (55, 400), (65, 400), (80, 400)]
ENTRY_RISE = 300
LANE_Y0, LANE_GAP = -1050, 300
LEDGE_LANES = [(32, 325), (77, 325), (78, 325), (64, 250), (72, 250), (77, 250), (78, 250),
               (40, 400), (64, 400), (77, 400), (78, 400)]
WALL_LANES = [(40, 0), (46, 0), (48, 0), (46, -20), (46, 20)]  # (width, centre offset in y)
FLOOR = 0
MAPS = {"wall": "OU-Wall", "ledge": "OU-Ledge", "steps": "OU-Steps", "ramps": "OU-Ramps", "air": "OU-Air", "slide": "OU-Slide", "entry": "OU-Entry"}
PROBE_RE = re.compile(r"(?m)^ScriptLog: PROBE (\S+) (" + r"\S+ " * 7 + r"\S+)")


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
    if kind == "ledge":
        return [(f"{h} j{jz}", h) for h, jz in LEDGE_LANES]
    if kind == "wall":
        return [(f"{w}@{c:+g}", w) for w, c in WALL_LANES]
    if kind == "air":
        return [(f"air {c:g}", c) for c in AIR_CONTROLS]
    if kind == "slide":
        return [(f"{a:g} deg", a) for a in SLIDE_ANGLES]
    if kind == "entry":
        return [(f"{a:g}d {v}", a) for a, v in ENTRY_LANES]
    return [(f"{a} deg", RAMP_RUN * math.tan(math.radians(a))) for a in RAMP_ANGLES]


def air_report(tracks):
    """{lane: (horizontal acceleration while falling, top horizontal speed)} from the falling samples."""
    out = {}
    for samples in tracks.values():
        falling = [s for s in samples if s[4] == 2 and s[0] > 0.1]
        if len(falling) < 2:
            continue
        a, b = falling[0], falling[-1]
        speed = lambda s: (s[5] ** 2 + s[6] ** 2) ** 0.5
        out[lane_of(samples[0][2])] = ((speed(b) - speed(a)) / (b[0] - a[0]), max(speed(s) for s in falling))
    return out


def slide_report(tracks):
    """{lane: steady acceleration along the slope while sliding down}: the median of the per-tick speed gains over
    the samples moving back (vx < -1) above the slope's lower third, leaving out the first two (the landing)."""
    out = {}
    for samples in tracks.values():
        sliding = [s for s in samples if s[4] == 2 and s[5] < -1 and s[3] > SLIDE_RISE / 3][2:]
        if len(sliding) < 2:
            continue
        speed = lambda s: (s[5] ** 2 + s[6] ** 2 + s[7] ** 2) ** 0.5
        gains = sorted((speed(b) - speed(a)) / (b[0] - a[0]) for a, b in zip(sliding, sliding[1:]))
        out[lane_of(samples[0][2])] = gains[len(gains) // 2]
    return out


def entry_report(tracks):
    """{lane: (speed along the slope on the first three falling ticks after walking, highest z)}."""
    out = {}
    for samples in tracks.values():
        lane = lane_of(samples[0][2])
        if not 0 <= lane < len(ENTRY_LANES):
            continue
        angle = math.radians(ENTRY_LANES[lane][0])
        walked = [i for i, s in enumerate(samples) if s[0] > 0.5 and s[4] == 1]
        start = next((i for i in range(walked[0] if walked else len(samples), len(samples)) if samples[i][4] == 2), None)
        if start is None:
            continue
        along = [s[5] * math.cos(angle) + s[7] * math.sin(angle) for s in samples[start:start + 3]]
        out[lane] = (along, max(s[3] for s in samples))
    return out


def ledge_report(tracks, walk_from=0.5):
    """{lane: (rest z, jumped, top z, z over the ledge)}: jumped is whether the pawn left the floor upward after it began
    walking; z over the ledge is the lowest z once past its front edge (x > 256 + radius), None if it never got there."""
    out = {}
    for samples in tracks.values():
        before = [s for s in samples if s[0] <= walk_from]
        after = [s for s in samples if s[0] > walk_from]
        if not before or not after:
            continue
        rest_z = before[-1][3]
        jumped = any(s[4] == 2 and s[7] > 0 for s in after)
        past = [s[3] for s in after if 273 < s[1] < 1000]
        out[lane_of(samples[0][2])] = (rest_z, jumped, max(s[3] for s in after), min(past) if past else None)
    return out


ADJ_RE = re.compile(r"(?m)^ScriptLog: PROBEADJ (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)")


def wall_report(text):
    """{pawn name: (first PickWallAdjust result, Destination it left, x of that call, farthest x reached)}."""
    tracks = parse_log(text)
    out = {}
    for m in ADJ_RE.finditer(text):
        name = m.group(1)
        if name in out or name not in tracks:
            continue
        dest = tuple(float(v) for v in m.group(6).split(","))
        at = min(tracks[name], key=lambda s: abs(s[0] - float(m.group(2))))
        out[name] = (m.group(3) == "True", dest, at[1], max(s[1] for s in tracks[name]))
    return {lane_of(tracks[n][0][2]): v for n, v in out.items()}


def build_lanes(mapgen, kind):
    sys.path.insert(0, os.path.join(mapgen, "tools"))
    from build import Map
    from t3d import box

    m = Map(MAPS[kind])
    lanes = rises(kind)
    y_top = LANE_Y0 + LANE_GAP * len(lanes)
    height = {"air": 2048, "slide": 1024, "entry": 1024}.get(kind, 512)
    m.add(box(-512, LANE_Y0 - 350, FLOOR, 2048, y_top + 100, FLOOR + height,
              floor="UTtech1.bmFloor", walls="UTtech1.bmwall3", ceiling="UTtech1.bmFloor"))
    for i, (_, rise) in enumerate(lanes):
        y = LANE_Y0 + i * LANE_GAP
        if kind == "air":
            m.actor("PathNode", 0, y, FLOOR + AIR_Z, Tag=f"Air{round(rise * 100)}")
            continue
        if kind == "entry":
            angle, walk = ENTRY_LANES[i]
            run = ENTRY_RISE / math.tan(math.radians(angle))
            m.add(wedge(256, 256 + run, y - 100, y + 100, FLOOR, FLOOR + ENTRY_RISE, "UTtech1.bmTrim", f"Lane{i}"))
            m.actor("PathNode", 0, y, FLOOR + 60, Tag=f"Walk{walk}")
            continue
        if kind == "slide":  # rise is the angle; the dropper lands three quarters up the slope
            run = SLIDE_RISE / math.tan(math.radians(rise))
            m.add(wedge(256, 256 + run, y - 100, y + 100, FLOOR, FLOOR + SLIDE_RISE, "UTtech1.bmTrim", f"Lane{i}"))
            m.actor("PathNode", 256 + 0.75 * run, y, FLOOR + 0.75 * SLIDE_RISE + 39 + 17 * math.tan(math.radians(rise))
                    + 40, Tag="Drop")
            continue
        if kind == "wall":
            c = WALL_LANES[i][1]
            m.add(box(256, y + c - rise / 2, FLOOR, 288, y + c + rise / 2, FLOOR + 200, texture="UTtech1.bmTrim",
                      csg="CSG_Add", name=f"Lane{i}"))
            m.actor("PathNode", 0, y, FLOOR + 60, Tag="Up1")
            continue
        if kind == "ledge":
            m.add(box(256, y - 100, FLOOR, 1024, y + 100, FLOOR + rise, texture="UTtech1.bmTrim", csg="CSG_Add",
                      name=f"Lane{i}"))
            m.actor("PathNode", 0, y, FLOOR + 60, Tag=f"Up{rise}J{LEDGE_LANES[i][1]}")
            continue
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
    shutil.copy(out, os.path.join(mapgen, "ut", "System"))  # where our engine's package paths find it


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


def run_ours(ut_script, game, map_name, seconds):
    url = f"{map_name}?mutator=OUProbe.ProbeMutator"
    res = subprocess.run([ut_script, game, "--map", url, str(seconds)], capture_output=True, text=True,
                         env=dict(os.environ, UTVM_LOG="1"), timeout=600)
    return res.stdout


def main(argv):
    if len(argv) < 3 or argv[2] not in MAPS:
        raise SystemExit(__doc__)
    mapgen, kind = os.path.abspath(argv[1]), argv[2]
    game = os.path.join(mapgen, "ut")
    seconds = float(argv[argv.index("--seconds") + 1]) if "--seconds" in argv else 12
    if "--angles" in argv:
        angles = [float(a) for a in argv[argv.index("--angles") + 1].split(",")]
        if kind == "entry":
            ENTRY_LANES[:] = [(a, 400) for a in angles]
        else:
            (SLIDE_ANGLES if kind == "slide" else RAMP_ANGLES)[:] = angles
    if "--no-build" not in argv:
        compile_probe(mapgen)
        build_lanes(mapgen, kind)
    log_path = os.path.join(mapgen, "work", f"probe-{kind}.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    tracks = parse_log(run_server(game, MAPS[kind], seconds, log_path))
    if not tracks:
        raise SystemExit(f"no probe lines in {log_path}")
    lanes = rises(kind)
    if kind == "wall":
        rows = {"original": wall_report(open(log_path, encoding="latin-1").read())}
        if "--ours" in argv:
            rows["ours"] = wall_report(run_ours(os.path.abspath(argv[argv.index("--ours") + 1]), game, MAPS[kind], seconds))
        for who, rep in rows.items():
            print(f"{who}:  lane     adjusted  destination left            at x   farthest x")
            for lane, (ok, dest, x, far) in sorted(rep.items()):
                d = ", ".join(f"{v:.0f}" for v in dest)
                print(f"      {lanes[lane][0]:>9}  {'yes' if ok else 'no':>8}  ({d:>22})  {x:6.0f}  {far:9.0f}")
        return
    ours = None
    report = {"ledge": ledge_report, "air": air_report, "slide": slide_report, "entry": entry_report}.get(kind, step_report)
    if "--ours" in argv:
        ours = report(parse_log(run_ours(os.path.abspath(argv[argv.index("--ours") + 1]), game, MAPS[kind], seconds)))
    lanes = rises(kind)
    if kind == "entry":
        print("lane       original: along slope (3 ticks)  top z" + ("   ours: along slope (3 ticks)  top z" if ours else ""))
        fmt = lambda e: " ".join(f"{a:6.1f}" for a in e[0]) + f"  {e[1]:6.1f}"
        for lane, e in sorted(report(tracks).items()):
            row = f"{lanes[lane][0]:>8}   {fmt(e):>32}"
            if ours is not None:
                row += f"   {fmt(ours[lane]):>28}" if lane in ours else "   (no track)"
            print(row)
        return
    if kind == "ledge":
        print("ledge  original: jumped  top z  on top" + ("   ours: jumped  top z  on top" if ours else ""))
        fmt = lambda h, e: f"{'yes' if e[1] else 'no':>6}  {e[2]:5.1f}  {'yes' if e[3] and e[3] - e[0] > h - 2 else 'no':>6}"
        for lane, e in sorted(report(tracks).items()):
            label, h = lanes[lane] if 0 <= lane < len(lanes) else ("?", 0)
            row = f"{label:>9}  {fmt(h, e):>30}"
            if ours is not None:
                row += f"   {fmt(h, ours[lane]):>30}" if lane in ours else "   (no track)"
            print(row)
        return
    if kind == "slide":
        print("lane      all gravity  original: accel" + ("   ours: accel" if ours is not None else ""))
        for lane, acc in sorted(report(tracks).items()):
            label, angle = lanes[lane] if 0 <= lane < len(lanes) else ("?", 0)
            row = f"{label:>8}  {GRAVITY * math.sin(math.radians(angle)):11.1f}  {acc:15.1f}"
            if ours is not None:
                row += f"   {ours[lane]:11.1f}" if lane in ours else "   (no track)"
            print(row)
        return
    if kind == "air":
        print("lane      original: accel  top speed" + ("   ours: accel  top speed" if ours is not None else ""))
        for lane, (acc, top) in sorted(report(tracks).items()):
            label = lanes[lane][0] if 0 <= lane < len(lanes) else "?"
            row = f"{label:>8}  {acc:15.1f}  {top:9.1f}"
            if ours is not None:
                o = ours.get(lane)
                row += f"   {o[0]:11.1f}  {o[1]:9.1f}" if o else "   (no track)"
            print(row)
        return
    print("lane      rise  original: rest z  top z  over" + ("   ours: rest z  top z  over" if ours is not None else ""))
    for lane, (rest_z, top) in sorted(step_report(tracks).items()):
        label, rise = lanes[lane] if 0 <= lane < len(lanes) else ("?", 0)
        row = f"{label:>7} {rise:6.1f}  {rest_z:16.1f}  {top:5.1f}  {over(rise, rest_z, top):4}"
        if ours is not None:
            o = ours.get(lane)
            row += f"   {o[0]:12.1f}  {o[1]:5.1f}  {over(rise, *o):4}" if o else "   (no track)"
        print(row)


def over(rise, rest_z, top):
    return "yes" if rise and top - rest_z > rise - 2 else "no"


if __name__ == "__main__":
    main(sys.argv)
