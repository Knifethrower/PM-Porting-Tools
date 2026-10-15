extends AudioStream
# Stands in for one imported music track. The game preloads its whole soundtrack;
# this keeps only the path and loads the real stream when a player starts it, so the
# Ogg data lives exactly as long as a playback of it.

@export var real_path := ""
@export var length := 0.0  # music length is never queried by the game; left 0


func _instantiate_playback() -> AudioStreamPlayback:
	var real: AudioStream = load(real_path)
	if real == null:
		push_error("LazyAudioStream: cannot load " + real_path)
		return null
	return real.instantiate_playback()


func _get_stream_name() -> String:
	return real_path.get_file()


func _get_length() -> float:
	return length


func _is_monophonic() -> bool:
	return false
