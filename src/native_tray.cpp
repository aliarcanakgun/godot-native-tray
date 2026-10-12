#include "native_tray.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

#ifdef _WIN32
const UINT WM_TRAYICON = WM_USER + 1;
const UINT MENU_ID_OFFSET = 1000;
UINT NativeTray::wm_taskbar_created = 0;

#ifndef MSGFLT_ALLOW
#define MSGFLT_ALLOW 1
#endif

#pragma pack(push, 1)
struct IcoHeader {
	uint16_t reserved;
	uint16_t type;
	uint16_t count;
};

struct IcoDirEntry {
	uint8_t width;
	uint8_t height;
	uint8_t color_count;
	uint8_t reserved;
	uint16_t planes;
	uint16_t bit_count;
	uint32_t bytes_in_res;
	uint32_t image_offset;
};
#pragma pack(pop)
#endif

void NativeTray::_bind_methods() {
	ClassDB::bind_method(D_METHOD("init_tray", "tooltip", "icon"), &NativeTray::init_tray);
	ClassDB::bind_method(D_METHOD("init_tray_ico", "tooltip", "path"), &NativeTray::init_tray_ico);
	ClassDB::bind_method(D_METHOD("update_icon", "icon"), &NativeTray::update_icon);
	ClassDB::bind_method(D_METHOD("update_icon_ico", "path"), &NativeTray::update_icon_ico);
	ClassDB::bind_method(D_METHOD("update_tooltip", "tooltip"), &NativeTray::update_tooltip);
	ClassDB::bind_method(D_METHOD("remove_tray"), &NativeTray::remove_tray);
	ClassDB::bind_method(D_METHOD("recreate_tray"), &NativeTray::recreate_tray);
	ClassDB::bind_method(D_METHOD("is_tray_active"), &NativeTray::is_tray_active);

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
	ADD_SIGNAL(MethodInfo("on_tray_recreated"));
}

NativeTray::NativeTray() {
}

NativeTray::~NativeTray() {
#ifdef _WIN32
	remove_tray();
	if (hMenu) {
		DestroyMenu(hMenu);
		hMenu = nullptr;
	}
	if (hwnd) {
		DestroyWindow(hwnd);
		hwnd = nullptr;
		UnregisterClassW(L"GodotNativeTrayMessageWindow", GetModuleHandle(nullptr));
	}
#endif
}

#ifdef _WIN32
void NativeTray::set_tooltip_internal(const String &p_tooltip) {
	Char16String utf16 = p_tooltip.utf16();
	const wchar_t *src = reinterpret_cast<const wchar_t *>(utf16.get_data());
	size_t max_chars = (sizeof(nid.szTip) / sizeof(nid.szTip[0])) - 1;
	if (src) {
		wcsncpy_s(nid.szTip, src, max_chars);
	} else {
		nid.szTip[0] = L'\0';
	}
	nid.szTip[max_chars] = L'\0';
}
#endif

void NativeTray::init_tray(const String &p_tooltip, const Variant &p_icon) {
#ifdef _WIN32
	HINSTANCE hInstance = GetModuleHandle(nullptr);

	if (wm_taskbar_created == 0) {
		wm_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
	}

	const wchar_t *CLASS_NAME = L"GodotNativeTrayMessageWindow";

	if (!hwnd) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = NativeTray::WndProc;
		wc.hInstance = hInstance;
		wc.lpszClassName = CLASS_NAME;
		RegisterClassExW(&wc);

		// create hidden top-level window
		// message-only windows dont receive broadcast messages
		hwnd = CreateWindowExW(
			WS_EX_TOOLWINDOW,
			CLASS_NAME,
			L"",
			WS_POPUP,
			0, 0, 0, 0,
			nullptr,
			nullptr,
			hInstance,
			nullptr
		);
		if (!hwnd) return;

		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		// allow 'TaskbarCreated' to bypass uipi
		// in case godot is running as admin
		if (wm_taskbar_created != 0) {
			ChangeWindowMessageFilterEx(hwnd, wm_taskbar_created, MSGFLT_ALLOW, nullptr);
		}

		if (!hMenu) {
			hMenu = CreatePopupMenu();
		}
	}

	if (current_icon) {
		DestroyIcon(current_icon);
		current_icon = nullptr;
	}

	current_icon = create_icon_from_variant(p_icon);

	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = current_icon;

	set_tooltip_internal(p_tooltip);

	if (Shell_NotifyIconW(NIM_ADD, &nid)) {
		is_active = true;
	} else {
		if (Shell_NotifyIconW(NIM_MODIFY, &nid)) {
			is_active = true;
		}
	}
#endif
}

void NativeTray::init_tray_ico(const String &p_tooltip, const String &p_path) {
	init_tray(p_tooltip, p_path);
}

