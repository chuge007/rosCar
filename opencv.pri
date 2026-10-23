# Override with qmake OPENCV_ROOT=... or the OPENCV_ROOT environment variable.
# Supported layouts:
#   Official package: <root>/build/include and <root>/build/x64/vc16
#   Local SDK:        <root>/include and <root>/lib
isEmpty(OPENCV_ROOT): OPENCV_ROOT = $$(OPENCV_ROOT)

isEmpty(OPENCV_ROOT) {
    OPENCV_DEFAULT_ROOT = D:/opencv/opencv
    exists($$OPENCV_DEFAULT_ROOT/build/include/opencv2/imgproc.hpp): OPENCV_ROOT = $$OPENCV_DEFAULT_ROOT
}

isEmpty(OPENCV_ROOT) {
    OPENCV_LOCAL_SDK_ROOT = $$clean_path($$PWD/../UtSDKAppLibs-win64)
    exists($$OPENCV_LOCAL_SDK_ROOT/include/opencv2/imgproc.hpp): OPENCV_ROOT = $$OPENCV_LOCAL_SDK_ROOT
}

isEmpty(OPENCV_ROOT): error("OpenCV SDK not found. Set OPENCV_ROOT to an official OpenCV package or place UtSDKAppLibs-win64 next to this project.")

OPENCV_LAYOUT = world
OPENCV_INCLUDE = $$OPENCV_ROOT/build/include
OPENCV_LIB = $$OPENCV_ROOT/build/x64/vc16/lib
OPENCV_BIN = $$OPENCV_ROOT/build/x64/vc16/bin
!exists($$OPENCV_INCLUDE/opencv2/imgproc.hpp) {
    OPENCV_LAYOUT = modular
    OPENCV_INCLUDE = $$OPENCV_ROOT/include
    OPENCV_LIB = $$OPENCV_ROOT/lib
    OPENCV_BIN = $$OPENCV_ROOT/lib
}

!win32-msvc: error("The configured OpenCV binaries require MSVC x64")
!contains(QT_ARCH, x86_64): error("The configured OpenCV binaries require x64 Qt")
!exists($$OPENCV_INCLUDE/opencv2/imgproc.hpp): error("OpenCV headers missing: $$OPENCV_INCLUDE")

equals(OPENCV_LAYOUT, modular) {
    isEmpty(OPENCV_VERSION_SUFFIX): OPENCV_VERSION_SUFFIX = 453
    OPENCV_CORE = opencv_core$${OPENCV_VERSION_SUFFIX}
    OPENCV_IMGPROC = opencv_imgproc$${OPENCV_VERSION_SUFFIX}
    !exists($$OPENCV_LIB/$${OPENCV_CORE}.lib): error("OpenCV library missing: $$OPENCV_CORE")
    !exists($$OPENCV_LIB/$${OPENCV_IMGPROC}.lib): error("OpenCV library missing: $$OPENCV_IMGPROC")
    !exists($$OPENCV_BIN/$${OPENCV_CORE}.dll): error("OpenCV runtime missing: $$OPENCV_CORE")
    !exists($$OPENCV_BIN/$${OPENCV_IMGPROC}.dll): error("OpenCV runtime missing: $$OPENCV_IMGPROC")
    LIBS += $$quote($$OPENCV_LIB/$${OPENCV_CORE}.lib)
    LIBS += $$quote($$OPENCV_LIB/$${OPENCV_IMGPROC}.lib)
} else {
    isEmpty(OPENCV_VERSION_SUFFIX): OPENCV_VERSION_SUFFIX = 500
    CONFIG(debug, debug|release) {
        OPENCV_WORLD = opencv_world$${OPENCV_VERSION_SUFFIX}d
    } else {
        OPENCV_WORLD = opencv_world$${OPENCV_VERSION_SUFFIX}
    }
    !exists($$OPENCV_LIB/$${OPENCV_WORLD}.lib): error("OpenCV library missing: $$OPENCV_WORLD")
    !exists($$OPENCV_BIN/$${OPENCV_WORLD}.dll): error("OpenCV runtime missing: $$OPENCV_WORLD")
    LIBS += $$quote($$OPENCV_LIB/$${OPENCV_WORLD}.lib)
}

INCLUDEPATH += $$quote($$OPENCV_INCLUDE)

# Runtime deployment is performed only when a build is explicitly requested.
equals(OPENCV_LAYOUT, modular) {
    OPENCV_RUNTIME_DEST = $$clean_path($$DESTDIR)
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$OPENCV_BIN/$${OPENCV_CORE}.dll)) $$shell_quote($$shell_path($$OPENCV_RUNTIME_DEST)) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$OPENCV_BIN/$${OPENCV_IMGPROC}.dll)) $$shell_quote($$shell_path($$OPENCV_RUNTIME_DEST)) $$escape_expand(\\n\\t)
} else {
    OPENCV_RUNTIME_DEST = $$OUT_PWD
    CONFIG(debug, debug|release): OPENCV_RUNTIME_DEST = $$OUT_PWD/debug
    else: OPENCV_RUNTIME_DEST = $$OUT_PWD/release
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$OPENCV_BIN/$${OPENCV_WORLD}.dll)) $$shell_quote($$shell_path($$OPENCV_RUNTIME_DEST)) $$escape_expand(\\n\\t)
}
