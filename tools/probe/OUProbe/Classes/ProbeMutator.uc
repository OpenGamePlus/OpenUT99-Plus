// OpenUT99-Plus probe (ADR 0008): one ProbePawn at every PathNode of the map, facing the node's yaw.
class ProbeMutator extends Mutator;

function PostBeginPlay()
{
	local PathNode N;

	Super.PostBeginPlay();
	foreach AllActors(class'PathNode', N)
		if (Spawn(class'ProbePawn', , , N.Location, N.Rotation) == None)
			Log("PROBE spawnfail" @ N.Location.X @ N.Location.Y @ N.Location.Z);
}
