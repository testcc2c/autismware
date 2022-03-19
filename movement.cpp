#include "includes.h"
#include <vector>



Movement g_movement{ };;


void Movement::JumpRelated() {
	if (g_cl.m_local->m_MoveType() == MOVETYPE_NOCLIP)
		return;

	if ((g_cl.m_cmd->m_buttons & IN_JUMP) && !(g_cl.m_flags & FL_ONGROUND)) {
		// bhop.
		if (g_menu.main.movement.bhop.get())
			g_cl.m_cmd->m_buttons &= ~IN_JUMP;

		// duck jump ( crate jump ).
		if (g_menu.main.movement.airduck.get())
			g_cl.m_cmd->m_buttons |= IN_DUCK;
	}
}






void Movement::Strafe() {
	vec3_t velocity;
	float  delta, abs_delta, velocity_delta, correct;

	// don't strafe while we prolly want to jump scout..
	// if (g_movement.m_slow_motion)
	//    return;

	// don't strafe while noclipping or on ladders..
	if (g_cl.m_local->m_MoveType() == MOVETYPE_NOCLIP || g_cl.m_local->m_MoveType() == MOVETYPE_LADDER)
		return;

	// get networked velocity ( maybe absvelocity better here? ).
	// meh, should be predicted anyway? ill see.
	velocity = g_cl.m_local->m_vecAbsVelocity();

	// get the velocity len2d ( speed ).
	m_speed = velocity.length_2d();

	// compute the ideal strafe angle for our velocity.
	m_ideal = (m_speed > 0.f) ? math::rad_to_deg(std::asin(15.f / m_speed)) : 90.f;
	m_ideal2 = (m_speed > 0.f) ? math::rad_to_deg(std::asin(30.f / m_speed)) : 90.f;

	// some additional sanity.
	math::clamp(m_ideal, 0.f, 90.f);
	math::clamp(m_ideal2, 0.f, 90.f);

	// save entity bounds ( used much in circle-strafer ).
	m_mins = g_cl.m_local->m_vecMins();
	m_maxs = g_cl.m_local->m_vecMaxs();

	// save our origin
	m_origin = g_cl.m_local->m_vecOrigin();

	// disable strafing while pressing shift.
	if ((g_cl.m_buttons & IN_SPEED) || (g_cl.m_flags & FL_ONGROUND))
		return;

	// for changing direction.
	// we want to change strafe direction every call.
	m_switch_value *= -1.f;

	// for allign strafer.
	++m_strafe_index;

	if (g_cl.m_pressing_move) {
		// took this idea from stacker, thank u !!!!
		enum EDirections {
			FORWARDS = 0,
			BACKWARDS = 180,
			LEFT = 90,
			RIGHT = -90,
			BACK_LEFT = 135,
			BACK_RIGHT = -135
		};

		float wish_dir{ };

		// get our key presses.
		bool holding_w = g_cl.m_buttons & IN_FORWARD;
		bool holding_a = g_cl.m_buttons & IN_MOVELEFT;
		bool holding_s = g_cl.m_buttons & IN_BACK;
		bool holding_d = g_cl.m_buttons & IN_MOVERIGHT;

		// move in the appropriate direction.
		if (holding_w) {
			//    forward left
			if (holding_a) {
				wish_dir += (EDirections::LEFT / 2);
			}
			//    forward right
			else if (holding_d) {
				wish_dir += (EDirections::RIGHT / 2);
			}
			//    forward
			else {
				wish_dir += EDirections::FORWARDS;
			}
		}
		else if (holding_s) {
			//    back left
			if (holding_a) {
				wish_dir += EDirections::BACK_LEFT;
			}
			//    back right
			else if (holding_d) {
				wish_dir += EDirections::BACK_RIGHT;
			}
			//    back
			else {
				wish_dir += EDirections::BACKWARDS;
			}

			g_cl.m_cmd->m_forward_move = 0;
		}
		else if (holding_a) {
			//    left
			wish_dir += EDirections::LEFT;
		}
		else if (holding_d) {
			//    right
			wish_dir += EDirections::RIGHT;
		}

		g_cl.m_strafe_angles.y += math::NormalizeYaw(wish_dir);
	}

	// cancel out any forwardmove values.
	g_cl.m_cmd->m_forward_move = 0.f;

	// do allign strafer.
	if (g_input.GetKeyState(g_menu.main.movement.astrafe.get())) {
		float angle = std::max(m_ideal2, 4.f);

		if (angle > m_ideal2 && !(m_strafe_index % 5))
			angle = m_ideal2;

		// add the computed step to the steps of the previous circle iterations.
		m_circle_yaw = math::NormalizedAngle(m_circle_yaw + angle);

		// apply data to usercmd.
		g_cl.m_strafe_angles.y = m_circle_yaw;
		g_cl.m_cmd->m_side_move = -450.f;

		return;
	}

	// do ciclestrafer
	else if (g_input.GetKeyState(g_menu.main.movement.cstrafe.get())) {
		// if no duck jump.
		if (!g_menu.main.movement.airduck.get()) {
			// crouch to fit into narrow areas.
			g_cl.m_cmd->m_buttons |= IN_DUCK;
		}

		DoPrespeed();
		return;
	}

	else if (g_input.GetKeyState(g_menu.main.movement.zstrafe.get())) {
		float freq = (g_menu.main.movement.z_freq.get() * 0.2f) * g_csgo.m_globals->m_realtime;

		// range [ 1, 100 ], aka grenerates a factor.
		float factor = g_menu.main.movement.z_dist.get() * 0.5f;

		g_cl.m_strafe_angles.y += (factor * std::sin(freq));
	}

	if (!g_menu.main.movement.autostrafe.get())
		return;

	// get our viewangle change.
	delta = math::NormalizedAngle(g_cl.m_strafe_angles.y - m_old_yaw);

	// convert to absolute change.
	abs_delta = std::abs(delta);

	// save old yaw for next call.
	m_circle_yaw = m_old_yaw = g_cl.m_strafe_angles.y;

	// set strafe direction based on mouse direction change.
	if (delta > 0.f)
		g_cl.m_cmd->m_side_move = -450.f;

	else if (delta < 0.f)
		g_cl.m_cmd->m_side_move = 450.f;

	// we can accelerate more, because we strafed less then needed
	// or we got of track and need to be retracked.
	if (abs_delta <= m_ideal || abs_delta >= 30.f) {
		// compute angle of the direction we are traveling in.
		ang_t velocity_angle;
		math::VectorAngles(velocity, velocity_angle);

		// get the delta between our direction and where we are looking at.
		velocity_delta = math::NormalizeYaw(g_cl.m_strafe_angles.y - velocity_angle.y);

		// correct our strafe amongst the path of a circle.
		correct = m_ideal;

		if (velocity_delta <= correct || m_speed <= 15.f) {
			// not moving mouse, switch strafe every tick.
			if (-correct <= velocity_delta || m_speed <= 15.f) {
				g_cl.m_strafe_angles.y += (m_ideal * m_switch_value);
				g_cl.m_cmd->m_side_move = 450.f * m_switch_value;
			}

			else {
				g_cl.m_strafe_angles.y = velocity_angle.y - correct;
				g_cl.m_cmd->m_side_move = 450.f;
			}
		}

		else {
			g_cl.m_strafe_angles.y = velocity_angle.y + correct;
			g_cl.m_cmd->m_side_move = -450.f;
		}
	}
}










