FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y make autoconf automake libtool cmake \
                      pkg-config curl check
COPY . $SRC/tinyexpr
COPY .clusterfuzzlite/build.sh $SRC/build.sh
COPY .clusterfuzzlite/*.c $SRC/
WORKDIR tinyexpr
