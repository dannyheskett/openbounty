// Test-only convenience header: a single include for the combat tests.
// The combat API itself is public in engine/include/combat.h, which this
// simply re-exports.

#ifndef OB_TEST_COMBAT_INTERNAL_H
#define OB_TEST_COMBAT_INTERNAL_H

#include "combat.h"

// engine/combat.c, exposed for tests: the attacker's morale rank under the
// pack's rule (REQ-385 item 3): 0 Normal, 1 Low, 2 High.
int combat_unit_morale_rank(const Combat *c, int side, int slot);

#endif
