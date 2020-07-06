# project configuration (currently just version definitions)
configure_file(config.h.in config.h)


# ------------------------------
# Module (sub-directories or sub-libraries) configurations
add_subdirectory(protocol)
add_library(
    protocol STATIC
    protocol/kinetic.pb-c.c
    ${STATIC_LIB_DIR}/libkinetic.pb-c-4.0.1.so
)


# This should link "libkinetic.pb-c-4.0.1.so"
target_link_libraries(kinetic_client ${STATIC_LIB_DIR}/libkinetic.pb-c-4.0.1.so)

# ------------------------------
# Executables

# toolbox executables
add_executable(write_request toolbox/write_request.c)
set_property(TARGET write_request PROPERTY C_STANDARD 99)


target_include_directories(write_request PUBLIC "${PROJECT_BINARY_DIR}")