void Movement::DoPrespeed() {
	float   mod, min, max, step, strafe, time, angle;
	vec3_t  plane;

	// min and max values are based on 128 ticks.
	mod = g_csgo.m_globals->m_interval * 128.f;

	// scale min and max based on tickrate.
	min = 2.25f * mod;
	max = 5.f * mod;

	// compute ideal strafe angle for moving in a circle.
	strafe = m_ideal * 2.f;

	// clamp ideal strafe circle value to min and max step.
	math::clamp(strafe, min, max);

	// calculate time.
	time = 320.f / m_speed;

	// clamp time.
	math::clamp(time, 0.35f, 1.f);

	// init step.
	step = strafe;

	while (true) {
		// if we will not collide with an object or we wont accelerate from such a big step anymore then stop.
		if (!WillCollide(time, step) || max <= step)
			break;

		// if we will collide with an object with the current strafe step then increment step to prevent a collision.
		step += 0.2f;
	}

	if (step > max) {
		// reset step.
		step = strafe;

		while (true) {
			// if we will not collide with an object or we wont accelerate from such a big step anymore then stop.
			if (!WillCollide(time, step) || step <= -min)
				break;

			// if we will collide with an object with the current strafe step decrement step to prevent a collision.
			step -= 0.2f;
		}

		if (step < -min) {
			if (GetClosestPlane(plane)) {
				// grab the closest object normal
				// compute the angle of the normal
				// and push us away from the object.
				angle = math::rad_to_deg(std::atan2(plane.y, plane.x));
				step = -math::NormalizedAngle(m_circle_yaw - angle) * 0.1f;
			}
		}

		else
			step -= 0.2f;
	}

	else
		step += 0.2f;

	// add the computed step to the steps of the previous circle iterations.
	m_circle_yaw = math::NormalizedAngle(m_circle_yaw + step);

	// apply data to usercmd.
	g_cl.m_cmd->m_view_angles.y = m_circle_yaw;
	g_cl.m_cmd->m_side_move = (step >= 0.f) ? -450.f : 450.f;
}

