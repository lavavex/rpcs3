# RPCS3 as a native PS5 title: the frontend and the input handlers it uses.
add_executable(rpcs3_ps5
    main_ps5.cpp
    ps5_audio_backend.cpp
    ps5_firmware.cpp
    ps5_pad_handler.cpp
    ../Input/mouse_gyro_handler.cpp
    ../Input/pad_thread.cpp
    ../rpcs3_version.cpp
    ../Input/product_info.cpp
    ../Input/ps_move_config.cpp
    ../Input/ps_move_tracker.cpp
)

target_include_directories(rpcs3_ps5 PRIVATE ${CMAKE_SOURCE_DIR}/rpcs3)
target_link_libraries(rpcs3_ps5 PRIVATE rpcs3_emu 3rdparty::stblib 3rdparty::zlib 3rdparty::fusion
    # The console's services (stubs from the payload SDK) and the PS5 platform layer
    ps5platform ScePad SceAudioOut SceUserService SceSystemService)

set_target_properties(rpcs3_ps5 PROPERTIES OUTPUT_NAME rpcs3_ps5.elf)

# The link is the title's (rpcs3-PS5 tools/link-title.sh): RADV, the platform layer's bindings,
# the title CRT, then ps5-native-tool signs dist/<TITLE_ID>/eboot.bin
set(CMAKE_CXX_LINK_EXECUTABLE "bash ${RPCS3_PS5_ROOT}/tools/link-title.sh <TARGET> <OBJECTS> <LINK_LIBRARIES> <LINK_FLAGS>")

# libc gaps in a title, answered by the platform layer (ps5platform/libc.h):
# accept4 and getpagesizes are not exported at all; realpath is refused (EPERM)
# and getcwd resolves to nothing, so both go to the platform's versions.
target_link_options(rpcs3_ps5 PRIVATE
    -Wl,--defsym=accept4=ps5_accept4
    -Wl,--defsym=getpagesizes=ps5_getpagesizes
    -Wl,--wrap=realpath -Wl,--defsym=__wrap_realpath=ps5_realpath
    -Wl,--wrap=getcwd -Wl,--defsym=__wrap_getcwd=ps5_getcwd
)
