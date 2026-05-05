#include "pending.h"

PendingFlow pending_flow = FLOW_NONE;

char pending_dwelling_troop[32] = { 0 };
char pending_dwelling_zone[24]  = { 0 };
int  pending_dwelling_x = -1, pending_dwelling_y = -1;

int  pending_friendly_count = 0;
char pending_friendly_foe_id[40] = { 0 };

char pending_audience_message[700] = { 0 };

char pending_nav_zones[5][32] = { {0} };
int  pending_nav_count = 0;

char pending_castle_id[24] = { 0 };

char pending_foe_id[24] = { 0 };
int  pending_foe_x = -1, pending_foe_y = -1;

int  pending_chest_gold       = 0;
int  pending_chest_leadership = 0;

WeekPhase pending_week_phase = WK_PHASE_NONE;
int       pending_week_id    = 0;
int       pending_week_paid  = 0;
int       pending_astrology_troop_idx = 0;
