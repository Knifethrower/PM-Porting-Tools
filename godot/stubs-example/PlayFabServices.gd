extends Node
# Offline stand-in for the PlayFabServices engine singleton of Dome Keeper's custom
# Godot build. Never initializes, never emits; every query returns an empty value.

enum AccessPolicy { LOBBY_PUBLIC, LOBBY_FRIENDS, LOBBY_PRIVATE }

signal logged_in
signal logged
signal login_failed
signal lobby_created
signal lobbies_found
signal find_lobbies_failed
signal join_lobby_failed
signal local_endpoint_disconnected
signal cloud_script_response
signal leaderboard_loaded
signal leaderboard_around_entity_loaded
signal load_leaderboard_failed
signal score_submitted
signal score_submission_failed


func is_initialized() -> bool:
	return false


func is_initializing() -> bool:
	return false


func get_entity_id(_a = null, _b = null) -> String:
	return ""


func get_entity_token(_a = null) -> String:
	return ""


func get_entity_type(_a = null) -> String:
	return ""


func get_platform_user_token(_a = null) -> String:
	return ""


func get_lobby_search_results(_a = null, _b = null) -> Dictionary:
	return {}


func set_port(_a = null) -> void:
	pass


func login_with_custom_id(_a = null, _b = null, _c = null) -> void:
	pass


func login_with_steam(_a = null, _b = null, _c = null) -> void:
	pass


func find_lobbies(_a = null, _b = null, _c = null) -> void:
	pass


func update_recent_player(_a = null, _b = null) -> void:
	pass


func execute_cloud_script(_a = null, _b = null, _c = null, _d = null) -> void:
	pass


func get_leaderboard(_a = null, _b = null, _c = null, _d = null, _e = null) -> void:
	pass


func get_leaderboard_around(_a = null, _b = null, _c = null, _d = null, _e = null) -> void:
	pass


func submit_score(_a = null, _b = null, _c = null, _d = null, _e = null) -> void:
	pass
