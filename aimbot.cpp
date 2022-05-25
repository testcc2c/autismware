#include "includes.h"

Aimbot g_aimbot{ };;
bool CanFireWithExploit(int m_iShiftedTick)
{
	// curtime before shift
	float curtime = game::TICKS_TO_TIME(g_cl.m_local->m_nTickBase() - m_iShiftedTick);
	return g_cl.CanFireWeapon(curtime);
}

bool Aimbot::CanDT() {

	int idx = g_cl.m_weapon->m_iItemDefinitionIndex();
	return g_cl.m_local->alive()
		&& g_csgo.m_cl->m_choked_commands <= 1
		&& m_double_tap;
}

void Aimbot::DoubleTap()
{
	if (!CanDT() || !m_double_tap)
		return;

	static bool did_shift_before = false;
	static int double_tapped = 0;
	static int prev_shift_ticks = 0;
	static bool reset = false;
	int shift_ticks = 14;
	g_cl.m_tick_to_shift = 0;

	if (CanFireWithExploit(shift_ticks) || !CanFireWithExploit(abs(-1 - prev_shift_ticks)) && !did_shift_before)
	{
		prev_shift_ticks = shift_ticks;
		double_tapped = 0;
	}
	else {
		double_tapped++;
		prev_shift_ticks = 0;
	}

	if (!prev_shift_ticks > 0)
		return;

	if (g_cl.m_weapon->DTable() && CanFireWithExploit(prev_shift_ticks)) //can dt check
	{
		if (g_cl.m_cmd->m_buttons & IN_ATTACK) //shift
		{
			g_cl.m_tick_to_shift = prev_shift_ticks;
			reset = true;
		}
		else if ((!(g_cl.m_cmd->m_buttons & IN_ATTACK) || !g_cl.m_shot) && reset && fabsf(g_cl.m_weapon->m_fLastShotTime() - game::TICKS_TO_TIME(g_cl.m_local->m_nTickBase())) > 0.5f) //recharge
		{
			g_cl.m_charged = false;
			g_cl.m_tick_to_recharge = shift_ticks;
			reset = false;
		}
	}


	did_shift_before = prev_shift_ticks != 0;
}

void AimPlayer::UpdateAnimations(LagRecord* record) {
	CCSGOPlayerAnimState* state = m_player->m_PlayerAnimState();
	if (!state)
		return;

	// player respawned.
	if (m_player->m_flSpawnTime() != m_spawn) {
		// reset animation state.
		game::ResetAnimationState(state);

		// note new spawn time.
		m_spawn = m_player->m_flSpawnTime();
	}

	// first off lets backup our globals.
	auto curtime = g_csgo.m_globals->m_curtime;
	auto realtime = g_csgo.m_globals->m_realtime;
	auto frametime = g_csgo.m_globals->m_frametime;
	auto absframetime = g_csgo.m_globals->m_abs_frametime;
	auto framecount = g_csgo.m_globals->m_frame;
	auto tickcount = g_csgo.m_globals->m_tick_count;
	auto interpolation = g_csgo.m_globals->m_interp_amt;

	// backup stuff that we do not want to fuck with.
	AnimationBackup_t backup;

	backup.m_origin = m_player->m_vecOrigin();
	backup.m_abs_origin = m_player->GetAbsOrigin();
	backup.m_velocity = m_player->m_vecVelocity();
	backup.m_abs_velocity = m_player->m_vecAbsVelocity();
	backup.m_flags = m_player->m_fFlags();
	backup.m_eflags = m_player->m_iEFlags();
	backup.m_duck = m_player->m_flDuckAmount();
	backup.m_body = m_player->m_flLowerBodyYawTarget();
	m_player->GetAnimLayers(backup.m_layers);

	// set globals.
	g_csgo.m_globals->m_curtime = record->m_anim_time;
	g_csgo.m_globals->m_realtime = record->m_anim_time;
	g_csgo.m_globals->m_frame = game::TIME_TO_TICKS(record->m_anim_time);
	g_csgo.m_globals->m_tick_count = game::TIME_TO_TICKS(record->m_anim_time);
	g_csgo.m_globals->m_frametime = g_csgo.m_globals->m_interval;
	g_csgo.m_globals->m_abs_frametime = g_csgo.m_globals->m_interval;
	g_csgo.m_globals->m_interp_amt = 0.f;


	// is player a bot?
	bool bot = game::IsFakePlayer(m_player->index());
	// reset resolver/fakewalk/fakeflick state.
	record->m_mode = Resolver::Modes::RESOLVE_NONE;
	record->m_fake_walk = false;
	record->m_fake_flick = false;

	// thanks llama.
	if (record->m_flags & FL_ONGROUND) {
		// they are on ground.
		state->m_ground = true;
		// no they didnt land.
		state->m_land = false;
	}

	// fix velocity.
	if (record->m_lag > 0 && record->m_lag < 16 && m_records.size() >= 2) {
		// get pointer to previous record.
		LagRecord* previous = m_records[1].get();

		// valid previous record.
		if (previous && !previous->dormant()) {
			// get the sim delta between both records.
			record->m_choke_time = record->m_sim_time - previous->m_sim_time;

			// how many ticks did he choke?
			const int choked = game::TIME_TO_TICKS(record->m_choke_time);

			// set this.
			record->m_lag = choked;

			// this is record is out of lagcompensation.
			if ((choked - 1) > 19 || previous->m_sim_time == 0.f) {
				record->m_choke_time = g_csgo.m_globals->m_interval;
				record->m_lag = 1;
			}

			// he isnt choking any packets.
			if (record->m_lag < 1) {
				record->m_choke_time = g_csgo.m_globals->m_interval;
				record->m_lag = 1;
			}

			// "default" velocity we will be using if all other calculations seem to fail.
			record->m_velocity = (record->m_origin - previous->m_origin) * (1.f / record->m_choke_time);
		}
	}


	// fix CGameMovement::FinishGravity
	if (!(m_player->m_fFlags() & FL_ONGROUND))
		record->m_velocity.z -= game::TICKS_TO_TIME(g_csgo.sv_gravity->GetFloat());
	else
		record->m_velocity.z = 0.0f;

	// set this fucker, it will get overriden.
	record->m_anim_velocity = record->m_velocity;

	// fix various issues with the game.
	// these issues can only occur when a player is choking data.
	if (record->m_lag > 1 && !bot) {

		// detect fakewalk.
		float speed = record->m_velocity.length();

		if (record->m_flags & FL_ONGROUND && record->m_layers[6].m_weight == 0.f && speed > 0.1f && speed < 100.f)
			record->m_fake_walk = true;

		if (record->m_fake_walk)
			record->m_anim_velocity = record->m_velocity = { 0.f, 0.f, 0.f };

		// detect fake flick players.
		if (m_records.size() >= 2) {
			auto previous = m_records[1].get();
			if (previous && !previous->dormant()) {
				// they are fake flicking, we DUMP HERE!
				if (record->m_velocity.length() < 18.f
					&& record->m_layers[6].m_weight != 1.0f
					&& record->m_layers[6].m_weight != 0.0f
					&& record->m_layers[6].m_weight != previous->m_layers[6].m_weight
					&& (record->m_flags & FL_ONGROUND))
					record->m_fake_flick = true;
			}
		}

		// if they fakewalk scratch this shit.
		if (record->m_fake_walk)
			record->m_anim_velocity = record->m_velocity = { 0.f, 0.f, 0.f };

		// we need atleast 2 updates/records
		// to fix these issues.
		if (m_records.size() >= 2) {
			// get pointer to previous record.
			LagRecord* previous = m_records[1].get();

			// valid previous record.
			if (previous && !previous->dormant()) {
				// LOL.
				if ((record->m_origin - previous->m_origin).is_zero())
					record->m_anim_velocity = record->m_velocity = { 0.f, 0.f, 0.f };

				// jumpfall.
				bool bOnGround = record->m_flags & FL_ONGROUND;
				bool bJumped = false;
				bool bLandedOnServer = false;
				float flLandTime = 0.f;

				if (record->m_layers[4].m_cycle < 0.5f && (!(record->m_flags & FL_ONGROUND) || !(previous->m_flags & FL_ONGROUND))) {
					flLandTime = record->m_sim_time - float(record->m_layers[4].m_playback_rate / record->m_layers[4].m_cycle);
					bLandedOnServer = flLandTime >= previous->m_sim_time;
				}

				if (bLandedOnServer && !bJumped) {
					if (flLandTime <= record->m_anim_time) {
						bJumped = true;
						bOnGround = true;
					}
					else {
						bOnGround = previous->m_flags & FL_ONGROUND;
					}
				}

				if (bOnGround) {
					m_player->m_fFlags() |= FL_ONGROUND;
				}
				else {
					m_player->m_fFlags() &= ~FL_ONGROUND;
				}

				// delta in duckamt and delta in time..
				float duck = record->m_duck - previous->m_duck;
				float time = record->m_sim_time - previous->m_sim_time;

				// get the duckamt change per tick.
				float change = (duck / time) * g_csgo.m_globals->m_interval;

				// fix legs staying up in air.
				if (record->m_flags & FL_ONGROUND)
					record->m_layers[4].m_cycle = 0;

				// fix crouching players.
				m_player->m_flDuckAmount() = previous->m_duck + change;

				if (!record->m_fake_walk) {
					// fix the velocity till the moment of animation.
					vec3_t velo = record->m_velocity - previous->m_velocity;

					// accel per tick.
					vec3_t accel = (velo / time) * g_csgo.m_globals->m_interval;

					// set the anim velocity to the previous velocity.
					// and predict one tick ahead.
					record->m_anim_velocity = previous->m_velocity + accel;
				}
			}
		}
	}

	// lol?
	for (int i = 0; i < 13; i++)
		m_player->m_AnimOverlay()[i].m_owner = m_player;

	bool fake = g_menu.main.aimbot.resolver.get() && !bot;

	// if using fake angles, correct angles.
	if (fake)
		g_resolver.ResolveAngles(m_player, record);

	// skip call to C_BaseEntity::CalcAbsoluteVelocity
	m_player->m_iEFlags() &= ~0x1000; // EFL_DIRTY_ABSVELOCITY

	// set stuff before animating.
	m_player->m_vecOrigin() = record->m_origin;
	m_player->m_vecVelocity() = m_player->m_vecAbsVelocity() = record->m_anim_velocity;
	m_player->m_flLowerBodyYawTarget() = record->m_body;

	// invalid angles check (z) 
	// in this build of csgo viewangels.z does not change hitboxes, so if viewangles.z != 0.0f the player is changing them.
	// this is known to cause resolving issues, so to fix this just set viewangles.z to 0.0f
	if (record->m_eye_angles.z != 0.0f)
		record->m_eye_angles.z = 0.0f;

	// write potentially resolved angles.
	m_player->m_angEyeAngles() = record->m_eye_angles;

	// fixes for networked players.
	g_csgo.m_globals->m_frametime = g_csgo.m_globals->m_interval;
	g_csgo.m_globals->m_curtime = m_player->m_flSimulationTime();

	// fix animating in same frame.
	if (state->m_frame == g_csgo.m_globals->m_frame)
		state->m_frame -= 1;

	// get invalidated bone cache.
	static auto& invalidatebonecache = pattern::find(g_csgo.m_client_dll, XOR("C6 05 ? ? ? ? ? 89 47 70")).add(0x2);

	// make sure we keep track of the original invalidation state
	const auto oldbonecache = invalidatebonecache;

	// update animations now.
	m_player->m_bClientSideAnimation() = true;
	m_player->UpdateClientSideAnimation();
	m_player->m_bClientSideAnimation() = false;

	// we don't want to enable cache invalidation by accident
	invalidatebonecache = oldbonecache;

	// player animations have updated.
	m_player->InvalidatePhysicsRecursive(InvalidatePhysicsBits_t::ANGLES_CHANGED);
	m_player->InvalidatePhysicsRecursive(InvalidatePhysicsBits_t::ANIMATION_CHANGED);
	m_player->InvalidatePhysicsRecursive(InvalidatePhysicsBits_t::BOUNDS_CHANGED);
	m_player->InvalidatePhysicsRecursive(InvalidatePhysicsBits_t::SEQUENCE_CHANGED);

	// if fake angles.
	if (fake) {
		// correct poses.
		g_resolver.ResolvePoses(m_player, record);
	}

	// store updated/animated poses and rotation in lagrecord.
	m_player->GetPoseParameters(record->m_poses);
	record->m_abs_ang = m_player->GetAbsAngles();

	// restore backup data.
	m_player->m_vecOrigin() = backup.m_origin;
	m_player->m_vecVelocity() = backup.m_velocity;
	m_player->m_vecAbsVelocity() = backup.m_abs_velocity;
	m_player->m_fFlags() = backup.m_flags;
	m_player->m_iEFlags() = backup.m_eflags;
	m_player->m_flDuckAmount() = backup.m_duck;
	m_player->m_flLowerBodyYawTarget() = backup.m_body;
	m_player->SetAbsOrigin(backup.m_abs_origin);
	m_player->SetAnimLayers(backup.m_layers);

	// restore globals.
	g_csgo.m_globals->m_curtime = curtime;
	g_csgo.m_globals->m_realtime = realtime;
	g_csgo.m_globals->m_frametime = frametime;
	g_csgo.m_globals->m_abs_frametime = absframetime;
	g_csgo.m_globals->m_frame = framecount;
	g_csgo.m_globals->m_tick_count = tickcount;
	g_csgo.m_globals->m_interp_amt = interpolation;

	// IMPORTANT: do not restore poses here, since we want to preserve them for rendering.
	// also dont restore the render angles which indicate the model rotation.
}

