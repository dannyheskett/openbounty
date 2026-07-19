// autoplay/baltree.c -- the AVL multiset backing the search frontier (AP-201).

#include "baltree.h"

#include <stdlib.h>

typedef struct Node {
    int          key;              // ordering key; meaning owned by the caller
    void        *payload;          // the search node
    struct Node *left, *right;
    int          height;
} Node;

struct BalTree {
    Node *root;
    int   size;
};

static int nheight(const Node *n) { return n ? n->height : 0; }
static int imax(int a, int b) { return a > b ? a : b; }
static void fix_height(Node *n) {
    n->height = 1 + imax(nheight(n->left), nheight(n->right));
}
static int balance_factor(const Node *n) {
    return n ? nheight(n->left) - nheight(n->right) : 0;
}

static Node *rot_right(Node *y) {
    Node *x = y->left, *t = x->right;
    x->right = y; y->left = t;
    fix_height(y); fix_height(x);
    return x;
}
static Node *rot_left(Node *x) {
    Node *y = x->right, *t = y->left;
    y->left = x; x->right = t;
    fix_height(x); fix_height(y);
    return y;
}

// Restore the AVL invariant (|balance| <= 1) at n after a height change below.
static Node *rebalance(Node *n) {
    fix_height(n);
    int bf = balance_factor(n);
    if (bf > 1) {                                   // left-heavy
        if (balance_factor(n->left) < 0) n->left = rot_left(n->left);
        return rot_right(n);
    }
    if (bf < -1) {                                  // right-heavy
        if (balance_factor(n->right) > 0) n->right = rot_right(n->right);
        return rot_left(n);
    }
    return n;
}

static Node *node_new(int key, void *payload) {
    Node *n = (Node *)malloc(sizeof *n);
    if (!n) return NULL;
    n->key = key; n->payload = payload;
    n->left = n->right = NULL; n->height = 1;
    return n;
}

// Equal keys go RIGHT, so a multiset stays balanced and pop order among ties is
// stable-ish. *ok is cleared on allocation failure (the subtree is unchanged).
static Node *node_insert(Node *n, int key, void *payload, bool *ok) {
    if (!n) {
        Node *nn = node_new(key, payload);
        if (!nn) { *ok = false; return NULL; }
        return nn;
    }
    if (key < n->key) n->left = node_insert(n->left, key, payload, ok);
    else              n->right = node_insert(n->right, key, payload, ok);
    if (!*ok) return n;
    return rebalance(n);
}

static Node *node_pop_min(Node *n, void **out_payload, int *out_key) {
    if (!n->left) {
        *out_payload = n->payload; *out_key = n->key;
        Node *r = n->right; free(n); return r;
    }
    n->left = node_pop_min(n->left, out_payload, out_key);
    return rebalance(n);
}
static Node *node_pop_max(Node *n, void **out_payload, int *out_key) {
    if (!n->right) {
        *out_payload = n->payload; *out_key = n->key;
        Node *l = n->left; free(n); return l;
    }
    n->right = node_pop_max(n->right, out_payload, out_key);
    return rebalance(n);
}

static void node_free_all(Node *n, void (*fp)(void *)) {
    if (!n) return;
    node_free_all(n->left, fp);
    node_free_all(n->right, fp);
    if (fp) fp(n->payload);
    free(n);
}

BalTree *baltree_new(void) { return (BalTree *)calloc(1, sizeof(BalTree)); }

void baltree_free(BalTree *t, void (*fp)(void *)) {
    if (!t) return;
    node_free_all(t->root, fp);
    free(t);
}

int baltree_size(const BalTree *t) { return t ? t->size : 0; }

bool baltree_insert(BalTree *t, int key, void *payload) {
    bool ok = true;
    Node *r = node_insert(t->root, key, payload, &ok);
    if (!ok) return false;
    t->root = r; t->size++;
    return true;
}

void *baltree_pop_min(BalTree *t, int *key_out) {
    if (!t->root) return NULL;
    void *p; int k;
    t->root = node_pop_min(t->root, &p, &k);
    t->size--;
    if (key_out) *key_out = k;
    return p;
}

void *baltree_pop_max(BalTree *t, int *key_out) {
    if (!t->root) return NULL;
    void *p; int k;
    t->root = node_pop_max(t->root, &p, &k);
    t->size--;
    if (key_out) *key_out = k;
    return p;
}
