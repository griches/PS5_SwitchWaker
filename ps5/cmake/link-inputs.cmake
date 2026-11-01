# Preserve the existing recipe while making CRT/stub compilation real dependencies.
function(ps5_attach_link_inputs)
    # Track the actual successful link/package output, not a nonexistent host executable.
    set_target_properties(switchwaker PROPERTIES OUTPUT_NAME "eboot" SUFFIX ".elf")
    set(work "${CMAKE_BINARY_DIR}")
    set(vulkan "${PS5_VULKAN_DIR}")
    set(sdk "${PS5_PAYLOAD_SDK}")
    add_custom_command(
        OUTPUT "${work}/link/obj/app_crt.o" "${work}/link/stubs/libSceAgc.so" "${work}/link/stubs/libSceAgcDriver.so"
        COMMAND "${CMAKE_COMMAND}" -E env "PS5_CLANG=$ENV{PS5_CLANG}"
            bash "${PS5_PORT_DIR}/ps5/tools/prepare-link-inputs.sh" "${work}" "${vulkan}" "${sdk}"
        DEPENDS "${PS5_PORT_DIR}/ps5/tools/prepare-link-inputs.sh"
            "${vulkan}/tooling/native/app_crt.cpp" "${vulkan}/tooling/prospero-clang18"
            "${vulkan}/vendor/ps5/sdk/stubs/agc_canary_link_stub.c"
            "${vulkan}/vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c"
            "${sdk}/bin/prospero-lld"
        DEPFILE "${work}/link/obj/app_crt.d"
        VERBATIM)
    add_custom_target(ps5_link_inputs DEPENDS "${work}/link/obj/app_crt.o"
        "${work}/link/stubs/libSceAgc.so" "${work}/link/stubs/libSceAgcDriver.so")
    add_dependencies(switchwaker ps5_link_inputs)
    set(archive "$ENV{RADV_ARCHIVE}")
    if(NOT archive)
        set(archive "${vulkan}/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a")
    endif()
    file(GLOB_RECURSE packaging_inputs CONFIGURE_DEPENDS
        "${PS5_PORT_DIR}/licenses/*" "${PS5_PORT_DIR}/ps5/sce_sys/*" "${PS5_PORT_DIR}/ps5/assets/controller-ui/*"
        "${PS5_PORT_DIR}/ps5/shader-cache/*")
    # These are consumed by the custom linker, outside CMake's target graph.
    file(GLOB sdk_link_inputs CONFIGURE_DEPENDS "${sdk}/target/lib/*.a" "${sdk}/target/lib/*.so")
    # compiler-rt is outside the SDK; follow radv-link.sh's ARM-host fallback.
    execute_process(COMMAND "$ENV{PS5_CLANG}" --print-resource-dir
        OUTPUT_VARIABLE compiler_resources OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(builtins "${compiler_resources}/lib/linux/libclang_rt.builtins-x86_64.a")
    if(NOT EXISTS "${builtins}")
        set(builtins "${vulkan}/.deps/native/compiler-rt-22/usr/lib/llvm-22/lib/clang/22/lib/linux/libclang_rt.builtins-x86_64.a")
    endif()
    if(EXISTS "${builtins}")
        list(APPEND sdk_link_inputs "${builtins}")
    endif()
    set_property(TARGET switchwaker APPEND PROPERTY LINK_DEPENDS
        "${work}/link/obj/app_crt.o" "${work}/link/stubs/libSceAgc.so" "${work}/link/stubs/libSceAgcDriver.so"
        "${PS5_PORT_DIR}/ps5/tools/link-title.sh" "${PS5_PORT_DIR}/ps5/tools/package-title.sh"
        "${PS5_PORT_DIR}/ps5/tools/make-div-table.py" "${PS5_PORT_DIR}/ps5/tools/source-provenance.py"
        "${sdk}/bin/prospero-lld" "${vulkan}/build/host/ps5-native-tool"
        "${vulkan}/tooling/psbc/ps5-pie-unwind.ld" "${vulkan}/tooling/native/app-symbols.map"
        ${sdk_link_inputs} "${work}/param.json"
        "${vulkan}/tools/radv-link.sh" "${vulkan}/runtime/libc.prx" "${archive}"
        "${PS5_PORT_DIR}/vendor/switchwaker/native/data/initial_pipeline_cache.db" ${packaging_inputs})
    # The custom link recipe publishes these files atomically. Declare them on
    # the same Ninja edge so a removed title (or resource) is rebuilt normally.
    file(READ "${PS5_PORT_DIR}/ps5/sce_sys/param.json" metadata)
    string(JSON title_id GET "${metadata}" titleId)
    set(app "${PS5_PORT_DIR}/dist/${title_id}")
    set(packaged_outputs "${app}/eboot.bin" "${app}/BUILD_INPUTS.json"
        "${app}/sce_sys/param.json" "${app}/sce_module/libc.prx"
        "${app}/initial_pipeline_cache.db" "${app}/res/div-table.bin")
    foreach(asset icon0.png pic0.dds pic1.dds snd0.at9)
        if(EXISTS "${PS5_PORT_DIR}/ps5/sce_sys/${asset}")
            list(APPEND packaged_outputs "${app}/sce_sys/${asset}")
        endif()
    endforeach()
    foreach(asset resources-us.bin resources-pal.bin menu-prompts.rgba)
        list(APPEND packaged_outputs "${app}/res/${asset}")
    endforeach()
    file(GLOB_RECURSE cache_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
        RELATIVE "${PS5_PORT_DIR}/ps5/shader-cache" "${PS5_PORT_DIR}/ps5/shader-cache/*")
    foreach(asset IN LISTS cache_files)
        list(APPEND packaged_outputs "${app}/res/shader-cache/${asset}")
    endforeach()
    foreach(asset SwitchWaker-LICENSE.txt SwitchWaker-game-LICENSE.txt Dolphin-DSPHLE-LICENSE.txt
            SwitchWaker-THIRD_PARTY.md Aurora-LICENSE.txt PS5_Vulkan-LICENSE.txt
            LLVM-compiler-rt.txt Controller-prompts.txt)
        list(APPEND packaged_outputs "${app}/licenses/${asset}")
    endforeach()
    add_custom_command(TARGET switchwaker POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E true
        BYPRODUCTS ${packaged_outputs}
        VERBATIM)
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL ps5_attach_link_inputs)
