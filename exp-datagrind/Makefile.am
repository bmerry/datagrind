include $(top_srcdir)/Makefile.tool.am

EXTRA_DIST = docs/dg-manual.xml

#----------------------------------------------------------------------------
# headers
#----------------------------------------------------------------------------

pkginclude_HEADERS = datagrind.h

noinst_HEADERS = dg_record.h dg_view_range.h dg_view_debuginfo.h \
                 dg_view_parse.h dg_view_pool.h dg_view.h

#----------------------------------------------------------------------------
# dg_view
#----------------------------------------------------------------------------

bin_PROGRAMS = dg_view

dg_view_SOURCES = dg_view.cpp dg_view_debuginfo.cpp dg_view_parse.cpp
dg_view_CPPFLAGS = $(AM_CPPFLAGS_PRI)
dg_view_CFLAGS = $(AM_CFLAGS_PRI)
dg_view_CCASFLAGS = $(AM_CCASFLAGS_PRI)
dg_view_LDFLAGS = $(AM_LDFLAGS_PRI)
dg_view_LDADD = -lglut -lGLEW -lGL -lbfd

#----------------------------------------------------------------------------
# exp-datagrind-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = exp-datagrind-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += exp-datagrind-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

NONE_SOURCES_COMMON = dg_main.c

exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = $(NONE_SOURCES_COMMON)
exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
if VGCONF_HAVE_PLATFORM_SEC
exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = $(NONE_SOURCES_COMMON)
exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
endif

#----------------------------------------------------------------------------
# vgpreload_exp-datagrind-<platform>.so
#----------------------------------------------------------------------------

noinst_PROGRAMS += vgpreload_exp-datagrind-@VGCONF_ARCH_PRI@-@VGCONF_OS@.so
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += vgpreload_exp-datagrind-@VGCONF_ARCH_SEC@-@VGCONF_OS@.so
endif

if VGCONF_OS_IS_DARWIN
noinst_DSYMS = $(noinst_PROGRAMS)
endif

vgpreload_exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_SOURCES      = 
vgpreload_exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
vgpreload_exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) $(AM_CFLAGS_PIC)
vgpreload_exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_DEPENDENCIES = \
	$(LIBREPLACEMALLOC_@VGCONF_PLATFORM_PRI_CAPS@)
vgpreload_exp_datagrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) \
	$(LIBREPLACEMALLOC_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
if VGCONF_HAVE_PLATFORM_SEC
vgpreload_exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_SOURCES      = 
vgpreload_exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
vgpreload_exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@) $(AM_CFLAGS_PIC)
vgpreload_exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_DEPENDENCIES = \
	$(LIBREPLACEMALLOC_@VGCONF_PLATFORM_SEC_CAPS@)
vgpreload_exp_datagrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@) \
	$(LIBREPLACEMALLOC_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
endif
