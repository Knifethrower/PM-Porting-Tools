"""Test builds only: patch a build copy so the game prints a timing breakdown every 5 seconds.

  FPSLOG fps F, steps/s S | ms per frame, wall (main thread cpu): logic a (b) = c per step,
         build d (e), submit f (g), swap h (i) | verts N | process cpu P%

  logic   the game's move() calls of the frame (1 to 5 steps)
  build   glClear and the game's draw(): the CPU work that fills the frame's vertex array
  submit  flushGL(): the glDrawArrays call (the driver takes the array)
  swap    SDL_GL_SwapWindow (waits for vsync and for the GPU when it is behind)
  wall against cpu: a phase whose wall time is far above its cpu time was waiting (GPU, vsync
  or another thread holding the cores); process cpu above 100% means other threads of the game
  (driver, audio) are busy as well.
(The garbage collector is not logged: on the PC it ran 3 times in 6,000 frames, 4 ms in all.)
Usage: fpslog_patch.py <path to sources dir of a build copy>"""
import sys

root = sys.argv[1]


def patch(path, reps):
    p = root + path
    s = open(p, newline='').read()
    for old, new in reps:
        assert s.count(old) == 1, (path, old)
        s = s.replace(old, new)
    open(p, 'w', newline='\n').write(s)
    print('fpslog patched', p)


CLOCK = """
// FPSLOG (test builds): wall clock and cpu clocks in milliseconds.
private import core.sys.posix.time : clock_gettime, timespec;
public double fpsClock(int id) {
  timespec ts;
  clock_gettime(id, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
public enum FPS_WALL = 1, FPS_PROCESS = 2, FPS_THREAD = 3;
"""

patch('/src/abagames/util/sdl/screen3d.d', [
    ("""/**
 * SDL screen handler(3D, OpenGL).
 */""", CLOCK + """
public __gshared double fpsSubmitWall = 0, fpsSubmitCpu = 0, fpsSwapWall = 0, fpsSwapCpu = 0;

/**
 * SDL screen handler(3D, OpenGL).
 */"""),
    ("""    flushGL();
    handleError();
    SDL_GL_SwapWindow(window);
""", """    double w0 = fpsClock(FPS_WALL), c0 = fpsClock(FPS_THREAD);
    flushGL();
    handleError();
    double w1 = fpsClock(FPS_WALL), c1 = fpsClock(FPS_THREAD);
    SDL_GL_SwapWindow(window);
    fpsSubmitWall += w1 - w0;
    fpsSubmitCpu += c1 - c0;
    fpsSwapWall += fpsClock(FPS_WALL) - w1;
    fpsSwapCpu += fpsClock(FPS_THREAD) - c1;
"""),
])

patch('/src/abagames/util/sdl/mainloop.d', [
    ("""private import bindbc.sdl;
""", """private import bindbc.sdl;
private import core.stdc.stdio;
private import abagames.util.sdl.screen3d;
private import abagames.util.sdl.gl : statVertices;
"""),
    ("""    screen.initSDL();
    initFirst();
    gameManager.start();
""", """    screen.initSDL();
    initFirst();
    gameManager.start();
    double fpsFrom = fpsClock(FPS_WALL), fpsProcFrom = fpsClock(FPS_PROCESS);
    long fpsDraws = 0, fpsSteps = 0, fpsVertsFrom = statVertices;
    double logicWall = 0, logicCpu = 0, buildWall = 0, buildCpu = 0;
"""),
    ("""      for (i = 0; i < frame; i++) {
	gameManager.move();
      }
      screen.clear();
      gameManager.draw();
      screen.flip();
""", """      double w0 = fpsClock(FPS_WALL), c0 = fpsClock(FPS_THREAD);
      for (i = 0; i < frame; i++) {
	gameManager.move();
      }
      double w1 = fpsClock(FPS_WALL), c1 = fpsClock(FPS_THREAD);
      screen.clear();
      gameManager.draw();
      double w2 = fpsClock(FPS_WALL), c2 = fpsClock(FPS_THREAD);
      screen.flip();
      logicWall += w1 - w0;
      logicCpu += c1 - c0;
      buildWall += w2 - w1;
      buildCpu += c2 - c1;
      fpsSteps += frame;
      fpsDraws++;
      double fpsNow = fpsClock(FPS_WALL);
      if (fpsNow - fpsFrom >= 5000) {
        double sec = (fpsNow - fpsFrom) / 1000;
        double n = fpsDraws;
        printf("FPSLOG fps %.1f, steps/s %.1f | ms per frame, wall (cpu): logic %.1f (%.1f) = %.2f per step, build %.1f (%.1f), submit %.1f (%.1f), swap %.1f (%.1f) | verts %d | process cpu %.0f%%\\n",
               n / sec, fpsSteps / sec, logicWall / n, logicCpu / n, logicWall / fpsSteps,
               buildWall / n, buildCpu / n, fpsSubmitWall / n, fpsSubmitCpu / n,
               fpsSwapWall / n, fpsSwapCpu / n, cast(int) ((statVertices - fpsVertsFrom) / fpsDraws),
               (fpsClock(FPS_PROCESS) - fpsProcFrom) / (fpsNow - fpsFrom) * 100);
        fflush(stdout);
        fpsFrom = fpsNow;
        fpsProcFrom = fpsClock(FPS_PROCESS);
        fpsVertsFrom = statVertices;
        fpsDraws = fpsSteps = 0;
        logicWall = logicCpu = buildWall = buildCpu = 0;
        fpsSubmitWall = fpsSubmitCpu = fpsSwapWall = fpsSwapCpu = 0;
      }
"""),
])
