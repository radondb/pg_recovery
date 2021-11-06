#include "postgres.h"
#include "funcapi.h"
#include "utils/snapmgr.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#if PG_VERSION_NUM >= 130000
#include "common/hashfn.h"
#include "access/attnum.h"
#endif
#include "access/heapam.h"
#include "access/table.h"
#include "access/sysattr.h"
#include "access/tupconvert.h"
#include "access/htup_details.h"
#include "access/xlog.h" /* RecoveryInProgress */
#include "storage/procarray.h" /* GetOldestXmin */

#define DeadFakeAttributeNumber FirstLowInvalidHeapAttributeNumber

PG_MODULE_MAGIC;

typedef struct
{
    Relation            rel;
    TupleDesc           reltupledesc;
    TupleConversionMap  *map;
    TableScanDesc       scan;
	HTAB				*active_ctid;
	bool				droppedcolumn;
} pg_recovery_ctx;

static const struct system_columns_t {
	char	   *attname;
	Oid			atttypid;
	int32		atttypmod;
	int			attnum;
} system_columns[] = {
	{ "ctid",     TIDOID,  -1, SelfItemPointerAttributeNumber },
	{ "xmin",     XIDOID,  -1, MinTransactionIdAttributeNumber },
	{ "cmin",     CIDOID,  -1, MinCommandIdAttributeNumber },
	{ "xmax",     XIDOID,  -1, MaxTransactionIdAttributeNumber },
	{ "cmax",     CIDOID,  -1, MaxCommandIdAttributeNumber },
	{ "tableoid", OIDOID,  -1, TableOidAttributeNumber },
	{ "recoveryrow",     BOOLOID, -1, DeadFakeAttributeNumber }, /* fake column to return HeapTupleIsSurelyDead */
	{ 0 },
};

#if PG_VERSION_NUM >= 130000
AttrMap *
#else
AttrNumber *
#endif
recovery_convert_tuples_by_name_map(TupleDesc indesc, TupleDesc outdesc, const char *msg, bool *droppedcolumn);

TupleConversionMap *
recovery_convert_tuples_by_name(TupleDesc indesc, TupleDesc outdesc, const char *msg, bool *droppedcolumn);

HeapTuple
recovery_do_convert_tuple(HeapTuple tuple, TupleConversionMap *map, bool alive);

PG_FUNCTION_INFO_V1(pg_recovery);
Datum pg_recovery(PG_FUNCTION_ARGS);

