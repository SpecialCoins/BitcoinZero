# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

ACLOCAL_AMFLAGS = -I build-aux/m4
SUBDIRS = src
.PHONY: deploy FORCE

GZIP_ENV="-9n"
export PYTHONPATH

dist_noinst_SCRIPTS = autogen.sh

CLEANFILES = $(OSX_DMG) $(BITCOIN_WIN_INSTALLER)