void AimPlayer::OnNetUpdate(Player* player) {
	bool reset = (!g_menu.main.aimbot.enable.get() || player->m_lifeState() == LIFE_DEAD || !player->enemy(g_cl.m_local));
	bool disable = (!reset && (!g_cl.m_processing || !g_cl.m_local->alive()));

	// if this happens, delete all the lagrecords.
	if (reset) {
		player->m_bClientSideAnimation() = true;
		m_records.clear();
		return;
	}

	// just disable anim if this is the case.
	if (disable) {
		player->m_bClientSideAnimation() = true;
		return;
	}

	// update player ptr if required.
	// reset player if changed.
	if (m_player != player)
		m_records.clear();

	// update player ptr.
	m_player = player;

	// indicate that this player has been out of pvs.
	// insert dummy record to separate records
	// to fix stuff like animation and prediction.
	if (player->dormant()) {
		bool insert = true;

		// we have any records already?
		if (!m_records.empty()) {

			LagRecord* front = m_records.front().get();

			// we already have a dormancy separator.
			if (front->dormant())
				insert = false;
		}

		if (insert) {
			// add new record.
			m_records.emplace_front(std::make_shared< LagRecord >(player));

			// get reference to newly added record.
			LagRecord* current = m_records.front().get();

			// mark as dormant.
			current->m_dormant = true;
		}
	}

	if (!(m_records.empty() || player->m_flSimulationTime() > m_records.front().get()->m_sim_time) && !player->dormant() && player->m_vecOrigin() != player->m_vecOldOrigin())
		player->m_flSimulationTime() = game::TICKS_TO_TIME(g_csgo.m_cl->m_server_tick); // fix our simulation time.

	// this is the first data update we are receving
	// OR we received data with a newer simulation context.
	if (m_records.empty() || player->m_flSimulationTime() > m_records.front().get()->m_sim_time) {
		// add new record.
		m_records.emplace_front(std::make_shared< LagRecord >(player));

		// get reference to newly added record.
		LagRecord* current = m_records.front().get();

		// mark as non dormant.
		current->m_dormant = false;

		// update animations on current record.
		// call resolver.
		UpdateAnimations(current);

		// create bone matrix for this record.
		g_bones.setup(m_player, nullptr, current);
	}

	// no need to store insane amt of data.
	while (m_records.size() > 256)
		m_records.pop_back();
}

void AimPlayer::OnRoundStart(Player* player) {
	m_player = player;
	m_walk_record = LagRecord{ };
	m_shots = 0;
	m_missed_shots = 0;
	m_stand_index = 0;
	m_brute_index = 0;
	m_spin_index = 0;
	m_air_index = 0;
	m_body_index = 0;
	m_freestand_index = 0;
	m_records.clear();
	m_hitboxes.clear();
}

