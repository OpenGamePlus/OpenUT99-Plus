// OpenUT99-Plus probe (ADR 0008): a probe pawn at every PathNode of the map, facing the node's yaw. A node tagged
// AirNN gets a ProbeFaller with AirControl NN / 100, WalkNNN a ProbePawn with GroundSpeed NNN, UpNNN a
// jumping ProbePawn walking to a point NNN higher (UpNNNJMMM: with JumpZ MMM;
// a further CRR_HH: collision radius RR and height HH), Drop a ProbeDropper,
// any other node a ProbePawn.
class ProbeMutator extends Mutator;

function PostBeginPlay()
{
	local PathNode N;
	local Pawn P;
	local string T, S;

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
		else if (Left(T, 4) ~= "Walk")
		{
			P = Spawn(class'ProbePawn', , , N.Location, N.Rotation);
			if (P != None)
				P.GroundSpeed = float(Mid(T, 4));
		}
		else if (Left(T, 2) ~= "Up")
		{
			P = Spawn(class'ProbePawn', , , N.Location, N.Rotation);
			if (P != None)
			{
				T = Mid(T, 2);
				if (InStr(Caps(T), "C") >= 0)
				{
					S = Mid(T, InStr(Caps(T), "C") + 1);
					P.SetCollisionSize(float(Left(S, InStr(S, "_"))), float(Mid(S, InStr(S, "_") + 1)));
					T = Left(T, InStr(Caps(T), "C"));
				}
				if (InStr(Caps(T), "J") >= 0)
				{
					P.JumpZ = float(Mid(T, InStr(Caps(T), "J") + 1));
					T = Left(T, InStr(Caps(T), "J"));
				}
				ProbePawn(P).UpZ = float(T);
				P.bCanJump = True;
			}
		}
		else if (T ~= "Drop")
			P = Spawn(class'ProbeDropper', , , N.Location, N.Rotation);
		else
			P = Spawn(class'ProbePawn', , , N.Location, N.Rotation);
		if (P == None)
			Log("PROBE spawnfail" @ N.Location.X @ N.Location.Y @ N.Location.Z);
	}
}
