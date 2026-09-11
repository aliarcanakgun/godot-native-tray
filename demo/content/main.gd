extends Control

var tray_icon_image: Image

func _ready() -> void:
	# load tray icon
	tray_icon_image = preload("res://icon.svg").get_image()
	
	# init tray
	NativeTray.init_tray("Godot Native Tray Demo", tray_icon_image)
	
	# bind signals
	NativeTray.on_tray_left_clicked.connect(_on_tray_left_clicked)
	NativeTray.on_tray_right_clicked.connect(_on_tray_right_clicked)
	NativeTray.on_tray_double_clicked.connect(_on_tray_double_clicked)
	NativeTray.on_menu_item_selected.connect(_on_menu_item_selected)
	
	# to use a custom godot window/popup instead of native win32 menu:
	#NativeTray.custom_menu_window = $custom_rmb_menu
	
	# populate native win32 menu (will be ignored if custom menu is set)
	NativeTray.add_menu_item(1, "Show Window")
	NativeTray.add_menu_item(2, "Hide Window")
	NativeTray.add_menu_separator()
	NativeTray.add_menu_item(3, "Exit")
	
	# override close behavior
	get_tree().auto_accept_quit = false

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		print("Close request received, hiding to tray instead of quitting.")
		NativeTray.hide_window()

func _on_tray_left_clicked() -> void:
	print("Tray Left Clicked!")
	NativeTray.show_window()

func _on_tray_right_clicked() -> void:
	print("Tray Right Clicked!")

func _on_tray_double_clicked() -> void:
	print("Tray Double Clicked!")
	NativeTray.show_window()

func _on_menu_item_selected(id: int) -> void:
	print("Menu Item Selected: ", id)
	if id == 1:
		NativeTray.show_window()
	elif id == 2:
		NativeTray.hide_window()
	elif id == 3:
		NativeTray.remove_tray()
		get_tree().quit()
