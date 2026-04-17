#pragma once

#include "../NavEngine/NavEngine.h"
#include "../BotUtils.h"

struct NavAreaScore_t
{
	CNavArea* m_pArea = nullptr;
	float m_flScore = 0.f;
};

namespace NavJobUtils
{
	auto FindClosestTargetEnemy(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> ClosestEnemy_t;
	auto TryNavToAreaScores(std::vector<NavAreaScore_t>& vAreaScores, PriorityListEnum::PriorityListEnum ePriority, bool bLowestScoreFirst = true, size_t nMaxAttempts = 0) -> bool;
}
