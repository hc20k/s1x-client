#include "std_include.hpp"
#include "loader/component_loader.hpp"
#include "command.hpp"
#include "scheduler.hpp"
#include "game_console.hpp"

#include "trickshot.hpp"
#include <utils/hook.hpp>

game::dvar_t* ts_botLocationRadius;
game::dvar_t* ts_enableNoSpread;

game::vec3_t saved_location = { -1 };
game::vec3_t saved_bot_location = { -1 };

utils::hook::detour ls_playerdatagetintbyname_hook;
utils::hook::detour bg_srand_hook;

#define iprintln(ln) game::SV_GameSendServerCommand(0, game::svscmd_type::SV_CMD_CAN_IGNORE, ln)

namespace trickshot {

	void save_location() {
		iprintln("e \"^:Saved location.\"");

		auto* ps = game::SV_GetPlayerstateForClientNum(0);
		reinterpret_cast<void(*)(game::playerState_s*, game::vec3_t*)>(0x1402dc250)(ps, &saved_location); //  G_GetPlayerEyePosition
		game_console::print(0, "Saved location: %f, %f, %f", saved_location[0], saved_location[1], saved_location[2]);
	}

	void save_bot_location() {
		iprintln("e \"^:Saved bot location.\"");

		auto* ps = game::SV_GetPlayerstateForClientNum(0);
		game::G_GetPlayerEyePosition(ps, &saved_bot_location);
		game_console::print(0, "Saved bot location: %f, %f, %f", saved_bot_location[0], saved_bot_location[1], saved_bot_location[2]);

		for (int i = 1; i < 32; i++) {
			auto ent = game::mp::g_entities[i];

			game::vec3_t offset_location;
			offset_location[0] = saved_bot_location[0] + (rand() & ts_botLocationRadius->current.integer);
			offset_location[1] = saved_bot_location[1] + (rand() & ts_botLocationRadius->current.integer);
			offset_location[2] = saved_bot_location[2] + (rand() & ts_botLocationRadius->current.integer);

			if (ent.client) {
				game::SetClientOrigin(&ent, &offset_location, 0);
			}
		}
	}

	void n_key_pressed() {
		if (saved_location[0] == -1) {
			iprintln("e \"^1Error: saved location doesn't exist.\"");
		}
		else {
			game::SetClientOrigin(&game::mp::g_entities[0], &saved_location, 0);
		}
	}

	namespace {

		void on_game_started() {
			scheduler::schedule([]()
			{
				// Wait for game to end
				if (game::CL_IsCgameInitialized())
				{
					return scheduler::cond_continue;
				}

				return scheduler::cond_end;
			}, scheduler::pipeline::main, 1s);
			command::execute("spawnBot 18", true);
		}

		void give_random_class() {
			auto ps = game::SV_GetPlayerstateForClientNum(0);
			game::G_GivePlayerWeapon(ps, game::G_GetWeaponForName("defaultweapon_mp"), 0, 0, 0, 0, 0, 0);
		}

		int LiveStorage_PlayerDataGetIntByName(unsigned int p1, game::scr_string_t p2, int p3) {
			int orig = ls_playerdatagetintbyname_hook.invoke<int>(p1, p2, p3);

			return orig;
		}

		void BG_srand(int* out) {
			if (ts_enableNoSpread->current.enabled)
				*out = 0;
			else
				*out = (((*out * 0x343fd + 0x269ec3) * 0x343fd + 0x269ec3) * 0x343fd + 0x269ec3) * 0x343fd + 0x269ec3;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			ls_playerdatagetintbyname_hook.create(0x1403be860, &LiveStorage_PlayerDataGetIntByName); // Only snipers can kill
			utils::hook::call(0x1402c9a1b, &BG_srand); // No spread

			command::add("ts_saveLocation", []()
			{
				save_location();
			});

			command::add("ts_botSaveLocation", []()
			{
				save_bot_location();
			});

			ts_botLocationRadius = game::Dvar_RegisterInt("ts_botLocationRadius", 50, 0, 500, game::DVAR_FLAG_SAVED, "The distance the bots can stray from the saved bot location.");
			ts_enableNoSpread = game::Dvar_RegisterBool("ts_enableNoSpread", true, game::DVAR_FLAG_SAVED, "Disables weapon fire randomness.");

			command::add("trickshot", []()
			{
				command::execute("g_gametype dm", true);
				command::execute("ui_gametype dm", true);
				command::execute("scr_dm_scorelimit 1", true);
				command::execute("scr_dm_timelimit 0", true);

				// Patch bots so they don't kill
				utils::hook::set(0x1404096A4, 0x00); // Bot_CanSeeEnemy

				if (!game::CL_IsCgameInitialized())
				{
					game::SV_StartMap(0, "mp_terrace", false);
				}
				else command::execute("map_restart", true);

				scheduler::schedule([]()
				{
					// Wait for game to start
					if (!game::CL_IsCgameInitialized())
					{
						return scheduler::cond_continue;
					}

					on_game_started();
					return scheduler::cond_end;
				}, scheduler::pipeline::main, 1s);
			});
		}
	};
}

REGISTER_COMPONENT(trickshot::component)