void AimPlayer::SetupHitboxes(LagRecord* record, bool history) {
	// reset hitboxes.
	m_hitboxes.clear();

	float speed = record->m_velocity.length_2d();
	bool prefer_head = record->m_velocity.length_2d() > 71.f;
	size_t mode = record->m_mode;
	bool only{ false };
	bool autoo = (g_cl.m_weapon_id == SCAR20 || G3SG1);
	bool scout = g_cl.m_weapon_id == SSG08;
	bool awp = g_cl.m_weapon_id == AWP;
	bool pistol = (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER);
	bool general = (!scout && !autoo && !awp && !pistol);

	if (g_csgo.m_cvar->FindVar(HASH("mp_damage_headshot_only"))->GetInt())
	{
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });
		return;
	}

	if (g_cl.m_weapon_id == ZEUS)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer head general

	if (g_menu.main.aimbot.head1general.get(0) && general)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if ((g_menu.main.aimbot.head1general.get(1) && prefer_head) || (g_menu.main.aimbot.head1general.get(1) && record->m_mode == Resolver::Modes::RESOLVE_STOPPED_MOVING) && general)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1general.get(2) && general && record->m_mode == Resolver::Modes::RESOLVE_LAST_LBY)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1general.get(3) && !(record->m_pred_flags & FL_ONGROUND) && general)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	// prefer head auto

	if (g_menu.main.aimbot.head1auto.get(0) && (g_cl.m_weapon_id == SCAR20 || g_cl.m_weapon_id == G3SG1))
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if ((g_menu.main.aimbot.head1auto.get(1) && prefer_head) || (g_menu.main.aimbot.head1auto.get(1) && record->m_mode == Resolver::Modes::RESOLVE_STOPPED_MOVING) && autoo)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1auto.get(2) && autoo && record->m_mode == Resolver::Modes::RESOLVE_LAST_LBY)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1auto.get(3) && !(record->m_pred_flags & FL_ONGROUND) && autoo)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	// prefer head scout

	if (g_menu.main.aimbot.head1scout.get(0) && scout)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if ((g_menu.main.aimbot.head1scout.get(1) && prefer_head) || (g_menu.main.aimbot.head1scout.get(1) && record->m_mode == Resolver::Modes::RESOLVE_STOPPED_MOVING) && scout)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1scout.get(2) && scout && record->m_mode == Resolver::Modes::RESOLVE_LAST_LBY)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1scout.get(3) && !(record->m_pred_flags & FL_ONGROUND) && scout)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	// prefer head pistol

	if (g_menu.main.aimbot.head1pistols.get(0) && pistol)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if ((g_menu.main.aimbot.head1pistols.get(1) && prefer_head) || (g_menu.main.aimbot.head1pistols.get(1) && record->m_mode == Resolver::Modes::RESOLVE_STOPPED_MOVING) && pistol)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1pistols.get(2) && pistol && record->m_mode == Resolver::Modes::RESOLVE_LAST_LBY)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1pistols.get(3) && !(record->m_pred_flags & FL_ONGROUND) && pistol)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	// prefer head awp

	if (g_menu.main.aimbot.head1awp.get(0) && awp)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if ((g_menu.main.aimbot.head1awp.get(1) && prefer_head) || (g_menu.main.aimbot.head1awp.get(1) && record->m_mode == Resolver::Modes::RESOLVE_STOPPED_MOVING) && awp)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1awp.get(2) && awp && record->m_mode == Resolver::Modes::RESOLVE_LAST_LBY)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	if (g_menu.main.aimbot.head1awp.get(3) && !(record->m_pred_flags & FL_ONGROUND) && awp)
		m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::PREFER });

	// prefer, always general
	if (g_menu.main.aimbot.baim1general.get(0) && general)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, always auto
	if (g_menu.main.aimbot.baim1auto.get(0) && autoo)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, always scout
	if (g_menu.main.aimbot.baim1scout.get(0) && scout)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, always pistols
	if (g_menu.main.aimbot.baim1pistols.get(0) && pistol)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, always awp
	if (g_menu.main.aimbot.baim1awp.get(0) && awp)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, lethal general
	if (g_menu.main.aimbot.baim1general.get(1) && general)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL });

	// prefer, lethal auto
	if (g_menu.main.aimbot.baim1auto.get(1) && autoo)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL });

	// prefer, lethal scout
	if (g_menu.main.aimbot.baim1scout.get(1) && scout)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL });

	// prefer, lethal pistols
	if (g_menu.main.aimbot.baim1pistols.get(1) && pistol)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL });

	// prefer, lethal awp
	if (g_menu.main.aimbot.baim1awp.get(1) && awp)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL });

	// prefer, lethal x2 general
	if (g_menu.main.aimbot.baim1general.get(2) && general)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL2 });

	// prefer, lethal x2 auto
	if (g_menu.main.aimbot.baim1auto.get(2) && autoo)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL2 });

	// prefer, lethal x2 scout
	if (g_menu.main.aimbot.baim1scout.get(2) && scout)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL2 });

	// prefer, lethal x2 pistols
	if (g_menu.main.aimbot.baim1pistols.get(2) && pistol)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL2 });

	// prefer, lethal x2 awp
	if (g_menu.main.aimbot.baim1awp.get(2) && awp)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::LETHAL2 });

	// prefer, stand general
	if (g_menu.main.aimbot.baim1general.get(3) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 1.f && general)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, stand auto
	if (g_menu.main.aimbot.baim1auto.get(3) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 1.f && autoo)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, stand scout
	if (g_menu.main.aimbot.baim1scout.get(3) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 1.f && scout)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, stand pistols
	if (g_menu.main.aimbot.baim1pistols.get(3) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 1.f && pistol)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, stand awp
	if (g_menu.main.aimbot.baim1awp.get(3) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 1.f && awp)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, in air general
	if (g_menu.main.aimbot.baim1general.get(4) && !(record->m_pred_flags & FL_ONGROUND) && general)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, in air auto
	if (g_menu.main.aimbot.baim1auto.get(4) && !(record->m_pred_flags & FL_ONGROUND) && autoo)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, in air scout
	if (g_menu.main.aimbot.baim1scout.get(4) && !(record->m_pred_flags & FL_ONGROUND) && scout)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, in air pistols
	if (g_menu.main.aimbot.baim1pistols.get(4) && !(record->m_pred_flags & FL_ONGROUND) && pistol)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// prefer, in air awp
	if (g_menu.main.aimbot.baim1awp.get(4) && !(record->m_pred_flags & FL_ONGROUND) && awp)
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });

	// only, always general 
	if (g_menu.main.aimbot.baim2general.get(0) && general) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, always auto
	if (g_menu.main.aimbot.baim2auto.get(0) && autoo) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, always scout
	if (g_menu.main.aimbot.baim2scout.get(0) && scout) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, always pistols
	if (g_menu.main.aimbot.baim2pistols.get(0) && pistol) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, always awp
	if (g_menu.main.aimbot.baim2awp.get(0) && awp) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, health general
	if (g_menu.main.aimbot.baim2general.get(1) && m_player->m_iHealth() <= (int)g_menu.main.aimbot.baim_hp.get() && general) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, health auto
	if (g_menu.main.aimbot.baim2auto.get(1) && m_player->m_iHealth() <= (int)g_menu.main.aimbot.baim_hp.get() && autoo) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, health scout
	if (g_menu.main.aimbot.baim2scout.get(1) && m_player->m_iHealth() <= (int)g_menu.main.aimbot.baim_hp.get() && scout) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, health pistols
	if (g_menu.main.aimbot.baim2pistols.get(1) && m_player->m_iHealth() <= (int)g_menu.main.aimbot.baim_hp.get() && pistol) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, health awp
	if (g_menu.main.aimbot.baim2awp.get(1) && m_player->m_iHealth() <= (int)g_menu.main.aimbot.baim_hp.get() && awp) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, stand general
	if (g_menu.main.aimbot.baim2general.get(2) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 3.f && general) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, stand auto
	if (g_menu.main.aimbot.baim2auto.get(2) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 3.f && autoo) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, stand scout
	if (g_menu.main.aimbot.baim2scout.get(2) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 3.f && scout) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, stand pistols
	if (g_menu.main.aimbot.baim2pistols.get(2) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 3.f && pistol) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, stand awp
	if (g_menu.main.aimbot.baim2awp.get(2) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK && speed < 3.f && awp) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, in air general
	if (g_menu.main.aimbot.baim2general.get(3) && !(record->m_pred_flags & FL_ONGROUND) && general) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, in air auto
	if (g_menu.main.aimbot.baim2auto.get(3) && !(record->m_pred_flags & FL_ONGROUND) && autoo) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, in air scout
	if (g_menu.main.aimbot.baim2scout.get(3) && !(record->m_pred_flags & FL_ONGROUND) && scout) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, in air pistols
	if (g_menu.main.aimbot.baim2pistols.get(3) && !(record->m_pred_flags & FL_ONGROUND) && pistol) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, in air awp
	if (g_menu.main.aimbot.baim2awp.get(3) && !(record->m_pred_flags & FL_ONGROUND) && awp) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}

	// only, on key.
	if (g_input.GetKeyState(g_menu.main.aimbot.baim_key.get())) {
		only = true;
		m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::PREFER });
	}


	// only baim conditions have been met.
	// do not insert more hitboxes.
	if (only)
		return;

	if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
		std::vector< size_t > hitbox{ history ? g_menu.main.aimbot.hitbox_history_pistols.GetActiveIndices() : g_menu.main.aimbot.hitbox_pistols.GetActiveIndices() };
		if (hitbox.empty())
			return;

		bool ignore_limbs = record->m_velocity.length_2d() > 71.f && g_menu.main.aimbot.ignor_limbs.get();

		for (const auto& h : hitbox) {
			// head.
			if (h == 0)
				m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::NORMAL });

			// chest.
			if (h == 1) {
				m_hitboxes.push_back({ HITBOX_THORAX, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_CHEST, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_UPPER_CHEST, HitscanMode::NORMAL });
			}

			// stomach.
			if (h == 2) {
				m_hitboxes.push_back({ HITBOX_PELVIS, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::NORMAL });
			}

			// arms.
			if (h == 3 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_UPPER_ARM, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_UPPER_ARM, HitscanMode::NORMAL });
			}

			// legs.
			if (h == 4) {
				m_hitboxes.push_back({ HITBOX_L_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_L_CALF, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_CALF, HitscanMode::NORMAL });
			}

			// foot.
			if (h == 5) {
				m_hitboxes.push_back({ HITBOX_L_FOOT, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_FOOT, HitscanMode::NORMAL });
			}
		}
	}
	else if (g_cl.m_weapon_id == SSG08) {
		std::vector< size_t > hitbox{ history ? g_menu.main.aimbot.hitbox_history_scout.GetActiveIndices() : g_menu.main.aimbot.hitbox_scout.GetActiveIndices() };
		if (hitbox.empty())
			return;

		bool ignore_limbs = record->m_velocity.length_2d() > 71.f && g_menu.main.aimbot.ignor_limbs.get();

		for (const auto& h : hitbox) {
			// head.
			if (h == 0)
				m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::NORMAL });

			// chest.
			if (h == 1) {
				m_hitboxes.push_back({ HITBOX_THORAX, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_CHEST, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_UPPER_CHEST, HitscanMode::NORMAL });
			}

			// stomach.
			if (h == 2) {
				m_hitboxes.push_back({ HITBOX_PELVIS, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::NORMAL });
			}

			// arms.
			if (h == 3 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_UPPER_ARM, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_UPPER_ARM, HitscanMode::NORMAL });
			}

			// legs.
			if (h == 4) {
				m_hitboxes.push_back({ HITBOX_L_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_L_CALF, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_CALF, HitscanMode::NORMAL });
			}

			// foot.
			if (h == 5 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_FOOT, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_FOOT, HitscanMode::NORMAL });
			}
		}
	}
	else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
		std::vector< size_t > hitbox{ history ? g_menu.main.aimbot.hitbox_history_auto.GetActiveIndices() : g_menu.main.aimbot.hitbox_auto.GetActiveIndices() };
		if (hitbox.empty())
			return;

		bool ignore_limbs = record->m_velocity.length_2d() > 71.f && g_menu.main.aimbot.ignor_limbs.get();

		for (const auto& h : hitbox) {
			// head.
			if (h == 0)
				m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::NORMAL });

			// chest.
			if (h == 1) {
				m_hitboxes.push_back({ HITBOX_THORAX, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_CHEST, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_UPPER_CHEST, HitscanMode::NORMAL });
			}

			// stomach.
			if (h == 2) {
				m_hitboxes.push_back({ HITBOX_PELVIS, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::NORMAL });
			}

			// arms.
			if (h == 3 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_UPPER_ARM, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_UPPER_ARM, HitscanMode::NORMAL });
			}

			// legs.
			if (h == 4) {
				m_hitboxes.push_back({ HITBOX_L_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_L_CALF, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_CALF, HitscanMode::NORMAL });
			}

			// foot.
			if (h == 5 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_FOOT, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_FOOT, HitscanMode::NORMAL });
			}
		}
	}
	else if (g_cl.m_weapon_id == AWP) {
		std::vector< size_t > hitbox{ history ? g_menu.main.aimbot.hitbox_history_awp.GetActiveIndices() : g_menu.main.aimbot.hitbox_awp.GetActiveIndices() };
		if (hitbox.empty())
			return;

		bool ignore_limbs = record->m_velocity.length_2d() > 71.f && g_menu.main.aimbot.ignor_limbs.get();

		for (const auto& h : hitbox) {
			// head.
			if (h == 0)
				m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::NORMAL });

			// chest.
			if (h == 1) {
				m_hitboxes.push_back({ HITBOX_THORAX, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_CHEST, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_UPPER_CHEST, HitscanMode::NORMAL });
			}

			// stomach.
			if (h == 2) {
				m_hitboxes.push_back({ HITBOX_PELVIS, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::NORMAL });
			}

			// arms.
			if (h == 3 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_UPPER_ARM, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_UPPER_ARM, HitscanMode::NORMAL });
			}

			// legs.
			if (h == 4) {
				m_hitboxes.push_back({ HITBOX_L_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_L_CALF, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_CALF, HitscanMode::NORMAL });
			}

			// foot.
			if (h == 5 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_FOOT, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_FOOT, HitscanMode::NORMAL });
			}
		}
	}
	else {
		std::vector< size_t > hitbox{ history ? g_menu.main.aimbot.hitbox_history_general.GetActiveIndices() : g_menu.main.aimbot.hitbox_general.GetActiveIndices() };
		if (hitbox.empty())
			return;

		bool ignore_limbs = record->m_velocity.length_2d() > 71.f && g_menu.main.aimbot.ignor_limbs.get();

		for (const auto& h : hitbox) {
			// head.
			if (h == 0)
				m_hitboxes.push_back({ HITBOX_HEAD, HitscanMode::NORMAL });

			// chest.
			if (h == 1) {
				m_hitboxes.push_back({ HITBOX_THORAX, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_CHEST, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_UPPER_CHEST, HitscanMode::NORMAL });
			}

			// stomach.
			if (h == 2) {
				m_hitboxes.push_back({ HITBOX_PELVIS, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_BODY, HitscanMode::NORMAL });
			}

			// arms.
			if (h == 3 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_UPPER_ARM, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_UPPER_ARM, HitscanMode::NORMAL });
			}

			// legs.
			if (h == 4) {
				m_hitboxes.push_back({ HITBOX_L_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_THIGH, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_L_CALF, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_CALF, HitscanMode::NORMAL });
			}

			// foot.
			if (h == 5 && !ignore_limbs) {
				m_hitboxes.push_back({ HITBOX_L_FOOT, HitscanMode::NORMAL });
				m_hitboxes.push_back({ HITBOX_R_FOOT, HitscanMode::NORMAL });
			}
		}
	}
}

