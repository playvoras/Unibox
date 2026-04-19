#pragma once

class CTFPlayer;
class CTFWeaponBase;
class CUserCmd;

struct NavBotJobResult_t
{
	bool m_bHasJob = false;
	bool m_bRunReload = false;
	bool m_bRunSafeReload = false;
};

class CNavBotJobSystem
{
public:
	void RefreshSharedState(CTFPlayer* pLocal);
	auto Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> NavBotJobResult_t;
	void Reset();

private:
	auto TryEscapeSpawn(CTFPlayer* pLocal) -> bool;
	auto TryEscapeProjectiles(CTFPlayer* pLocal) -> bool;
	auto TryEscapeDanger(CTFPlayer* pLocal) -> bool;
	auto TryGetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio) -> bool;
	auto TryGetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool;
	auto TryEngineer(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool;
	auto TryRunReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TrySafeReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TryMelee(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool;
	auto TryCapture(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TrySnipeSentry(CTFPlayer* pLocal) -> bool;
	auto TryStayNear(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TryGroupWithOthers(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TryRoam(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
};
