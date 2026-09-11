#ifndef NATIVE_TRAY_H
#define NATIVE_TRAY_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

class NativeTray : public Object {
	GDCLASS(NativeTray, Object)

private:
#ifdef _WIN32
	HWND hwnd = NULL;
	HMENU hMenu = NULL;
	NOTIFYICONDATAW nid = {};
	HICON current_icon = NULL;

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void process_tray_message(WPARAM wParam, LPARAM lParam);
	void process_menu_command(int command_id);
	
	HICON create_icon_from_image(const Ref<Image> &p_image);
#endif

protected:
	static void _bind_methods();

public:
	NativeTray();
	~NativeTray();

	void init_tray(const String &p_tooltip, const Ref<Image> &p_icon);
	void update_icon(const Ref<Image> &p_icon);
	void update_tooltip(const String &p_tooltip);
	void remove_tray();
	
	void hide_window();
	void show_window();
	
	void clear_menu();
	void add_menu_item(int p_id, const String &p_text, bool p_disabled = false, bool p_checked = false);
	void add_menu_separator();
};

} // namespace godot

#endif // NATIVE_TRAY_H