void Aimbot::init() {
	// clear old targets.
	m_targets.clear();

	m_target = nullptr;
	m_aim = vec3_t{ };
	m_angle = ang_t{ };
	m_damage = 0.f;
	m_record = nullptr;
	m_stop = false;

	m_best_dist = std::numeric_limits< float >::max();
	m_best_fov = 180.f + 1.f;
	m_best_damage = 0.f;
	m_best_hp = 100 + 1;
	m_best_lag = std::numeric_limits< float >::max();
	m_best_height = std::numeric_limits< float >::max();
}

void Aimbot::StripAttack() {
	if (g_cl.m_weapon_id == REVOLVER)
		g_cl.m_cmd->m_buttons &= ~IN_ATTACK2;

	else
		g_cl.m_cmd->m_buttons &= ~IN_ATTACK;
}

void Aimbot::think() {
	// do all startup routines.
	init();

	// sanity.
	if (!g_cl.m_weapon)
		return;

	// no grenades or bomb.
	if (g_cl.m_weapon_type == WEAPONTYPE_GRENADE || g_cl.m_weapon_type == WEAPONTYPE_C4)
		return;

	if (!g_cl.m_weapon_fire)
		StripAttack();

	// we have no aimbot enabled.
	if (!g_menu.main.aimbot.enable.get())
		return;

	// animation silent aim, prevent the ticks with the shot in it to become the tick that gets processed.
	// we can do this by always choking the tick before we are able to shoot.
	bool revolver = g_cl.m_weapon_id == REVOLVER && g_cl.m_revolver_cock != 0;

	// one tick before being able to shoot.
	if (revolver && g_cl.m_revolver_cock > 0 && g_cl.m_revolver_cock == g_cl.m_revolver_query) {
		*g_cl.m_packet = false;
		return;
	}

	// we have a normal weapon or a non cocking revolver
	// choke if its the processing tick.
	if (g_cl.m_weapon_fire && !g_cl.m_lag && !revolver) {
		*g_cl.m_packet = false;
		StripAttack();
		return;
	}

	// no point in aimbotting if we cannot fire this tick.
	if (!g_cl.m_weapon_fire)
		return;

	// setup bones for all valid targets.
	for (int i{ 1 }; i <= g_csgo.m_globals->m_max_clients; ++i) {
		Player* player = g_csgo.m_entlist->GetClientEntity< Player* >(i);

		if (!IsValidTarget(player))
			continue;

		AimPlayer* data = &m_players[i - 1];
		if (!data)
			continue;

		// store player as potential target this tick.
		m_targets.emplace_back(data);
	}

	// run knifebot.
	if (g_cl.m_weapon_type == WEAPONTYPE_KNIFE && g_cl.m_weapon_id != ZEUS) {

		if (g_menu.main.aimbot.knifebot.get())
			knife();

		return;
	}

	// scan available targets... if we even have any.
	find();

	// finally set data when shooting.
	apply();
}

