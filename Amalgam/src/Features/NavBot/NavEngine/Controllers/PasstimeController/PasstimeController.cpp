#include "PasstimeController.h"
#include "../../NavEngine.h"

namespace
{
	constexpr float kPasstimeGoalPointEpsilon = 8.0f;
	constexpr float kPasstimeSideSwitchHysteresis = 512.0f;

	Vector AdjustObjectivePosToNav(Vector vPos)
	{
		if (!F::NavEngine.IsNavMeshLoaded())
			return vPos;

		if (auto pArea = F::NavEngine.FindClosestNavArea(vPos, false))
		{
			Vector vCorrected = pArea->GetNearestPoint(vPos.Get2D());
			vCorrected.z = pArea->GetZ(vCorrected.x, vCorrected.y);
			return vCorrected;
		}

		return vPos;
	}

	bool HasPasstimeThrowStandSpace(const Vector& vPos)
	{
		CTraceFilterWorldAndPropsOnly filter = {};
		CGameTrace trace = {};

		const Vector vStart = vPos + Vec3(0.0f, 0.0f, 4.0f);
		const Vector vEnd = vStart;
		const Vector vHullMins = Vec3(-20.0f, -20.0f, 0.0f);
		const Vector vHullMaxs = Vec3(20.0f, 20.0f, 72.0f);
		SDK::TraceHull(vStart, vEnd, vHullMins, vHullMaxs, MASK_PLAYERSOLID, &filter, &trace);
		if (trace.startsolid || trace.allsolid)
			return false;

		CGameTrace groundTrace = {};
		SDK::TraceHull(vPos + Vec3(0.0f, 0.0f, 24.0f), vPos - Vec3(0.0f, 0.0f, 56.0f), Vec3(-18.0f, -18.0f, 0.0f), Vec3(18.0f, 18.0f, 2.0f), MASK_PLAYERSOLID, &filter, &groundTrace);
		return groundTrace.DidHit();
	}

	Vector GetObjectiveOrigin(CBaseEntity* pEntity)
	{
		if (!pEntity)
			return {};

		Vector vOut = pEntity->GetCenter();
		if (!vOut.IsZero())
			return vOut;

		vOut = pEntity->GetAbsOrigin();
		if (!vOut.IsZero())
			return vOut;

		return pEntity->m_vecOrigin();
	}

	Vector GetObjectiveOrigin(CServerBaseEntity* pEntity)
	{
		return GetObjectiveOrigin(reinterpret_cast<CBaseEntity*>(pEntity));
	}

	Vector GetGoalWorldMins(CFuncPasstimeGoal* pGoal)
	{
		auto pEntity = reinterpret_cast<CBaseEntity*>(pGoal);
		return pEntity->GetAbsOrigin() + pEntity->m_vecMins();
	}

	Vector GetGoalWorldMaxs(CFuncPasstimeGoal* pGoal)
	{
		auto pEntity = reinterpret_cast<CBaseEntity*>(pGoal);
		return pEntity->GetAbsOrigin() + pEntity->m_vecMaxs();
	}

	auto GetPasstimeTeamName(int iTeam) -> const char*
	{
		switch (iTeam)
		{
		case TF_TEAM_RED: return "RED";
		case TF_TEAM_BLUE: return "BLU";
		default: return "OTHER";
		}
	}

	auto GetPasstimeSideName(int iSide) -> const char*
	{
		switch (iSide)
		{
		case -1: return "NEG";
		case 1: return "POS";
		default: return "MID";
		}
	}

	int GetOpposingPasstimeTeam(int iTeam)
	{
		return iTeam == TF_TEAM_RED ? TF_TEAM_BLUE : iTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TEAM_UNASSIGNED;
	}

	float GetClosestRespawnTriggerDistSqr(int iTeam, const Vector& vPos)
	{
		float flBestDist = FLT_MAX;
		for (const auto& tRespawnRoom : F::NavEngine.GetRespawnRooms())
		{
			if (tRespawnRoom.m_iTeam != 0 && tRespawnRoom.m_iTeam != iTeam)
				continue;

			if (tRespawnRoom.tData.m_vCenter.IsZero())
				continue;

			flBestDist = std::min(flBestDist, tRespawnRoom.tData.m_vCenter.DistToSqr(vPos));
		}

		return flBestDist;
	}

