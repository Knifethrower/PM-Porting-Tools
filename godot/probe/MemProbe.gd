extends Node
# Test-only autoload: at fixed times, writes Godot's memory monitors and every imported
# asset that is currently loaded (ResourceLoader cache) with an estimated in-RAM size.

const DIR := "@DIR@"   # replaced by run_pck.sh (PROBE=1); assets.tsv comes from pck_assets.py
const TIMES := [45.0, 100.0]

var _t := 0.0
var _next := 0
var _assets := []


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var f := FileAccess.open(DIR + "/probe/assets.tsv", FileAccess.READ)
	while f and not f.eof_reached():
		var parts := f.get_line().split("\t")
		if parts.size() == 3:
			_assets.append(parts)


func _process(delta: float) -> void:
	_t += delta
	if _next < TIMES.size() and _t >= TIMES[_next]:
		_dump("%s/probe-%ds.tsv" % [DIR, int(TIMES[_next])])
		_next += 1


func _estimate(res: Resource) -> int:
	if res is AudioStreamWAV:
		return res.data.size()
	if res is AudioStreamOggVorbis and res.packet_sequence:
		var n := 0
		for p in res.packet_sequence.packet_data:
			for q in p:
				n += q.size()
		return n
	if res is Texture2D:
		return res.get_width() * res.get_height() * 4
	if res is FontFile:
		return res.data.size()
	return 0


func _dump(path: String) -> void:
	var out := FileAccess.open(path, FileAccess.WRITE)
	var mon := {
		"static_mem": Performance.MEMORY_STATIC,
		"static_mem_max": Performance.MEMORY_STATIC_MAX,
		"objects": Performance.OBJECT_COUNT,
		"resources": Performance.OBJECT_RESOURCE_COUNT,
		"nodes": Performance.OBJECT_NODE_COUNT,
		"texture_mem": Performance.RENDER_TEXTURE_MEM_USED,
		"buffer_mem": Performance.RENDER_BUFFER_MEM_USED,
		"video_mem": Performance.RENDER_VIDEO_MEM_USED,
	}
	for k in mon:
		out.store_line("#%s\t%d" % [k, Performance.get_monitor(mon[k])])
	out.store_line("#scene\t%s" % [get_tree().current_scene.scene_file_path if get_tree().current_scene else ""])
	for a in _assets:
		if ResourceLoader.has_cached(a[0]):
			var res: Resource = ResourceLoader.load(a[0])
			out.store_line("%s\t%s\t%s\t%d" % [a[0], a[1], a[2], _estimate(res)])
	out.close()
	print("MemProbe: wrote ", path)
