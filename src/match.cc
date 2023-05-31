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

#include "config.h"
#include "db.h"
#include "structures.h"
#include "match.h"
#include "parse_cmd.h"
#include "storage.h"
#include "unparse.h"
#include "utils.h"

Var *
aliases(Objid oid)
{
    Var value;
    db_prop_handle h;

    h = db_find_property(Var::new_obj(oid), "aliases", &value);
    if (!h.ptr || value.type != TYPE_LIST) {
        /* Simulate a pointer to an empty list */
        return &zero;
    } else
        return value.v.list;
}

struct match_data {
    int lname;
    const char *name;
    Objid exact, partial;
};

static int
match_proc(void *data, Objid oid)
{
    struct match_data *d = (struct match_data *)data;
    Var *names = aliases(oid);
    int i;
    const char *name;

    for (i = 0; i <= names[0].v.num; i++) {
        if (i == 0)
            name = db_object_name(oid);
        else if (names[i].type != TYPE_STR)
            continue;
        else
            name = names[i].v.str;

        if (!strncasecmp(name, d->name, d->lname)) {
            if (name[d->lname] == '\0') {   /* exact match */
                if (d->exact == NOTHING || d->exact == oid)
                    d->exact = oid;
                else
                    return 1;
            } else {        /* partial match */
                if (d->partial == FAILED_MATCH || d->partial == oid)
                    d->partial = oid;
                else
                    d->partial = AMBIGUOUS;
            }
        }
    }

    return 0;
}

static Objid
match_contents(Objid player, const char *name)
{
    Objid loc;
    int step;
    Objid oid;
    struct match_data d;

    d.lname = strlen(name);
    d.name = name;
    d.exact = NOTHING;
    d.partial = FAILED_MATCH;

    if (!valid(player))
        return FAILED_MATCH;
    loc = db_object_location(player);

    for (oid = player, step = 0; step < 2; oid = loc, step++) {
        if (!valid(oid))
            continue;
        if (db_for_all_contents(oid, match_proc, &d))
            /* We only abort the enumeration for exact ambiguous matches... */
            return AMBIGUOUS;
    }

    if (d.exact != NOTHING)
        return d.exact;
    else
        return d.partial;
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
    return match_contents(player, name);
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
