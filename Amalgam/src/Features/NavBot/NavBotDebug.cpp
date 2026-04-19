#include "NavBotDebug.h"

#include <algorithm>
#include <format>
#include <string>

#include "DangerManager/DangerManager.h"
#include "NavBotCore.h"
#include "NavBotJobs/Capture.h"
#include "NavBotJobs/Engineer.h"
#include "NavBotJobs/Reload.h"
#include "NavBotJobs/Roam.h"
#include "NavBotJobs/StayNear.h"
#include "NavEngine/NavEngine.h"
#include "../CritHack/CritHack.h"
#include "../../SDK/SDK.h"

namespace
{
	auto BuildJobLabel() -> std::wstring
	{
		switch (F::NavEngine.m_eCurrentPriority)
		{
		case PriorityListEnum::Patrol:
		{
			auto s_job = F::NavBotRoam.m_bDefending ? std::wstring(L"Defend") : std::wstring(L"Patrol");
			if (F::NavBotRoam.m_bDefending && !F::NavBotCapture.m_sCaptureStatus.empty())
			{
				s_job += L" (";
				s_job += F::NavBotCapture.m_sCaptureStatus;
				s_job += L')';
			}
			return s_job;
		}
		case PriorityListEnum::LowPrioGetHealth:
			return L"Get health (Low-Prio)";
		case PriorityListEnum::StayNear:
			return std::format(L"Stalk enemy ({})", F::NavBotStayNear.m_sFollowTargetName.data());
		case PriorityListEnum::RunReload:
			return L"Run reload";
		case PriorityListEnum::RunSafeReload:
			return L"Run safe reload";
		case PriorityListEnum::SnipeSentry:
			return L"Snipe sentry";
		case PriorityListEnum::GetAmmo:
			return L"Get ammo";
		case PriorityListEnum::Capture:
		{
			auto s_job = std::wstring(L"Capture");
			if (!F::NavBotCapture.m_sCaptureStatus.empty())
			{
				s_job += L" (";
				s_job += F::NavBotCapture.m_sCaptureStatus;
				s_job += L')';
			}
			return s_job;
		}
		case PriorityListEnum::MeleeAttack:
			return L"Melee";
		case PriorityListEnum::Engineer:
		{
			std::wstring s_job = L"Engineer (";
			switch (F::NavBotEngineer.m_eTaskStage)
			{
			case EngineerTaskStageEnum::BuildSentry:
				s_job += L"Build sentry";
				break;
			case EngineerTaskStageEnum::BuildDispenser:
				s_job += L"Build dispenser";
				break;
			case EngineerTaskStageEnum::SmackSentry:
				s_job += L"Smack sentry";
				break;
			case EngineerTaskStageEnum::SmackDispenser:
				s_job += L"Smack dispenser";
				break;
			default:
				s_job += L"None";
				break;
			}
			s_job += L')';
			return s_job;
		}
		case PriorityListEnum::GetHealth:
			return L"Get health";
		case PriorityListEnum::EscapeSpawn:
			return L"Escape spawn";
		case PriorityListEnum::EscapeDanger:
			return L"Escape danger";
		case PriorityListEnum::Followbot:
			return L"FollowBot";
		default:
			return L"None";
		}
	}
}