void NativeTray::update_icon(const Variant &p_icon) {
#ifdef _WIN32
	if (!hwnd || !is_active) return;
	
	HICON new_icon = create_icon_from_variant(p_icon);
	if (new_icon) {
		if (current_icon) DestroyIcon(current_icon);
		current_icon = new_icon;
		nid.hIcon = current_icon;

		NOTIFYICONDATAW icon_nid = {};
		icon_nid.cbSize = sizeof(icon_nid);
		icon_nid.hWnd = hwnd;
		icon_nid.uID = nid.uID;
		icon_nid.uFlags = NIF_ICON;
		icon_nid.hIcon = current_icon;
		Shell_NotifyIconW(NIM_MODIFY, &icon_nid);
	}
#endif
}

void NativeTray::update_icon_ico(const String &p_path) {
	update_icon(p_path);
}

void NativeTray::update_tooltip(const String &p_tooltip) {
#ifdef _WIN32
	if (!hwnd || !is_active) return;
	
	set_tooltip_internal(p_tooltip);

	NOTIFYICONDATAW tip_nid = {};
	tip_nid.cbSize = sizeof(tip_nid);
	tip_nid.hWnd = hwnd;
	tip_nid.uID = nid.uID;
	tip_nid.uFlags = NIF_TIP;
	wcsncpy_s(tip_nid.szTip, nid.szTip, _TRUNCATE);
	Shell_NotifyIconW(NIM_MODIFY, &tip_nid);
#endif
}

void NativeTray::remove_tray() {
#ifdef _WIN32
	if (!hwnd || !is_active) return;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	if (current_icon) {
		DestroyIcon(current_icon);
		current_icon = nullptr;
	}
	nid.hIcon = nullptr;
	is_active = false;
#endif
}

void NativeTray::recreate_tray() {
#ifdef _WIN32
	if (!is_active || !hwnd) return;

	if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
		Shell_NotifyIconW(NIM_MODIFY, &nid);
	}

	emit_signal("on_tray_recreated");
#endif
}

bool NativeTray::is_tray_active() const {
#ifdef _WIN32
	return is_active;
#else
	return false;
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
		if (IsIconic(godot_hwnd)) {
			ShowWindow(godot_hwnd, SW_RESTORE);
		} else {
			ShowWindow(godot_hwnd, SW_SHOW);
		}
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
	Char16String utf16 = p_text.utf16();
	AppendMenuW(hMenu, flags, MENU_ID_OFFSET + p_id, reinterpret_cast<LPCWSTR>(utf16.get_data()));
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
		} else if (msg != 0 && msg == wm_taskbar_created) {
			pSelf->recreate_tray();
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
						if (x < mi.rcWork.left) x = mi.rcWork.left;
						if (y < mi.rcWork.top) y = mi.rcWork.top;
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
	if (p_image.is_null() || p_image->is_empty()) {
		HICON default_icon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(1));
		if (!default_icon) {
			default_icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
		}
		if (default_icon) {
			return CopyIcon(default_icon);
		}
		return nullptr;
	}

	Ref<Image> img = p_image->duplicate();
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}

	int sm_cx = GetSystemMetrics(SM_CXSMICON);
	int sm_cy = GetSystemMetrics(SM_CYSMICON);
	if (sm_cx <= 0) sm_cx = 16;
	if (sm_cy <= 0) sm_cy = 16;

	if (img->get_width() > sm_cx || img->get_height() > sm_cy) {
		img->resize(sm_cx, sm_cy, Image::INTERPOLATE_LANCZOS);
	}

	int w = img->get_width();
	int h = img->get_height();

	BITMAPV5HEADER bi = {};
	bi.bV5Size = sizeof(bi);
	bi.bV5Width = w;
	bi.bV5Height = -h; // top down
	bi.bV5Planes = 1;
	bi.bV5BitCount = 32;
	bi.bV5Compression = BI_BITFIELDS;
	bi.bV5RedMask = 0x00FF0000;
	bi.bV5GreenMask = 0x0000FF00;
	bi.bV5BlueMask = 0x000000FF;
	bi.bV5AlphaMask = 0xFF000000;

	HDC hdc = GetDC(nullptr);
	void *lpBits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &lpBits, nullptr, 0);
	ReleaseDC(nullptr, hdc);

	if (!hBitmap || !lpBits) {
		if (hBitmap) DeleteObject(hBitmap);
		return nullptr;
	}

	PackedByteArray data = img->get_data();
	const uint8_t *src = data.ptr();
	uint32_t *dst = reinterpret_cast<uint32_t*>(lpBits);

	for (int i = 0; i < w * h; i++, src += 4) {
		dst[i] = (static_cast<uint32_t>(src[3]) << 24) |
		         (static_cast<uint32_t>(src[0]) << 16) |
		         (static_cast<uint32_t>(src[1]) << 8)  |
		          static_cast<uint32_t>(src[2]);
	}

	int mask_pitch = ((w + 15) / 16) * 2;
	PackedByteArray mask_bits;
	mask_bits.resize(mask_pitch * h);
	memset(mask_bits.ptrw(), 0, mask_pitch * h);
	HBITMAP hMonoBitmap = CreateBitmap(w, h, 1, 1, mask_bits.ptr());

	ICONINFO ii = {};
	ii.fIcon = TRUE;
	ii.hbmMask = hMonoBitmap;
	ii.hbmColor = hBitmap;

	HICON hIcon = CreateIconIndirect(&ii);

	DeleteObject(hBitmap);
	if (hMonoBitmap) DeleteObject(hMonoBitmap);

	return hIcon;
}

