/******************************************************************************
  Copyright (c) 1992, 1995, 1996 Xerox Corporation.  All rights reserved.
  Portions of this code were written by Stephen White, aka ghond.
  Use and copying of this software and preparation of derivative works based
  upon this software are permitted.  Any distribution of this software or
  derivative works must comply with all applicable United States export
  control laws.  This software is made available AS IS, and Xerox Corporation
  makes no warranty about the software, its performance or its conformity to
  any specification.  Any person obtaining a copy of this software is requested
  to send their name and post office or electronic mail address to:
    Pavel Curtis
    Xerox PARC
    3333 Coyote Hill Rd.
    Palo Alto, CA 94304
    Pavel@Xerox.Com
 *****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <vector>
#include <regex>

#include "config.h"
#include "db.h"
#include "structures.h"
#include "match.h"
#include "parse_cmd.h"
#include "storage.h"
#include "unparse.h"
#include "utils.h"
#include "tasks.h"
#include "list.h"

Var
name_and_aliases(Objid player, Objid oid)
{
    Var results = new_list(1);
    results.v.list[1] = str_dup_to_var(db_object_name(oid));

    Var aliases;
    db_prop_handle h;
    h = db_find_property(Var::new_obj(oid), "aliases", &aliases);
    if (!h.ptr || aliases.type != TYPE_LIST) {
        // Do nothing it was an empty list.
    } else {
        results = listconcat(results, var_ref(aliases));
    }
    return results;
}

struct match_data {
    Objid player;
    Var targets;
    Var keys;
};

static int
match_proc(void *data, Objid oid)
{
    struct match_data *d = (struct match_data *)data;

    d->targets = listappend(d->targets, Var::new_obj(oid));
    d->keys = listappend(d->keys, name_and_aliases(d->player, oid));

    return 0;
}

Objid
match_object(Objid player, const char *name)
{
    if (name[0] == '\0')
        return NOTHING;
    if (name[0] == '#' && is_wizard(player)) {
        char *p;
        Objid r = strtol(name + 1, &p, 10);

        if (*p != '\0' || !valid(r))
            return FAILED_MATCH;
        return r;
    }
    if (!valid(player))
        return FAILED_MATCH;
    if (!strcasecmp(name, "me"))
        return player;
    if (!strcasecmp(name, "here"))
        return db_object_location(player);
        
    int step;
    Objid oid;
    Objid loc = db_object_location(player);
    struct match_data d = {player, new_list(0), new_list(0)};
    d.player = player;
    for (oid = player, step = 0; step < 2; oid = loc, step++) {
        if (!valid(oid))
            continue;
        db_for_all_contents(oid, match_proc, &d);
    }
    
    std::vector<int> matches = complex_match(name, &d.keys);
    Objid result = AMBIGUOUS;
    if (matches.size() <= 0) {
        result = FAILED_MATCH;
    } else if (matches.size() == 1) {
        result = d.targets.v.list[matches.back()].v.obj;
    }
    
    free_var(d.keys);
    free_var(d.targets);
    return result;
}

#define NUM_ORDINALS 19
#define MAX_ORDINAL_STRINGS 4
int findOrdinalIndex(const char* str, const char* ordinals[NUM_ORDINALS][MAX_ORDINAL_STRINGS]) {
    for (int i = 0; i < NUM_ORDINALS; ++i) {
        for (int j = 0; ordinals[i][j] != NULL; ++j) {
            if (!strcasecmp(str, ordinals[i][j])) {
                return i;
            }
        }
    }
    return -1;  // Return -1 if no match is found
}

const char* ordinals[NUM_ORDINALS][MAX_ORDINAL_STRINGS] = {
    {"first", NULL},
    {"second", "twenty", "twentieth", NULL},
    {"third", "thirty", "thirtieth", NULL},
    {"fourth", "fourtieth", "fourty", NULL},
    {"fifth", "fiftieth", "fifty", NULL},
    {"sixth", "sixtieth", "sixty", NULL},
    {"seventh", "seventieth", "seventy", NULL},
    {"eighth", "eightieth", "eighty", NULL},
    {"ninth", "ninetieth", "ninty", NULL},
    {"tenth", NULL},
    {"eleventh", NULL},
    {"twelth", NULL},
    {"thirteenth", NULL},
    {"fourteenth", NULL},
    {"fifteenth", NULL},
    {"sixteenth", NULL},
    {"seventeeth", NULL},
    {"eighteenth", NULL},
    {"nineteenth", NULL}
};

int
parse_ordinal(const char* word) {
    const std::regex e ("(\\d+)(th|st|nd|rd)");

    // First order of operations is to split up the ordinal into tokens.
    Var tokens = new_list(0);
    char *found, *return_string, *freeme;
    freeme = return_string = strdup(word);
    found = strtok(return_string, "-");
    while (found != nullptr) {
        tokens = listappend(tokens, str_dup_to_var(found));
        found = strtok(nullptr, "-");
    }

    std::vector<int> ordinalTokens;
    for (int i=1;i <= tokens.v.list[0].v.num;i++) {
        // Easiest matching: 1. 2. etc.
        Var token = tokens.v.list[i];

        if (memo_strlen(token.v.str) > 1 && token.v.str[memo_strlen(token.v.str) - 1] == '.') {
            try {
                ordinalTokens.push_back(atoi(strndup(token.v.str, memo_strlen(token.v.str) - 2)));
            } catch (...) {
                free_var(tokens);
                free(freeme);
                return FAILED_MATCH;
            }
        }

        // Brute force ordinal matching
        int ordinalIndex = findOrdinalIndex(token.v.str, ordinals);
        if (ordinalIndex != -1) {
            ordinalTokens.push_back(ordinalIndex + 1);
            continue;
        }
        
        // Third order matching, try matching 1st, etc.
        std::smatch sm;
        std::string str(token.v.str);
        if (std::regex_search(str, sm, e) && sm.size() > 1) {
            try {
                ordinalTokens.push_back(std::stoi(sm.str(1)));
            } catch (...) {
                free_var(tokens);
                free(freeme);
                return FAILED_MATCH;
            }
        }
    }

    int ordinal;
    if (ordinalTokens.size() == 1) {
        free_var(tokens);
        free(freeme);
        return ordinalTokens[0];
    } else if (ordinalTokens.size() == 2) {
        ordinal = ordinalTokens[0] * 10;
        ordinal = ordinal + ordinalTokens[1];
        free_var(tokens);
        free(freeme);
        return ordinal;
    }
    free_var(tokens);
    free(freeme);
    return FAILED_MATCH;
}

template<typename T>
void pushIfNotExists(std::vector<T>& vec, const T& value) {
    if (std::find(vec.begin(), vec.end(), value) == vec.end()) {
        vec.push_back(value);
    }
}

std::vector<int>
complex_match(const char* inputSubject, Var *targets) {
    // Guard check for no targets
    if (targets->v.list[0].v.num <= 0) return {};

    Var subjectWords = new_list(0);
    char *found, *return_string, *freeme;
    freeme = return_string = strdup(inputSubject);
    found = strtok(return_string, " ");
    while (found != nullptr) {
        subjectWords = listappend(subjectWords, str_dup_to_var(found));
        found = strtok(nullptr, " ");
    }
    // Guard check for no subject
    if (subjectWords.v.list[0].v.num <= 0) {
        free_var(subjectWords);
        free(freeme);
        return {};
    }

    // Ordinal matches 
    int ordinal = parse_ordinal(subjectWords.v.list[1].v.str);
    const char* subject = str_dup(inputSubject);
    if (ordinal <= 0)  {
        // Safety in case ordinal didn't match
        ordinal = 0; 
    } else {
        subjectWords = listdelete(subjectWords, 1);
        if (subjectWords.v.list[0].v.num <= 0) return {};
        // subject = boost::algorithm::join(subjectWords, " ");
        Stream* s = new_stream(100);
        for (int i=1; i <= subjectWords.v.list[0].v.num; i++) {
            stream_add_string(s, subjectWords.v.list[i].v.str);
            if (i < subjectWords.v.list[0].v.num) stream_add_char(s, ' ');
        }
        subject = str_dup(stream_contents(s));
        free_stream(s);
    }

    // Clear up this since we don't need it anymore...
    free_var(subjectWords);
    free(freeme);
    
    std::vector<int> exactMatches, startMatches, containMatches;
    
    for(int i = 1; i <= targets->v.list[0].v.num; i++) {
        for(int i2 = 1 ; i2 <= targets->v.list[i].v.list[0].v.num; i2++) {
            const char* alias = targets->v.list[i].v.list[i2].v.str;
            
            if(!strcasecmp(subject, alias)) {
                if (ordinal > 0 && ordinal == (exactMatches.size() + 1)) {
                    return {i};
                }
                pushIfNotExists(exactMatches, i);
            } 
            
            if (strindex(alias, memo_strlen(alias), subject, memo_strlen(subject), 0) == 1) {
                pushIfNotExists(startMatches, i);
            }
            
            if (strindex(alias, memo_strlen(alias), subject, memo_strlen(subject), 0) >= 1) {
                pushIfNotExists(containMatches, i);
            }
        }
    }
    
    if (ordinal > 0) {
        if(ordinal <= exactMatches.size()) return {exactMatches[ordinal - 1]};
        if(ordinal <= startMatches.size()) return {startMatches[ordinal - 1]};
        if(ordinal <= containMatches.size()) return {containMatches[ordinal - 1]};
        return {};
    }

    if(exactMatches.size() > 0) return exactMatches;
    if(startMatches.size() > 0) return startMatches;
    if(containMatches.size() > 0) return containMatches;
    return {};
}
