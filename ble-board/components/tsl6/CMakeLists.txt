# The TSL6 car features as an engine (port/tsl6_engine.h). vendor/ and gen/
# come from tools/tsl6-import.py. Each TSL6 module includes its own header by
# a plain name, so each module directory is on the include path of its own
# sources only (m2.h, m3.h ... must not shadow each other).
set(modules m1 m2 m3 m4 m5 m7 m8)
set(srcs port/tsl6_engine.c port/tsl6_tmos.c port/tsl6_tables.c port/tsl6_stubs.c
    gen/resident_slots.c gen/ram_regions.S)
foreach(m ${modules})
    file(GLOB msrcs "${CMAKE_CURRENT_LIST_DIR}/vendor/${m}/*.c")
    list(APPEND srcs ${msrcs})
endforeach()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "port"
    PRIV_INCLUDE_DIRS "vendor/include"
)

foreach(m ${modules})
    file(GLOB msrcs "${CMAKE_CURRENT_LIST_DIR}/vendor/${m}/*.c")
    set_source_files_properties(${msrcs} PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/vendor/${m}")
endforeach()

# The stock code was built with signed char, and it reads one object through
# pointers of other types. Its warnings are the stock code's, not ours.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fsigned-char -fno-strict-aliasing -w)