void Aimbot::find() {
	struct BestTarget_t { Player* player;  AimPlayer* target; vec3_t pos; float damage; LagRecord* record; };

	vec3_t       tmp_pos;
	float        tmp_damage;
	BestTarget_t best;
	best.player = nullptr;
	best.target = nullptr;
	best.damage = -1.f;
	best.pos = vec3_t{ };
	best.record = nullptr;


	if (m_targets.empty())
		return;

	//if (g_cl.m_weapon_id == ZEUS && !g_menu.main.aimbot.zeusbot.get())
		//return;

	// iterate all targets.
	for (const auto& t : m_targets) {
		if (t->m_records.empty())
			continue;

		// this player broke lagcomp.
		// his bones have been resetup by our lagcomp.
		// therfore now only the front record is valid.
		if (g_menu.main.aimbot.lagfix.get() && g_lagcomp.StartPrediction(t)) {
			LagRecord* front = t->m_records.front().get();

			t->SetupHitboxes(front, false);
			if (t->m_hitboxes.empty())
				continue;
			// rip something went wrong..
			if (t->GetBestAimPosition(tmp_pos, tmp_damage, front) && SelectTarget(front, tmp_pos, tmp_damage)) {
				// if we made it so far, set shit.
				best.player = t->m_player;
				best.pos = tmp_pos;
				best.damage = tmp_damage;
				best.record = front;
				best.target = t;
			}
		}
		// player did not break lagcomp.
		// history aim is possible at this point.
		else {
			LagRecord* ideal = g_resolver.FindIdealRecord(t);
			if (!ideal)
				continue;

			if (g_csgo.m_globals->m_tick_count % 5 == 1) {
				t->SetupHitboxes(ideal, false);
				if (t->m_hitboxes.empty())
					continue;
				// try to select best record as target.
				if (t->GetBestAimPosition(tmp_pos, tmp_damage, ideal) && SelectTarget(ideal, tmp_pos, tmp_damage)) {
					// if we made it so far, set shit.
					best.player = t->m_player;
					best.pos = tmp_pos;
					best.damage = tmp_damage;
					best.record = ideal;
					best.target = t;
				}
			}


			LagRecord* last = g_resolver.FindLastRecord(t);
			if (!last || last == ideal)
				continue;

			t->SetupHitboxes(last, true);
			if (t->m_hitboxes.empty())
				continue;

			// rip something went wrong..
			if (t->GetBestAimPosition(tmp_pos, tmp_damage, last) && SelectTarget(last, tmp_pos, tmp_damage)) {
				// if we made it so far, set shit.
				best.player = t->m_player;
				best.pos = tmp_pos;
				best.damage = tmp_damage;
				best.record = last;
				best.target = t;
			}
		}
	}

	// verify our target and set needed data.
	if (best.player && best.record) {
		// calculate aim angle.
		math::VectorAngles(best.pos - g_cl.m_shoot_pos, m_angle);

		// set member vars.
		m_target = best.player;
		m_aim = best.pos;
		m_damage = best.damage;
		m_record = best.record;

		// write data, needed for traces / etc.
		m_record->cache();

		const bool bOnLand = !(g_cl.m_flags & FL_ONGROUND) && g_cl.m_local->m_fFlags() & FL_ONGROUND;

		// set autostop shit.
		m_stop = !(g_cl.m_buttons & IN_JUMP);

		bool can_hit_on_fd = !g_hvh.m_fakeduck || g_hvh.m_fakeduck && g_cl.m_local->m_flDuckAmount() == 0.f;
		bool on = false;

		if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER)
			on = g_menu.main.aimbot.hitchance_pistols.get();
		else if (g_cl.m_weapon_id == SSG08)
			on = g_menu.main.aimbot.hitchance_scout.get();
		else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20)
			on = g_menu.main.aimbot.hitchance_auto.get();
		else if (g_cl.m_weapon_id == AWP)
			on = g_menu.main.aimbot.hitchance_awp.get();
		else
			on = g_menu.main.aimbot.hitchance_general.get();

		bool hit = on && CheckHitchance(m_target, m_angle);

		// if we can scope.
		bool can_scope = !g_cl.m_local->m_bIsScoped() && (g_cl.m_weapon_id == AUG || g_cl.m_weapon_id == SG553 || g_cl.m_weapon_type == WEAPONTYPE_SNIPER_RIFLE);

		if (can_scope) {
			// always.
			if (g_menu.main.aimbot.zoom.get() == 1) {
				g_cl.m_cmd->m_buttons |= IN_ATTACK2;
				return;
			}

			// hitchance fail.
			else if (g_menu.main.aimbot.zoom.get() == 2 && on && !hit) {
				g_cl.m_cmd->m_buttons |= IN_ATTACK2;
				return;
			}
		}

		if (hit || !on) {
			// right click attack.
			if (g_menu.main.aimbot.nospread.get() && g_cl.m_weapon_id == REVOLVER)
				g_cl.m_cmd->m_buttons |= IN_ATTACK2;

			// left click attack.
			else
				g_cl.m_cmd->m_buttons |= IN_ATTACK;

			m_old_target = best.target;
		}
	}
}

bool Aimbot::CanHit(vec3_t start, vec3_t end, LagRecord* record, int box, bool in_shot, BoneArray* bones)
{
	if (!record || !record->m_player)
		return false;

	// backup player
	const auto backup_origin = record->m_player->m_vecOrigin();
	const auto backup_abs_origin = record->m_player->GetAbsOrigin();
	const auto backup_abs_angles = record->m_player->GetAbsAngles();
	const auto backup_obb_mins = record->m_player->m_vecMins();
	const auto backup_obb_maxs = record->m_player->m_vecMaxs();
	const auto backup_cache = record->m_player->m_iBoneCache();

	// always try to use our aimbot matrix first.
	auto matrix = record->m_bones;

	// this is basically for using a custom matrix.
	if (in_shot)
		matrix = bones;

	if (!matrix)
		return false;

	const model_t* model = record->m_player->GetModel();
	if (!model)
		return false;

	studiohdr_t* hdr = g_csgo.m_model_info->GetStudioModel(model);
	if (!hdr)
		return false;

	mstudiohitboxset_t* set = hdr->GetHitboxSet(record->m_player->m_nHitboxSet());
	if (!set)
		return false;

	mstudiobbox_t* bbox = set->GetHitbox(box);
	if (!bbox)
		return false;

	vec3_t min, max;
	const auto IsCapsule = bbox->m_radius != -1.f;

	if (IsCapsule) {
		math::VectorTransform(bbox->m_mins, matrix[bbox->m_bone], min);
		math::VectorTransform(bbox->m_maxs, matrix[bbox->m_bone], max);
		const auto dist = math::SegmentToSegment(start, end, min, max);

		if (dist < bbox->m_radius) {
			return true;
		}
	}
	else {
		CGameTrace tr;

		// setup trace data
		record->m_player->m_vecOrigin() = record->m_origin;
		record->m_player->SetAbsOrigin(record->m_origin);
		record->m_player->SetAbsAngles(record->m_abs_ang);
		record->m_player->m_vecMins() = record->m_mins;
		record->m_player->m_vecMaxs() = record->m_maxs;
		record->m_player->m_iBoneCache() = reinterpret_cast<matrix3x4_t**>(matrix);

		// setup ray and trace.
		g_csgo.m_engine_trace->ClipRayToEntity(Ray(start, end), MASK_SHOT, record->m_player, &tr);

		record->m_player->m_vecOrigin() = backup_origin;
		record->m_player->SetAbsOrigin(backup_abs_origin);
		record->m_player->SetAbsAngles(backup_abs_angles);
		record->m_player->m_vecMins() = backup_obb_mins;
		record->m_player->m_vecMaxs() = backup_obb_maxs;
		record->m_player->m_iBoneCache() = backup_cache;

		// check if we hit a valid player / hitgroup on the player and increment total hits.
		if (tr.m_entity == record->m_player && game::IsValidHitgroup(tr.m_hitgroup))
			return true;
	}

	return false;
}

bool Aimbot::CheckHitchance(Player* player, const ang_t& angle) {
	constexpr float HITCHANCE_MAX = 100.f;
	constexpr int   SEED_MAX = 255;

	const auto info = g_cl.m_weapon_info;

	if (!info)
		return false;

	float hc = FLT_MAX;

	if (g_cl.m_weapon_id == ZEUS)
		hc = g_menu.main.aimbot.hitchance_amount_zeus.get();
	else if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER && !CanDT())
		hc = g_menu.main.aimbot.hitchance_amount_pistols.get();
	else if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER && CanDT())
		hc = g_menu.main.aimbot.hitchance_dt_amount_pistols.get();
	else if (g_cl.m_weapon_id == SSG08)
		hc = g_menu.main.aimbot.hitchance_amount_scout.get();
	else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20 && !CanDT())
		hc = g_menu.main.aimbot.hitchance_amount_auto.get();
	else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20 && CanDT())
		hc = g_menu.main.aimbot.hitchance_dt_amount_auto.get();
	else if (g_cl.m_weapon_id == AWP)
		hc = g_menu.main.aimbot.hitchance_amount_awp.get();
	else
		hc = g_menu.main.aimbot.hitchance_amount_general.get();

	vec3_t forward, right, up;
	float      inaccuracy, spread;
	CGameTrace tr;
	size_t     total_hits{ }, needed_hits{ (size_t)std::ceil((hc * SEED_MAX) / HITCHANCE_MAX) };

	// get needed directional vectors.
	math::AngleVectors(angle, &forward, &right, &up);

	// store off inaccuracy / spread ( these functions are quite intensive and we only need them once ).
	g_cl.m_weapon->UpdateAccuracyPenalty();
	float weap_spread = g_cl.m_weapon->GetSpread();
	const auto weap_inaccuracy = g_cl.m_weapon->GetInaccuracy();

	float weapon_range = g_cl.m_weapon->GetWpnData()->m_range;
	vec3_t src = g_cl.m_shoot_pos;
	int cHits = 0;
	int cNeededHits = static_cast<int>(255 * (hc / 100));
	for (int i = 0; i < 255; i++)
	{
		math::random_seed(i);

		float a = math::random_float(0.f, 1.f);
		float b = math::random_float(0.f, 2.f * M_PI);
		float c = math::random_float(0.f, 1.f);
		float d = math::random_float(0.f, 2.f * M_PI);

		float inaccuracy = a * weap_inaccuracy;
		float spread = c * weap_spread;

		vec3_t spreadView((cos(b) * inaccuracy) + (cos(d) * spread), (sin(b) * inaccuracy) + (sin(d) * spread), 0), direction;

		direction.x = forward.x + (spreadView.x * right.x) + (spreadView.y * up.x);
		direction.y = forward.y + (spreadView.x * right.y) + (spreadView.y * up.y);
		direction.z = forward.z + (spreadView.x * right.z) + (spreadView.y * up.z);
		direction.normalized();

		ang_t viewAnglesSpread;
		math::VectorAngles3(direction, up, viewAnglesSpread);
		math::Normalize(viewAnglesSpread);

		vec3_t viewForward;
		math::AngleVectors69(viewAnglesSpread, viewForward);
		viewForward.normalize();
		viewForward = src + (viewForward * weapon_range);
		g_csgo.m_engine_trace->ClipRayToEntity(Ray(src, viewForward), MASK_SHOT, player, &tr);

		if (tr.m_entity == player && game::IsValidHitgroup(tr.m_hitgroup))
			cHits++;

		if (static_cast<int>((static_cast<float>(cHits) / 255.f) * 100.f) >= hc)
			return true;

		if ((255 - i + cHits) < cNeededHits)
			return false;
	}
	return false;
}

float GetFov(const ang_t& viewAngle, const ang_t& aimAngle) {
	vec3_t ang, aim;

	math::AngleVectors(viewAngle, &aim);
	math::AngleVectors(aimAngle, &ang);

	return math::rad_to_deg(acos(aim.dot(ang) / aim.length_sqr()));
}

