packages:=boost openssl libevent gmp zlib backtrace tor bls-dash
darwin_packages:=zeromq
linux_packages:=zeromq
native_packages :=

qt_linux_packages:=qt expat libxcb xcb_proto libXau xproto freetype fontconfig libxkbcommon libxcb_util libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm

qt_darwin_packages=qt
qt_mingw32_packages=qt

qrencode_linux_packages = qrencode
qrencode_android_packages = qrencode
qrencode_darwin_packages = qrencode
qrencode_mingw32_packages = qrencode

bdb_packages=bdb

upnp_packages=miniupnpc

zmq_packages=zeromq

darwin_native_packages=
$(host_arch)_$(host_os)_native_packages+=native_b2

