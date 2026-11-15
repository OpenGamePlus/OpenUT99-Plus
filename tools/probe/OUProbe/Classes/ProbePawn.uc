// OpenUT99-Plus probe (ADR 0008): falls to the floor, waits 0.5 s, then walks forward like a bot (latent MoveTo
// 1500 units along its yaw, raised by UpZ) and logs its state every tick:
// PROBE <name> <seconds> <x> <y> <z> <physics> <vx> <vy> <vz>
class ProbePawn extends Pawn;

var float Born;
var float UpZ;

function PostBeginPlay()
{
	Super.PostBeginPlay();
	Born = Level.TimeSeconds;
	SetPhysics(PHYS_Falling);
}

event Tick(float DeltaTime)
{
	Log("PROBE" @ Name @ Level.TimeSeconds - Born @ Location.X @ Location.Y @ Location.Z @ int(Physics)
		@ Velocity.X @ Velocity.Y @ Velocity.Z);
}

// PROBEHIT <name> <seconds> <normal> <physics>; pawns walking to a raised goal (UpZ) then call PickWallAdjust as bots
// do and log PROBEADJ <name> <seconds> <result> <physics> <velocity> <destination>.
event HitWall(vector HitNormal, actor Wall)
{
	local bool Adjusted;

	Log("PROBEHIT" @ Name @ Level.TimeSeconds - Born @ HitNormal @ int(Physics));
	if (UpZ > 0)
	{
		Adjusted = PickWallAdjust();
		Log("PROBEADJ" @ Name @ Level.TimeSeconds - Born @ Adjusted @ int(Physics) @ Velocity @ Destination);
	}
}

auto state Walking
{
Begin:
	Sleep(0.5);
	MoveTo(Location + vector(Rotation) * 1500 + vect(0,0,1) * UpZ);
}

defaultproperties
{
	GroundSpeed=400
	AccelRate=2048
	JumpZ=325
	CollisionRadius=17
	CollisionHeight=39
	bCollideActors=True
	bCollideWorld=True
	bBlockActors=True
	bBlockPlayers=True
	bGameRelevant=True
}
