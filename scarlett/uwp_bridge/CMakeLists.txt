include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(UWP_BRIDGE_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}"
)

function(_uwp_discover_sources OUT_VAR SUBDIR)
    set(_result "")
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}")
    if(EXISTS "${_dir}/sources.txt")
        file(STRINGS "${_dir}/sources.txt" _lines ENCODING UTF-8)
        foreach(_line IN LISTS _lines)
            string(STRIP "${_line}" _s)
            if(NOT _s STREQUAL "" AND NOT _s MATCHES "^#")
                if(IS_ABSOLUTE "${_s}")
                    list(APPEND _result "${_s}")
                elseif(_s MATCHES "^${SUBDIR}/")
                    list(APPEND _result "${CMAKE_CURRENT_SOURCE_DIR}/${_s}")
                else()
                    list(APPEND _result "${_dir}/${_s}")
                endif()
            endif()
        endforeach()
    elseif(IS_DIRECTORY "${_dir}")
        file(GLOB _c_srcs CONFIGURE_DEPENDS "${_dir}/*.c" "${_dir}/*.cpp")
        list(APPEND _result ${_c_srcs})
    endif()
    set(${OUT_VAR} "${_result}" PARENT_SCOPE)
endfunction()

function(_uwp_build_forwarder_group GROUP_DIR)
    set(_base "${CMAKE_CURRENT_SOURCE_DIR}/${GROUP_DIR}")
    if(NOT IS_DIRECTORY "${_base}")
        return()
    endif()
    if(EXISTS "${_base}/CMakeLists.txt")
        add_subdirectory("${GROUP_DIR}")
        return()
    endif()

    foreach(_dll IN LISTS ARGN)
        string(TOLOWER "${_dll}" _dll_lower)
        set(_def_file "")
        foreach(_cand_def "${_base}/${_dll}.def" "${_base}/${_dll_lower}.def")
            if(_def_file STREQUAL "" AND EXISTS "${_cand_def}")
                set(_def_file "${_cand_def}")
            endif()
        endforeach()

        set(_src_file "")
        foreach(_cand_src
            "${_base}/${_dll}.c"
            "${_base}/${_dll_lower}.c"
            "${_base}/${_dll}_pe.c"
            "${_base}/${_dll_lower}_pe.c"
            "${_base}/${_dll}_stubs.c"
            "${_base}/${_dll_lower}_stubs.c"
            "${_base}/${_dll}.cpp"
            "${_base}/${_dll_lower}.cpp"
        )
            if(_src_file STREQUAL "" AND EXISTS "${_cand_src}")
                set(_src_file "${_cand_src}")
            endif()
        endforeach()

        if(NOT _def_file STREQUAL "" AND NOT _src_file STREQUAL "")
            add_library(${_dll} SHARED "${_src_file}" "${_def_file}")
            set_target_properties(${_dll} PROPERTIES PREFIX "" SUFFIX ".dll")
            target_include_directories(${_dll} PRIVATE
                ${UWP_BRIDGE_INCLUDE_DIRS}
                "${_base}"
            )
            target_compile_options(${_dll} PRIVATE -Wall -Wextra)
            target_link_libraries(${_dll} PRIVATE kernel32)
            install(TARGETS ${_dll}
                RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
            )
        endif()
    endforeach()
endfunction()

# Windows PE64 targets: UWPHOST.dll + 21 *_APP.dll / XINPUTUAP.dll forwarders
if(SCARLETT_BUILD_PE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/uwphost/CMakeLists.txt")
        add_subdirectory(uwphost)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/uwphost/UWPHOST.def")
        _uwp_discover_sources(_uwphost_srcs "uwphost")
        _uwp_discover_sources(_winrt_ui_srcs "winrt_ui")
        _uwp_discover_sources(_winrt_sys_srcs "winrt_sys")

        set(_uwphost_all_srcs ${_uwphost_srcs} ${_winrt_ui_srcs} ${_winrt_sys_srcs})
        if(_uwphost_all_srcs)
            add_library(UWPHOST SHARED
                ${_uwphost_all_srcs}
                "${CMAKE_CURRENT_SOURCE_DIR}/uwphost/UWPHOST.def"
            )
            set_target_properties(UWPHOST PROPERTIES PREFIX "" SUFFIX ".dll")
            target_include_directories(UWPHOST PRIVATE
                ${UWP_BRIDGE_INCLUDE_DIRS}
                "${CMAKE_CURRENT_SOURCE_DIR}/uwphost"
                "${CMAKE_CURRENT_SOURCE_DIR}/winrt_ui"
                "${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys"
            )
            target_compile_options(UWPHOST PRIVATE -Wall -Wextra)
            target_link_libraries(UWPHOST PRIVATE
                kernel32
                user32
                gdi32
                ole32
                advapi32
            )
            install(TARGETS UWPHOST
                RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
            )
        endif()
    endif()

    _uwp_build_forwarder_group("forwarders/sys"
        KERNEL32_APP
        USER32_APP
        GDI32_APP
        ADVAPI32_APP
        OLE32_APP
        COMBASE_APP
        RPCRT4_APP
        WS2_32_APP
        WINHTTP_APP
        CRYPT32_APP
        BCRYPT_APP
        MMDEVAPI_APP
        XINPUT1_4_APP
        XINPUTUAP
    )

    _uwp_build_forwarder_group("forwarders/crt"
        VCRUNTIME140_APP
        VCRUNTIME140_1_APP
        MSVCP140_APP
        MSVCP140_1_APP
        MSVCP140_2_APP
        CONCRT140_APP
        vccorlib140_app
    )
endif()

# Host Linux verification and unit tests
if(SCARLETT_BUILD_HOST_TESTS AND NOT SCARLETT_BUILD_PE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(Threads REQUIRED)
    enable_testing()

    foreach(_sub IN ITEMS "uwphost/tests" "winrt_ui/tests" "winrt_sys/tests" "forwarders/sys/tests" "forwarders/crt" "tests")
        set(_abs_sub "${CMAKE_CURRENT_SOURCE_DIR}/${_sub}")
        if(EXISTS "${_abs_sub}/CMakeLists.txt")
            add_subdirectory("${_sub}")
        elseif(IS_DIRECTORY "${_abs_sub}")
            file(GLOB _py_tests CONFIGURE_DEPENDS "${_abs_sub}/test_*.py")
            list(SORT _py_tests)
            foreach(_py_test IN LISTS _py_tests)
                get_filename_component(_py_name "${_py_test}" NAME_WE)
                string(REPLACE "/" "_" _prefix "${_sub}")
                add_test(
                    NAME "uwp_${_prefix}_${_py_name}"
                    COMMAND ${Python3_EXECUTABLE} "${_py_test}"
                )
            endforeach()
        endif()
    endforeach()
endif()
