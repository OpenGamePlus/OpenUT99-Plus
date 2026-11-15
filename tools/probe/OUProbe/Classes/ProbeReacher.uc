// OpenUT99-Plus probe (ADR 0008): falls to the floor, waits 0.5 s, then asks the engine whether a point at Offset from
// its location is reachable and logs the answer:
// PROBEREACH <name> <seconds> <pointReachable> <location> <point>
class ProbeReacher extends ProbePawn;

var vector Offset;

auto state Walking
{
Begin:
	Sleep(0.5);
	Log("PROBEREACH" @ Name @ Level.TimeSeconds - Born @ pointReachable(Location + Offset) @ Location
		@ (Location + Offset));
}
