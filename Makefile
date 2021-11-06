MODULE_big = pg_recovery
OBJS = pg_recovery.o

EXTENSION = pg_recovery
DATA = pg_recovery--1.0.sql

REGRESS = recovery

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