namespace NavBotDebug
{
	void Draw(CTFPlayer* pLocal)
	{
		if (!pLocal || !(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive())
			return;

		const bool b_is_ready = F::NavEngine.IsReady();
		if (!Vars::Debug::Info.Value && !b_is_ready)
			return;

		int x = Vars::Menu::NavBotDisplay.Value.x;
		int y = Vars::Menu::NavBotDisplay.Value.y + 8;
		const auto& f_font = H::Fonts.GetFont(FONT_INDICATORS);
		const int n_tall = f_font.m_nTall + H::Draw.Scale(1);

		EAlign e_align = ALIGN_TOP;
		if (x <= 100 + H::Draw.Scale(50, Scale_Round))
		{
			x -= H::Draw.Scale(42, Scale_Round);
			e_align = ALIGN_TOPLEFT;
		}
		else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
		{
			x += H::Draw.Scale(42, Scale_Round);
			e_align = ALIGN_TOPRIGHT;
		}

		const auto& t_color = F::NavEngine.IsPathing() ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
		const auto& t_ready_color = b_is_ready ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
		int i_in_spawn = -1;
		int i_area_flags = -1;
		if (F::NavEngine.IsNavMeshLoaded())
		{
			if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
			{
				i_area_flags = pLocalArea->m_iTFAttributeFlags;
				i_in_spawn = i_area_flags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED);
			}
		}

		const auto s_job = BuildJobLabel();
		H::Draw.StringOutlined(
			f_font,
			x,
			y,
			t_color,
			Vars::Menu::Theme::Background.Value,
			e_align,
			std::format(L"Job: {} {}", s_job, std::wstring(F::CritHack.m_bForce ? L"(Crithack on)" : L"")).data());

		if (F::NavEngine.IsPathing())
		{
			auto p_crumbs = F::NavEngine.GetCrumbs();
			const float fl_dist = pLocal->GetAbsOrigin().DistTo(F::NavEngine.m_vLastDestination);
			H::Draw.StringOutlined(f_font, x, y += n_tall, t_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Nodes: {} (Dist: {:.0f})", p_crumbs->size(), fl_dist).c_str());
		}

		const float fl_idle_time = SDK::PlatFloatTime() - F::NavBotCore.m_tIdleTimer.GetLastUpdate();
		if (fl_idle_time > 2.0f && F::NavEngine.IsPathing())
			H::Draw.StringOutlined(f_font, x, y += n_tall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, e_align, std::format("Stuck: {:.1f}s", fl_idle_time).c_str());

		if (!F::NavEngine.IsPathing() && !F::NavEngine.m_sLastFailureReason.empty())
			H::Draw.StringOutlined(f_font, x, y += n_tall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, e_align, std::format("Failed: {}", F::NavEngine.m_sLastFailureReason).c_str());

		if (!Vars::Debug::Info.Value)
			return;

		H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Is ready: {}", std::to_string(b_is_ready)).c_str());
		H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Priority: {}", static_cast<int>(F::NavEngine.m_eCurrentPriority)).c_str());
		H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("In spawn: {}", std::to_string(i_in_spawn)).c_str());
		H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Area flags: {}", std::to_string(i_area_flags)).c_str());

		if (F::NavEngine.IsNavMeshLoaded())
		{
			H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Map: {}", F::NavEngine.GetNavFilePath()).c_str());
			if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
				H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Area ID: {}", pLocalArea->m_uId).c_str());
			H::Draw.StringOutlined(f_font, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Total areas: {}", F::NavEngine.GetNavFile()->m_vAreas.size()).c_str());
		}

		if (F::NavEngine.IsPathing() || F::NavEngine.m_vLastDestination.Length() > 0.f)
		{
			const auto& v_dest = F::NavEngine.m_vLastDestination;
			H::Draw.StringOutlined(f_font, x, y += n_tall, t_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Dest: {:.0f}, {:.0f}, {:.0f}", v_dest.x, v_dest.y, v_dest.z).c_str());
		}

		const bool b_is_idle = F::NavEngine.m_eCurrentPriority == PriorityListEnum::None || !F::NavEngine.IsPathing();
		H::Draw.StringOutlined(
			f_font,
			x,
			y += n_tall,
			b_is_idle ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value,
			Vars::Menu::Theme::Background.Value,
			e_align,
			std::format("Idle: {} ({:.1f}s)", b_is_idle ? "Yes" : "No", std::max(0.f, fl_idle_time)).c_str());

		if (!Vars::Misc::Movement::NavBot::DangerOverlay.Value)
			return;

		int i_drawn = 0;
		const float fl_max_dist = Vars::Misc::Movement::NavBot::DangerOverlayMaxDist.Value;
		const float fl_max_dist_sqr = fl_max_dist * fl_max_dist;
		for (const auto& [p_area, t_data] : F::DangerManager.GetDangerMap())
		{
			if (!F::NavEngine.GetNavMap() || !F::NavEngine.GetNavMap()->IsAreaValid(p_area) || t_data.m_flScore <= 0.f)
				continue;

			if (p_area->m_vCenter.DistToSqr(pLocal->GetAbsOrigin()) > fl_max_dist_sqr)
				continue;

			Color_t t_overlay_color = Color_t(255, 200, 0, 80);
			if (t_data.m_flScore >= DANGER_SCORE_STICKY)
				t_overlay_color = Color_t(255, 50, 50, 90);
			else if (t_data.m_flScore >= DANGER_SCORE_ENEMY_NORMAL)
				t_overlay_color = Color_t(255, 140, 0, 90);

			G::SphereStorage.push_back({ p_area->m_vCenter, 24.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, t_overlay_color, Color_t(), true });

			if (++i_drawn >= 64)
				break;
		}
	}
}
