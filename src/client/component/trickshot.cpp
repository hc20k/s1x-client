#include "std_include.hpp"
#include "loader/component_loader.hpp"
#include "command.hpp"
#include "scheduler.hpp"
#include "game_console.hpp"

#include "trickshot.hpp"
#include <utils/hook.hpp>
#include <utils/string.hpp>

game::dvar_t* ts_botLocationRadius;
game::dvar_t* ts_enableNoSpread;

game::vec3_t saved_location = { -1 };
game::vec3_t saved_bot_location = { -1 };

utils::hook::detour clientspawn_hook;
utils::hook::detour bg_getdamage_hook;
utils::hook::detour bg_getsurfacepenetrationdepth_hook;

bool is_first_spawn = false;

#define ssc(ln) game::SV_GameSendServerCommand(0, game::svscmd_type::SV_CMD_CAN_IGNORE, ln)

namespace trickshot {

	void save_location() {
		ssc("e \"^:Saved location.\"");

		auto* ps = game::SV_GetPlayerstateForClientNum(0);
		reinterpret_cast<void(*)(game::playerState_s*, game::vec3_t*)>(0x1402dc250)(ps, &saved_location); //  G_GetPlayerEyePosition
		game_console::print(0, "Saved location: %f, %f, %f", saved_location[0], saved_location[1], saved_location[2]);
	}

	void save_bot_location() {
		ssc("e \"^:Saved bot location.\"");

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
			ssc("e \"^1Error: saved location doesn't exist.\"");
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

				saved_location[0] = -1;
				saved_bot_location[0] = -1;
				is_first_spawn = false;

				return scheduler::cond_end;
			}, scheduler::pipeline::main, 1s);


			//game::Dvar_FindVar("perk_bulletPenetrationMultiplier")->current.value = 30.f;
		}

		void give_random_class() {
			auto ps = game::SV_GetPlayerstateForClientNum(0);
			game::G_GivePlayerWeapon(ps, game::G_GetWeaponForName("defaultweapon_mp"), 0, 0, 0, 0, 0, 0);
		}

		void BG_srand(int* out) {
			if (ts_enableNoSpread->current.enabled)
				*out = 0;
			else
				*out = (((*out * 0x343fd + 0x269ec3) * 0x343fd + 0x269ec3) * 0x343fd + 0x269ec3) * 0x343fd + 0x269ec3;
		}

		void on_host_spawned(game::mp::gentity_s* ent) {
			ent->flags ^= 1; // god mode

			if (!is_first_spawn) {
				is_first_spawn = true;
				command::execute(utils::string::va("spawnBot %d", game::Dvar_FindVar("party_maxplayers")->current.integer - 1), true);
			}
		}

		void ClientSpawn(game::mp::gentity_s* ent, float* origin, float* angles) {
			if (ent->client) {
				int clientNum = ent->client->__pad0[0x5090];

				if (clientNum == 0) {
					on_host_spawned(ent);
					if (saved_location[0] != -1) {
						origin[0] = saved_location[0];
						origin[1] = saved_location[1];
						origin[2] = saved_location[2];
					}
				}
				else {
					if (saved_bot_location[0] != -1) {
						origin[0] = saved_bot_location[0];
						origin[1] = saved_bot_location[1];
						origin[2] = saved_bot_location[2];
					}
				}
			}
			clientspawn_hook.invoke<void>(ent, origin, angles);
		}

		int BG_GetDamage(unsigned int weapon, unsigned int p2, unsigned long long p3) {
			// BG_GetWeaponClass(weap, bool) 0x14016c8c0
			auto weaponClass = reinterpret_cast<game::weapClass_t(*)(unsigned int, bool)>(0x14016c8c0)(weapon, true);
		
			if (weaponClass != game::weapClass_t::WEAPCLASS_SNIPER) {
				return 0;
			}

			return 999;
		}
		
		float BG_GetSurfacePenetrationDepth(unsigned int weapon, bool p2, int p3) {
			return 999.f;
		}

	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::call(0x1402c9a1b, &BG_srand); // No spread
			clientspawn_hook.create(0x1402db5a0, &ClientSpawn);
			bg_getsurfacepenetrationdepth_hook.create(0x1401641a0, &BG_GetSurfacePenetrationDepth);
			bg_getdamage_hook.create(0x140169f70, &BG_GetDamage);

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

			command::add("ts_addBot", []()
				{
					auto* ent = game::SV_AddTestClient(0);
					game_console::print(0, "added bot @ %p", &ent);
				});

			command::add("trickshot", []()
			{
				command::execute("g_gametype dm", true);
				command::execute("ui_gametype dm", true);
				command::execute("scr_dm_scorelimit 1", true);
				command::execute("scr_dm_timelimit 0", true);
				command::execute("onlinemode 1", true);

				// Patch bots so they don't kill
				utils::hook::nop(0x14042a082, 5); // Bot_UpdateThreat
				utils::hook::nop(0x14042a092, 5); // Bot_UpdateDistToEnemy
				utils::hook::set(0x1404096A4, 0x00); // Bot_CanSeeEnemy

				if (!game::CL_IsCgameInitialized())
				{
					game::SV_StartMap(0, "mp_venus", false);
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