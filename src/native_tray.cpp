#include "native_tray.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>

using namespace godot;

#ifdef _WIN32
const UINT WM_TRAYICON = WM_USER + 1;
const UINT MENU_ID_OFFSET = 1000;
#endif

void NativeTray::_bind_methods() {
	ClassDB::bind_method(D_METHOD("init_tray", "tooltip", "icon"), &NativeTray::init_tray);
	ClassDB::bind_method(D_METHOD("update_icon", "icon"), &NativeTray::update_icon);
	ClassDB::bind_method(D_METHOD("update_tooltip", "tooltip"), &NativeTray::update_tooltip);
	ClassDB::bind_method(D_METHOD("remove_tray"), &NativeTray::remove_tray);

	ClassDB::bind_method(D_METHOD("hide_window"), &NativeTray::hide_window);
	ClassDB::bind_method(D_METHOD("show_window"), &NativeTray::show_window);

	ClassDB::bind_method(D_METHOD("clear_menu"), &NativeTray::clear_menu);
	ClassDB::bind_method(D_METHOD("add_menu_item", "id", "text", "disabled", "checked"), &NativeTray::add_menu_item, DEFVAL(false), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_menu_separator"), &NativeTray::add_menu_separator);

	ClassDB::bind_method(D_METHOD("set_custom_menu_window", "node"), &NativeTray::set_custom_menu_window);
	ClassDB::bind_method(D_METHOD("get_custom_menu_window"), &NativeTray::get_custom_menu_window);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_menu_window", PROPERTY_HINT_NODE_TYPE, "Window"), "set_custom_menu_window", "get_custom_menu_window");

	ADD_SIGNAL(MethodInfo("on_tray_left_clicked"));
	ADD_SIGNAL(MethodInfo("on_tray_right_clicked"));
	ADD_SIGNAL(MethodInfo("on_tray_double_clicked"));
	ADD_SIGNAL(MethodInfo("on_menu_item_selected", PropertyInfo(Variant::INT, "id")));
}

NativeTray::NativeTray() {
}

NativeTray::~NativeTray() {
#ifdef _WIN32
	remove_tray();
	if (hMenu) DestroyMenu(hMenu);
	if (hwnd) DestroyWindow(hwnd);
#endif
}

void NativeTray::init_tray(const String &p_tooltip, const Ref<Image> &p_icon) {
#ifdef _WIN32
	if (hwnd) return; // already initialized

	HINSTANCE hInstance = GetModuleHandle(nullptr);

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = NativeTray::WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"GodotNativeTrayMessageWindow";
	RegisterClassExW(&wc);

	hwnd = CreateWindowExW(0, L"GodotNativeTrayMessageWindow", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
	if (!hwnd) return;

	SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	hMenu = CreatePopupMenu();

	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;

	current_icon = create_icon_from_image(p_icon);
	nid.hIcon = current_icon;

	Char16String utf16 = p_tooltip.utf16();
	int len = utf16.length();
	if (len > 127) len = 127;
	memcpy(nid.szTip, utf16.get_data(), len * sizeof(wchar_t));
	nid.szTip[len] = 0;

	Shell_NotifyIconW(NIM_ADD, &nid);
#endif
}

void NativeTray::update_icon(const Ref<Image> &p_icon) {
#ifdef _WIN32
	if (!hwnd) return;
	
	HICON new_icon = create_icon_from_image(p_icon);
	if (new_icon) {
		if (current_icon) DestroyIcon(current_icon);
		current_icon = new_icon;
		nid.hIcon = current_icon;
		Shell_NotifyIconW(NIM_MODIFY, &nid);
	}
#endif
}

void NativeTray::update_tooltip(const String &p_tooltip) {
#ifdef _WIN32
	if (!hwnd) return;
	
	Char16String utf16 = p_tooltip.utf16();
	int len = utf16.length();
	if (len > 127) len = 127;
	memcpy(nid.szTip, utf16.get_data(), len * sizeof(wchar_t));
	nid.szTip[len] = 0;
	
	nid.uFlags = NIF_TIP;
	Shell_NotifyIconW(NIM_MODIFY, &nid);
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
#endif
}

void NativeTray::remove_tray() {
#ifdef _WIN32
	if (!hwnd) return;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	if (current_icon) {
		DestroyIcon(current_icon);
		current_icon = nullptr;
	}
#endif
}

void NativeTray::hide_window() {
#ifdef _WIN32
	if (Engine::get_singleton()->is_editor_hint()) return;
	int64_t handle = DisplayServer::get_singleton()->window_get_native_handle(DisplayServer::WINDOW_HANDLE, 0);
	if (HWND godot_hwnd = reinterpret_cast<HWND>(handle)) {
		ShowWindow(godot_hwnd, SW_HIDE);
	}
#endif
}

void NativeTray::show_window() {
#ifdef _WIN32
	if (Engine::get_singleton()->is_editor_hint()) return;
	int64_t handle = DisplayServer::get_singleton()->window_get_native_handle(DisplayServer::WINDOW_HANDLE, 0);
	if (HWND godot_hwnd = reinterpret_cast<HWND>(handle)) {
		ShowWindow(godot_hwnd, SW_SHOW);
		SetForegroundWindow(godot_hwnd);
	}
#endif
}

void NativeTray::clear_menu() {
#ifdef _WIN32
	if (!hMenu) return;
	while (GetMenuItemCount(hMenu) > 0) {
		RemoveMenu(hMenu, 0, MF_BYPOSITION);
	}
#endif
}

void NativeTray::add_menu_item(int p_id, const String &p_text, bool p_disabled, bool p_checked) {
#ifdef _WIN32
	if (!hMenu) return;
	UINT flags = MF_STRING;
	if (p_disabled) flags |= MF_DISABLED | MF_GRAYED;
	if (p_checked) flags |= MF_CHECKED;
	AppendMenuW(hMenu, flags, MENU_ID_OFFSET + p_id, (LPCWSTR)p_text.utf16().get_data());
#endif
}

void NativeTray::add_menu_separator() {
#ifdef _WIN32
	if (!hMenu) return;
	AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
#endif
}

void NativeTray::set_custom_menu_window(Window *p_node) {
	if (p_node) {
		custom_menu_instance_id = p_node->get_instance_id();
	} else {
		custom_menu_instance_id = ObjectID();
	}
}

Window *NativeTray::get_custom_menu_window() const {
	if (custom_menu_instance_id.is_valid()) {
		return Object::cast_to<Window>(ObjectDB::get_instance(custom_menu_instance_id));
	}
	return nullptr;
}

#ifdef _WIN32
LRESULT CALLBACK NativeTray::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (NativeTray *pSelf = reinterpret_cast<NativeTray*>(GetWindowLongPtr(hwnd, GWLP_USERDATA))) {
		if (msg == WM_TRAYICON) {
			pSelf->process_tray_message(wParam, lParam);
			return 0;
		} else if (msg == WM_COMMAND) {
			pSelf->process_menu_command(LOWORD(wParam));
			return 0;
		}
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void NativeTray::process_tray_message(WPARAM wParam, LPARAM lParam) {
	switch (LOWORD(lParam)) {
		case WM_LBUTTONUP:
			emit_signal("on_tray_left_clicked");
			break;
		case WM_RBUTTONUP: {
			emit_signal("on_tray_right_clicked");
			
			bool handled_by_custom = false;
			if (custom_menu_instance_id.is_valid()) {
				if (Window *win = Object::cast_to<Window>(ObjectDB::get_instance(custom_menu_instance_id))) {
					POINT pt;
					GetCursorPos(&pt);
					
					// prevent the menu from going off-screen
					Vector2i size = win->get_size();
					int x = pt.x;
					int y = pt.y;

					HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
					MONITORINFO mi = { sizeof(mi) };
					if (GetMonitorInfo(hMonitor, &mi)) {
						if (x + size.x > mi.rcWork.right) x = pt.x - size.x;
						if (y + size.y > mi.rcWork.bottom) y = pt.y - size.y;
					}

					win->set_position(Vector2i(x, y));
					win->set_visible(true);
					
					int32_t window_id = win->get_window_id();
					if (window_id != DisplayServer::INVALID_WINDOW_ID) {
						if (int64_t handle = DisplayServer::get_singleton()->window_get_native_handle(DisplayServer::WINDOW_HANDLE, window_id)) {
							HWND hw = reinterpret_cast<HWND>(handle);
							// force the window to be topmost to render over the taskbar.
							// this bypasses godot's transient error when 'always on top' is used.
							SetWindowPos(hw, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
							SetForegroundWindow(hw);
						}
					}
					handled_by_custom = true;
				}
			}
			
			if (!handled_by_custom && hMenu && GetMenuItemCount(hMenu) > 0) {
				POINT pt;
				GetCursorPos(&pt);
				SetForegroundWindow(hwnd);
				TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
				PostMessage(hwnd, WM_NULL, 0, 0);
			}
			break;
		}
		case WM_LBUTTONDBLCLK:
			emit_signal("on_tray_double_clicked");
			break;
	}
}

void NativeTray::process_menu_command(int command_id) {
	if (command_id >= MENU_ID_OFFSET) {
		int actual_id = command_id - MENU_ID_OFFSET;
		emit_signal("on_menu_item_selected", actual_id);
	}
}

HICON NativeTray::create_icon_from_image(const Ref<Image> &p_image) {
	if (p_image.is_null() || p_image->is_empty()) return nullptr;

	Ref<Image> img = p_image->duplicate();
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}

	int w = img->get_width();
	int h = img->get_height();

	BITMAPV5HEADER bi = {};
	bi.bV5Size = sizeof(bi);
	bi.bV5Width = w;
	bi.bV5Height = -h; // Top-down
	bi.bV5Planes = 1;
	bi.bV5BitCount = 32;
	bi.bV5Compression = BI_BITFIELDS;
	bi.bV5RedMask = 0x00FF0000;
	bi.bV5GreenMask = 0x0000FF00;
	bi.bV5BlueMask = 0x000000FF;
	bi.bV5AlphaMask = 0xFF000000;

	HDC hdc = GetDC(nullptr);
	void *lpBits;
	HBITMAP hBitmap = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &lpBits, nullptr, 0);
	ReleaseDC(nullptr, hdc);

	if (!hBitmap) return nullptr;

	PackedByteArray data = img->get_data();
	const uint8_t *src = data.ptr();
	uint32_t *dst = reinterpret_cast<uint32_t*>(lpBits);

	for (int i = 0; i < w * h; i++, src += 4) {
		dst[i] = (src[3] << 24) | (src[0] << 16) | (src[1] << 8) | src[2];
	}

	HBITMAP hMonoBitmap = CreateBitmap(w, h, 1, 1, nullptr);

	ICONINFO ii = {};
	ii.fIcon = TRUE;
	ii.hbmMask = hMonoBitmap;
	ii.hbmColor = hBitmap;

	HICON hIcon = CreateIconIndirect(&ii);

	DeleteObject(hBitmap);
	DeleteObject(hMonoBitmap);

	return hIcon;
}
#endif
