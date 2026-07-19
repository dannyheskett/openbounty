// autoplay/baltree.h
//
// The search frontier (AP-201): a self-balancing (AVL) multiset keyed on an
// int, each key carrying a void* payload (a search node). Three O(log n)
// operations are what a memory-bounded frontier needs:
//   * pop-min  -- take the next node to expand,
//   * insert   -- add its children, and re-insert it while branches remain,
//   * pop-max  -- evict when the frontier outgrows its cap.
// A heap gives pop-min + insert but not pop-max; the balanced tree gives all
// three. The key's MEANING belongs to the caller: autoplay/search.c keys on
// the negated creation sequence, which makes pop-min the newest node and
// pop-max the oldest -- depth-first expansion with root-most eviction.
// Equal keys are allowed (a multiset); order among equal keys is unspecified.

#ifndef OB_AUTOPLAY_BALTREE_H
#define OB_AUTOPLAY_BALTREE_H

#include <stdbool.h>

typedef struct BalTree BalTree;

BalTree *baltree_new(void);

// Free the tree. If free_payload is non-NULL it is called on every remaining
// payload first (the caller owns payload lifetime otherwise).
void     baltree_free(BalTree *t, void (*free_payload)(void *p));

int      baltree_size(const BalTree *t);

// Insert `payload` at `key`. Returns false only on allocation failure.
bool     baltree_insert(BalTree *t, int key, void *payload);

// Remove and return the smallest-key payload -- the next node to expand.
void    *baltree_pop_min(BalTree *t, int *key_out);

// Remove and return the largest-key payload -- the frontier trim.
void    *baltree_pop_max(BalTree *t, int *key_out);

#endif
