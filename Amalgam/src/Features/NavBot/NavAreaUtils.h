#pragma once

#include "../../SDK/Definitions/Types.h"
#include <utility>

class CNavArea;

namespace NavAreaUtils
{
	auto FindClosestHidingSpot(
		CNavArea* pArea,
		const Vector& vVischeckPoint,
		int iRecursionCount,
		std::pair<CNavArea*, int>& tOut,
		bool bVischeck = true,
		int iRecursionIndex = 0) -> bool;
}