Datum
pg_recovery(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    pg_recovery_ctx	*usr_ctx;
    HeapTuple           tuplein;
	bool				recoveryrow;

	recoveryrow = PG_GETARG_BOOL(1);

    if (SRF_IS_FIRSTCALL())
    {
		Oid                 relid;
        TupleDesc           tupdesc;
        MemoryContext       oldcontext;

		relid = PG_GETARG_OID(0);
		if (!OidIsValid(relid))
			elog(ERROR, "invalid relation oid \"%d\"", relid);

        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
        usr_ctx = (pg_recovery_ctx *) palloc0(sizeof(pg_recovery_ctx));
#if PG_VERSION_NUM >= 130000
        usr_ctx->rel = table_open(relid, AccessShareLock);
#else
        usr_ctx->rel = heap_open(relid, AccessShareLock);
#endif
        usr_ctx->reltupledesc = RelationGetDescr(usr_ctx->rel);
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context "
                         "that cannot accept type record")));
        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
		usr_ctx->map = recovery_convert_tuples_by_name(usr_ctx->reltupledesc,
				funcctx->tuple_desc, "Error converting tuple descriptors!", &usr_ctx->droppedcolumn);
		usr_ctx->active_ctid = NULL;
        usr_ctx->scan = heap_beginscan(usr_ctx->rel, SnapshotAny, 0, NULL , NULL, 0);

		/* init active ctid hash */
		if (recoveryrow == true || usr_ctx->map != NULL)
		{
			HASHCTL			hash_ctl;
			TableScanDesc	active_scan;

			memset(&hash_ctl, 0, sizeof(hash_ctl));
			hash_ctl.keysize = sizeof(ItemPointerData);
			hash_ctl.entrysize = sizeof(ItemPointerData);
			hash_ctl.hash = tag_hash;
			hash_ctl.hcxt = funcctx->multi_call_memory_ctx;;

			/* 2 is init length. This will auto increment */
			usr_ctx->active_ctid = hash_create("active ctid hash", 2, &hash_ctl, HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

			active_scan = heap_beginscan(usr_ctx->rel, GetActiveSnapshot(), 0, NULL , NULL, 0);
    		while ((tuplein = heap_getnext(active_scan, ForwardScanDirection)) != NULL)
				hash_search(usr_ctx->active_ctid, (void*)&tuplein->t_self, HASH_ENTER, NULL);

			heap_endscan(active_scan);
		}

        funcctx->user_fctx = (void *) usr_ctx;
        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    usr_ctx = (pg_recovery_ctx *) funcctx->user_fctx;

get_tuple:
    if ((tuplein = heap_getnext(usr_ctx->scan, ForwardScanDirection)) != NULL)
    {
		bool alive = false;

		if (recoveryrow == true || usr_ctx->map != NULL)
		{
			MemoryContext       oldcontext;

			oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
			hash_search(usr_ctx->active_ctid, (void*)&tuplein->t_self, HASH_FIND, &alive);
			MemoryContextSwitchTo(oldcontext);
		}

		if (usr_ctx->droppedcolumn == false && recoveryrow == true && alive == true)
			goto get_tuple;

        if (usr_ctx->map != NULL)
        {
            tuplein = recovery_do_convert_tuple(tuplein, usr_ctx->map, alive);
            SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuplein));
        }
        else
            SRF_RETURN_NEXT(funcctx, heap_copy_tuple_as_datum(tuplein, usr_ctx->reltupledesc));
    }
    else
    {
        heap_endscan(usr_ctx->scan);
#if PG_VERSION_NUM >= 130000
        table_close(usr_ctx->rel, AccessShareLock);
#else
        heap_close(usr_ctx->rel, AccessShareLock);
#endif
        SRF_RETURN_DONE(funcctx);
    }
}

/*
 * Set up for tuple conversion, matching input and output columns by name.
 * (Dropped columns are ignored in both input and output.)	This is intended
 * for use when the rowtypes are related by inheritance, so we expect an exact
 * match of both type and typmod.  The error messages will be a bit unhelpful
 * unless both rowtypes are named composite types.
 */
