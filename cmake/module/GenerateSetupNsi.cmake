# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  set(PACKAGE_URL ${PROJECT_HOMEPAGE_URL})
  set(CLIENT_TARNAME "BZX")
  set(BITCOIN_GUI_NAME "bitcoinzero-qt")
  set(BITCOIN_DAEMON_NAME "bitcoinzerod")
  set(BITCOIN_CLI_NAME "bitcoinzero-cli")
  set(BITCOIN_TX_NAME "bitcoinzero-tx")
  set(BITCOIN_WALLET_TOOL_NAME "bitcoinzero-wallet")
  set(BITCOIN_TEST_NAME "test_bitcoinzero")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  # Set executable directory based on build system
  # CMake/Guix builds put executables in bin/, autotools in release/
  set(EXECUTABLE_DIR "bin")
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/bitcoinzero-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
