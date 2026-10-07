# Sourced by the run scripts. gl4es_env <variant> prints the env assignments that put a variant in front.
#   ref                     Mesa desktop GL (llvmpipe): the reference
#   <lib>                   gl4es from $GT/lib/<lib> on Mesa llvmpipe GLES 2 via EGL, Mali capability profile
#   <lib>@K=V@K2=V2         the same with extra gl4es settings, e.g. ship@LIBGL_BEGINEND=0
#   a lib name containing "asan" also preloads the AddressSanitizer runtime
GT=${GT:-$HOME/gl4es-test}
gl4es_env() {
    local spec=$1 lib=${1%%@*} extra=""
    [ "$spec" != "$lib" ] && extra=$(echo "${spec#*@}" | tr '@' ' ')
    case $lib in
        ref) echo "LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1" ;;
        *)   local asan=""
             case $lib in *asan*) asan="ASAN_OPTIONS=detect_leaks=0:abort_on_error=0:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1 LD_PRELOAD=$(gcc -print-file-name=libasan.so)" ;; esac
             echo "LD_LIBRARY_PATH=$GT/lib/$lib LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 LIBGL_ES=2 LIBGL_GL=21 LIBGL_NOTEST=1 GL4ES_MALI_PROFILE=1 LIBGL_NOBANNER=1 LIBGL_SILENTSTUB=1 $asan $extra" ;;
    esac
}
# one Xvfb per run; WSL kills it when the wsl call that started it ends, so callers stay in one script
start_xvfb() {   # start_xvfb <display number>
    [ -x $GT/bin/gtXvfb ] || cp /usr/bin/Xvfb $GT/bin/gtXvfb   # own name: immune to other scripts' pkill -x Xvfb
    $GT/bin/gtXvfb :$1 -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
    XVFB_PID=$!; export DISPLAY=:$1
    for i in $(seq 50); do xdpyinfo >/dev/null 2>&1 && return 0; sleep 0.1; done
    echo "Xvfb :$1 did not start"; return 1
}
