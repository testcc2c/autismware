#pragma once

class ShotRecord;

class Resolver {
public:
	enum Modes : size_t {
		RESOLVE_NONE,
		RESOLVE_STAND,
		RESOLVE_WALK,
		RESOLVE_AIR,
		RESOLVE_LBY_UPDATE,
		RESOLVE_OVERRIDE,
		RESOLVE_LAST_LBY,
		RESOLVE_BRUTEFORCE,
		RESOLVE_FREESTAND,
		RESOLVE_STOPPED_MOVING,
		RESOLVE_SPIN
	};

public:
	LagRecord* FindIdealRecord(AimPlayer* data);
	LagRecord* FindLastRecord(AimPlayer* data);

	void OnBodyUpdate(Player* player, float value);
	float GetAwayAngle(LagRecord* record);
	void SetMode(LagRecord* record);
	void MatchShot(AimPlayer* data, LagRecord* record);
	void ResolveAngles(Player* player, LagRecord* record);
	void ResolveOverride(Player* player, LagRecord* record, AimPlayer* data);
	bool ShouldUseFreestand(LagRecord* record);
	void Freestand(Player* player, AimPlayer* data, LagRecord* record);
	bool Spin_Detection(AimPlayer* data);
	void ResolveStand(Player* player, AimPlayer* data, LagRecord* record);
	void ResolveWalk(AimPlayer* data, LagRecord* record);
	void ResolveAir(Player* player, AimPlayer* data, LagRecord* record);
	void ResolvePoses(Player* player, LagRecord* record);
	bool bFacingright;
	bool bFacingleft; // genius code btw.
	float spindelta;
	float spinbody;
	int spin_step;

public:
	using records_t = std::deque< std::shared_ptr< LagRecord > >;
	records_t m_records;
	int	   iPlayers[64];
};

extern Resolver g_resolver;