bool AimPlayer::SetupHitboxPoints(LagRecord* record, BoneArray* bones, int index, std::vector< vec3_t >& points) {
	// reset points.
	points.clear();

	const model_t* model = m_player->GetModel();
	if (!model)
		return false;

	studiohdr_t* hdr = g_csgo.m_model_info->GetStudioModel(model);
	if (!hdr)
		return false;

	mstudiohitboxset_t* set = hdr->GetHitboxSet(m_player->m_nHitboxSet());
	if (!set)
		return false;

	mstudiobbox_t* bbox = set->GetHitbox(index);
	if (!bbox)
		return false;

	// get hitbox scales.
	float scale = 0.f;

	if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER)
		scale = g_menu.main.aimbot.scale_pistols.get() / 100.f;
	else if (g_cl.m_weapon_id == SSG08)
		scale = g_menu.main.aimbot.scale_scout.get() / 100.f;
	else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20)
		scale = g_menu.main.aimbot.scale_auto.get() / 100.f;
	else if (g_cl.m_weapon_id == AWP)
		scale = g_menu.main.aimbot.scale_awp.get() / 100.f;
	else
		scale = g_menu.main.aimbot.scale_general.get() / 100.f;

	// big inair fix.
	if (!(record->m_pred_flags & FL_ONGROUND))
		scale = 0.7f;

	float bscale = 0.f;


	if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER)
		bscale = g_menu.main.aimbot.body_scale_pistols.get() / 100.f;
	else if (g_cl.m_weapon_id == SSG08)
		bscale = g_menu.main.aimbot.body_scale_scout.get() / 100.f;
	else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20)
		bscale = g_menu.main.aimbot.body_scale_auto.get() / 100.f;
	else if (g_cl.m_weapon_id == AWP)
		bscale = g_menu.main.aimbot.body_scale_awp.get() / 100.f;
	else
		bscale = g_menu.main.aimbot.body_scale_general.get() / 100.f;

	// these indexes represent boxes.
	if (bbox->m_radius <= 0.f) {
		// references: 
		//      https://developer.valvesoftware.com/wiki/Rotation_Tutorial
		//      CBaseAnimating::GetHitboxBonePosition
		//      CBaseAnimating::DrawServerHitboxes

		// convert rotation angle to a matrix.
		matrix3x4_t rot_matrix;
		g_csgo.AngleMatrix(bbox->m_angle, rot_matrix);

		// apply the rotation to the entity input space (local).
		matrix3x4_t matrix;
		math::ConcatTransforms(bones[bbox->m_bone], rot_matrix, matrix);

		// extract origin from matrix.
		vec3_t origin = matrix.GetOrigin();

		// compute raw center point.
		vec3_t center = (bbox->m_mins + bbox->m_maxs) / 2.f;


		// nothing to do here we are done.
		if (points.empty())
			return false;

		// rotate our bbox points by their correct angle
		// and convert our points to world space.
		for (auto& p : points) {
			// VectorRotate.
			// rotate point by angle stored in matrix.
			p = { p.dot(matrix[0]), p.dot(matrix[1]), p.dot(matrix[2]) };

			// transform point to world space.
			p += origin;
		}
	}

	// these hitboxes are capsules.
	else {
		// factor in the pointscale.
		float r = bbox->m_radius * scale;
		float br = bbox->m_radius * bscale;
		float flMod = bbox->m_radius != -1.f ? bbox->m_radius : 0.f;
		float flBodyResize = flMod * bscale; // basically the same as br
		float flHalfRadius = r * 0.5f;
		// compute raw center point.
		vec3_t center = (bbox->m_mins + bbox->m_maxs) / 2.f;
		// enemy eyeangles
		ang_t _EyeAngles = { record->m_eye_angles.x, record->m_eye_angles.y, record->m_eye_angles.z }; // never change this unless our resolver turns to shit; if so switch it out for absyaw.x, goalfeetyaw, absyaw.z

		// get at enemy angle
		ang_t _AtEnemy = math::CalcAngle2(vec3_t(0.0f, g_cl.m_local->GetShootPosition().y, center.z), center);

		// check our relative rotation towards the entity
		const float FOV = GetFov(_AtEnemy, _EyeAngles);
		const bool bIsForwardOrBack = 1;

		ang_t angAngle = math::CalcAngle2(g_cl.m_local->GetEyePosition(), center);

		vec3_t vecForward;
		math::AngleVectors(angAngle, &vecForward);

		vec3_t vecRight = vecForward.cross(vec3_t(0, 0, 2.33f));
		vec3_t vecLeft = vec3_t(-vecRight.x, -vecRight.y, vecRight.z);

		vec3_t vecTop = vec3_t(0, 0, 3.25f);
		vec3_t vecBottom = vec3_t(0, 0, -3.25f);

		int32_t iAngleToPlayer = (int32_t)(fabsf(math::normalize_float(math::normalize_float(math::normalize_float(record->m_eye_angles.y) - math::normalize_float(math::CalcAngle(g_cl.m_local->GetEyePosition(), m_player->GetAbsOrigin()).y + 180.0f)))));
		int32_t iDistanceToPlayer = (int32_t)(g_cl.m_local->GetAbsOrigin().dist_to(m_player->m_vecOrigin()));

		vec3_t vAngle = math::CalcAngle(g_cl.m_local->GetEyePosition(), center);

		float flCos = cosf(math::deg_to_rad(vAngle.y));
		float flSin = sinf(math::deg_to_rad(vAngle.y));

		// head has 5 points.
		if (index == HITBOX_HEAD) {
			// add center.
			points.push_back(center);


			if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
				if (g_menu.main.aimbot.multipoint_pistols.get(0)) {

					if (bIsForwardOrBack == 1) {

						if (_AtEnemy.y > 0.f) {
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}

						else {
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}
					}

					// lookint at their sides
					else {

						if (_AtEnemy.y > 0.f) {
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}

						else {
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}
					}
				}
			}
			else if (g_cl.m_weapon_id == SSG08) {
				if (g_menu.main.aimbot.multipoint_scout.get(0)) {

					if (bIsForwardOrBack == 1) {

						if (_AtEnemy.y > 0.f) {
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}

						else {
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}
					}

					// lookint at their sides
					else {

						if (_AtEnemy.y > 0.f) {
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}

						else {
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}
					}
				}
			}
			else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
				if (g_menu.main.aimbot.multipoint_auto.get(0)) {

					if (bIsForwardOrBack == 1) {

						if (_AtEnemy.y > 0.f) {
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}

						else {
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}
					}

					// lookint at their sides
					else {

						if (_AtEnemy.y > 0.f) {
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}

						else {
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}
					}
				}
			}
			else if (g_cl.m_weapon_id == AWP) {
				if (g_menu.main.aimbot.multipoint_awp.get(0)) {

					if (bIsForwardOrBack == 1) {

						if (_AtEnemy.y > 0.f) {
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}

						else {
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}
					}

					// lookint at their sides
					else {

						if (_AtEnemy.y > 0.f) {
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}

						else {
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}
					}
				}
			}
			else {
				if (g_menu.main.aimbot.multipoint_general.get(0)) {

					if (bIsForwardOrBack == 1) {

						if (_AtEnemy.y > 0.f) {
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}

						else {
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}
					}

					// lookint at their sides
					else {

						if (_AtEnemy.y > 0.f) {
							//back of skull top right
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//front of skull top right
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y - flHalfRadius, center.z + r });
							//back of skull bottom right
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
							//front of skull bottom right
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y - flHalfRadius, center.z - r });
						}

						else {
							//front of skull top left
							points.push_back({ bbox->m_maxs.x + flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//back of skull top left
							points.push_back({ bbox->m_maxs.x - flHalfRadius, bbox->m_maxs.y + flHalfRadius, center.z + r });
							//front of skull bottom left
							points.push_back({ bbox->m_mins.x + flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
							//back of skull bottom left
							points.push_back({ bbox->m_mins.x - flHalfRadius, bbox->m_mins.y + flHalfRadius, center.z - r });
						}
					}
				}
			}
		}

		// body has 5 points.
		else if (index == HITBOX_BODY) {
			// center.
			points.push_back(center + vec3_t(0, 0, 3.0f));
			float_t flModifier = br / 33.0f;
			if (iAngleToPlayer < 120 && iAngleToPlayer > 60)
				flModifier = (br / 33.0f) - 0.10f;
			if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
				// back.
				if (g_menu.main.aimbot.multipoint_pistols.get(2)) {
					points.push_back(center + vecRight * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
					points.push_back(center + vecLeft * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
				}
			}
			else if (g_cl.m_weapon_id == SSG08) {
				// back.
				if (g_menu.main.aimbot.multipoint_scout.get(2)) { // multipoint.
					points.push_back(center + vecRight * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
					points.push_back(center + vecLeft * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
				}
			}
			else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
				// back.
				if (g_menu.main.aimbot.multipoint_auto.get(2)) { // multipoint.
					points.push_back(center + vecRight * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
					points.push_back(center + vecLeft * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
				}
			}
			else if (g_cl.m_weapon_id == AWP) {
				// back.
				if (g_menu.main.aimbot.multipoint_awp.get(2)) { // multipoint.
					points.push_back(center + vecRight * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
					points.push_back(center + vecLeft * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
				}
			}
			else {
				// back.
				if (g_menu.main.aimbot.multipoint_general.get(2)) { // multipoint.
					points.push_back(center + vecRight * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
					points.push_back(center + vecLeft * flModifier + vec3_t(0.0f, 0.0f, 3.0f));
				}
			}
		}

		else if (index == HITBOX_PELVIS || index == HITBOX_UPPER_CHEST) {
			// back.
			points.push_back({ center.x, bbox->m_maxs.y - r, center.z });
			points.push_back(vec3_t(center.x + flCos + flBodyResize * flSin, center.y + flSin - flBodyResize * flCos, center.z));
			points.push_back(vec3_t(center.x + flCos - flBodyResize * flSin, center.y + flSin + flBodyResize * flCos, center.z));
		}

		// other stomach/chest hitboxes have 2 points.
		else if (index == HITBOX_THORAX || index == HITBOX_CHEST) {
			float_t flModifier = 3.05f * (br / 80.0f);
			if (iAngleToPlayer < 140 && iAngleToPlayer > 30)
				flModifier = 2.0f * (br / 80.0f);

			// enforce modified center.
			points.push_back(center + vec3_t(0, 0, 3));

		}

		else if (index == HITBOX_R_CALF || index == HITBOX_L_CALF) {
			// add center.
			points.push_back(center);

			if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
				// half bottom.
				if (g_menu.main.aimbot.multipoint_pistols.get(3))
					points.push_back({ bbox->m_maxs.x - (bbox->m_radius / 2.f), bbox->m_maxs.y, bbox->m_maxs.z });
			}
			else if (g_cl.m_weapon_id == SSG08) {
				// half bottom.
				if (g_menu.main.aimbot.multipoint_scout.get(3))
					points.push_back({ bbox->m_maxs.x - (bbox->m_radius / 2.f), bbox->m_maxs.y, bbox->m_maxs.z });
			}
			else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
				// half bottom.
				if (g_menu.main.aimbot.multipoint_auto.get(3))
					points.push_back({ bbox->m_maxs.x - (bbox->m_radius / 2.f), bbox->m_maxs.y, bbox->m_maxs.z });
			}
			else if (g_cl.m_weapon_id == AWP) {
				// half bottom.
				if (g_menu.main.aimbot.multipoint_awp.get(3))
					points.push_back({ bbox->m_maxs.x - (bbox->m_radius / 2.f), bbox->m_maxs.y, bbox->m_maxs.z });
			}
			else {
				// half bottom.
				if (g_menu.main.aimbot.multipoint_general.get(3))
					points.push_back({ bbox->m_maxs.x - (bbox->m_radius / 2.f), bbox->m_maxs.y, bbox->m_maxs.z });
			}
		}

		// the feet hiboxes have a side, heel and the toe.
		if (index == HITBOX_R_FOOT || index == HITBOX_L_FOOT) {
			float d1 = (bbox->m_mins.z - center.z) * 0.875f;

			// invert.
			if (index == HITBOX_L_FOOT)
				d1 *= -1.f;

			// side is more optimal then center.
			points.push_back({ center.x, center.y, center.z + d1 });

			if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
				if (g_menu.main.aimbot.multipoint_pistols.get(3)) {
					// get point offset relative to center point
					// and factor in hitbox scale.
					float d2 = (bbox->m_mins.x - center.x) * scale;
					float d3 = (bbox->m_maxs.x - center.x) * scale;

					// heel.
					points.push_back({ center.x + d2, center.y, center.z });

					// toe.
					points.push_back({ center.x + d3, center.y, center.z });
				}
			}
			else if (g_cl.m_weapon_id == SSG08) {
				if (g_menu.main.aimbot.multipoint_scout.get(3)) {
					// get point offset relative to center point
					// and factor in hitbox scale.
					float d2 = (bbox->m_mins.x - center.x) * scale;
					float d3 = (bbox->m_maxs.x - center.x) * scale;

					// heel.
					points.push_back({ center.x + d2, center.y, center.z });

					// toe.
					points.push_back({ center.x + d3, center.y, center.z });
				}
			}
			else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
				if (g_menu.main.aimbot.multipoint_auto.get(3)) {
					// get point offset relative to center point
					// and factor in hitbox scale.
					float d2 = (bbox->m_mins.x - center.x) * scale;
					float d3 = (bbox->m_maxs.x - center.x) * scale;

					// heel.
					points.push_back({ center.x + d2, center.y, center.z });

					// toe.
					points.push_back({ center.x + d3, center.y, center.z });
				}
			}
			else if (g_cl.m_weapon_id == AWP) {
				if (g_menu.main.aimbot.multipoint_awp.get(3)) {
					// get point offset relative to center point
					// and factor in hitbox scale.
					float d2 = (bbox->m_mins.x - center.x) * scale;
					float d3 = (bbox->m_maxs.x - center.x) * scale;

					// heel.
					points.push_back({ center.x + d2, center.y, center.z });

					// toe.
					points.push_back({ center.x + d3, center.y, center.z });
				}
			}
			else {
				if (g_menu.main.aimbot.multipoint_general.get(3)) {
					// get point offset relative to center point
					// and factor in hitbox scale.
					float d2 = (bbox->m_mins.x - center.x) * scale;
					float d3 = (bbox->m_maxs.x - center.x) * scale;

					// heel.
					points.push_back({ center.x + d2, center.y, center.z });

					// toe.
					points.push_back({ center.x + d3, center.y, center.z });
				}
			}
		}
		else if (index == HITBOX_R_THIGH || index == HITBOX_L_THIGH) {
			// add center.
			points.push_back(center);
		}

		// arms get only one point.
		else if (index == HITBOX_R_UPPER_ARM || index == HITBOX_L_UPPER_ARM) {
			// elbow.
			points.push_back({ bbox->m_maxs.x + bbox->m_radius, center.y, center.z });
		}

		// nothing left to do here.
		if (points.empty())
			return false;

		// transform capsule points.
		for (auto& p : points)
			math::VectorTransform(p, bones[bbox->m_bone], p);
	}

	return true;
}
bool AimPlayer::GetBestAimPosition(vec3_t& aim, float& damage, LagRecord* record) {
	bool                  done, pen;
	float                 dmg, pendmg;
	float getscoutdmg = g_menu.main.aimbot.minimal_damage_scout.get();
	float getscoutpendmg = g_menu.main.aimbot.penetrate_minimal_damage_scout.get();
	float getawpdmg = g_menu.main.aimbot.minimal_damage_awp.get();
	float getawppendmg = g_menu.main.aimbot.penetrate_minimal_damage_awp.get();
	float getpistolsminovrdmg = g_menu.main.aimbot.override_dmg_value_pistols.get();
	float getscoutminovrdmg = g_menu.main.aimbot.override_dmg_value_scout.get();
	float getawpminovrdmg = g_menu.main.aimbot.override_dmg_value_awp.get();
	float getautominovrdmg = g_menu.main.aimbot.override_dmg_value_auto.get();
	HitscanData_t         scan;
	std::vector< vec3_t > points;

	// get player hp.
	int hp = std::min(100, m_player->m_iHealth());

	if (g_cl.m_weapon_id == ZEUS) {
		dmg = pendmg = hp;
		pen = false;
	}
	else {
		if (g_aimbot.m_damage_toggle) {
			if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
				if (getpistolsminovrdmg > 100.f) {
					dmg = hp + getpistolsminovrdmg - 100.f;
					pendmg = hp + getpistolsminovrdmg - 100.f;
				}
				else
					dmg = getpistolsminovrdmg;
				pendmg = getpistolsminovrdmg;
				pen = getpistolsminovrdmg;
			}
			else if (g_cl.m_weapon_id == SSG08) {
				if (getscoutminovrdmg > 100.f) {
					dmg = hp + getscoutminovrdmg - 100.f;
					pendmg = hp + getscoutminovrdmg - 100.f;
				}
				else
					dmg = getscoutminovrdmg;
				pendmg = getscoutminovrdmg;
				pen = getscoutminovrdmg;
			}
			else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
				if (getautominovrdmg > 100.f) {
					dmg = hp + getautominovrdmg - 100.f;
					pendmg = hp + getautominovrdmg - 100.f;
				}
				else
					dmg = getautominovrdmg;
				pendmg = getautominovrdmg;
				pen = getautominovrdmg;
			}
			else if (g_cl.m_weapon_id == AWP) {
				if (getawpminovrdmg > 100.f) {
					dmg = hp + getawpminovrdmg - 100.f;
					pendmg = hp + getawpminovrdmg - 100.f;
				}
				else
					dmg = getawpminovrdmg;
				pendmg = getawpminovrdmg;
				pen = getawpminovrdmg;
			}
		}
		else {

			if (g_cl.m_weapon_id == GLOCK || g_cl.m_weapon_id == P2000 || g_cl.m_weapon_id == USPS || g_cl.m_weapon_id == ELITE || g_cl.m_weapon_id == P250 || g_cl.m_weapon_id == TEC9 || g_cl.m_weapon_id == CZ75A || g_cl.m_weapon_id == DEAGLE || g_cl.m_weapon_id == REVOLVER) {
				dmg = g_menu.main.aimbot.minimal_damage_pistols.get();
				if (g_menu.main.aimbot.minimal_damage_hp_pistols.get())
					dmg = std::ceil((dmg / 100.f) * hp);

				pendmg = g_menu.main.aimbot.penetrate_minimal_damage_pistols.get();
				if (g_menu.main.aimbot.penetrate_minimal_damage_hp_pistols.get())
					pendmg = std::ceil((pendmg / 100.f) * hp);

				pen = g_menu.main.aimbot.penetrate_pistols.get();
			}
			else if (g_cl.m_weapon_id == SSG08) {
				dmg = g_menu.main.aimbot.minimal_damage_scout.get();
				if (g_menu.main.aimbot.minimal_damage_hp_scout.get() && getscoutdmg < 100.f)
					dmg = std::ceil((dmg / 100.f) * hp);
				if (getscoutdmg > 100.f)
					dmg = hp + getscoutdmg - 100.f;

				pendmg = g_menu.main.aimbot.penetrate_minimal_damage_scout.get();
				if (g_menu.main.aimbot.penetrate_minimal_damage_hp_scout.get() && getscoutpendmg < 100.f)
					pendmg = std::ceil((pendmg / 100.f) * hp);
				if (getscoutpendmg > 100.f)
					dmg = hp + getscoutpendmg - 100.f;

				pen = g_menu.main.aimbot.penetrate_scout.get();
			}
			else if (g_cl.m_weapon_id == G3SG1 || g_cl.m_weapon_id == SCAR20) {
				dmg = g_menu.main.aimbot.minimal_damage_auto.get();
				if (g_menu.main.aimbot.minimal_damage_hp_auto.get())
					dmg = std::ceil((dmg / 100.f) * hp);

				pendmg = g_menu.main.aimbot.penetrate_minimal_damage_auto.get();
				if (g_menu.main.aimbot.penetrate_minimal_damage_hp_auto.get())
					pendmg = std::ceil((pendmg / 100.f) * hp);

				pen = g_menu.main.aimbot.penetrate_auto.get();
			}
			else if (g_cl.m_weapon_id == AWP) {
				dmg = g_menu.main.aimbot.minimal_damage_awp.get();
				if (g_menu.main.aimbot.minimal_damage_hp_awp.get() && getawpdmg < 100.f)
					dmg = std::ceil((dmg / 100.f) * hp);
				if (getawpdmg > 100.f)
					dmg = hp + getawpdmg - 100.f;

				pendmg = g_menu.main.aimbot.penetrate_minimal_damage_awp.get();
				if (g_menu.main.aimbot.penetrate_minimal_damage_hp_awp.get() && getawppendmg < 100.f)
					pendmg = std::ceil((pendmg / 100.f) * hp);
				if (getawppendmg > 100.f)
					dmg = hp + getawppendmg - 100.f;

				pen = g_menu.main.aimbot.penetrate_awp.get();
			}
			else {
				dmg = g_menu.main.aimbot.minimal_damage_general.get();
				if (g_menu.main.aimbot.minimal_damage_hp_general.get())
					dmg = std::ceil((dmg / 100.f) * hp);

				pendmg = g_menu.main.aimbot.penetrate_minimal_damage_general.get();
				if (g_menu.main.aimbot.penetrate_minimal_damage_hp_general.get())
					pendmg = std::ceil((pendmg / 100.f) * hp);

				pen = g_menu.main.aimbot.penetrate_general.get();
			}
		}
	}


	// write all data of this record l0l.
	record->cache();

	// iterate hitboxes.
	for (const auto& it : m_hitboxes) {
		done = false;

		// setup points on hitbox.
		if (!SetupHitboxPoints(record, record->m_bones, it.m_index, points))
			continue;

		// iterate points on hitbox.
		for (const auto& point : points) {
			penetration::PenetrationInput_t in;

			in.m_damage = dmg;
			in.m_damage_pen = pendmg;
			in.m_can_pen = pen;
			in.m_target = m_player;
			in.m_from = g_cl.m_local;
			in.m_pos = point;

			// ignore mindmg.
			if (it.m_mode == HitscanMode::LETHAL || it.m_mode == HitscanMode::LETHAL2)
				in.m_damage = in.m_damage_pen = 1.f;

			penetration::PenetrationOutput_t out;

			// we can hit p!
			if (penetration::run(&in, &out)) {

				// nope we did not hit head..
				if (it.m_index == HITBOX_HEAD && out.m_hitgroup != HITGROUP_HEAD)
					continue;

				// prefered hitbox, just stop now.
				if (it.m_mode == HitscanMode::PREFER)
					done = true;

				// this hitbox requires lethality to get selected, if that is the case.
				// we are done, stop now.
				else if (it.m_mode == HitscanMode::LETHAL && out.m_damage >= m_player->m_iHealth())
					done = true;

				// 2 shots will be sufficient to kill.
				else if (it.m_mode == HitscanMode::LETHAL2 && (out.m_damage * 2.f) >= m_player->m_iHealth())
					done = true;

				// this hitbox has normal selection, it needs to have more damage.
				else if (it.m_mode == HitscanMode::NORMAL) {
					// we did more damage.
					if (out.m_damage > scan.m_damage) {
						// save new best data.
						scan.m_damage = out.m_damage;
						scan.m_pos = point;

						// if the first point is lethal
						// screw the other ones.
						if (point == points.front() && out.m_damage >= m_player->m_iHealth())
							break;
					}
				}

				// we found a preferred / lethal hitbox.
				if (done) {
					// save new best data.
					scan.m_damage = out.m_damage;
					scan.m_pos = point;
					break;
				}
			}
		}

		// ghetto break out of outer loop.
		if (done)
			break;
	}


	// we found something that we can damage.
	// set out vars.
	if (scan.m_damage > 0.f) {
		aim = scan.m_pos;
		damage = scan.m_damage;
		return true;
	}

	return false;
}

