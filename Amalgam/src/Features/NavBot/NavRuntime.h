#pragma once

class CTFPlayer;
class CTFWeaponBase;
class CUserCmd;

namespace NavRuntime
{
	auto IsMovementLocked(CTFPlayer* pLocal) -> bool;
	auto IsMinigunJumpLocked(CTFWeaponBase* pWeapon, CUserCmd* pCmd) -> bool;
	auto CanIssueNavJump(CTFWeaponBase* pWeapon, CUserCmd* pCmd) -> bool;
}