TupleConversionMap *
recovery_convert_tuples_by_name(TupleDesc indesc,
					   TupleDesc outdesc,
					   const char *msg, bool *droppedcolumn)
{
	TupleConversionMap *map;
#if PG_VERSION_NUM >= 130000
	AttrMap *attrMap;
#else
	AttrNumber *attrMap;
#endif
	int			n = outdesc->natts;
	int			i;
	bool		same;

	/* Verify compatibility and prepare attribute-number map */
	attrMap = recovery_convert_tuples_by_name_map(indesc, outdesc, msg, droppedcolumn);

	/*
	 * Check to see if the map is one-to-one, in which case we need not do a
	 * tuple conversion.  We must also insist that both tupdescs either
	 * specify or don't specify an OID column, else we need a conversion to
	 * add/remove space for that.  (For some callers, presence or absence of
	 * an OID column perhaps would not really matter, but let's be safe.)
	 */
	if (indesc->natts == outdesc->natts)
	{
		same = true;
		for (i = 0; i < n; i++)
		{
			Form_pg_attribute inatt;
			Form_pg_attribute outatt;

#if PG_VERSION_NUM >= 130000
			if (attrMap->attnums[i] == (i + 1))
#else
			if (attrMap[i] == (i + 1))
#endif
				continue;

			/*
			 * If it's a dropped column and the corresponding input column is
			 * also dropped, we needn't convert.  However, attlen and attalign
			 * must agree.
			 */
			inatt = TupleDescAttr(indesc, i);
			outatt = TupleDescAttr(outdesc, i);
#if PG_VERSION_NUM >= 130000
			if (attrMap->attnums[i] == 0 &&
#else
			if (attrMap[i] == 0 &&
#endif
				inatt->attisdropped &&
				inatt->attlen == outatt->attlen &&
				inatt->attalign == outatt->attalign)
				continue;

			same = false;
			break;
		}
	}
	else
		same = false;

	if (same)
	{
		/* Runtime conversion is not needed */
		elog(DEBUG1, "tuple conversion is not needed");
		pfree(attrMap);
		return NULL;
	}

	/* Prepare the map structure */
	map = (TupleConversionMap *) palloc(sizeof(TupleConversionMap));
	map->indesc = indesc;
	map->outdesc = outdesc;
	map->attrMap = attrMap;
	/* preallocate workspace for Datum arrays */
	map->outvalues = (Datum *) palloc(n * sizeof(Datum));
	map->outisnull = (bool *) palloc(n * sizeof(bool));
	n = indesc->natts + 1;		/* +1 for NULL */
	map->invalues = (Datum *) palloc(n * sizeof(Datum));
	map->inisnull = (bool *) palloc(n * sizeof(bool));
	map->invalues[0] = (Datum) 0;	/* set up the NULL entry */
	map->inisnull[0] = true;

	return map;
}

/*
 * Return a palloc'd bare attribute map for tuple conversion, matching input
 * and output columns by name.  (Dropped columns are ignored in both input and
 * output.)  This is normally a subroutine for convert_tuples_by_name, but can
 * be used standalone.
 *
 * This version from recovery_tupconvert.c adds the ability to retrieve dropped
 * columns by requesting "dropped_attnum_N" as output column, where N is the attnum.
 */
#if PG_VERSION_NUM >= 130000
AttrMap *
#else
AttrNumber *
#endif
recovery_convert_tuples_by_name_map(TupleDesc indesc,
						   TupleDesc outdesc,
						   const char *msg, bool *droppedcolumn)
{
#if PG_VERSION_NUM >= 130000
	AttrMap *attrMap;
#else
	AttrNumber *attrMap;
#endif
	int			n;
	int			i;

	*droppedcolumn = false;
	n = outdesc->natts;
#if PG_VERSION_NUM >= 130000
	attrMap = make_attrmap(n);
#else
	attrMap = (AttrNumber *) palloc0(n * sizeof(AttrNumber));
#endif
	for (i = 0; i < n; i++)
	{
		Form_pg_attribute outatt = TupleDescAttr(outdesc, i);
		char	   *attname;
		Oid			atttypid;
		int32		atttypmod;
		int			j;

		if (outatt->attisdropped)
			continue;			/* attrMap[i] is already 0 */
		attname = NameStr(outatt->attname);
		atttypid = outatt->atttypid;
		atttypmod = outatt->atttypmod;
		for (j = 0; j < indesc->natts; j++)
		{
			Form_pg_attribute inatt = TupleDescAttr(indesc, j);

			if (inatt->attisdropped)
				continue;
			if (strcmp(attname, NameStr(inatt->attname)) == 0)
			{
				/* Found it, check type */
				if (atttypid != inatt->atttypid || atttypmod != inatt->atttypmod)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Attribute \"%s\" has type %s in corresponding attribute of type %s.",
									   attname,
									   format_type_with_typemod(inatt->atttypid, inatt->atttypmod),
									   format_type_be(indesc->tdtypeid))));
#if PG_VERSION_NUM >= 130000
				attrMap->attnums[i] = (AttrNumber) (j + 1);
#else
				attrMap[i] = (AttrNumber) (j + 1);
#endif
				break;
			}
		}

		/* Check dropped columns */
#if PG_VERSION_NUM >= 130000
		if (attrMap->attnums[i] == 0)
#else
		if (attrMap[i] == 0)
