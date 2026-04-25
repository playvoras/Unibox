#include "ConVars.h"

#include "../../Definitions/Interfaces/ICVar.h"
#include "../../../Utils/Hash/FNV1A.h"

bool CConVars::UnlockInternal()
{
	if (!m_bUnlocked)
	{
		ConCommandBase* pCmdBase = I::CVar->GetCommands();
		while (pCmdBase != nullptr)
		{
			m_mFlagMap[pCmdBase] = pCmdBase->m_nFlags;
			pCmdBase->m_nFlags &= ~(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT | FCVAR_NOT_CONNECTED);
			pCmdBase = pCmdBase->m_pNext;
		}
		m_bUnlocked = true;

		return true;
	}
	return false;
}

bool CConVars::RestoreInternal()
{
	if (m_bUnlocked)
	{
		for (auto& [pCmdBase, nFlags] : m_mFlagMap)
			pCmdBase->m_nFlags = nFlags;
		m_mFlagMap.clear();
		m_bUnlocked = false;

		return true;
	}
	return false;
}

bool CConVars::Unlock()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return UnlockInternal();
}

bool CConVars::Restore()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return RestoreInternal();
}

bool CConVars::Modify(bool bUnlock)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	if (bUnlock == m_bUnlocked)
		return false;

	return bUnlock ? UnlockInternal() : RestoreInternal();
}

ConVar* CConVars::FindVar(const char* sCVar)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	auto uHash = FNV1A::Hash32(sCVar);
	if (!m_mCVarMap.contains(uHash))
		m_mCVarMap[uHash] = I::CVar->FindVar(sCVar);
	return m_mCVarMap[uHash];
}
