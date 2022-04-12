include_directories(${CMAKE_SOURCE_DIR}/unix/common)
include_directories(${CMAKE_SOURCE_DIR}/unix)
include_directories(${CMAKE_SOURCE_DIR}/common)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PACKAGES REQUIRED freerdp2 freerdp-server2 winpr2 xkbcommon)
find_package(utf8cpp REQUIRED)
add_executable(vnc2rdp
  vnc2rdp.cxx
  RDPServer.cxx
  RDPSession.cxx
  VNCClient.cxx
)

target_link_libraries(vnc2rdp tx rfb network rdr unixcommon)

target_link_libraries(vnc2rdp ${PACKAGES_LINK_LIBRARIES} font)
target_include_directories(vnc2rdp PRIVATE ${PACKAGES_INCLUDE_DIRS} ${utf8cpp_INCLUDE_DIRS})
target_compile_options(vnc2rdp PUBLIC ${PACKAGES_CFLAGS_OTHER})
set_property(TARGET vnc2rdp PROPERTY CXX_STANDARD 17)

install(TARGETS vnc2rdp DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
