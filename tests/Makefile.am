# Copyright (C) 2010-2011 Daiki Ueno <ueno@unixuser.org>
# Copyright (C) 2010-2011 Red Hat, Inc.

# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.

# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301 USA

INCLUDES = -I$(top_srcdir) $(GIO2_CFLAGS) $(GTK_CFLAGS) $(LIBXKLAVIER_CFLAGS)

TESTS_ENVIRONMENT = EEKBOARD_KEYBOARDSDIR=$(top_srcdir)/data/keyboards

TESTS = eek-simple-test eek-xml-test
noinst_PROGRAMS = $(TESTS)

eek_simple_test_SOURCES = eek-simple-test.c
eek_simple_test_LDADD = $(top_builddir)/eek/libeek.la $(GIO2_LIBS)

eek_xml_test_SOURCES = eek-xml-test.c
eek_xml_test_LDADD = $(top_builddir)/eek/libeek.la $(top_builddir)/eek/libeek-xkl.la $(GIO2_LIBS) $(GTK_LIBS)

-include $(top_srcdir)/git.mk
