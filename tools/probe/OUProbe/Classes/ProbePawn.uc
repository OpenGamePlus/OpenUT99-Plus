// OpenUT99-Plus probe (ADR 0008): falls to the floor, waits 0.5 s, then walks forward like a bot (latent MoveTo
// 1500 units along its yaw) and logs its state every tick:
// PROBE <name> <seconds> <x> <y> <z> <physics> <vx> <vy> <vz>
class ProbePawn extends Pawn;

var float Born;

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

auto state Walking
{
Begin:
	Sleep(0.5);
	MoveTo(Location + vector(Rotation) * 1500);
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
