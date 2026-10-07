extends Node2D
# The arrow drawn by pm_cursor.gd, in the game's own (stretched) coordinates: about 1/20 of the
# visible height, whatever the base resolution. License: 0BSD.

func _draw():
	var s = max(1.0, get_viewport_rect().size.y / 240.0)
	var outline = PoolVector2Array([Vector2(-1, -2), Vector2(9, 8), Vector2(4, 9), Vector2(1, 13)])
	var fill = PoolVector2Array([Vector2(0, 0), Vector2(7, 7), Vector2(3.5, 7), Vector2(0.5, 11)])
	for i in outline.size():
		outline[i] *= s
	for i in fill.size():
		fill[i] *= s
	draw_colored_polygon(outline, Color(0, 0, 0))
	draw_colored_polygon(fill, Color(1, 1, 1))
