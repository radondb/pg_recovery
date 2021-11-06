-- create extension
CREATE EXTENSION pg_recovery;

-- check version
SELECT setting::int >= 120000 AS is_pg_12 FROM pg_settings WHERE name = 'server_version_num';

-- Create table and disable autovacuum
CREATE TABLE foo (bar bigint, baz text);
ALTER TABLE foo SET (
  autovacuum_enabled = false, toast.autovacuum_enabled = false
);

-- single row
INSERT INTO foo VALUES (1, 'Hello world');
SELECT * FROM pg_recovery('foo') as t(bar bigint, baz text);
DELETE FROM foo;
SELECT * FROM pg_recovery('foo') as t(bar bigint, baz text);
select * from foo;

VACUUM foo;

-- multiple rows
INSERT INTO foo VALUES (1, 'Delete'), (2, 'Insert'), (3, 'Update'), (4, 'Not deleted'), (5, 'Not updated');
DELETE FROM foo WHERE bar = 1;
UPDATE foo SET baz = 'Updated' WHERE bar = 3;
BEGIN;
	DELETE FROM foo WHERE bar = 4;
	UPDATE foo SET baz = 'Not quite updated' where bar = 5;
	INSERT INTO foo VALUES (6, 'Not inserted');
ROLLBACK;
SELECT * FROM foo;
SELECT * FROM pg_recovery('foo') as t(bar bigint, baz text);

-- system columns (don't show tableoid and xmin, but make sure they are numbers)
SELECT CASE WHEN tableoid >= 0 THEN 0 END AS tableoid,
	ctid,
	CASE WHEN xmin::text::int >= 0 THEN 0 END AS xmin,
	CASE WHEN xmax::text <> '0' THEN xmax::text::int - xmin::text::int END AS xmax,
	cmin, cmax, recoveryrow, bar, baz
	FROM pg_recovery('foo', recoveryrow => false)
	AS t(tableoid oid, ctid tid, xmin xid, xmax xid, cmin cid, cmax cid, recoveryrow boolean, bar bigint, baz text);

-- error cases
SELECT pg_recovery('foo');
SELECT * FROM pg_recovery(0) as t(bar bigint, baz text);
SELECT * FROM pg_recovery('foo') as t(bar int, baz text);
SELECT * FROM pg_recovery('foo') as t(moo bigint);
SELECT * FROM pg_recovery('foo') as t(tableoid bigint);
SELECT * FROM pg_recovery('foo') as t(ctid bigint);
SELECT * FROM pg_recovery('foo') as t(xmin bigint);
SELECT * FROM pg_recovery('foo') as t(xmax bigint);
SELECT * FROM pg_recovery('foo') as t(cmin bigint);
SELECT * FROM pg_recovery('foo') as t(cmax bigint);
SELECT * FROM pg_recovery('foo') as t(recoveryrow bigint);

SET ROLE luser;
SELECT * FROM pg_recovery('foo') as t(bar bigint, baz text);
RESET ROLE;

CREATE INDEX ON foo(bar);
SELECT * FROM pg_recovery('foo_bar_idx') as t(bar bigint);

-- reading from dropped columns
CREATE TABLE bar (
	id int,
	a int,
	b bigint,
	c text,
	d varchar(10),
	e boolean,
	f bigint[],
	z int
);
ALTER TABLE bar SET (
  autovacuum_enabled = false, toast.autovacuum_enabled = false
);
INSERT INTO bar VALUES (1, 2, 3, '4', '5', true, '{7}', 8);
ALTER TABLE bar DROP COLUMN a, DROP COLUMN b, DROP COLUMN c, DROP COLUMN d, DROP COLUMN e, DROP COLUMN f;
INSERT INTO bar VALUES (2, 8);
SELECT * FROM bar;
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);

-- errors
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_0 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_9 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 bigint, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 int, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
-- mismatch not catched:
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 timestamptz, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 int,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(11), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 text, dropped_attnum_7 bigint[], z int);
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 int[], z int);
-- mismatch not catched:
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 timestamptz[], z int);

-- clean table
VACUUM FULL bar;
SELECT * FROM pg_recovery('bar')
  bar(id int, dropped_attnum_2 int, dropped_attnum_3 bigint, dropped_attnum_4 text,
      dropped_attnum_5 varchar(10), dropped_attnum_6 boolean, dropped_attnum_7 bigint[], z int);
