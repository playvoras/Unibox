#pragma once
#include "../../../../../SDK/SDK.h"

struct PasstimeGoalInfo
{
	CFuncPasstimeGoal* m_pGoal = nullptr;
	int m_iGoalType = CFuncPasstimeGoal::TYPE_HOOP;
	int m_iTeam = TEAM_UNASSIGNED;
	Vector m_vOrigin = {};
	Vector m_vMins = {};
	Vector m_vMaxs = {};
};

class CPasstimeController
{
private:
	std::vector<CFuncPasstimeGoal*> m_vGoals = {};
	CPasstimeBall* m_pBall = nullptr;
	CTFPasstimeLogic* m_pLogic = nullptr;
	Vector m_vGoalAxis = { 1.0f, 0.0f, 0.0f };
	float m_flGoalAxisCenter = 0.0f;
	bool m_bHasGoalAxis = false;
	int m_iTeamHomeSide[4] = {};
	bool m_bHasTeamHomeSide[4] = {};
	Vector m_vTeamSpawnCenter[4] = {};
	bool m_bHasTeamSpawnCenter[4] = {};

	void UpdateGoalAxis();
	void UpdateTeamHomeSides();
	void UpdateTeamSpawnCenters();
	int GetGoalSide(const Vector& vGoalPos) const;
	int GetTeamHomeSide(int iTeam) const;
	bool IsNeutralGoalForScoringTeam(int iScoringTeam, const Vector& vGoalPos) const;
	int GetGoalTeam(CFuncPasstimeGoal* pGoal, std::string* pTargetname = nullptr) const;

public:
	void Init();
	void Update();

	CPasstimeBall* GetBall();
	CTFPasstimeLogic* GetLogic();
	int GetCarrier();
	float GetMaxPassRange();

	bool GetGoalInfo(int iScoringTeam, const Vector& vRelativePos, PasstimeGoalInfo& tOut);
	bool GetGoalPos(int iScoringTeam, const Vector& vRelativePos, Vector& vOut);
	bool GetBallPos(Vector& vOut);
	bool IsEndzoneGoal(int iGoalType) const;
	bool IsPointInGoal(const PasstimeGoalInfo& tGoal, const Vector& vPoint) const;
	Vector GetThrowTargetPos(const PasstimeGoalInfo& tGoal, const Vector& vRelativePos);
};

ADD_FEATURE(CPasstimeController, PasstimeController);
