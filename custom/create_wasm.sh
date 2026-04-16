#!/bin/bash

if [ -z "$1" ]; then
    echo "Error: You must specify the C source file to compile."
    echo "Example: ./build.sh example.c"
    exit 1
fi

if [ -z "$WASI_SDK_PATH" ]; then
    echo "CRITICAL ERROR: WASI_SDK_PATH environment variable is not set."
    echo "Please run the following command before executing this script:"
    echo "export WASI_SDK_PATH=/absolute/path/to/your/wasi-sdk"
    exit 1
fi

CLANG_CMD="${WASI_SDK_PATH}/bin/clang"

if [ ! -x "$CLANG_CMD" ]; then
    echo "CRITICAL ERROR: clang compiler not found at: $CLANG_CMD"
    exit 1
fi

SRC_FILE=$1                  
BASE_NAME=${SRC_FILE%.c}     

echo "Compiling WASM module from ${SRC_FILE} ..."

$CLANG_CMD -O3 \
        -z stack-size=4096 -Wl,--initial-memory=65536 \
        -o ${BASE_NAME}.wasm ${SRC_FILE} \
        -Wl,--export=main -Wl,--export=__main_argc_argv \
        -Wl,--export=__data_end -Wl,--export=__heap_base \
        -Wl,--strip-all,--no-entry \
        -Wl,--allow-undefined \
        -nostdlib

echo "Done! Generated file: ${BASE_NAME}.wasm"
