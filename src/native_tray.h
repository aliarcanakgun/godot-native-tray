#ifndef NATIVE_TRAY_H
#define NATIVE_TRAY_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot {

class NativeTray : public Object {
	GDCLASS(NativeTray, Object)

private:
#ifdef _WIN32
	HWND hwnd = nullptr;
	HMENU hMenu = nullptr;
	NOTIFYICONDATAW nid = {};
	HICON current_icon = nullptr;
	bool is_active = false;

	static UINT wm_taskbar_created;

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void process_tray_message(WPARAM wParam, LPARAM lParam);
	void process_menu_command(int command_id);
	
	HICON create_icon_from_variant(const Variant &p_icon);
	HICON create_icon_from_image(const Ref<Image> &p_image);
	HICON create_icon_from_file(const String &p_path);
	HICON create_icon_from_ico_data(const uint8_t *p_data, size_t p_size);
	void set_tooltip_internal(const String &p_tooltip);
#endif

	ObjectID custom_menu_instance_id;

protected:
	static void _bind_methods();

public:
	NativeTray();
	~NativeTray();

	void init_tray(const String &p_tooltip, const Variant &p_icon);
	void init_tray_ico(const String &p_tooltip, const String &p_path);
	void update_icon(const Variant &p_icon);
	void update_icon_ico(const String &p_path);
	void update_tooltip(const String &p_tooltip);
	void remove_tray();
	void recreate_tray();
	bool is_tray_active() const;
	
	void hide_window();
	void show_window();
	
	void clear_menu();
	void add_menu_item(int p_id, const String &p_text, bool p_disabled = false, bool p_checked = false);
	void add_menu_separator();

	void set_custom_menu_window(Window *p_node);
	Window *get_custom_menu_window() const;
};

} // namespace godot

#endif // NATIVE_TRAY_H
