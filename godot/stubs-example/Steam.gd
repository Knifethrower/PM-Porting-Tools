extends Node
# Compile-time stand-in for the GodotSteam singleton; the game only reaches Steam
# through PlatformFacadeSteam, which is not used once the "steam" feature is gone.

const OVERLAY_TO_STORE_FLAG_NONE = 0


func setItemPreview(_a = null, _b = null) -> bool:
	return false
