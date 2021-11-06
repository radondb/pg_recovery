CREATE FUNCTION pg_recovery(regclass, recoveryrow bool DEFAULT true)
	RETURNS SETOF record
	AS 'MODULE_PATHNAME'
	LANGUAGE C;
