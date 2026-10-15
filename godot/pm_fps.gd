# pm_fps.gd: test-build autoload (export_pck3.sh PM_FPS=1) that prints the frame rate to stderr
# every 2 s, since frt ignores --print-fps. Not for releases. License: 0BSD.
extends Node

var _t := 0.0

func _process(delta: float) -> void:
	_t += delta
	if _t >= 2.0:
		_t = 0.0
		printerr("PMFPS %d" % Engine.get_frames_per_second())
