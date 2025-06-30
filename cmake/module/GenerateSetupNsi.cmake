# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  set(CLIENT_TARNAME "BZX")
  set(BITCOIN_GUI_NAME "BZX-qt")
  set(BITCOIN_DAEMON_NAME "BZXd")
  set(BITCOIN_CLI_NAME "BZX-cli")
  set(BITCOIN_TX_NAME "BZX-tx")
  set(BITCOIN_WALLET_TOOL_NAME "BZX-wallet")
  set(BITCOIN_TEST_NAME "test_BZX")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/BZX-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