bool Movement::GetClosestPlane(vec3_t& plane) {
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;
	vec3_t                start{ m_origin };
	float                 smallest{ 1.f };
	const float		      dist{ 75.f };

	// trace around us in a circle
	for (float step{ }; step <= math::pi_2; step += (math::pi / 10.f)) {
		// extend endpoint x units.
		vec3_t end = start;
		end.x += std::cos(step) * dist;
		end.y += std::sin(step) * dist;

		g_csgo.m_engine_trace->TraceRay(Ray(start, end, m_mins, m_maxs), CONTENTS_SOLID, &filter, &trace);

		// we found an object closer, then the previouly found object.
		if (trace.m_fraction < smallest) {
			// save the normal of the object.
			plane = trace.m_plane.m_normal;
			smallest = trace.m_fraction;
		}
	}

	// did we find any valid object?
	return smallest != 1.f && plane.z < 0.1f;
}

bool Movement::WillCollide(float time, float change) {
	struct PredictionData_t {
		vec3_t start;
		vec3_t end;
		vec3_t velocity;
		float  direction;
		bool   ground;
		float  predicted;
	};

	PredictionData_t      data;
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;

	// set base data.
	data.ground = g_cl.m_flags & FL_ONGROUND;
	data.start = m_origin;
	data.end = m_origin;
	data.velocity = g_cl.m_local->m_vecVelocity();
	data.direction = math::rad_to_deg(std::atan2(data.velocity.y, data.velocity.x));

	for (data.predicted = 0.f; data.predicted < time; data.predicted += g_csgo.m_globals->m_interval) {
		// predict movement direction by adding the direction change.
		// make sure to normalize it, in case we go over the -180/180 turning point.
		data.direction = math::NormalizedAngle(data.direction + change);

		// pythagoras.
		float hyp = data.velocity.length_2d();

		// adjust velocity for new direction.
		data.velocity.x = std::cos(math::deg_to_rad(data.direction)) * hyp;
		data.velocity.y = std::sin(math::deg_to_rad(data.direction)) * hyp;

		// assume we bhop, set upwards impulse.
		if (data.ground)
			data.velocity.z = g_csgo.sv_jump_impulse->GetFloat();

		else
			data.velocity.z -= g_csgo.sv_gravity->GetFloat() * g_csgo.m_globals->m_interval;

		// we adjusted the velocity for our new direction.
		// see if we can move in this direction, predict our new origin if we were to travel at this velocity.
		data.end += (data.velocity * g_csgo.m_globals->m_interval);

		// trace
		g_csgo.m_engine_trace->TraceRay(Ray(data.start, data.end, m_mins, m_maxs), MASK_PLAYERSOLID, &filter, &trace);

		// check if we hit any objects.
		if (trace.m_fraction != 1.f && trace.m_plane.m_normal.z <= 0.9f)
			return true;
		if (trace.m_startsolid || trace.m_allsolid)
			return true;

		// adjust start and end point.
		data.start = data.end = trace.m_endpos;

		// move endpoint 2 units down, and re-trace.
		// do this to check if we are on th floor.
		g_csgo.m_engine_trace->TraceRay(Ray(data.start, data.end - vec3_t{ 0.f, 0.f, 2.f }, m_mins, m_maxs), MASK_PLAYERSOLID, &filter, &trace);

		// see if we moved the player into the ground for the next iteration.
		data.ground = trace.hit() && trace.m_plane.m_normal.z > 0.7f;
	}

	// the entire loop has ran
	// we did not hit shit.
	return false;
}

