#include "pending.h"

#include <string.h>

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

int  pending_discard_spell_idx = -1;

WeekPhase pending_week_phase = WK_PHASE_NONE;
int       pending_week_id    = 0;
int       pending_week_paid  = 0;
int       pending_astrology_troop_idx = 0;

void pending_reset(void) {
    pending_flow = FLOW_NONE;
    pending_dwelling_troop[0] = '\0';
    pending_dwelling_zone[0]  = '\0';
    pending_dwelling_x = pending_dwelling_y = -1;
    pending_friendly_count = 0;
    pending_friendly_foe_id[0] = '\0';
    pending_audience_message[0] = '\0';
    memset(pending_nav_zones, 0, sizeof pending_nav_zones);
    pending_nav_count = 0;
    pending_castle_id[0] = '\0';
    pending_foe_id[0] = '\0';
    pending_foe_x = pending_foe_y = -1;
    pending_chest_gold = 0;
    pending_chest_leadership = 0;
    pending_discard_spell_idx = -1;
    pending_week_phase = WK_PHASE_NONE;
    pending_week_id = 0;
    pending_week_paid = 0;
    pending_astrology_troop_idx = 0;
}
