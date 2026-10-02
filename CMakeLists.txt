cmake_minimum_required(VERSION 3.20)
project(vitals LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- hardening flags (see skill notes on secure builds) ---
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE)
    add_link_options(-pie)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_link_options(-Wl,-z,relro,-z,now)
    endif()
endif()

include(FetchContent)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.17 # pin: master/v1.0 moved to a new ImPlotSpec-based styling API
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(implot)

find_package(OpenGL REQUIRED)

# --- system tray icon (optional) ---------------------------------------
# zserge/tray (github.com/zserge/tray): a single header, all `static`
# functions — one #include is the whole library, no extra .c to compile.
# Its Linux backend needs GTK3 + an AppIndicator implementation (neither
# bundled by FetchContent — real system libraries, found via pkg-config).
# When they're not present, tray.h's own unconditional #else branch (see
# the header) already compiles a no-op stub (tray_init() returns -1) — so
# nothing here needs its own #ifdef gating: tray_icon.cpp always compiles,
# and the feature is live or inert purely based on whether TRAY_APPINDICATOR
# got defined below.
FetchContent_Declare(tray
    GIT_REPOSITORY https://github.com/zserge/tray.git
    GIT_TAG 8dd1358b92562faf7c032cf5362fa97cbc7e13e9 # pin: no tagged releases as of writing
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(tray)

set(VITALS_HAVE_TRAY FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK3 QUIET gtk+-3.0)
        pkg_check_modules(APPINDICATOR QUIET ayatana-appindicator3-0.1)
        if(APPINDICATOR_FOUND)
            # tray.h hardcodes <libappindicator/app-indicator.h>, but Ayatana (the
            # only variant on current Debian/Ubuntu) ships the header under
            # libayatana-appindicator/. Forward one to the other via a generated shim.
            set(VITALS_TRAY_SHIM_DIR ${CMAKE_BINARY_DIR}/tray_shim)
            file(WRITE ${VITALS_TRAY_SHIM_DIR}/libappindicator/app-indicator.h
                 "#pragma once\n#include <libayatana-appindicator/app-indicator.h>\n")
        else()
            pkg_check_modules(APPINDICATOR QUIET appindicator3-0.1)
        endif()
        if(GTK3_FOUND AND APPINDICATOR_FOUND)
            set(VITALS_HAVE_TRAY TRUE)
        endif()
    endif()
endif()

add_executable(vitals
    src/main.cpp
    src/app_shell.cpp
    src/app_icon.cpp
    src/settings_persistence.cpp
    src/logging.cpp
    src/tray_icon.cpp
    src/theme.cpp
    src/widgets.cpp
    src/metrics.cpp
    src/benchmark_controller.cpp
    src/metrics/proc_util.cpp
    src/metrics/thermal_sensors.cpp
    src/metrics/gpu_sysfs.cpp
    src/metrics/system_identity.cpp
    src/metrics/disk_volumes.cpp
    src/metrics/system_users.cpp
    src/metrics/services.cpp
    src/metrics/desktop_apps.cpp
    src/metrics/process_detail.cpp
    src/dram_oc/dram_oc.cpp
    src/dram_oc/daemon_ipc.cpp
    src/gpu_ctl/gpu_ctl.cpp
    src/gpu_ctl/gpu_ctl_ipc.cpp
    src/pages/page_common.cpp
    src/pages/summary/summary.cpp
    src/pages/performance/performance.cpp
    src/pages/performance/shared.cpp
    src/pages/performance/cpu/cpu.cpp
    src/pages/performance/memory/memory.cpp
    src/pages/performance/disks/disks.cpp
    src/pages/performance/network/network.cpp
    src/pages/performance/energy/energy.cpp
    src/pages/performance/thermals/thermals.cpp
    src/pages/performance/gpu/gpu.cpp
    src/pages/processes/processes.cpp
    src/pages/system_info/system_info.cpp
    src/pages/startup_apps/startup_apps.cpp
    src/pages/users/users.cpp
    src/pages/services/services.cpp
    src/pages/benchmarks/benchmarks.cpp
    src/pages/installed_apps/installed_apps.cpp
    src/pages/disk_space/disk_space.cpp
    src/pages/settings/settings.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

find_package(Threads REQUIRED)

target_include_directories(vitals PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
    ${tray_SOURCE_DIR}
    src
    include
)

if(VITALS_HAVE_TRAY)
    target_compile_definitions(vitals PRIVATE TRAY_APPINDICATOR)
    target_include_directories(vitals PRIVATE ${VITALS_TRAY_SHIM_DIR} ${GTK3_INCLUDE_DIRS} ${APPINDICATOR_INCLUDE_DIRS})
    target_link_libraries(vitals PRIVATE ${GTK3_LIBRARIES} ${APPINDICATOR_LIBRARIES})
    target_compile_options(vitals PRIVATE ${GTK3_CFLAGS_OTHER} ${APPINDICATOR_CFLAGS_OTHER})
endif()

find_library(RT_LIBRARY rt)

target_link_libraries(vitals PRIVATE glfw OpenGL::GL Threads::Threads)
if(RT_LIBRARY)
    target_link_libraries(vitals PRIVATE ${RT_LIBRARY}) # shm_open/shm_unlink on older glibc
endif()

if(MSVC)
    target_compile_options(vitals PRIVATE /W4)
else()
    target_compile_options(vitals PRIVATE -Wall -Wextra)
endif()

# --- ram_oc_daemon: separate persistent GPLv3 process -------------------
# DRAM/SMU telemetry + the memory benchmark are vendored GPLv3 code
# (ram_oc/, from TuxTimings) — reused as-is rather than re-deriving AMD's
# per-family PM-table offsets. Compiling it into `vitals` would make the
# whole combined binary a GPLv3 derivative work if ever distributed, so it
# builds as its own executable instead: a persistent daemon that refreshes
# POSIX shared memory (VITALS_SHM_NAME, versioned) and serves benchmark requests over a
# Unix socket (see include/ipc/ram_oc_shm.h for the wire contract,
# src/dram_oc/daemon_ipc.cpp for the vitals-side client). Only this daemon
# binary is GPLv3 — correctly, since it's built from GPL sources.
add_executable(ram_oc_daemon
    ram_oc_helper/daemon_main.c
    ram_oc/Linux/src/backend.c
    ram_oc/Linux/src/dram.c
    ram_oc/Linux/src/pm_table.c
    ram_oc/Linux/src/util.c
    ram_oc/Linux/src/bench.c
)

# bench.c uses AVX2 intrinsics directly (outside its AVX-512 target-attributed
# functions), so it needs -mavx2 on this one file — not the whole binary,
# unlike ram_oc's own build which compiles everything with -march=native.
set_source_files_properties(ram_oc/Linux/src/bench.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mclflushopt")

target_include_directories(ram_oc_daemon PRIVATE ram_oc/Linux/src include)
target_link_libraries(ram_oc_daemon PRIVATE Threads::Threads)
if(RT_LIBRARY)
    target_link_libraries(ram_oc_daemon PRIVATE ${RT_LIBRARY}) # shm_open/shm_unlink on older glibc
endif()

# --- gpu_ctl_daemon: separate, root-only GPU control service ------------
# LACT-master is MIT (unlike ram_oc/'s GPLv3), so unlike ram_oc_daemon this
# split isn't a license requirement — it's a privilege one: AMD's
# power1_cap/pwm1/pp_od_clk_voltage and NVML's Set* calls are root-only
# unconditionally, and `vitals` itself must never run as root just to reach
# them (see sensors.md's "never auto-elevates itself" policy). This binary
# runs as a systemd service the user installs once (scripts/install-daemons.sh,
# packaging/systemd/vitals-gpud.service) — never spawned by `vitals` (see
# src/gpu_ctl/gpu_ctl_ipc.cpp, which only ever pings). Nvidia support dlopen's
# libnvidia-ml.so.1/libnvidia-api.so.1 at runtime (nvml_dlopen.cpp,
# nvidia_nvapi_shim.cpp) instead of linking against proprietary SDK headers,
# so this target has no Nvidia SDK dependency either.
add_executable(gpu_ctl_daemon
    gpu_ctl_daemon/main.cpp
    gpu_ctl_daemon/sysfs_io.cpp
    gpu_ctl_daemon/amd_backend.cpp
    gpu_ctl_daemon/nvml_dlopen.cpp
    gpu_ctl_daemon/nvidia_backend.cpp
    gpu_ctl_daemon/nvidia_nvapi_shim.cpp
    gpu_ctl_daemon/config_store.cpp
    gpu_ctl_daemon/pending_apply.cpp
)
target_include_directories(gpu_ctl_daemon PRIVATE include)
target_link_libraries(gpu_ctl_daemon PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# Land all three executables side by side so vitals can find ram_oc_daemon
# via /proc/self/exe regardless of cwd (see FindDaemonPath in
# dram_oc/daemon_ipc.cpp) — gpu_ctl_daemon doesn't need the same trick
# (it's never spawned by vitals, only pinged over its fixed socket path),
# but there's no reason to scatter build output across two directories.
set_target_properties(vitals ram_oc_daemon gpu_ctl_daemon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Assert-based self-check for gpu_ctl_daemon's parsers/sanitizer; not built by default.
add_executable(gpuctl_check EXCLUDE_FROM_ALL tools/gpuctl_check.cpp gpu_ctl_daemon/amd_backend.cpp gpu_ctl_daemon/sysfs_io.cpp
    src/metrics/gpu_sysfs.cpp src/metrics/proc_util.cpp)
target_include_directories(gpuctl_check PRIVATE include src)
target_link_libraries(gpuctl_check PRIVATE Threads::Threads)

# --- app icon: rendered at build time from AppIcon::RenderSVG() ---------
# Procedural (see src/app_icon.h) — dumping it to an .svg here avoids a
# checked-in binary asset and an image-decoding dependency just to install
# an icon.
add_executable(dump_icon tools/dump_icon.cpp src/app_icon.cpp)
target_include_directories(dump_icon PRIVATE src)

set(VITALS_ICON_SVG ${CMAKE_BINARY_DIR}/vitals.svg)
add_custom_command(OUTPUT ${VITALS_ICON_SVG}
    COMMAND dump_icon ${VITALS_ICON_SVG}
    DEPENDS dump_icon
    COMMENT "Rendering vitals.svg")
add_custom_target(vitals_icon ALL DEPENDS ${VITALS_ICON_SVG})

# --- install layout -------------------------------------------------------
# ram_oc_daemon must land next to `vitals` (FindDaemonPath in
# src/dram_oc/daemon_ipc.cpp resolves it via /proc/self/exe's own
# directory), so both go under a private dir with a /usr/bin symlink for
# vitals. gpu_ctl_daemon has no such constraint — it's only ever reached
# over its fixed socket path — so it installs as a normal sbin binary.
include(GNUInstallDirs)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # /usr/local/share/applications isn't in most desktop environments'
    # default XDG_DATA_DIRS, so a plain (non-packaged) install needs /usr to
    # make the .desktop entry/icon actually show up.
    set(CMAKE_INSTALL_PREFIX /usr CACHE PATH "" FORCE)
endif()

install(TARGETS vitals ram_oc_daemon
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/vitals
    COMPONENT vitals)
# Second copy at the fixed path packaging/systemd/vitals-ramocd.service runs
# (LIBDIR may be a multiarch dir, so the unit can't point at the copy above).
install(TARGETS ram_oc_daemon
    RUNTIME DESTINATION libexec/vitals
    COMPONENT vitals)
install(FILES packaging/systemd/vitals-ramocd.service
    DESTINATION lib/systemd/system
    COMPONENT vitals)
# CMAKE_INSTALL_PREFIX is escaped (\${...}) so it's resolved when the
# generated install script actually runs, not baked in at configure time —
# CPack overrides it to /usr for the .deb regardless of this project's own
# configured prefix, same as install(TARGETS/FILES ... DESTINATION) already
# does for the lines above.
install(CODE "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/vitals/vitals
    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/vitals\")"
    COMPONENT vitals)
install(FILES ${VITALS_ICON_SVG}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME vitals.svg
    COMPONENT vitals)
install(FILES packaging/vitals.desktop packaging/vitals-root.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    COMPONENT vitals)
install(PROGRAMS packaging/vitals-root
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT vitals)
install(DIRECTORY assets/fonts
    DESTINATION ${CMAKE_INSTALL_DATADIR}/vitals
    COMPONENT vitals)

install(TARGETS gpu_ctl_daemon
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
    COMPONENT gpud)
install(FILES packaging/systemd/vitals-gpud.service
    DESTINATION lib/systemd/system # fixed systemd convention, not CMAKE_INSTALL_LIBDIR (which may be a multiarch dir)
    COMPONENT gpud)

# --- CPack: .deb packages ---------------------------------------------------
# Two packages, matching the existing trust split: `vitals` never needs root
# to install or run; `vitals-gpud` is the opt-in root service (see the
# gpu_ctl_daemon comment block above and README's "GPU power limit / fan
# curve" section) — installing it is what creates the vitals-gpu group and
# enables the systemd unit, same as scripts/install-daemons.sh does by hand.
set(CPACK_PACKAGE_NAME "vitals")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_CONTACT "unspecified")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dear ImGui system monitor for Linux")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_COMPONENTS_ALL vitals gpud)

set(CPACK_DEBIAN_VITALS_PACKAGE_NAME "vitals")
set(CPACK_DEBIAN_VITALS_FILE_NAME "vitals_${CPACK_PACKAGE_VERSION}_amd64.deb")
# pkexec is a hard dep (vitals-root calls it directly); the agent is only a
# Recommends because without one, pkexec has nothing to show a password
# prompt through and fails silently — see packaging/vitals-root's notify-send
# fallback for that case.
set(CPACK_DEBIAN_VITALS_PACKAGE_DEPENDS "pkexec")
set(CPACK_DEBIAN_VITALS_PACKAGE_RECOMMENDS "policykit-1-gnome | polkit-kde-agent-1 | lxqt-policykit | xfce-polkit")
set(CPACK_DEBIAN_VITALS_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/vitals/postinst;${CMAKE_SOURCE_DIR}/packaging/debian/vitals/prerm")

set(CPACK_DEBIAN_GPUD_PACKAGE_NAME "vitals-gpud")
set(CPACK_DEBIAN_GPUD_FILE_NAME "vitals-gpud_${CPACK_PACKAGE_VERSION}_amd64.deb")
set(CPACK_DEBIAN_GPUD_PACKAGE_DEPENDS "vitals (= ${CPACK_PACKAGE_VERSION})")
set(CPACK_DEBIAN_GPUD_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/gpud/postinst;${CMAKE_SOURCE_DIR}/packaging/debian/gpud/prerm")

include(CPack)
