# Owned renderer modules retain Aurora's target and worker/resource lifetimes.
function(ps5_attach_aurora_extensions)
    target_sources(aurora_gx PRIVATE
        "${PS5_PORT_DIR}/ps5/aurora/gfx/post_fx.cpp")
    target_include_directories(aurora_gx PRIVATE
        "${PS5_PORT_DIR}/ps5/aurora/gfx"
        "${aurora_SOURCE_DIR}/lib"
        "${aurora_SOURCE_DIR}/lib/gfx")
    # Core and FIFO tests include frame_packet.hpp through engine-private headers.
    foreach(target aurora_core gfx_recording_tests gx_fifo_tests)
        if(TARGET ${target})
            target_include_directories(${target} PRIVATE "${PS5_PORT_DIR}/ps5/aurora/gfx")
        endif()
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL ps5_attach_aurora_extensions)
