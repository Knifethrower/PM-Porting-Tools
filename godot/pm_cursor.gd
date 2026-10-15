extends CanvasLayer
# pm_cursor.gd: software mouse pointer for Godot 3 games on PortMaster (KMSDRM has no hardware
# cursor, so frt shows none). Added as an autoload by export_pck3.sh when PM_CURSOR=1: draws an
# arrow at the mouse position on top of everything; hidden while the mouse is captured/hidden and
# after 3 s without mouse movement (so it does not sit over the game while playing with the pad).
# License: 0BSD.

const IDLE_HIDE = 3.0

var _pointer := Node2D.new()
var _last := Vector2(-1, -1)
var _idle := IDLE_HIDE

func _ready():
	layer = 128
	pause_mode = Node.PAUSE_MODE_PROCESS
	_pointer.set_script(preload("res://pm_cursor_draw.gd"))
	add_child(_pointer)

func _process(delta):
	var pos = get_viewport().get_mouse_position()
	if pos != _last:
		_last = pos
		_idle = 0.0
	else:
		_idle += delta
	_pointer.position = pos
	var mode = Input.get_mouse_mode()
	_pointer.visible = _idle < IDLE_HIDE and (mode == Input.MOUSE_MODE_VISIBLE or mode == Input.MOUSE_MODE_CONFINED)
