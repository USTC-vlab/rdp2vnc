include_directories(${CMAKE_SOURCE_DIR}/unix/common)
include_directories(${CMAKE_SOURCE_DIR}/unix)
include_directories(${CMAKE_SOURCE_DIR}/common)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PACKAGES REQUIRED freerdp2 freerdp-client2 winpr2 xkbcommon vterm)
find_package(utf8cpp REQUIRED)
add_custom_command(OUTPUT font.o
  COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && ld -r -b binary -o ${CMAKE_CURRENT_BINARY_DIR}/font.o font
  COMMAND objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents ${CMAKE_CURRENT_BINARY_DIR}/font.o ${CMAKE_CURRENT_BINARY_DIR}/font.o)
add_library(font STATIC font.o)
set_source_files_properties(font.o PROPERTIES EXTERNAL_OBJECT true GENERATED true)
set_target_properties(font PROPERTIES LINKER_LANGUAGE C)
add_executable(rdp2vnc
  buildtime.c
  rdp2vnc.cxx
  Geometry.cxx
  RDPPixelBuffer.cxx
  RDPDesktop.cxx
  RDPClient.cxx
  Terminal.cxx
  Greeter.cxx
)

target_link_libraries(rdp2vnc tx rfb network rdr unixcommon)

target_link_libraries(rdp2vnc ${PACKAGES_LINK_LIBRARIES} font)
target_include_directories(rdp2vnc PRIVATE ${PACKAGES_INCLUDE_DIRS} ${utf8cpp_INCLUDE_DIRS})
target_compile_options(rdp2vnc PUBLIC ${PACKAGES_CFLAGS_OTHER})
set_property(TARGET rdp2vnc PROPERTY CXX_STANDARD 17)

install(TARGETS rdp2vnc DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
