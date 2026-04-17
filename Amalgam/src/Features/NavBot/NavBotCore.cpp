#include "NavBotCore.h"

#include "DangerManager/DangerManager.h"
#include "NavAreaUtils.h"
#include "NavEngine/NavEngine.h"
#include "NavBotJobs/Engineer.h"
#include "NavBotJobs/Reload.h"
#include "NavBotJobs/StayNear.h"
#include "NavRuntime.h"
#include "../FollowBot/FollowBot.h"
#include "../CritHack/CritHack.h"
#include "../Misc/Misc.h"
#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"


void CNavBotCore::UpdateSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	static Timer tSlotTimer{};
	if (!tSlotTimer.Run(0.2f))
		return;

	// Prioritize reloading
	int iReloadSlot = F::NavBotReload.m_iLastReloadSlot = F::NavBotReload.GetReloadWeaponSlot(pLocal, tClosestEnemy);

	if (F::NavBotEngineer.IsEngieMode(pLocal))
	{
		int iSwitch = 0;
		switch (F::NavBotEngineer.m_eTaskStage)
		{
			// We are currently building something
		case EngineerTaskStageEnum::BuildSentry:
		case EngineerTaskStageEnum::BuildDispenser:
			if (F::NavBotEngineer.m_tCurrentBuildingSpot.m_flCost != FLT_MAX && F::NavBotEngineer.m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= 500.f)
			{
				if (pLocal->m_bCarryingObject())
				{
					auto pWeapon = pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>();
					if (pWeapon && pWeapon->GetSlot() != 3)
						F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
				}
				return;
			}
			break;
			// We are currently upgrading/repairing something
		case EngineerTaskStageEnum::SmackSentry:
			iSwitch = F::NavBotEngineer.m_flDistToSentry <= 300.f;
			break;
		case EngineerTaskStageEnum::SmackDispenser:
			iSwitch = F::NavBotEngineer.m_flDistToDispenser <= 500.f;
			break;
		default:
			break;
		}

		if (iSwitch)
		{
			if (iSwitch == 1)
			{
				if (F::BotUtils.m_iCurrentSlot < SLOT_MELEE)
					F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
			}
			return;
		}
	}

	const int iDesiredSlot = iReloadSlot != -1 ? iReloadSlot : Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1;
	if (F::BotUtils.m_iCurrentSlot != iDesiredSlot)
		F::BotUtils.SetSlot(pLocal, iDesiredSlot);
}

void CNavBotCore::UpdateRunReloadInput(CUserCmd* pCmd, bool bShouldHold)
{
	if (!pCmd)
	{
		m_bHoldingRunReload = bShouldHold;
		return;
	}

	if (bShouldHold)
		pCmd->buttons |= IN_RELOAD;
	else if (m_bHoldingRunReload)
		pCmd->buttons &= ~IN_RELOAD;

	m_bHoldingRunReload = bShouldHold;
}

void CNavBotCore::ResetRuntimeState(CUserCmd* pCmd)
{
	F::NavBotStayNear.m_iStayNearTargetIdx = -1;
	F::NavBotReload.m_iLastReloadSlot = -1;
	m_tIdleTimer.Update();
	m_tAntiStuckTimer.Update();
	UpdateRunReloadInput(pCmd, false);
}

