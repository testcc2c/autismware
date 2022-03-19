#pragma once

class AimPlayer;

class LagCompensation {
public:
	enum LagType : size_t {
		INVALID = 0,
		CONSTANT,
		ADAPTIVE,
		RANDOM,
	};
	

public:
	void Extrapolate(Player* player, vec3_t& origin, vec3_t& velocity, int& flags, bool wasonground);
	bool StartPrediction(AimPlayer* player);
	void PlayerMove(LagRecord* record);
	void PredictAnimations(CCSGOPlayerAnimState* state, LagRecord* record);
};

extern LagCompensation g_lagcomp;