void Movement::MoonWalk(CUserCmd* cmd) {
	if (g_cl.m_local->m_MoveType() == MOVETYPE_LADDER || g_cl.m_local->m_MoveType() == MOVETYPE_NOCLIP)
		return;

	// slide walk
	g_cl.m_cmd->m_buttons |= IN_BULLRUSH;

	if (g_menu.main.misc.slide_walk.get()) {
		if (cmd->m_side_move < 0.f)
		{
			cmd->m_buttons |= IN_MOVERIGHT;
			cmd->m_buttons &= ~IN_MOVELEFT;
		}

		if (cmd->m_side_move > 0.f)
		{
			cmd->m_buttons |= IN_MOVELEFT;
			cmd->m_buttons &= ~IN_MOVERIGHT;
		}

		if (cmd->m_forward_move > 0.f)
		{
			cmd->m_buttons |= IN_BACK;
			cmd->m_buttons &= ~IN_FORWARD;
		}

		if (cmd->m_forward_move < 0.f)
		{
			cmd->m_buttons |= IN_FORWARD;
			cmd->m_buttons &= ~IN_BACK;
		}
	}

}

void Movement::FixMove(CUserCmd* cmd, const ang_t& wish_angles) {

	vec3_t  move, dir;
	float   delta, len;
	ang_t   move_angle;
	static auto fps_max = g_csgo.m_cvar->FindVar(HASH("fps_max"));

	// roll nospread fix.
	if (!(g_cl.m_flags & FL_ONGROUND) && cmd->m_view_angles.z != 0.f)
		cmd->m_side_move = 0.f;

	// convert movement to vector.
	move = { cmd->m_forward_move, cmd->m_side_move, 0.f };

	// get move length and ensure we're using a unit vector ( vector with length of 1 ).
	len = move.normalize();
	if (!len)
		return;

	// convert move to an angle.
	math::VectorAngles(move, move_angle);

	// calculate yaw delta.
	delta = (cmd->m_view_angles.y - wish_angles.y);

	// accumulate yaw delta.
	move_angle.y += delta;

	// calculate our new move direction.
	// dir = move_angle_forward * move_length
	math::AngleVectors(move_angle, &dir);

	// scale to og movement.
	dir *= len;

	// strip old flags.
	g_cl.m_cmd->m_buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

	// fix ladder and noclip.
	if (g_cl.m_local->m_MoveType() == MOVETYPE_LADDER) {
		// invert directon for up and down.
		if (cmd->m_view_angles.x >= 45.f && wish_angles.x < 45.f && std::abs(delta) <= 65.f)
			dir.x = -dir.x;

		// write to movement.
		cmd->m_forward_move = dir.x;
		cmd->m_side_move = dir.y;

		// set new button flags.
		if (cmd->m_forward_move > 200.f)
			cmd->m_buttons |= IN_FORWARD;

		else if (cmd->m_forward_move < -200.f)
			cmd->m_buttons |= IN_BACK;

		if (cmd->m_side_move > 200.f)
			cmd->m_buttons |= IN_MOVERIGHT;

		else if (cmd->m_side_move < -200.f)
			cmd->m_buttons |= IN_MOVELEFT;
	}

	// we are moving normally.
	else {
		// we must do this for pitch angles that are out of bounds.
		if (cmd->m_view_angles.x < -90.f || cmd->m_view_angles.x > 90.f)
			dir.x = -dir.x;

		// set move.
		cmd->m_forward_move = dir.x;
		cmd->m_side_move = dir.y;

		// set new button flags.
		if (cmd->m_forward_move > 0.f)
			cmd->m_buttons |= IN_FORWARD;

		else if (cmd->m_forward_move < 0.f)
			cmd->m_buttons |= IN_BACK;

		if (cmd->m_side_move > 0.f)
			cmd->m_buttons |= IN_MOVERIGHT;

		else if (cmd->m_side_move < 0.f)
			cmd->m_buttons |= IN_MOVELEFT;
	}
	// cripwalk movement fix.
	if (g_input.GetKeyState(g_menu.main.movement.cripwalk.get()))
		fps_max->SetValue(XOR("60"));
	else
		fps_max->SetValue(XOR("0"));
}