	float GetFarthestRespawnTriggerDistSqr(int iTeam, const Vector& vPos)
	{
		float flBestDist = -1.0f;
		for (const auto& tRespawnRoom : F::NavEngine.GetRespawnRooms())
		{
			if (tRespawnRoom.m_iTeam != 0 && tRespawnRoom.m_iTeam != iTeam)
				continue;

			if (tRespawnRoom.tData.m_vCenter.IsZero())
				continue;

			flBestDist = std::max(flBestDist, tRespawnRoom.tData.m_vCenter.DistToSqr(vPos));
		}

		return flBestDist;
	}
}

void CPasstimeController::Init()
{
	m_vGoals.clear();
	m_pBall = nullptr;
	m_pLogic = nullptr;
	for (int i = 0; i < 4; i++)
	{
		m_iTeamHomeSide[i] = 0;
		m_bHasTeamHomeSide[i] = false;
		m_vTeamSpawnCenter[i] = {};
		m_bHasTeamSpawnCenter[i] = false;
	}
}

void CPasstimeController::Update()
{
	Init();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		switch (pEntity->GetClassID())
		{
		case ETFClassID::CFuncPasstimeGoal:
			m_vGoals.push_back(pEntity->As<CFuncPasstimeGoal>());
			break;
		case ETFClassID::CPasstimeBall:
			m_pBall = pEntity->As<CPasstimeBall>();
			break;
		case ETFClassID::CTFPasstimeLogic:
			m_pLogic = pEntity->As<CTFPasstimeLogic>();
			break;
		}
	}

	if (!m_pBall && m_pLogic)
		m_pBall = m_pLogic->GetBall();

	UpdateGoalAxis();
	UpdateTeamSpawnCenters();
	UpdateTeamHomeSides();

	static Timer tLogTimer{};
	if (Vars::Debug::Logging.Value && tLogTimer.Run(1.0f))
	{
		SDK::Output("PasstimeController", std::format(
			"Update: goals={} ball={} logic={}",
			m_vGoals.size(),
			m_pBall ? m_pBall->entindex() : -1,
			m_pLogic ? m_pLogic->entindex() : -1).c_str(), { 100, 200, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);

		if (m_vGoals.empty())
		{
			SDK::Output("PasstimeController", "Update: no CFuncPasstimeGoal entities cached", { 255, 140, 140 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}

		for (size_t i = 0; i < m_vGoals.size(); i++)
		{
			auto pGoal = m_vGoals[i];
			if (!pGoal)
				continue;

			auto pGoalEntity = reinterpret_cast<CBaseEntity*>(pGoal);
			std::string sTargetname = {};
			const int iGoalTeam = GetGoalTeam(pGoal, &sTargetname);
			const int iMapOwnerTeam = SDK::GetPasstimeGoalMapTeam(GetObjectiveOrigin(pGoalEntity), nullptr);
			SDK::Output("PasstimeController", std::format(
				"Goal[{}]: ent={} score_team={} owner_team={} live_team={} targetname={} type={} disabled={} side={} origin=({:.0f},{:.0f},{:.0f}) mins=({:.0f},{:.0f},{:.0f}) maxs=({:.0f},{:.0f},{:.0f})",
				i,
				pGoalEntity->entindex(),
				GetPasstimeTeamName(iGoalTeam),
				GetPasstimeTeamName(iMapOwnerTeam),
				GetPasstimeTeamName(pGoal->m_iTeamNum()),
				sTargetname.empty() ? "<none>" : sTargetname,
				pGoal->m_iGoalType(),
				pGoal->m_bTriggerDisabled() ? 1 : 0,
				GetPasstimeSideName(GetGoalSide(GetObjectiveOrigin(pGoal))),
				pGoalEntity->GetAbsOrigin().x, pGoalEntity->GetAbsOrigin().y, pGoalEntity->GetAbsOrigin().z,
				pGoalEntity->m_vecMins().x, pGoalEntity->m_vecMins().y, pGoalEntity->m_vecMins().z,
				pGoalEntity->m_vecMaxs().x, pGoalEntity->m_vecMaxs().y, pGoalEntity->m_vecMaxs().z).c_str(),
				{ 100, 255, 180 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}

		if (m_bHasGoalAxis)
		{
			SDK::Output("PasstimeController", std::format(
				"Axis: dir=({:.1f},{:.1f},{:.1f}) center={:.0f} redHome={} bluHome={}",
				m_vGoalAxis.x, m_vGoalAxis.y, m_vGoalAxis.z, m_flGoalAxisCenter,
				GetPasstimeSideName(GetTeamHomeSide(TF_TEAM_RED)),
				GetPasstimeSideName(GetTeamHomeSide(TF_TEAM_BLUE))).c_str(),
				{ 120, 220, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}

		const auto& vRespawnRooms = F::NavEngine.GetRespawnRooms();
		for (size_t i = 0; i < vRespawnRooms.size(); i++)
		{
			const auto& tRespawnRoom = vRespawnRooms[i];
			SDK::Output("PasstimeController", std::format(
				"RespawnRoom[{}]: team={} center=({:.0f},{:.0f},{:.0f}) origin=({:.0f},{:.0f},{:.0f})",
				i,
				GetPasstimeTeamName(tRespawnRoom.m_iTeam),
				tRespawnRoom.tData.m_vCenter.x, tRespawnRoom.tData.m_vCenter.y, tRespawnRoom.tData.m_vCenter.z,
				tRespawnRoom.tData.m_vOrigin.x, tRespawnRoom.tData.m_vOrigin.y, tRespawnRoom.tData.m_vOrigin.z).c_str(),
				{ 255, 210, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
	}

	if (Vars::Debug::Info.Value)
	{
		for (auto pGoal : m_vGoals)
		{
			if (!pGoal)
				continue;

			PasstimeGoalInfo tGoal = {};
			tGoal.m_pGoal = pGoal;
			tGoal.m_iGoalType = pGoal->m_iGoalType();
			tGoal.m_iTeam = GetGoalTeam(pGoal);
			tGoal.m_vOrigin = GetObjectiveOrigin(pGoal);
			tGoal.m_vMins = GetGoalWorldMins(pGoal);
			tGoal.m_vMaxs = GetGoalWorldMaxs(pGoal);

			Color_t tEdge = { 255, 255, 255, 180 };
			if (pGoal->m_bTriggerDisabled())
				tEdge = { 255, 80, 80, 180 };
			else if (tGoal.m_iTeam == TEAM_UNASSIGNED || tGoal.m_iTeam == TEAM_INVALID || tGoal.m_iTeam == 0)
				tEdge = GetGoalSide(tGoal.m_vOrigin) == GetTeamHomeSide(H::Entities.GetLocal() ? H::Entities.GetLocal()->m_iTeamNum() : TEAM_UNASSIGNED)
					? Color_t(255, 180, 80, 180)
					: Color_t(80, 255, 120, 180);

			G::BoxStorage.emplace_back(tGoal.m_vOrigin, tGoal.m_vMins - tGoal.m_vOrigin, tGoal.m_vMaxs - tGoal.m_vOrigin, Vec3(), I::GlobalVars->curtime + 0.2f, tEdge, Color_t(0, 0, 0, 0), true);
		}
	}
}

void CPasstimeController::UpdateGoalAxis()
{
	m_bHasGoalAxis = false;
	if (m_vGoals.size() < 2)
		return;

	float flMinX = FLT_MAX, flMaxX = -FLT_MAX;
	float flMinY = FLT_MAX, flMaxY = -FLT_MAX;
	for (auto pGoal : m_vGoals)
	{
		if (!pGoal || pGoal->m_bTriggerDisabled())
			continue;

		const Vector vPos = GetObjectiveOrigin(pGoal);
		flMinX = std::min(flMinX, vPos.x);
		flMaxX = std::max(flMaxX, vPos.x);
		flMinY = std::min(flMinY, vPos.y);
		flMaxY = std::max(flMaxY, vPos.y);
	}

	if (flMinX == FLT_MAX || flMinY == FLT_MAX)
		return;

	if ((flMaxX - flMinX) >= (flMaxY - flMinY))
	{
		m_vGoalAxis = { 1.0f, 0.0f, 0.0f };
		m_flGoalAxisCenter = (flMinX + flMaxX) * 0.5f;
	}
	else
	{
		m_vGoalAxis = { 0.0f, 1.0f, 0.0f };
		m_flGoalAxisCenter = (flMinY + flMaxY) * 0.5f;
	}

	m_bHasGoalAxis = true;
}

void CPasstimeController::UpdateTeamHomeSides()
{
	if (!m_bHasGoalAxis)
		return;

	struct side_sample_t
	{
		float flProjectionSum = 0.0f;
		int iCount = 0;
	};

	side_sample_t tSamples[4] = {};

	const int iLocalTeam = H::Entities.GetLocal() ? H::Entities.GetLocal()->m_iTeamNum() : TEAM_UNASSIGNED;
	const int iEnemyTeam = iLocalTeam == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED;

	for (int iTeam : { TF_TEAM_RED, TF_TEAM_BLUE })
	{
		if (m_bHasTeamSpawnCenter[iTeam])
		{
			const float flDelta = m_vTeamSpawnCenter[iTeam].Dot(m_vGoalAxis) - m_flGoalAxisCenter;
			if (fabsf(flDelta) > 64.0f)
			{
				m_iTeamHomeSide[iTeam] = flDelta >= 0.0f ? 1 : -1;
				m_bHasTeamHomeSide[iTeam] = true;
			}
		}
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
			if (!pEntity || pEntity->IsDormant())
				continue;

			auto pPlayer = pEntity->As<CTFPlayer>();
			if (!pPlayer || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() != iLocalTeam)
				continue;

			const float flProjection = pPlayer->GetAbsOrigin().Dot(m_vGoalAxis);
			tSamples[iLocalTeam].flProjectionSum += flProjection;
			tSamples[iLocalTeam].iCount++;
		}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() != iEnemyTeam)
			continue;

		const float flProjection = pPlayer->GetAbsOrigin().Dot(m_vGoalAxis);
		tSamples[iEnemyTeam].flProjectionSum += flProjection;
		tSamples[iEnemyTeam].iCount++;
	}

	for (int iTeam : { TF_TEAM_RED, TF_TEAM_BLUE })
	{
		if (!tSamples[iTeam].iCount)
			continue;

		const float flAverageProjection = tSamples[iTeam].flProjectionSum / tSamples[iTeam].iCount;
		const float flDelta = flAverageProjection - m_flGoalAxisCenter;
		if (fabsf(flDelta) < kPasstimeSideSwitchHysteresis && m_bHasTeamHomeSide[iTeam])
			continue;

		m_iTeamHomeSide[iTeam] = flDelta >= 0.0f ? 1 : -1;
		m_bHasTeamHomeSide[iTeam] = true;
	}
}

void CPasstimeController::UpdateTeamSpawnCenters()
{
	Vector vSum[4] = {};
	int iCount[4] = {};

	for (const auto& tRespawnRoom : F::NavEngine.GetRespawnRooms())
	{
		if (tRespawnRoom.tData.m_vCenter.IsZero())
			continue;

		if (tRespawnRoom.m_iTeam == 0 || tRespawnRoom.m_iTeam == TF_TEAM_RED)
		{
			vSum[TF_TEAM_RED] += tRespawnRoom.tData.m_vCenter;
			iCount[TF_TEAM_RED]++;
		}
		if (tRespawnRoom.m_iTeam == 0 || tRespawnRoom.m_iTeam == TF_TEAM_BLUE)
		{
			vSum[TF_TEAM_BLUE] += tRespawnRoom.tData.m_vCenter;
			iCount[TF_TEAM_BLUE]++;
		}
	}

	if (!iCount[TF_TEAM_RED] && !iCount[TF_TEAM_BLUE])
	{
		if (!F::NavEngine.IsNavMeshLoaded())
			return;

		for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			if (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED)
			{
				vSum[TF_TEAM_RED] += tArea.m_vCenter;
				iCount[TF_TEAM_RED]++;
			}
			if (tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)
			{
				vSum[TF_TEAM_BLUE] += tArea.m_vCenter;
				iCount[TF_TEAM_BLUE]++;
			}
		}
	}

	for (int iTeam : { TF_TEAM_RED, TF_TEAM_BLUE })
	{
		if (!iCount[iTeam])
			continue;

		m_vTeamSpawnCenter[iTeam] = vSum[iTeam] / static_cast<float>(iCount[iTeam]);
		m_bHasTeamSpawnCenter[iTeam] = true;
	}
}

int CPasstimeController::GetGoalSide(const Vector& vGoalPos) const
{
	if (!m_bHasGoalAxis)
		return 0;

	const float flDelta = vGoalPos.Dot(m_vGoalAxis) - m_flGoalAxisCenter;
	if (fabsf(flDelta) <= 64.0f)
		return 0;
	return flDelta >= 0.0f ? 1 : -1;
}

int CPasstimeController::GetTeamHomeSide(int iTeam) const
{
	if (iTeam < 0 || iTeam >= 4 || !m_bHasTeamHomeSide[iTeam])
		return 0;
	return m_iTeamHomeSide[iTeam];
}

bool CPasstimeController::IsNeutralGoalForScoringTeam(int iScoringTeam, const Vector& vGoalPos) const
{
	const int iTeamHomeSide = GetTeamHomeSide(iScoringTeam);
	const int iGoalSide = GetGoalSide(vGoalPos);
	if (!iTeamHomeSide || !iGoalSide)
		return true;
	return iGoalSide != iTeamHomeSide;
}

int CPasstimeController::GetGoalTeam(CFuncPasstimeGoal* pGoal, std::string* pTargetname) const
{
	if (!pGoal)
		return TEAM_UNASSIGNED;

	const int iMapTeam = SDK::GetPasstimeGoalMapTeam(GetObjectiveOrigin(pGoal), pTargetname);
	if (iMapTeam == TF_TEAM_RED || iMapTeam == TF_TEAM_BLUE)
		return GetOpposingPasstimeTeam(iMapTeam);

	return pGoal->m_iTeamNum();
}

CPasstimeBall* CPasstimeController::GetBall()
{
	if (!m_pBall && m_pLogic)
		m_pBall = m_pLogic->GetBall();

	return m_pBall;
}

CTFPasstimeLogic* CPasstimeController::GetLogic()
{
	return m_pLogic;
}

int CPasstimeController::GetCarrier()
{
	auto pBall = GetBall();
	if (!pBall)
		return -1;

	auto pCarrier = pBall->GetCarrier();
	if (!pCarrier || pCarrier->IsDormant() || !pCarrier->IsAlive())
		return -1;

	return pCarrier->entindex();
}

float CPasstimeController::GetMaxPassRange()
{
	return m_pLogic ? m_pLogic->m_flMaxPassRange() : FLT_MAX;
}

bool CPasstimeController::GetGoalInfo(int iScoringTeam, const Vector& vRelativePos, PasstimeGoalInfo& tOut)
{
	CFuncPasstimeGoal* pBestGoal = nullptr;
	float flBestDist = FLT_MAX;
	CFuncPasstimeGoal* pBestNeutralGoal = nullptr;
	float flBestNeutralEnemyRespawnDist = FLT_MAX;
	float flBestNeutralOwnRespawnDist = -1.0f;
	std::vector<std::string> vRejectReasons = {};
	vRejectReasons.reserve(m_vGoals.size());
	const int iOpposingTeam = iScoringTeam == TF_TEAM_BLUE ? TF_TEAM_RED : iScoringTeam == TF_TEAM_RED ? TF_TEAM_BLUE : TEAM_UNASSIGNED;

	for (auto pGoal : m_vGoals)
	{
		if (!pGoal)
		{
			vRejectReasons.push_back("goal=null");
			continue;
		}

		auto pGoalEntity = reinterpret_cast<CBaseEntity*>(pGoal);
		if (pGoal->m_bTriggerDisabled())
		{
			vRejectReasons.push_back(std::format("ent={} rejected=disabled team={} type={}",
				pGoalEntity->entindex(), GetPasstimeTeamName(GetGoalTeam(pGoal)), pGoal->m_iGoalType()));
			continue;
		}

		Vector vGoalPos = GetObjectiveOrigin(pGoal);
		if (vGoalPos.IsZero())
		{
			vRejectReasons.push_back(std::format("ent={} rejected=zero_origin team={} type={}",
				pGoalEntity->entindex(), GetPasstimeTeamName(GetGoalTeam(pGoal)), pGoal->m_iGoalType()));
			continue;
		}

		float flDist = vRelativePos.IsZero() ? 0.f : vRelativePos.DistToSqr(vGoalPos);
		const int iGoalTeam = GetGoalTeam(pGoal);
		if (iGoalTeam == iScoringTeam)
		{
			if (!pBestGoal || flDist < flBestDist)
			{
				pBestGoal = pGoal;
				flBestDist = flDist;
			}
			continue;
		}

		if (iGoalTeam == TEAM_UNASSIGNED || iGoalTeam == TEAM_INVALID || iGoalTeam == 0)
		{
			float flEnemyRespawnDist = iOpposingTeam != TEAM_UNASSIGNED
				? GetClosestRespawnTriggerDistSqr(iOpposingTeam, vGoalPos)
				: FLT_MAX;
			float flOwnRespawnDist = iScoringTeam != TEAM_UNASSIGNED
				? GetFarthestRespawnTriggerDistSqr(iScoringTeam, vGoalPos)
				: -1.0f;

			if (flEnemyRespawnDist == FLT_MAX && iOpposingTeam != TEAM_UNASSIGNED && m_bHasTeamSpawnCenter[iOpposingTeam])
				flEnemyRespawnDist = m_vTeamSpawnCenter[iOpposingTeam].DistToSqr(vGoalPos);
			if (flOwnRespawnDist < 0.0f && iScoringTeam != TEAM_UNASSIGNED && m_bHasTeamSpawnCenter[iScoringTeam])
				flOwnRespawnDist = m_vTeamSpawnCenter[iScoringTeam].DistToSqr(vGoalPos);

			const bool bBetterEnemyFit = flEnemyRespawnDist < flBestNeutralEnemyRespawnDist;
			const bool bEqualEnemyFit = fabsf(flEnemyRespawnDist - flBestNeutralEnemyRespawnDist) <= 1.0f;
			const bool bBetterOwnSeparation = flOwnRespawnDist > flBestNeutralOwnRespawnDist;
			if (!pBestNeutralGoal || bBetterEnemyFit || (bEqualEnemyFit && bBetterOwnSeparation))
			{
				pBestNeutralGoal = pGoal;
				flBestNeutralEnemyRespawnDist = flEnemyRespawnDist;
				flBestNeutralOwnRespawnDist = flOwnRespawnDist;
			}
			vRejectReasons.push_back(std::format("ent={} neutral_goal candidate type={} side={} enemy_spawn={:.0f} own_spawn={:.0f}",
				pGoalEntity->entindex(),
				pGoal->m_iGoalType(),
				GetPasstimeSideName(GetGoalSide(vGoalPos)),
				flEnemyRespawnDist == FLT_MAX ? -1.0f : sqrtf(flEnemyRespawnDist),
				flOwnRespawnDist < 0.0f ? -1.0f : sqrtf(flOwnRespawnDist)));
			continue;
		}

		vRejectReasons.push_back(std::format("ent={} rejected=team wanted={} actual={} type={}",
			pGoalEntity->entindex(), GetPasstimeTeamName(iScoringTeam), GetPasstimeTeamName(iGoalTeam), pGoal->m_iGoalType()));
	}

	if (!pBestGoal && pBestNeutralGoal)
	{
		pBestGoal = pBestNeutralGoal;
		static Timer tNeutralLogTimer{};
		if (Vars::Debug::Logging.Value && tNeutralLogTimer.Run(0.5f))
		{
			auto pGoalEntity = reinterpret_cast<CBaseEntity*>(pBestGoal);
			SDK::Output("PasstimeController", std::format(
				"GetGoalInfo: using neutral fallback ent={} requestedTeam={} goalType={} enemy_spawn={:.0f} own_spawn={:.0f}",
				pGoalEntity->entindex(),
				GetPasstimeTeamName(iScoringTeam),
				pBestGoal->m_iGoalType(),
				flBestNeutralEnemyRespawnDist == FLT_MAX ? -1.0f : sqrtf(flBestNeutralEnemyRespawnDist),
				flBestNeutralOwnRespawnDist < 0.0f ? -1.0f : sqrtf(flBestNeutralOwnRespawnDist)).c_str(),
				{ 255, 220, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
	}

	if (!pBestGoal)
	{
		static Timer tFailLogTimer{};
		if (Vars::Debug::Logging.Value && tFailLogTimer.Run(0.5f))
		{
			SDK::Output("PasstimeController", std::format(
				"GetGoalInfo: no goal for scoringTeam={} relativePos=({:.0f},{:.0f},{:.0f}) cachedGoals={}",
				GetPasstimeTeamName(iScoringTeam), vRelativePos.x, vRelativePos.y, vRelativePos.z, m_vGoals.size()).c_str(),
				{ 255, 140, 140 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);

			for (const auto& sReason : vRejectReasons)
			{
				SDK::Output("PasstimeController", sReason.c_str(), { 255, 180, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			}
		}
		return false;
	}

	tOut.m_pGoal = pBestGoal;
	tOut.m_iGoalType = pBestGoal->m_iGoalType();
	tOut.m_iTeam = GetGoalTeam(pBestGoal);
	tOut.m_vOrigin = GetObjectiveOrigin(pBestGoal);
	tOut.m_vMins = GetGoalWorldMins(pBestGoal);
	tOut.m_vMaxs = GetGoalWorldMaxs(pBestGoal);

	static Timer tSuccessLogTimer{};
	if (Vars::Debug::Logging.Value && tSuccessLogTimer.Run(0.5f))
	{
		auto pGoalEntity = reinterpret_cast<CBaseEntity*>(pBestGoal);
		SDK::Output("PasstimeController", std::format(
			"GetGoalInfo: selected ent={} scoringTeam={} goalTeam={} type={} origin=({:.0f},{:.0f},{:.0f})",
			pGoalEntity->entindex(),
			GetPasstimeTeamName(iScoringTeam),
			GetPasstimeTeamName(tOut.m_iTeam),
			tOut.m_iGoalType,
			tOut.m_vOrigin.x, tOut.m_vOrigin.y, tOut.m_vOrigin.z).c_str(),
			{ 120, 255, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}

	return !tOut.m_vOrigin.IsZero();
}

bool CPasstimeController::GetGoalPos(int iScoringTeam, const Vector& vRelativePos, Vector& vOut)
{
	PasstimeGoalInfo tGoal = {};
	if (!GetGoalInfo(iScoringTeam, vRelativePos, tGoal))
		return false;

	vOut = IsEndzoneGoal(tGoal.m_iGoalType)
		? AdjustObjectivePosToNav(tGoal.m_vOrigin)
		: GetThrowTargetPos(tGoal, vRelativePos);
	return !vOut.IsZero();
}

bool CPasstimeController::GetBallPos(Vector& vOut)
{
	auto pBall = GetBall();
	if (!pBall)
		return false;

	auto pCarrier = pBall->GetCarrier();
	if (pCarrier && !pCarrier->IsDormant() && pCarrier->IsAlive())
	{
		vOut = AdjustObjectivePosToNav(pCarrier->GetAbsOrigin());
		return !vOut.IsZero();
	}

	vOut = AdjustObjectivePosToNav(GetObjectiveOrigin(pBall));
	return !vOut.IsZero();
}

bool CPasstimeController::IsEndzoneGoal(int iGoalType) const
{
	return iGoalType == CFuncPasstimeGoal::TYPE_ENDZONE;
}

bool CPasstimeController::IsPointInGoal(const PasstimeGoalInfo& tGoal, const Vector& vPoint) const
{
	if (!tGoal.m_pGoal)
		return false;

	return vPoint.x >= tGoal.m_vMins.x - kPasstimeGoalPointEpsilon && vPoint.x <= tGoal.m_vMaxs.x + kPasstimeGoalPointEpsilon
		&& vPoint.y >= tGoal.m_vMins.y - kPasstimeGoalPointEpsilon && vPoint.y <= tGoal.m_vMaxs.y + kPasstimeGoalPointEpsilon
		&& vPoint.z >= tGoal.m_vMins.z - kPasstimeGoalPointEpsilon && vPoint.z <= tGoal.m_vMaxs.z + kPasstimeGoalPointEpsilon;
}

Vector CPasstimeController::GetThrowTargetPos(const PasstimeGoalInfo& tGoal, const Vector& vRelativePos)
{
	if (!tGoal.m_pGoal)
		return {};

	Vector vGoalCenter = (tGoal.m_vMins + tGoal.m_vMaxs) * 0.5f;
	const Vector vHalfExtents = (tGoal.m_vMaxs - tGoal.m_vMins) * 0.5f;
	const float flGoalRadius = std::max(vHalfExtents.Length2D(), 96.0f);
	const float flMaxPassRange = GetMaxPassRange();
	const std::array<float, 4> vStandOffs = flMaxPassRange != FLT_MAX
		? std::array<float, 4>{
			std::clamp(flMaxPassRange * 0.18f, 120.0f, 220.0f),
			std::clamp(flMaxPassRange * 0.28f, 180.0f, 320.0f),
			std::clamp(flMaxPassRange * 0.38f, 240.0f, 420.0f),
			std::clamp(flMaxPassRange * 0.48f, 300.0f, 520.0f) }
		: std::array<float, 4>{ 140.0f, 220.0f, 320.0f, 420.0f };

	Vector vPreferredDir = vRelativePos - vGoalCenter;
	vPreferredDir.z = 0.0f;
	if (vPreferredDir.Normalize() <= 0.01f)
		vPreferredDir = { 1.0f, 0.0f, 0.0f };

	Vector vBestCandidate = {};
	float flBestScore = -FLT_MAX;
	for (float flStandOff : vStandOffs)
	{
		for (int i = 0; i < 12; i++)
		{
			const float flYaw = Math::Deg2Rad(30.0f * i);
			Vector vDir = { cosf(flYaw), sinf(flYaw), 0.0f };
			Vector vCandidate = vGoalCenter + vDir * (flGoalRadius + flStandOff);
			vCandidate.z = tGoal.m_vOrigin.z;
			vCandidate = AdjustObjectivePosToNav(vCandidate);
			if (vCandidate.IsZero())
				continue;
			if (!HasPasstimeThrowStandSpace(vCandidate))
				continue;

			float flScore = 0.0f;
			flScore += vDir.Dot(vPreferredDir) * 120.0f;
			flScore -= vCandidate.DistToSqr(vRelativePos) * 0.0008f;
			flScore -= flStandOff * 2.0f;

			if (F::NavEngine.IsVectorVisibleNavigation(vCandidate + Vec3(0, 0, 45), vGoalCenter + Vec3(0, 0, 45), MASK_SHOT | CONTENTS_GRATE))
				flScore += 1200.0f;

			if (flScore > flBestScore)
			{
				flBestScore = flScore;
				vBestCandidate = vCandidate;
			}
		}
	}

	if (!vBestCandidate.IsZero())
		return vBestCandidate;

	Vector vFallback = vGoalCenter + vPreferredDir * (flGoalRadius + vStandOffs.front());
	vFallback.z = tGoal.m_vOrigin.z;
	return AdjustObjectivePosToNav(vFallback);
}