bool Aimbot::SelectTarget(LagRecord* record, const vec3_t& aim, float damage) {
	float dist, fov, height;
	int   hp;

	// fov check.
	if (g_menu.main.aimbot.fov.get()) {
		// if out of fov, retn false.
		if (math::GetFOV(g_cl.m_view_angles, g_cl.m_shoot_pos, aim) > g_menu.main.aimbot.fov_amount.get())
			return false;
	}

	switch (g_menu.main.aimbot.selection.get()) {

		// distance.
	case 0:
		dist = (record->m_pred_origin - g_cl.m_shoot_pos).length();

		if (dist < m_best_dist) {
			m_best_dist = dist;
			return true;
		}

		break;

		// crosshair.
	case 1:
		fov = math::GetFOV(g_cl.m_view_angles, g_cl.m_shoot_pos, aim);

		if (fov < m_best_fov) {
			m_best_fov = fov;
			return true;
		}

		break;

		// damage.
	case 2:
		if (damage > m_best_damage) {
			m_best_damage = damage;
			return true;
		}

		break;

		// lowest hp.
	case 3:
		// fix for retarded servers?
		hp = std::min(100, record->m_player->m_iHealth());

		if (hp < m_best_hp) {
			m_best_hp = hp;
			return true;
		}

		break;

		// least lag.
	case 4:
		if (record->m_lag < m_best_lag) {
			m_best_lag = record->m_lag;
			return true;
		}

		break;

		// height.
	case 5:
		height = record->m_pred_origin.z - g_cl.m_local->m_vecOrigin().z;

		if (height < m_best_height) {
			m_best_height = height;
			return true;
		}

		break;

	default:
		return false;
	}

	return false;
}