void Movement::cripwalk() {

	if (g_aimbot.m_stop)
		return;

	if (!g_input.GetKeyState(g_menu.main.movement.cripwalk.get()))
		return;

	if (g_input.GetKeyState(g_menu.main.movement.cripwalk.get())) { /// was added

		static int oldcmds;

		if (oldcmds != g_cl.m_cmd->m_command_number)
			oldcmds = g_cl.m_cmd->m_command_number;

		if (*g_cl.m_packet) {
			g_cl.m_cmd->m_command_number = oldcmds;
			g_cl.m_cmd->m_tick = INT_MAX;
		}
	}
}
void Movement::QuickStop()
{
	if (!g_menu.main.movement.autostop_always_on.get())
		return;

	if (g_input.GetKeyState(g_menu.main.movement.fakewalk.get()))
		return;

	if (!g_cl.m_ground)
		return;

	// don't do autostop on the freeze period, if we're frozen or on round end.
	if (g_csgo.m_gamerules->m_bFreezePeriod() || (g_cl.m_flags & FL_FROZEN))// || g_cl.m_round_end )
		return;

	if (g_aimbot.m_stop)
	{
		auto rotate_movement = [](float yaw) -> void
		{
			float rotation = math::deg_to_rad(g_cl.m_cmd->m_view_angles.y - yaw);

			float cos_rot = cos(rotation);
			float sin_rot = sin(rotation);

			float new_forwardmove = (cos_rot * g_cl.m_cmd->m_forward_move) - (sin_rot * g_cl.m_cmd->m_side_move);
			float new_sidemove = (sin_rot * g_cl.m_cmd->m_forward_move) + (cos_rot * g_cl.m_cmd->m_side_move);

			g_cl.m_cmd->m_forward_move = new_forwardmove;
			g_cl.m_cmd->m_side_move = new_sidemove;
		};
		auto TicksToStop = [](vec3_t velocity) -> int
		{
			static auto sv_maxspeed = g_csgo.m_cvar->FindVar(HASH("sv_maxspeed"));

			const float max_speed = sv_maxspeed->GetFloat();
			const float acceleration = 5.5f;
			const float max_accelspeed = acceleration * max_speed * g_csgo.m_globals->m_interval;

			return velocity.length() / max_accelspeed;
		};
		const int ticks_to_stop = TicksToStop(g_cl.m_local->m_vecVelocity());
		const auto new_eye_position = g_cl.m_local->GetAbsOrigin() + g_cl.m_local->m_vecViewOffset() +
			(g_cl.m_local->m_vecVelocity() * g_csgo.m_globals->m_interval * ticks_to_stop);


		if (!g_cl.m_flags & FL_ONGROUND)
			return;

		static const auto nospread = g_csgo.m_cvar->FindVar(HASH("weapon_accuracy_nospread"));

		if (nospread->GetInt() || !g_cl.m_flags & FL_ONGROUND ||
			(g_cl.m_weapon && g_cl.m_weapon_id == ZEUS) && g_cl.m_flags & FL_ONGROUND)
			return;

		const auto wpn_info = g_cl.m_weapon;

		if (!wpn_info)
			return;

		auto& info = get_autostop_info(g_cl.m_cmd);

		if (info.call_time == g_csgo.m_globals->m_curtime)
		{
			info.did_stop = true;
			return;
		}

		info.did_stop = false;
		info.call_time = g_csgo.m_globals->m_curtime;

		if (g_cl.m_local->m_vecVelocity().length_2d() <= g_cl.m_weapon->GetInaccuracy())
			return;
		else
		{
			g_cl.m_cmd->m_side_move = 0;
			g_cl.m_cmd->m_forward_move = g_cl.m_local->m_vecVelocity().length_2d() > 13.f ? 450.f : 0.f;
			rotate_movement(math::CalcAngle(vec3_t(0, 0, 0), g_cl.m_local->m_vecVelocity()).y + 180.f);


			if (g_input.GetKeyState(g_menu.main.movement.fakewalk.get()))
			{
				info.did_stop = true;
				return;
			}

			if (g_cl.m_local->m_vecVelocity().length_2d() <= g_cl.m_weapon->GetInaccuracy())
				return;
		}

		ang_t dir;
		math::VectorAngles(g_cl.m_local->m_vecVelocity(), dir);
		const auto angles = g_cl.m_cmd->m_view_angles;
		dir.y = angles.y - dir.y;

		vec3_t move;
		math::AngleVectors69(dir, move);

		if (g_cl.m_local->m_vecVelocity().length_2d() > .1f)
			move *= -math::forward_bounds / std::max(std::abs(move.x), std::abs(move.y));

		g_cl.m_cmd->m_forward_move = move.x;
		g_cl.m_cmd->m_side_move = move.y;

		const auto backup = g_cl.m_cmd->m_view_angles;
		g_cl.m_cmd->m_view_angles = angles;
		g_cl.m_cmd->m_view_angles = backup;

		if (g_cl.m_local->m_vecVelocity().length_2d() > g_cl.m_local->m_vecVelocity().length_2d())
		{
			g_cl.m_cmd->m_side_move = 0;
			g_cl.m_cmd->m_forward_move = g_cl.m_local->m_vecVelocity().length_2d() > 13.f ? 450.f : 0.f;
			rotate_movement(math::CalcAngle(vec3_t(0, 0, 0), g_cl.m_local->m_vecVelocity()).y + 180.f);
		}
		g_aimbot.m_stop = false;
	}
}

