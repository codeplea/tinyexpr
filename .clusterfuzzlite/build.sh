#!/bin/bash
for file in "smoke.c tinyexpr.c repl.c benchmark.c"; do
  $CC $CFLAGS -c ${file}
done

llvm-ar rcs libfuzz.a *.o


$CC $CFLAGS $LIB_FUZZING_ENGINE $SRC/fuzzer.c \
  -Wl,--whole-archive $SRC/tinyexpr/libfuzz.a -Wl,--allow-multiple-definition \
  -I$SRC/tinyexpr/  -o $OUT/fuzzer
