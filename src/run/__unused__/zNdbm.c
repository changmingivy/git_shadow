#include <fcntl.h>
#include <gdbm/ndbm.h>

// DBM *
// dbm_open(const char *base,	int flags, mode_t mode);
//
// void
// dbm_close(DBM *db);
//
// int
// dbm_store(DBM *db,	datum key, datum data, int flags);
//
// datum
// dbm_fetch(DBM *db,	datum key);
//
// int
// dbm_delete(DBM *db, datum key);
//
// datum
// dbm_firstkey(DBM *db);
//
// datum
// dbm_nextkey(DBM *db);
//
// int
// dbm_error(DBM *db);
//
// int
// dbm_clearerr(DBM *db);
