// OpenUT99-Plus probe (ADR 0008): a probe pawn at every PathNode of the map, facing the node's yaw. A node tagged
// AirNN gets a ProbeFaller with AirControl NN / 100; any other node a ProbePawn.
class ProbeMutator extends Mutator;

function PostBeginPlay()
{
	local PathNode N;
	local Pawn P;
	local string T;

	Super.PostBeginPlay();
	foreach AllActors(class'PathNode', N)
	{
		T = string(N.Tag);
		if (Left(T, 3) ~= "Air")
		{
			P = Spawn(class'ProbeFaller', , , N.Location, N.Rotation);
			if (P != None)
				P.AirControl = float(Mid(T, 3)) / 100.0;
		}
		else
			P = Spawn(class'ProbePawn', , , N.Location, N.Rotation);
		if (P == None)
			Log("PROBE spawnfail" @ N.Location.X @ N.Location.Y @ N.Location.Z);
	}
}
