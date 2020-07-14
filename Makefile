CC=gcc
CFLAGS=-g
LDFLAGS=-Ivendor/list/ -Ivendor/protobuf-c

# directories
BIN_DIR=bin
TOOLBOX_DIR=toolbox

# source files for external dependencies (separated by project or repo)
LIBPROTOBUF_SRC=vendor/protobuf-c/protobuf-c/protobuf-c.c
LIBLIST_SRC=vendor/list/list.c
KINETIC_PROTO_SRC=src/protocol/kinetic.pb-c.c

# source files containing main functions (entrypoints)
READ_UTIL=${TOOLBOX_DIR}/read_request.c
WRITE_UTIL=${TOOLBOX_DIR}/write_request.c

# source files for library and dependency code
LIB_SRC_FILES=src/getlog.c src/protocol_interface.c ${LIBLIST_SRC} ${LIBPROTOBUF_SRC}
DEP_SRC_FILES=${KINETIC_PROTO_SRC} ${LIBPROTOBUF_SRC} ${LIBLIST_SRC}

test_read:
	${CC} ${CFLAGS} ${LDFLAGS} -o ${BIN_DIR}/test_read ${READ_UTIL} ${LIB_SRC_FILES} ${DEP_SRC_FILES}

test_write:
	${CC} ${CFLAGS} ${LDFLAGS} -o ${BIN_DIR}/test_write ${WRITE_UTIL} ${LIB_SRC_FILES} ${DEP_SRC_FILES}
