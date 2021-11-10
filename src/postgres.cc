#include "options.h"

#ifdef PQXX_FOUND

#include <unordered_map>

#include <pqxx/pqxx>
#include "background.h"
#include "functions.h"
#include "numbers.h"
#include "utils.h"
#include "list.h"
#include "storage.h"
#include "log.h"
#include "server.h"
#include "map.h"

Var 
col2var(pqxx::field col) 
{
    Var val;
    switch (col.type()) {
        case 16:
            val.type = TYPE_BOOL;
            val.v.truth = col.as<bool>();
            break;
        case 20:
        case 23:
            val.type = TYPE_INT;
            val.v.num = col.as<int>();
            break;
        case 25:
        case 1043:
            val = str_dup_to_var(col.c_str());
            break;
        case 701:
            val.type = TYPE_FLOAT;
            val.v.fnum = col.as<float>();
            break;
    }
    return val;
}

Var
result2var(pqxx::result res)
{
    Var ret = new_list(0);
    for (auto row: res) {
        Var rv = new_list(0);
        for (auto col: row) {
            rv = listappend(rv, col2var(col));
        }
        ret = listappend(ret, rv);
    }
    return ret;
}

static package 
bf_sql_query(Var arglist, Byte next, void *vdata, Objid progr) 
{
    pqxx::connection c{"postgresql://moo@localhost/moo"};
    pqxx::work txn{c};

    pqxx::result res{txn.exec(arglist.v.list[1].v.str)};
    Var ret = result2var(res);

    txn.commit();

    free_var(arglist);
    return make_var_pack(ret);
}

static package 
bf_sql_execute(Var arglist, Byte next, void *vdata, Objid progr) 
{

}

void register_postgres(void) 
{
    oklog("REGISTER_POSTGRESS: Using PQXX Library\n");

    register_function("sql_query", 1, 2, bf_sql_query, TYPE_STR, TYPE_LIST);
    register_function("sql_execute", 1, 2, bf_sql_execute, TYPE_STR, TYPE_LIST);
}
#else /* PQXX_FOUND */
void register_postgres(void) { }
#endif