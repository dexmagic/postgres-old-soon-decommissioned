# $PostgreSQL$

MODULE_big = chkpass
OBJS = chkpass.o
SHLIB_LINK = $(filter -lcrypt, $(LIBS))
DATA_built = chkpass.sql
DATA = uninstall_chkpass.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/chkpass
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