void Aimbot::apply() {
	bool attack, attack2;


	// attack states.
	attack = (g_cl.m_cmd->m_buttons & IN_ATTACK);
	attack2 = (g_cl.m_weapon_id == REVOLVER && g_cl.m_cmd->m_buttons & IN_ATTACK2);

	// ensure we're attacking.
	if (attack || attack2) {
		// dont choke every shot.
		if (!g_hvh.m_fakeduck)
			*g_cl.m_packet = true;

		if (m_target) {
			// make sure to aim at un-interpolated data.
			// do this so BacktrackEntity selects the exact record.
			if (m_record && !m_record->m_broke_lc)
				g_cl.m_cmd->m_tick = game::TIME_TO_TICKS(m_record->m_sim_time + g_cl.m_lerp);

			// set angles to target.
			g_cl.m_cmd->m_view_angles = m_angle;

			// if not silent aim, apply the viewangles.debugaim
			if (!g_menu.main.aimbot.silent.get())
				g_csgo.m_engine->SetViewAngles(m_angle);

			if (g_menu.main.aimbot.debugaim.get())
				g_visuals.DrawHitboxMatrix(m_record, g_menu.main.aimbot.debugaimcol.get(), 10.f);
		}

		// nospread.
		if (g_menu.main.aimbot.nospread.get())
			NoSpread();

		// norecoil.
		if (g_menu.main.aimbot.norecoil.get())
			g_cl.m_cmd->m_view_angles -= g_cl.m_local->m_aimPunchAngle() * g_csgo.weapon_recoil_scale->GetFloat();

		// store fired shot.
		g_shots.OnShotFire(m_target ? m_target : nullptr, m_target ? m_damage : -1.f, g_cl.m_weapon_info->m_bullets, m_target ? m_record : nullptr, m_hitbox);

		// set that we fired.
		g_cl.m_shot = true;
	}
}

void Aimbot::NoSpread() {
	bool    attack2;
	vec3_t  spread, forward, right, up, dir;

	// revolver state.
	attack2 = (g_cl.m_weapon_id == REVOLVER && (g_cl.m_cmd->m_buttons & IN_ATTACK2));

	// get spread.
	spread = g_cl.m_weapon->CalculateSpread(g_cl.m_cmd->m_random_seed, attack2);

	// compensate.
	g_cl.m_cmd->m_view_angles -= { -math::rad_to_deg(std::atan(spread.length_2d())), 0.f, math::rad_to_deg(std::atan2(spread.x, spread.y)) };
}