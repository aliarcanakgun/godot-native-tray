#ifndef NATIVE_TRAY_H
#define NATIVE_TRAY_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2i.hpp>
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
	bool window_hidden_by_tray = false;
	bool was_maximized_before_hide = false;
	bool system_resumed_while_hidden = false;
	ULONGLONG hidden_timestamp_ms = 0;
	Variant cached_icon_source;

	static UINT wm_taskbar_created;

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static DWORD WINAPI PrefetchWorkerThreadProc(LPVOID lpParameter);

	void process_tray_message(WPARAM wParam, LPARAM lParam);
	void process_menu_command(int command_id);
	void process_power_broadcast(WPARAM wParam, LPARAM lParam);

	void restore_process_qos_and_priority();
	void prefetch_process_working_set(bool p_async = false);
	void force_swapchain_and_dwm_refresh(HWND p_godot_hwnd);
	void force_foreground_window(HWND p_godot_hwnd);
	void _deferred_post_show_refresh();

	HICON create_icon_from_variant(const Variant &p_icon);
	HICON create_icon_from_image(const Ref<Image> &p_image);
	HICON create_icon_from_file(const String &p_path);
	HICON create_icon_from_ico_data(const uint8_t *p_data, size_t p_size);
	void set_tooltip_internal(const String &p_tooltip);
	void cleanup_tray_resources();
#endif

	bool shutdown_hook_bound = false;
	ObjectID custom_menu_instance_id;

	void disconnect_all_signals();
	void _bind_shutdown_cleanup();
	void _on_engine_shutdown();

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
	Rect2i get_tray_rect() const;

	void hide_window();
	void show_window();
	bool is_window_hidden() const;

	void clear_menu();
	void add_menu_item(int p_id, const String &p_text, bool p_disabled = false, bool p_checked = false);
	void add_menu_separator();

	void set_custom_menu_window(Window *p_node);
	Window *get_custom_menu_window() const;
};

} // namespace godot

#endif // NATIVE_TRAY_H