static bool IsWeaponValidForDT(CTFWeaponBase* pWeapon)
{
	if (!pWeapon || F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
		return false;

	auto iWepID = pWeapon->GetWeaponID();
	if (iWepID == TF_WEAPON_SNIPERRIFLE || iWepID == TF_WEAPON_SNIPERRIFLE_CLASSIC || iWepID == TF_WEAPON_SNIPERRIFLE_DECAP)
		return false;

	return SDK::WeaponDoesNotUseAmmo(pWeapon, false);
}

void CNavBotCore::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value ||
		!pLocal->IsAlive() || F::NavEngine.m_eCurrentPriority == PriorityListEnum::Followbot || F::FollowBot.m_bActive || !F::NavEngine.IsReady())
	{
		ResetRuntimeState(pCmd);
		return;
	}

	if (NavRuntime::IsMovementLocked(pLocal))
	{
		if (F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();
		
		ResetRuntimeState(pCmd);
		return;
	}

	if (Vars::Debug::Info.Value)
	{
		for (const auto& segment : F::BotUtils.m_vWalkableSegments)
		{
			G::LineStorage.push_back({ { segment.first, segment.second }, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 0, 255, 0, 255 } });
		}

		if (F::BotUtils.m_vPredictedJumpPos.Length() > 0.f)
		{
			G::LineStorage.push_back({ { pLocal->GetAbsOrigin(), F::BotUtils.m_vPredictedJumpPos }, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 255, 255, 0, 255 } });
			G::SphereStorage.push_back({ F::BotUtils.m_vJumpPeakPos, 5.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
			G::SphereStorage.push_back({ F::BotUtils.m_vPredictedJumpPos, 5.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 0, 0, 255, 255 }, { 0, 0, 0, 0 } });
		}
	}

	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::StayNear)
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
	{
		ResetRuntimeState(pCmd);
		return;
	}

	if (!pWeapon)
	{
		ResetRuntimeState(pCmd);
		return;
	}

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
	{
		m_vStuckAngles = pCmd->viewangles;
		ResetRuntimeState(pCmd);
		return;
	}

	if (pLocal->m_iClass() == TF_CLASS_ENGINEER && pLocal->m_bCarryingObject() && !F::NavBotEngineer.IsEngieMode(pLocal))
	{
		if (F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();

		static Timer tDropCarriedObjectTimer{};
		if (tDropCarriedObjectTimer.Run(0.5f))
		{
			I::EngineClient->ClientCmd_Unrestricted("destroy 0");
			I::EngineClient->ClientCmd_Unrestricted("destroy 1");
			I::EngineClient->ClientCmd_Unrestricted("destroy 2");
			I::EngineClient->ClientCmd_Unrestricted("destroy 3");
		}

		F::NavBotEngineer.Reset();
		ResetRuntimeState(pCmd);
		return;
	}

	// Update our current nav area
	if (!F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
	{
		// This should never happen.
		// In case it did then theres something wrong with nav engine
		ResetRuntimeState(pCmd);
		return;
	}

	// Recharge doubletap every n seconds
	static Timer tDoubletapRecharge{};
	if (Vars::Misc::Movement::NavBot::RechargeDT.Value && IsWeaponValidForDT(pWeapon))
	{
		if (!F::Ticks.m_bRechargeQueue &&
			(Vars::Misc::Movement::NavBot::RechargeDT.Value != Vars::Misc::Movement::NavBot::RechargeDTEnum::WaitForFL || !Vars::Fakelag::Fakelag.Value || !F::FakeLag.m_iGoal) &&
			G::Attacking != 1 &&
			(F::Ticks.m_iShiftedTicks < F::Ticks.m_iShiftedGoal) && tDoubletapRecharge.Check(Vars::Misc::Movement::NavBot::RechargeDTDelay.Value))
			F::Ticks.m_bRechargeQueue = true;
		else if (F::Ticks.m_iShiftedTicks >= F::Ticks.m_iShiftedGoal)
			tDoubletapRecharge.Update();
	}

	// Not used
	// RefreshSniperSpots();
	m_tJobSystem.RefreshSharedState(pLocal);

	m_tSelectedConfig = NavBotConfig::Select(pLocal, pWeapon);

	UpdateSlot(pLocal, F::BotUtils.m_tClosestEnemy);
	F::DangerManager.Update(pLocal);

	// TODO:
	// Add engie logic and target sentries logic. (Done)
	// Also maybe add some spy sapper logic? (No.)
	// Fix defend and help capture logic
	// Fix reload stuff because its really janky
	// Finish auto wewapon stuff
	// Make a better closest enemy logic
	// Fix dormant player blacklist not actually running

	const auto tJobResult = m_tJobSystem.Run(pCmd, pLocal, pWeapon);

	bool bShouldHoldReload = tJobResult.m_bRunReload || tJobResult.m_bRunSafeReload;
	if (bShouldHoldReload && F::NavBotReload.m_iLastReloadSlot != -1 && F::BotUtils.m_iCurrentSlot != F::NavBotReload.m_iLastReloadSlot)
		bShouldHoldReload = false;

	UpdateRunReloadInput(pCmd, bShouldHoldReload);

	if (tJobResult.m_bHasJob)
	{
		bool bIsPathing = F::NavEngine.IsPathing();
		if (!bIsPathing)
		{
			// If we have a job but no path, we consider it idle (stuck or waiting for gods agreement to move lol)
		}
		else
		{
			m_tIdleTimer.Update();
			m_tAntiStuckTimer.Update();
		}

		// Force crithack in dangerous conditions
		// TODO:
		// Maybe add some logic to it (more logic)
		CTFPlayer* pPlayer = nullptr;
		switch (F::NavEngine.m_eCurrentPriority)
		{
		case PriorityListEnum::StayNear:
			pPlayer = I::ClientEntityList->GetClientEntity(F::NavBotStayNear.m_iStayNearTargetIdx)->As<CTFPlayer>();
			if (pPlayer)
				F::CritHack.m_bForce = !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		case PriorityListEnum::MeleeAttack:
		case PriorityListEnum::GetHealth:
		case PriorityListEnum::EscapeDanger:
			pPlayer = I::ClientEntityList->GetClientEntity(F::BotUtils.m_tClosestEnemy.m_iEntIdx)->As<CTFPlayer>();
			F::CritHack.m_bForce = pPlayer && !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		default:
			F::CritHack.m_bForce = false;
			break;
		}
	}
	else if (F::NavEngine.IsReady() && !F::NavEngine.IsSetupTime())
	{
		float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
		if (flIdleTime > m_flNextIdleTime)
		{
			if (flIdleTime < m_flNextIdleTime + 0.5f)
			{
				pCmd->forwardmove = 450.f;

				if (m_tAntiStuckTimer.Run(m_flNextStuckAngleChange))
				{
					m_flNextStuckAngleChange = SDK::RandomFloat(0.1f, 0.3f);
					m_vStuckAngles.y += SDK::RandomFloat(-15.f, 15.f);
					Math::ClampAngles(m_vStuckAngles);
				}

				SDK::FixMovement(pCmd, m_vStuckAngles);
			}
			else
			{
				m_tIdleTimer.Update();
				m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
			}
		}
	}
	else
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		m_vStuckAngles = pCmd->viewangles;
		m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
	}
}

void CNavBotCore::Reset()
{
	m_tJobSystem.Reset();
	m_bHoldingRunReload = false;
	m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
}

void CNavBotCore::Draw(CTFPlayer* pLocal)
{
	NavBotDebug::Draw(pLocal);
}