HICON NativeTray::create_icon_from_variant(const Variant &p_icon) {
	if (p_icon.get_type() == Variant::OBJECT) {
		Ref<Image> img = p_icon;
		if (img.is_valid()) {
			return create_icon_from_image(img);
		}
	} else if (p_icon.get_type() == Variant::STRING) {
		String path = p_icon;
		if (!path.is_empty()) {
			return create_icon_from_file(path);
		}
	}

	return create_icon_from_image(Ref<Image>());
}

HICON NativeTray::create_icon_from_file(const String &p_path) {
	if (p_path.is_empty()) return nullptr;

	String ext = p_path.get_extension().to_lower();
	if (ext != "ico") {
		String global_path = ProjectSettings::get_singleton()->globalize_path(p_path);
		Ref<Image> img = Image::load_from_file(global_path);
		if (img.is_valid() && !img->is_empty()) {
			return create_icon_from_image(img);
		}
	}

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_valid()) {
		uint64_t len = file->get_length();
		if (len > 0 && len < 16 * 1024 * 1024) {
			PackedByteArray buf = file->get_buffer(len);
			HICON hIcon = create_icon_from_ico_data(buf.ptr(), buf.size());
			if (hIcon) return hIcon;
		}
	}
	
	String global_path = ProjectSettings::get_singleton()->globalize_path(p_path);
	Char16String utf16 = global_path.utf16();
	int target_cx = GetSystemMetrics(SM_CXSMICON);
	int target_cy = GetSystemMetrics(SM_CYSMICON);
	if (target_cx <= 0) target_cx = 16;
	if (target_cy <= 0) target_cy = 16;

	HICON hIcon = reinterpret_cast<HICON>(LoadImageW(
		nullptr,
		reinterpret_cast<LPCWSTR>(utf16.get_data()),
		IMAGE_ICON,
		target_cx,
		target_cy,
		LR_LOADFROMFILE
	));
	return hIcon;
}

HICON NativeTray::create_icon_from_ico_data(const uint8_t *p_data, size_t p_size) {
	if (!p_data || p_size < sizeof(IcoHeader)) return nullptr;

	const IcoHeader *header = reinterpret_cast<const IcoHeader*>(p_data);
	if (header->reserved != 0 || header->type != 1 || header->count == 0) {
		return nullptr;
	}

	if (p_size < sizeof(IcoHeader) + header->count * sizeof(IcoDirEntry)) {
		return nullptr;
	}

	const IcoDirEntry *entries = reinterpret_cast<const IcoDirEntry*>(p_data + sizeof(IcoHeader));

	int target_cx = GetSystemMetrics(SM_CXSMICON);
	int target_cy = GetSystemMetrics(SM_CYSMICON);
	if (target_cx <= 0) target_cx = 16;
	if (target_cy <= 0) target_cy = 16;

	int best_index = -1;
	int best_score = 100000;

	for (int i = 0; i < header->count; i++) {
		int w = entries[i].width == 0 ? 256 : entries[i].width;
		int h = entries[i].height == 0 ? 256 : entries[i].height;

		if (w == target_cx && h == target_cy) {
			best_index = i;
			break;
		}

		int score = 0;
		if (w >= target_cx && h >= target_cy) {
			score = (w - target_cx) + (h - target_cy);
		} else {
			score = 1000 + (target_cx - w) + (target_cy - h);
		}

		if (score < best_score) {
			best_score = score;
			best_index = i;
		}
	}

	if (best_index < 0) return nullptr;

	const IcoDirEntry &best = entries[best_index];
	if (best.image_offset + best.bytes_in_res > p_size) {
		return nullptr;
	}

	const uint8_t *icon_bytes = p_data + best.image_offset;
	return CreateIconFromResourceEx(
		const_cast<PBYTE>(icon_bytes),
		best.bytes_in_res,
		TRUE,
		0x00030000,
		target_cx,
		target_cy,
		LR_DEFAULTCOLOR
	);
}
#endif