#endif
			if (strncmp(attname, "dropped_attnum_", sizeof("dropped_attnum_") - 1) == 0)
			{
				Form_pg_attribute inatt;
				*droppedcolumn = true;
				j = atoi(attname + sizeof("dropped_attnum_") - 1);
				if (j < 1 || j > indesc->natts)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Attribute \"%s\" index is out of range 1 .. %d.",
									 attname, indesc->natts)));
				inatt = TupleDescAttr(indesc, j - 1);
				if (! inatt->attisdropped)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Attribute %d is not a dropped column.", j)));

				if (outatt->attlen != inatt->attlen)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Type length of dropped column \"%s\" was %d.",
									   attname, inatt->attlen)));
				if (outatt->attbyval != inatt->attbyval)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("\"By value\" of dropped column \"%s\" does not match.",
									   attname)));
				if (outatt->attalign != inatt->attalign)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Alignment of dropped column \"%s\" was %c.",
									   attname, inatt->attalign)));

				inatt->atttypid = atttypid;
				if (atttypmod != inatt->atttypmod)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Type modifier of dropped column \"%s\" was %s.",
									   attname,
									   format_type_with_typemod(inatt->atttypid, inatt->atttypmod))));
#if PG_VERSION_NUM >= 130000
				attrMap->attnums[i] = (AttrNumber) j;
#else
				attrMap[i] = (AttrNumber) j;
#endif
			}

		/* Check system columns */
#if PG_VERSION_NUM >= 130000
		if (attrMap->attnums[i] == 0)
#else
		if (attrMap[i] == 0)
#endif
			for (j = 0; system_columns[j].attname; j++)
				if (strcmp(attname, system_columns[j].attname) == 0)
				{
					/* Found it, check type */
					if (atttypid != system_columns[j].atttypid || atttypmod != system_columns[j].atttypmod)
						ereport(ERROR,
								(errcode(ERRCODE_DATATYPE_MISMATCH),
								 errmsg_internal("%s", _(msg)),
								 errdetail("Attribute \"%s\" has type %s in corresponding attribute of type %s.",
										   attname,
										   format_type_be(system_columns[j].atttypid),
										   format_type_be(indesc->tdtypeid))));
					/* GetOldestXmin() is not available during recovery */
					if (system_columns[j].attnum == DeadFakeAttributeNumber &&
							RecoveryInProgress())
						ereport(ERROR,
								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								 errmsg("Cannot use \"dead\" column during recovery")));
#if PG_VERSION_NUM >= 130000
					attrMap->attnums[i] = system_columns[j].attnum;
#else
					attrMap[i] = system_columns[j].attnum;
#endif
					break;
				}

#if PG_VERSION_NUM >= 130000
		if (attrMap->attnums[i] == 0)
#else
		if (attrMap[i] == 0)
#endif
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg_internal("%s", _(msg)),
					 errdetail("Attribute \"%s\" does not exist in type %s.",
							   attname,
							   format_type_be(indesc->tdtypeid))));
	}

	return attrMap;
}

/*
 * Perform conversion of a tuple according to the map.
 */
HeapTuple
recovery_do_convert_tuple(HeapTuple tuple, TupleConversionMap *map, bool alive)
{
#if PG_VERSION_NUM >= 130000
	AttrMap *attrMap = map->attrMap;
#else
	AttrNumber *attrMap = map->attrMap;
#endif
	Datum	   *invalues = map->invalues;
	bool	   *inisnull = map->inisnull;
	Datum	   *outvalues = map->outvalues;
	bool	   *outisnull = map->outisnull;
	int			outnatts = map->outdesc->natts;
	int			i;

	/*
	 * Extract all the values of the old tuple, offsetting the arrays so that
	 * invalues[0] is left NULL and invalues[1] is the first source attribute;
	 * this exactly matches the numbering convention in attrMap.
	 */
	heap_deform_tuple(tuple, map->indesc, invalues + 1, inisnull + 1);

	/*
	 * Transpose into proper fields of the new tuple.
	 */
	for (i = 0; i < outnatts; i++)
	{
#if PG_VERSION_NUM >= 130000
		int			j = attrMap->attnums[i];
#else
		int			j = attrMap[i];
#endif

		if (j == DeadFakeAttributeNumber)
		{
			outvalues[i] = !alive;
			outisnull[i] = false;
		}
		else if (j < 0)
			outvalues[i] = heap_getsysattr(tuple, j, map->indesc, &outisnull[i]);
		else
		{
			outvalues[i] = invalues[j];
			outisnull[i] = inisnull[j];
		}
	}

	/*
	 * Now form the new tuple.
	 */
	return heap_form_tuple(map->outdesc, outvalues, outisnull);
}
