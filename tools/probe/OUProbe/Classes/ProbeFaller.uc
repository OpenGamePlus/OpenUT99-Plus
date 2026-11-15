// OpenUT99-Plus probe (ADR 0008): spawned in the air, it falls while a latent MoveTo pulls it 1500 units along its
// yaw from the start, so falling physics gets its Acceleration (scaled by AirControl). Logs like ProbePawn.
class ProbeFaller extends ProbePawn;

auto state Drop
{
Begin:
	MoveTo(Location + vector(Rotation) * 1500);
}
