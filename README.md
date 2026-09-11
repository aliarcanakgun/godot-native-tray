# godot-native-tray

A GDExtension for Godot 4.5+ that provides native System Tray integration on Windows. It allows your Godot application to display an icon in the taskbar notification area, create context menus, and handle background clicks—perfect for background utilities, system tools, and background overlays.

Currently, it only supports **Windows**.

## Features

- **System Tray Icon:** Add your app to the Windows system tray with a custom icon and tooltip.
- **Click Detection:** Detect left, right, and double clicks on the tray icon.
- **Native Context Menu:** Built-in standard Windows context menu with separators, disabled states, and checked states.
- **Custom Godot Window Menu:** Ability to set a custom Godot `Window` node to act as the menu (bypassing the standard Win32 menu).
- **Window Management:** Hide and show the main Godot window programmatically.
- **High Performance & Stability:** Designed with native Win32 API for low latency and high reliability.

## Installation

### Method 1: Pre-built Releases (Recommended)

1. Go to the [Releases](https://github.com/aliarcanakgun/godot-native-tray/releases) page.
2. Download the latest release `.zip` archive.
3. Extract the contents directly into your Godot project's folder. It should create an `addons/godot-native-tray/` structure.
4. Reload your Godot project. The extension will automatically load and be ready to use.

### Method 2: Building from Source

If you prefer to compile the extension yourself:

1. Clone this repository and initialize submodules:
   ```bash
   git clone https://github.com/aliarcanakgun/godot-native-tray.git
   cd godot-native-tray
   git submodule update --init --recursive
   ```

2. Make sure you have [SCons](https://scons.org/) and a modern C++ compiler (like MSVC on Windows) installed.

3. Compile the extension for your desired target:
   ```bash
   scons target=template_debug
   scons target=template_release
   ```

4. The compiled extension libraries will be naturally generated under the `demo/addons/godot-native-tray/<platform>/` folder. You can safely copy the entire `addons/godot-native-tray` folder to your Godot project.

## Usage

The extension provides a `NativeTray` singleton that you can access from any GDScript file.

### Initializing the Tray

**Add the icon to the system tray with a tooltip:**
```gdscript
# Load your icon
var icon = preload("res://icon.svg")

# Initialize the tray
NativeTray.init_tray("My Godot App", icon)
```

### Updating Tray State

**Update the icon or tooltip dynamically:**
```gdscript
var new_icon = preload("res://new_icon.png")
NativeTray.update_icon(new_icon)
NativeTray.update_tooltip("Status: Connected")
```

### Handling Clicks

**Connect to tray click signals:**
```gdscript
func _ready():
    NativeTray.on_tray_left_clicked.connect(_on_left_click)
    NativeTray.on_tray_right_clicked.connect(_on_right_click)
    NativeTray.on_tray_double_clicked.connect(_on_double_click)

func _on_left_click():
    print("Tray left clicked")
    
func _on_right_click():
    print("Tray right clicked")
    
func _on_double_click():
    print("Tray double clicked")
```

### Native Context Menu

**Add a native right-click menu to the tray:**
```gdscript
func _ready():
    # ID, text, is_disabled, is_checked
    NativeTray.add_menu_item(1, "Open Settings", false, false)
    NativeTray.add_menu_separator()
    NativeTray.add_menu_item(2, "Connected", true, true)
    NativeTray.add_menu_separator()
    NativeTray.add_menu_item(3, "Exit", false, false)
    
    NativeTray.on_menu_item_selected.connect(_on_menu_item_selected)

func _on_menu_item_selected(id: int):
    match id:
        1: print("Settings clicked")
        3: get_tree().quit()
```

### Custom Godot Window Menu

**Use a custom Godot Window node instead of the native menu:**
```gdscript
@onready var my_custom_menu_window: Window = $MyCustomWindow

func _ready():
    # When assigned, the plugin will automatically position and show this window 
    # perfectly above the taskbar when the tray icon is right-clicked.
    NativeTray.set_custom_menu_window(my_custom_menu_window)
```

### Hiding and Showing the Game

**Hide the game window to run completely in the background:**
```gdscript
# Hides the Godot window from the taskbar and desktop
NativeTray.hide_window()

# Brings the window back to the front
NativeTray.show_window()
```

### Cleanup

**Remove the tray icon when your game closes:**
```gdscript
func _notification(what):
    if what == NOTIFICATION_WM_CLOSE_REQUEST:
        NativeTray.remove_tray()
```
