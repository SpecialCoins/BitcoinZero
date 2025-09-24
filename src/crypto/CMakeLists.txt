# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(bitcoin_crypto STATIC EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_SOURCE_DIR}/aes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/chacha20.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hmac_sha256.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hmac_sha512.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ripemd160.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sha1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sha256.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sha512.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scrypt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Lyra2Z/Lyra2.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Lyra2Z/Lyra2Z.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Lyra2Z/Sponge.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Lyra2Z/blake.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../support/cleanse.cpp
)

target_link_libraries(bitcoin_crypto
  PRIVATE
    core_interface
    ${Boost_LIBRARIES}
)

target_include_directories(bitcoin_crypto
  PUBLIC
  ${Boost_INCLUDE_DIR}
)
