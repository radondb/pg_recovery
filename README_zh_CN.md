# pg_recovery

PostgreSQL 数据找回工具 `pg_recovery` 可以找回 `commit` / `delete` / `rollback` / `drop column` 操作后的数据. 并以表的形式返回,可以方便的各种查询,帮助寻找需要的数据.

```
https://github.com/radondb/pg_recovery
```


## 初始化数据

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



## 找回Update数据

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



## 找回Delete数据

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



## 找回Rollback数据

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



## 找回删除的列数据

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



## 显示全部数据

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



## 保留恢复数据

`pg_recovery` 通过读取dead元组来恢复不可见的表数据,所有如果元组被vacuum清除掉.那么 `pg_recovery` 便不能恢复出数据.

- vacuum_defer_cleanup_age

  设置保留恢复最近多少个事务的数据.



## 支持的版本

当前仅支持 `12/13/14` 版本



## 编译

> 根据自己的环境,设置PG_CONFIG

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

