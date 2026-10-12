# Override with qmake OPENCV_ROOT=... (root containing build/include).
isEmpty(OPENCV_ROOT): OPENCV_ROOT = $$(OPENCV_ROOT)
isEmpty(OPENCV_ROOT): OPENCV_ROOT = D:/opencv/opencv
OPENCV_INCLUDE = $$OPENCV_ROOT/build/include
OPENCV_LIB = $$OPENCV_ROOT/build/x64/vc16/lib
OPENCV_BIN = $$OPENCV_ROOT/build/x64/vc16/bin
isEmpty(OPENCV_VERSION_SUFFIX): OPENCV_VERSION_SUFFIX = 500
!win32-msvc: error("The configured OpenCV binaries require MSVC x64")
!contains(QT_ARCH, x86_64): error("The configured OpenCV binaries require x64 Qt")
!exists($$OPENCV_INCLUDE/opencv2/imgproc.hpp): error("OpenCV headers missing: $$OPENCV_INCLUDE")
CONFIG(debug, debug|release) {
    OPENCV_WORLD = opencv_world$${OPENCV_VERSION_SUFFIX}d
} else {
    OPENCV_WORLD = opencv_world$${OPENCV_VERSION_SUFFIX}
}
!exists($$OPENCV_LIB/$${OPENCV_WORLD}.lib): error("OpenCV library missing: $$OPENCV_WORLD")
!exists($$OPENCV_BIN/$${OPENCV_WORLD}.dll): error("OpenCV runtime missing: $$OPENCV_WORLD")
INCLUDEPATH += $$quote($$OPENCV_INCLUDE)
LIBS += $$quote($$OPENCV_LIB/$${OPENCV_WORLD}.lib)
# Runtime deployment is performed only when a build is explicitly requested.
OPENCV_RUNTIME_DEST = $$OUT_PWD
CONFIG(debug, debug|release): OPENCV_RUNTIME_DEST = $$OUT_PWD/debug
else: OPENCV_RUNTIME_DEST = $$OUT_PWD/release
QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$OPENCV_BIN/$${OPENCV_WORLD}.dll)) $$shell_quote($$shell_path($$OPENCV_RUNTIME_DEST)) $$escape_expand(\\n\\t)
