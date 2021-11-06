# pg_recovery

recovery postgresql table data by update/delete/rollback/dropcolumn command.

```
https://github.com/radondb/pg_recovery
```



## init data

```
lzzhang=# create extension pg_recovery ;
CREATE EXTENSION
lzzhang=# create table lzzhang(id int, dp int);
CREATE TABLE
lzzhang=# insert into lzzhang values(1, 1);
INSERT 0 1
lzzhang=# insert into lzzhang values(2, 2);
INSERT 0 1
```



## recovery update

```
lzzhang=# update lzzhang set id=3, dp=3;
UPDATE 2
lzzhang=# select * from pg_recovery('lzzhang') as (id int, dp int);
 id | dp 
----+----
  1 |  1
  2 |  2
(2 rows)

lzzhang=# select * from lzzhang;
 id | dp 
----+----
  3 |  3
  3 |  3
(2 rows)

```



## recovery delete

```
lzzhang=# delete from lzzhang;
DELETE 2
lzzhang=# select * from lzzhang;
 id | dp 
----+----
(0 rows)

lzzhang=# select * from pg_recovery('lzzhang') as (id int, dp int);
 id | dp 
----+----
  1 |  1
  2 |  2
  3 |  3
  3 |  3
(4 rows)
```



## recovery rollback

```
lzzhang=# begin ;
BEGIN
lzzhang=# insert into lzzhang values(4, 4);
INSERT 0 1
lzzhang=# rollback ;
ROLLBACK
lzzhang=# select * from lzzhang;
 id | dp 
----+----
(0 rows)

lzzhang=# select * from pg_recovery('lzzhang') as (id int, dp int);
 id | dp 
----+----
  1 |  1
  2 |  2
  3 |  3
  3 |  3
  4 |  4
(5 rows)

```



## recovery drop column

```
lzzhang=# alter table lzzhang drop column dp;
ALTER TABLE
lzzhang=# select attnum from pg_attribute, pg_class where attrelid = pg_class.oid and pg_class.relname='lzzhang' and attname ~ 'dropped';
 attnum 
--------
      2
(1 row)

lzzhang=# select * from lzzhang;
 id 
----
(0 rows)

lzzhang=# select * from pg_recovery('lzzhang') as (id int, dropped_attnum_2 int);
 id | dropped_attnum_2 
----+------------------
  1 |                1
  2 |                2
  3 |                3
  3 |                3
  4 |                4
(5 rows)

-- dropped_attnum_2: if the drop attnum is 5, set dropped_attnum_2 to dropped_attnum_5
```



## show all data

```
lzzhang=# insert into lzzhang values(5);
INSERT 0 1
lzzhang=# select * from lzzhang;
 id 
----
  5
(1 row)

lzzhang=# select * from pg_recovery('lzzhang', recoveryrow => false) as (id int, recoveryrow bool);
 id | recoveryrow 
----+-------------
  1 | t
  2 | t
  3 | t
  3 | t
  4 | t
  5 | f
(6 rows)

```



## retention the recovery data

pg_recovery read the dead tuple from postgresql. so if the tuple is vacuum pg_recovery can't read the table data.

- vacuum_defer_cleanup_age

  retention these transactions.



## Support Version

current only support PostgreSQL-12/13/14



## Compile

```
[lzzhang@lzzhang-pc pg_recovery]$ make PG_CONFIG=/home/lzzhang/PG/postgresql/base/bin/pg_config
gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Werror=vla -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -Wno-format-truncation -Wno-stringop-truncation -g -g -O0 -fPIC -I. -I./ -I/home/lzzhang/PG/postgresql/base/include/server -I/home/lzzhang/PG/postgresql/base/include/internal  -D_GNU_SOURCE   -c -o pg_recovery.o pg_recovery.c
gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Werror=vla -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -Wno-format-truncation -Wno-stringop-truncation -g -g -O0 -fPIC -shared -o pg_recovery.so pg_recovery.o -L/home/lzzhang/PG/postgresql/base/lib    -Wl,--as-needed -Wl,-rpath,'/home/lzzhang/PG/postgresql/base/lib',--enable-new-dtags  

[lzzhang@lzzhang-pc pg_recovery]$ make install PG_CONFIG=/home/lzzhang/PG/postgresql/base/bin/pg_config
/usr/bin/mkdir -p '/home/lzzhang/PG/postgresql/base/lib'
/usr/bin/mkdir -p '/home/lzzhang/PG/postgresql/base/share/extension'
/usr/bin/mkdir -p '/home/lzzhang/PG/postgresql/base/share/extension'
/usr/bin/install -c -m 755  pg_recovery.so '/home/lzzhang/PG/postgresql/base/lib/pg_recovery.so'
/usr/bin/install -c -m 644 .//pg_recovery.control '/home/lzzhang/PG/postgresql/base/share/extension/'
/usr/bin/install -c -m 644 .//pg_recovery--1.0.sql  '/home/lzzhang/PG/postgresql/base/share/extension/'

```

