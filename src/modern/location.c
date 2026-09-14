// src/modern/location.c -- see location.h.

#include "modern/location.h"
#include "resources.h"
#include "tables.h"
#include <stdio.h>
#include <string.h>

static struct {
    bool pending;
    bool begun;
    int  gold_before, gold_after;
    int  recruited;
    char troop_id[32];
    char message[RES_BANNER_LEN];
} deal;

void loc_deal_clear(void) { memset(&deal, 0, sizeof deal); }

void loc_deal_begin(const Game *g) {
    loc_deal_clear();
    deal.begun = true;
    deal.gold_before = g ? g->stats.gold : 0;
}

void loc_deal_done(const Game *g, int recruited, const char *troop_id) {
    if (!deal.begun) return;
    deal.pending = true;
    deal.gold_after = g ? g->stats.gold : 0;
    deal.recruited = (deal.gold_after < deal.gold_before) ? recruited : 0;   // paid: they joined
    snprintf(deal.troop_id, sizeof deal.troop_id, "%s", troop_id ? troop_id : "");
}

void loc_deal_absorb(const char *text) {
    deal.pending = true;
    if (!deal.begun) { deal.gold_before = deal.gold_after = -1; }
    snprintf(deal.message, sizeof deal.message, "%s", text ? text : "");
}

bool loc_deal_pending(void) { return deal.pending; }

void loc_deal_text(const Game *g, char *out, int cap) {
    out[0] = '\0';
    if (!g || !g->res) return;
    const ResBanners *bn = &g->res->banners;
    int off = 0;
    if (deal.message[0])
        off += snprintf(out + off, (size_t)(cap - off), "%s", deal.message);
    if (deal.recruited > 0 && off < cap) {
        const TroopDef *t = troop_by_id(deal.troop_id);
        char cb[16], line[RES_BANNER_LEN];
        snprintf(cb, sizeof cb, "%d", deal.recruited);
        ResTemplateVar v[] = { { "COUNT", cb }, { "TROOP", t ? t->name : deal.troop_id } };
        resources_format_template(line, sizeof line, bn->loc_joined, v, 2);
        off += snprintf(out + off, (size_t)(cap - off), "%s%s", off ? "\n" : "", line);
    }
    if (deal.begun && deal.gold_after != deal.gold_before && off < cap) {
        char fb[16], tb[16], line[RES_BANNER_LEN];
        snprintf(fb, sizeof fb, "%d", deal.gold_before);
        snprintf(tb, sizeof tb, "%d", deal.gold_after);
        ResTemplateVar v[] = { { "FROM", fb }, { "TO", tb } };
        resources_format_template(line, sizeof line, bn->loc_gold_change, v, 2);
        snprintf(out + off, (size_t)(cap - off), "%s%s", off ? "\n\n" : "", line);
    }
}
