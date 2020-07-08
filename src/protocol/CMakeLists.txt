add_library(
    protocol-generated STATIC
    protocol/kinetic.pb-c.c
    ${STATIC_LIB_DIR}/libkinetic.pb-c-4.0.1.so
)
