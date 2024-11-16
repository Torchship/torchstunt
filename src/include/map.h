/******************************************************************************
  Copyright 2010 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY TODD SUNDSTED ``AS IS'' AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
  EVENT SHALL TODD SUNDSTED OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are
  those of the authors and should not be interpreted as representing official
  policies, either expressed or implied, of Todd Sundsted.
 *****************************************************************************/

#include "structures.h"

#ifndef MAP_H
#define MAP_H
#define HEIGHT_LIMIT 64     /* Tallest allowable tree */

struct rbtree {
    rbnode *root;       /* Top of the tree */
    size_t size;        /* Number of items */
};

struct rbnode {
    Var key;
    Var value;
    int red;            /* Color (1=red, 0=black) */
    rbnode *link[2];        /* Left (0) and right (1) links */
};

struct rbtrav {
    rbtree *tree;       /* Paired tree */
    rbnode *it;         /* Current node */
    rbnode *path[HEIGHT_LIMIT]; /* Traversal path */
    size_t top;         /* Top of stack */
};
#endif

extern Var new_map(void);
extern void destroy_map(Var map);
extern Var map_dup(Var map);

extern Var mapinsert(Var map, Var key, Var value);
extern const rbnode *maplookup(Var map, Var key, Var *value, int case_matters);
extern const rbnode *mapstrlookup(Var map, const char *key, Var *value, int case_matters);
extern int mapseek(Var map, Var key, Var *iter, int case_matters);
extern int mapequal(Var lhs, Var rhs, int case_matters);
extern Num maplength(Var map);
extern int mapempty(Var map);
extern rbnode *rbtfirst(rbtrav *trav, rbtree *tree);
extern rbnode *rbtnext(rbtrav *trav);

extern int map_sizeof(rbtree *tree);

extern int mapfirst(Var map, var_pair *pair);
extern int maplast(Var map, var_pair *pair);

extern Var new_iter(Var map);
extern void destroy_iter(Var iter);
extern Var iter_dup(Var iter);

extern int iterget(Var iter, var_pair *pair);
extern void iternext(Var iter);

extern Var maprange(Var map, rbtrav *from, rbtrav *to);
extern enum error maprangeset(Var map, rbtrav *from, rbtrav *to, Var value, Var *_new);

typedef int (*mapfunc) (Var key, Var value, void *data, int first);
extern int mapforeach(Var map, mapfunc func, void *data);

/*
 * Iterate over the key-value pairs in the map `mp`. Sets `key` and `val`
 * to each key-value pair in turn. `idx` and `cnt` must be int variables.
 * In the body of the statement, they hold the current index and total count,
 * respectively. Use the macro as follows (assuming you already have a map in `items`):
 *
 *   Var key, value;
 *   int i, c;
 *   FOR_EACH_MAP(key, value, items) {
 *       printf("key = %s, value = %s\n", value_to_literal(key), value_to_literal(value));
 *   }
 */
#define FOR_EACH_MAP(key, val, map) \
    for (rbtrav trav__ = {0}; \
         ((trav__.it = (trav__.it ? rbtnext(&trav__) : rbtfirst(&trav__, (map).v.tree))) != NULL) && \
         ((key) = trav__.it->key, (val) = trav__.it->value, 1); )


/* You're never going to need to use this!
 * Clears a node in place by setting the associated value type to
 * `E_NONE'.  This _destructively_ updates the associated tree.  The
 * method is used in `execute.c' to clear a node's value in a map when
 * the vm knows that it will eventually replace that value.  This
 * removes a `var_ref' and eventual `map_dup' when the vm can
 * guarantee that a nested map is not shared.
 */
extern void clear_node_value(const rbnode *node);