void Movement::FakeWalk() {
	auto fwspeed = g_menu.main.movement.fakewalkspeed.get();
	vec3_t velocity{ g_cl.m_local->m_vecVelocity() };
	int    ticks{ };

	if (g_cl.m_weapon_id == REVOLVER)
		fwspeed = 6;

	if (!g_input.GetKeyState(g_menu.main.movement.fakewalk.get()))
		return;

	if (!g_cl.m_local->GetGroundEntity())
		return;

	// reference:
	// https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/game/shared/gamemovement.cpp#L1612

	// calculate friction.
	float friction = g_csgo.sv_friction->GetFloat() * g_cl.m_local->m_surfaceFriction();

	for (; ticks < g_cl.m_max_lag; ++ticks) {
		// calculate speed.
		float speed = velocity.length();

		// if too slow return.
		if (speed <= 0.1f)
			break;

		// bleed off some speed, but if we have less than the bleed, threshold, bleed the threshold amount.
		float control = std::max(speed, g_csgo.sv_stopspeed->GetFloat());

		// calculate the drop amount.
		float drop = control * friction * g_csgo.m_globals->m_interval;

		// scale the velocity.
		float newspeed = std::max(0.f, speed - drop);

		if (newspeed != speed) {
			// determine proportion of old speed we are using.
			newspeed /= speed;

			// adjust velocity according to proportion.
			velocity *= newspeed;
		}
	}

	// zero forwardmove and sidemove.
	if (ticks > ((fwspeed - 1) - g_csgo.m_cl->m_choked_commands) || !g_csgo.m_cl->m_choked_commands) {
		g_cl.m_cmd->m_forward_move = g_cl.m_cmd->m_side_move = 0.f;
	}
}

