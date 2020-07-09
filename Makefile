CC=gcc
CFLAGS=-g

# directories
BIN_DIR=bin
TOOLBOX_DIR=toolbox

# protocol-related sources
LIBPROTOBUF_SRC=${HOME}/code/protobuf-c/protobuf-c/protobuf-c.c
KINETIC_PROTO_SRC=src/protocol/kinetic.pb-c.c

# sources containing main functions (entrypoints)
READ_UTIL=${TOOLBOX_DIR}/read_request.c
WRITE_UTIL=${TOOLBOX_DIR}/write_request.c

# library sources
LIB_SRC=src/protocol_interface.c

test_read:
	${CC} ${CFLAGS} -o ${BIN_DIR}/test_read ${LIB_SRC} ${READ_UTIL} ${KINETIC_PROTO_SRC} ${LIBPROTOBUF_SRC}

test_write:
	${CC} ${CFLAGS} -o ${BIN_DIR}/test_write ${LIB_SRC} ${WRITE_UTIL} ${KINETIC_PROTO_SRC} ${LIBPROTOBUF_SRC}
