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
#include <boost/algorithm/string.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/range/adaptors.hpp>

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

std::vector<std::string>
var_to_vector(Var v) 
{
    std::vector<std::string> results;
    switch (v.type) {
        case TYPE_LIST:
            for (int i=1; i <= v.v.list[0].v.num;i++) {
                if (v.v.list[i].type != TYPE_STR) continue;
                results.push_back(std::string(v.v.list[i].v.str));
            }
            break;
        case TYPE_STR:
            results.push_back(std::string(v.v.str));
            break;
    }
    return results;
}

std::vector<std::string>
name_and_aliases(Objid player, Objid oid)
{
    std::vector<std::string> results;
    Var sysobj = Var::new_obj(SYSTEM_OBJECT);
    Var args = new_list(1);
    args.v.list[1] = Var::new_obj(oid);

    Var name;
    if (run_server_task(player, sysobj, "_name_of", var_dup(args), "", &name) == OUTCOME_DONE && name.type == TYPE_STR) {        
        std::vector<std::string> unionVector;
        boost::range::set_union(results, var_to_vector(name), std::back_inserter(unionVector));
        results = unionVector;
        free_var(name);
    } else {
        results.push_back(db_object_name(oid));
    }

    Var aliases;
    if (run_server_task(player, sysobj, "_aliases_of", var_dup(args), "", &aliases) == OUTCOME_DONE && aliases.type == TYPE_LIST) {
        std::vector<std::string> unionVector;
        boost::range::set_union(results, var_to_vector(aliases), std::back_inserter(unionVector));
        results = unionVector;
    } else {
        db_prop_handle h;
        h = db_find_property(Var::new_obj(oid), "aliases", &aliases);
        if (!h.ptr || aliases.type != TYPE_LIST) {
            // Do nothing it was an empty list.
        } else {
            std::vector<std::string> unionVector;
            boost::range::set_union(results, var_to_vector(aliases), std::back_inserter(unionVector));
            results = unionVector;
        }
    }

    return results;
}

struct match_data {
    const char *name;
    Objid player;
    std::vector<Objid> targets;
    std::vector<std::vector<std::string>> keys;
};

static int
match_proc(void *data, Objid oid)
{
    struct match_data *d = (struct match_data *)data;
    std::vector<std::string> keys = name_and_aliases(d->player, oid);

    d->targets.push_back(oid);
    d->keys.push_back(keys);

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
    struct match_data d;
    d.player = player;
    for (oid = player, step = 0; step < 2; oid = loc, step++) {
        if (!valid(oid))
            continue;
        db_for_all_contents(oid, match_proc, &d);
    }

    const std::vector<int> matches = complex_match(name, d.keys);
    if (matches.size() <= 0)
      return FAILED_MATCH;
    if (matches.size() == 1)
      return d.targets[matches.back()];
    return AMBIGUOUS;
}

int findOrdinalIndex(const std::string& str, const std::vector<std::vector<std::string>>& ordinals) {
    for (size_t i = 0; i < ordinals.size(); ++i) {
        for (const auto& ordinal : ordinals[i]) {
            if (boost::algorithm::iequals(str, ordinal)) {
                return i;
            }
        }
    }
    return -1;  // Return -1 if no match is found
}

int
parse_ordinal(std::string word) {
    const std::regex e ("(\\d+)(th|st|nd|rd)");
    const std::vector<std::vector<std::string>> ordinals = {
        {"first"},
        {"second", "twenty", "twentieth"},
        {"third", "thirty", "thirtieth"},
        {"fourth", "fourtieth", "fourty"},
        {"fifth", "fiftieth", "fifty"},
        {"sixth", "sixtieth", "sixty"},
        {"seventh", "seventieth", "seventy"},
        {"eighth", "eightieth", "eighty"},
        {"ninth", "ninetieth", "ninty"},
        {"tenth"},
        {"eleventh"},
        {"twelth"},
        {"thirteenth"},
        {"fourteenth"},
        {"fifteenth"},
        {"sixteenth"},
        {"seventeeth"},
        {"eighteenth"},
        {"nineteenth"}
    };
    std::vector<std::string> tokens;
    word = boost::algorithm::to_lower_copy(word);
    boost::split(tokens, word, boost::is_any_of("-"));
    if (tokens.size() > 2) return FAILED_MATCH; // Error, no idea what this is.
    std::vector<int> ordinalTokens;
    for (const std::string &token : tokens) {
        // Easiest matching: 1. 2. etc.
        if (token.back() == '.') {
            try {
                ordinalTokens.push_back(std::stoi(token.substr(0, token.size() - 1)));
            } catch (...) {
                return FAILED_MATCH;
            }
        }

        // Brute force ordinal matching
        int ordinalIndex = findOrdinalIndex(token, ordinals);
        if (ordinalIndex != -1) {
            ordinalTokens.push_back(ordinalIndex + 1);
            continue;
        }
        
        // Third order matching, try matching 1st, etc.
        std::smatch sm;
        if (std::regex_search(token, sm, e) && sm.size() > 1) {
            try {
                ordinalTokens.push_back(std::stoi(sm.str(1)));
            } catch (...) {
                return FAILED_MATCH;
            }
        }
    }

    int ordinal;
    if (ordinalTokens.size() == 1)
        return ordinalTokens[0];
    else if (ordinalTokens.size() == 2) {
        ordinal = ordinalTokens[0] * 10;
        ordinal = ordinal + ordinalTokens[1];
        return ordinal;
    }
    return FAILED_MATCH;
}

#include "log.h"

std::vector<int>
complex_match(std::string subject, std::vector<std::vector<std::string>> targets) {
    std::vector<std::string> subjectWords;
    boost::split(subjectWords, subject, boost::is_any_of(" "));
    
    // Guard check for no subject
    if (subjectWords.size() <= 0) return {};
    // Guard check for no targets
    if (targets.size() <= 0) return {};

    // Ordinal matches 
    int ordinal = parse_ordinal(subjectWords[0]);
    if (ordinal <= 0) ordinal = 0; // Safety in case ordinal didn't match
    else {
        subjectWords.erase(subjectWords.begin());
        if (subjectWords.size() <= 0) return {};
        subject = boost::algorithm::join(subjectWords, " ");
    }
    
    std::vector<int> exactMatches, startMatches, containMatches;
    
    for(int i = 0; i < targets.size(); i++) {
        std::string lowerSubject = boost::algorithm::to_lower_copy(subject);
        for (const std::string& alias : targets[i]) {
            std::string lowerAlias = boost::algorithm::to_lower_copy(alias);
            
            bool found_match = false;
            if(lowerSubject == lowerAlias) {
                if (ordinal > 0 && ordinal == (exactMatches.size() + 1)) {
                    return {i};
                }
                exactMatches.push_back(i);
                found_match = true;
            } 
            
            if(boost::algorithm::starts_with(lowerAlias, lowerSubject)) {
                startMatches.push_back(i);
                found_match = true;
            }
            
            if(boost::algorithm::contains(lowerAlias, lowerSubject)) {
                containMatches.push_back(i);
                found_match = true;
            }

            if (found_match) break;
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