bool position_reset = false;
vec3_t position = { 0, 0, 0 };

static constexpr long double M_RADPI = 57.295779513082f;
static constexpr long double M_PIRAD = 0.01745329251f;
static constexpr float SMALL_NUM = 0.00000001f;

__forceinline vec3_t vector_angles(const vec3_t& start, const vec3_t& end) {
	vec3_t delta = end - start;

	float magnitude = sqrtf(delta.x * delta.x + delta.y * delta.y);
	float pitch = atan2f(-delta.z, magnitude) * M_RADPI;
	float yaw = atan2f(delta.y, delta.x) * M_RADPI;

	vec3_t angle(pitch, yaw, 0.0f);
	return angle.clamp();
}

__forceinline vec3_t angle_vectors(const vec3_t& angles) {
	float sp, sy, cp, cy;
	sp = sinf(angles.x * M_PIRAD);
	cp = cosf(angles.x * M_PIRAD);
	sy = sinf(angles.y * M_PIRAD);
	cy = cosf(angles.y * M_PIRAD);

	return vec3_t{ cp * cy, cp * sy, -sp };
}

void Movement::AutoPeek() {
	if (!(g_cl.m_flags & FL_ONGROUND))
		return;

	// set to invert if we press the button.
	if (g_input.GetKeyState(g_menu.main.movement.autopeek.get())) {
		if (!position_reset)
		{
			position_reset = true;
			position = g_cl.m_local->GetAbsOrigin();
		}

		if (g_cl.m_old_shot)
			m_invert = true;

		if (m_invert)
		{
			vec3_t direction = vector_angles(g_cl.m_local->GetAbsOrigin(), position);
			direction.y = g_cl.m_cmd->m_view_angles.y - direction.y;

			vec3_t new_move = angle_vectors(direction);
			new_move *= 450.f;

			new_move.x = std::clamp< float >(new_move.x, -450.f, 450.f);
			new_move.y = std::clamp< float >(new_move.y, -450.f, 450.f);

			g_cl.m_cmd->m_forward_move = new_move.x;
			g_cl.m_cmd->m_side_move = new_move.y;

			float distance = (g_cl.m_local->GetAbsOrigin() - position).length_2d_sqr();

			if (distance < 5 && g_cl.m_local->m_vecVelocity().length() < 10.f)
				m_invert = false;
		}
	}
	else
	{
		position_reset = false;
		m_invert = false;
	}

	bool can_stop = g_menu.main.movement.autostop_always_on.get();
	if ((g_input.GetKeyState(g_menu.main.movement.autopeek.get()) || can_stop) && g_aimbot.m_stop) {
		Movement::QuickStop();
	}
}

Movement::autostop_info& Movement::get_autostop_info(CUserCmd* cmd)
{
	if (g_cl.m_flags & FL_ONGROUND)
	{
		static autostop_info info{ -FLT_MAX, false };
		return info;
	}

	Movement::autostop_info stop;

	return stop;
}