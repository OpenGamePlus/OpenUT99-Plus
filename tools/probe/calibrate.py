"""Measure the original engine with our probe (ADR 0008).

    python3 tools/probe/calibrate.py MAPGEN_DIR steps [--seconds S] [--no-build]

MAPGEN_DIR is a ut99-mapgen checkout set up as in DEVELOPMENT_GUIDE: ut/ holds a copy of the install with the
OldUnreal Linux patch that matches it (the server we measure); make-469e/ holds a System with the v469e Linux patch,
only to compile the probe (the 469f-rc5 Linux `ucc make` recurses forever into "Classes/./"). The runner compiles
tools/probe/OUProbe there (the package lands in ~/.utpg/System, where the server finds it), builds the calibration
map with mapgen, runs a dedicated server with the probe mutator and reads the probe lines from its log.

Calibration "steps": lanes of steps of growing height; a ProbePawn walks at each one. Reports, per lane, the step
height and how high the pawn got: a climbed step lifts it by its height.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEP_HEIGHTS = [16, 24, 28, 29, 30, 31, 32, 40]
LANE_Y0, LANE_GAP = -1050, 300
FLOOR = 0
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


def build_steps(mapgen):
    sys.path.insert(0, os.path.join(mapgen, "tools"))
    from build import Map
    from t3d import box

    m = Map("OU-Steps")
    y_top = LANE_Y0 + LANE_GAP * len(STEP_HEIGHTS)
    m.add(box(-512, LANE_Y0 - 350, FLOOR, 2048, y_top + 100, FLOOR + 512,
              floor="UTtech1.bmFloor", walls="UTtech1.bmwall3", ceiling="UTtech1.bmFloor"))
    for i, h in enumerate(STEP_HEIGHTS):
        y = LANE_Y0 + i * LANE_GAP
        m.add(box(256, y - 100, FLOOR, 1024, y + 100, FLOOR + h, texture="UTtech1.bmTrim", csg="CSG_Add",
                  name=f"Step{i}"))
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
    if len(argv) < 3 or argv[2] != "steps":
        raise SystemExit(__doc__)
    mapgen = os.path.abspath(argv[1])
    game = os.path.join(mapgen, "ut")
    seconds = float(argv[argv.index("--seconds") + 1]) if "--seconds" in argv else 12
    if "--no-build" not in argv:
        compile_probe(mapgen)
        build_steps(mapgen)
    log_path = os.path.join(mapgen, "work", "probe-steps.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    tracks = parse_log(run_server(game, "OU-Steps", seconds, log_path))
    if not tracks:
        raise SystemExit(f"no probe lines in {log_path}")
    print("step  rest z  top z  climbed")
    for lane, (rest_z, top) in sorted(step_report(tracks).items()):
        h = STEP_HEIGHTS[lane] if 0 <= lane < len(STEP_HEIGHTS) else None
        print(f"{h!s:>4}  {rest_z:6.1f}  {top:5.1f}  {'yes' if h and top - rest_z > h - 2 else 'no'}")


if __name__ == "__main__":
    main(sys.argv)
