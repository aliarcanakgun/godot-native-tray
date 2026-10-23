#include "native_tray.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>

using namespace godot;

#ifdef _WIN32
const UINT WM_TRAYICON = WM_USER + 1;
const UINT MENU_ID_OFFSET = 1000;
UINT NativeTray::wm_taskbar_created = 0;

static volatile LONG g_prefetch_in_progress = 0;

#ifndef MSGFLT_ALLOW
#define MSGFLT_ALLOW 1
#endif

#ifndef DWMWA_TRANSITIONS_FORCEDISABLED
#define DWMWA_TRANSITIONS_FORCEDISABLED 3
#endif

#ifndef PBT_APMSUSPEND
#define PBT_APMSUSPEND 0x0004
#endif
#ifndef PBT_APMRESUMESUSPEND
#define PBT_APMRESUMESUSPEND 0x0007
#endif
#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif

namespace {

struct TrayMemoryPriorityInfo {
	ULONG MemoryPriority;
};

struct TrayProcessPowerThrottlingState {
	ULONG Version;
	ULONG ControlMask;
	ULONG StateMask;
};

struct TrayWin32MemoryRangeEntry {
	PVOID VirtualAddress;
	SIZE_T NumberOfBytes;
};

typedef BOOL(WINAPI *PFN_SetProcessInformation)(
	HANDLE hProcess,
	int ProcessInformationClass,
	LPVOID ProcessInformation,
	DWORD ProcessInformationSize
);

typedef BOOL(WINAPI *PFN_PrefetchVirtualMemory)(
	HANDLE hProcess,
	ULONG_PTR NumberOfEntries,
	TrayWin32MemoryRangeEntry *VirtualAddresses,
	ULONG Flags
);

typedef HRESULT(WINAPI *PFN_DwmSetWindowAttribute)(
	HWND hwnd,
	DWORD dwAttribute,
	LPCVOID pvAttribute,
	DWORD cbAttribute
);

typedef HRESULT(WINAPI *PFN_DwmFlush)(void);

typedef UINT(WINAPI *PFN_timeBeginPeriod)(UINT uPeriod);
typedef UINT(WINAPI *PFN_timeEndPeriod)(UINT uPeriod);

void set_dwm_transitions_disabled(HWND p_hwnd, BOOL p_disabled) {
	if (!p_hwnd) return;
	HMODULE hDwm = GetModuleHandleW(L"dwmapi.dll");
	if (!hDwm) {
		hDwm = LoadLibraryW(L"dwmapi.dll");
	}
	if (hDwm) {
		PFN_DwmSetWindowAttribute pfnSetAttr = reinterpret_cast<PFN_DwmSetWindowAttribute>(
			GetProcAddress(hDwm, "DwmSetWindowAttribute")
		);
		if (pfnSetAttr) {
			pfnSetAttr(p_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &p_disabled, sizeof(p_disabled));
		}
	}
}

void flush_dwm_composition() {
	HMODULE hDwm = GetModuleHandleW(L"dwmapi.dll");
	if (hDwm) {
		PFN_DwmFlush pfnFlush = reinterpret_cast<PFN_DwmFlush>(GetProcAddress(hDwm, "DwmFlush"));
		if (pfnFlush) {
			pfnFlush();
		}
	}
}

}

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
	ClassDB::bind_method(D_METHOD("get_tray_rect"), &NativeTray::get_tray_rect);

	ClassDB::bind_method(D_METHOD("hide_window"), &NativeTray::hide_window);
	ClassDB::bind_method(D_METHOD("show_window"), &NativeTray::show_window);
	ClassDB::bind_method(D_METHOD("is_window_hidden"), &NativeTray::is_window_hidden);

	ClassDB::bind_method(D_METHOD("clear_menu"), &NativeTray::clear_menu);
	ClassDB::bind_method(D_METHOD("add_menu_item", "id", "text", "disabled", "checked"), &NativeTray::add_menu_item, DEFVAL(false), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_menu_separator"), &NativeTray::add_menu_separator);

	ClassDB::bind_method(D_METHOD("set_custom_menu_window", "node"), &NativeTray::set_custom_menu_window);
	ClassDB::bind_method(D_METHOD("get_custom_menu_window"), &NativeTray::get_custom_menu_window);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_menu_window", PROPERTY_HINT_NODE_TYPE, "Window"), "set_custom_menu_window", "get_custom_menu_window");

#ifdef _WIN32
	ClassDB::bind_method(D_METHOD("_deferred_post_show_refresh"), &NativeTray::_deferred_post_show_refresh);
#endif
	ClassDB::bind_method(D_METHOD("_bind_shutdown_cleanup"), &NativeTray::_bind_shutdown_cleanup);
	ClassDB::bind_method(D_METHOD("_on_engine_shutdown"), &NativeTray::_on_engine_shutdown);

	ADD_SIGNAL(MethodInfo("on_tray_left_clicked"));
	ADD_SIGNAL(MethodInfo("on_tray_right_clicked"));
	ADD_SIGNAL(MethodInfo("on_tray_double_clicked"));
	ADD_SIGNAL(MethodInfo("on_menu_item_selected", PropertyInfo(Variant::INT, "id")));
	ADD_SIGNAL(MethodInfo("on_tray_recreated"));
	ADD_SIGNAL(MethodInfo("on_system_suspend"));
	ADD_SIGNAL(MethodInfo("on_system_resume"));
}

NativeTray::NativeTray() {
	call_deferred("_bind_shutdown_cleanup");
}

NativeTray::~NativeTray() {
#ifdef _WIN32
	cleanup_tray_resources();
	if (hMenu) {
		DestroyMenu(hMenu);
		hMenu = nullptr;
	}
#endif
}

void NativeTray::disconnect_all_signals() {
	// release lambda callables
	// before scriptserver tears down
	TypedArray<Dictionary> signals = get_signal_list();
	for (int i = 0; i < signals.size(); i++) {
		Dictionary sig_info = signals[i];
		StringName sig_name = sig_info["name"];
		TypedArray<Dictionary> connections = get_signal_connection_list(sig_name);
		for (int j = 0; j < connections.size(); j++) {
			Dictionary conn = connections[j];
			Callable callable = conn["callable"];
			if (is_connected(sig_name, callable)) {
				disconnect(sig_name, callable);
			}
		}
	}
}

void NativeTray::_bind_shutdown_cleanup() {
	if (shutdown_hook_bound) return;

	Engine *engine = Engine::get_singleton();
	if (!engine || engine->is_editor_hint()) return;

	SceneTree *tree = Object::cast_to<SceneTree>(engine->get_main_loop());
	if (!tree) return;

	Window *root = tree->get_root();
	if (!root) return;

	Callable cb(this, "_on_engine_shutdown");
	if (!root->is_connected("tree_exited", cb)) {
		root->connect("tree_exited", cb, CONNECT_ONE_SHOT);
	}
	shutdown_hook_bound = true;
}

void NativeTray::_on_engine_shutdown() {
	remove_tray();
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

void NativeTray::restore_process_qos_and_priority() {
	HANDLE hProcess = GetCurrentProcess();

	DWORD prio_class = GetPriorityClass(hProcess);
	if (prio_class == IDLE_PRIORITY_CLASS || prio_class == BELOW_NORMAL_PRIORITY_CLASS) {
		SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS);
	}

	HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
	if (hKernel32) {
		PFN_SetProcessInformation pfnSetProcInfo = reinterpret_cast<PFN_SetProcessInformation>(
			GetProcAddress(hKernel32, "SetProcessInformation")
		);
		if (pfnSetProcInfo) {
			// disable ecoqos and
			// timer resolution throttling
			TrayProcessPowerThrottlingState throttling = {};
			throttling.Version = 1;
			throttling.ControlMask = 0x1 | 0x4;
			throttling.StateMask = 0;
			if (!pfnSetProcInfo(hProcess, 4, &throttling, sizeof(throttling))) {
				throttling.ControlMask = 0x1;
				throttling.StateMask = 0;
				pfnSetProcInfo(hProcess, 4, &throttling, sizeof(throttling));
			}

			// restore normal memory page priority
			TrayMemoryPriorityInfo mem_prio = {};
			mem_prio.MemoryPriority = 5;
			pfnSetProcInfo(hProcess, 1, &mem_prio, sizeof(mem_prio));
		}
	}

	// refresh timer resolution
	// for low processor usage mode
	HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");
	if (hWinmm) {
		PFN_timeBeginPeriod pfnBegin = reinterpret_cast<PFN_timeBeginPeriod>(GetProcAddress(hWinmm, "timeBeginPeriod"));
		PFN_timeEndPeriod pfnEnd = reinterpret_cast<PFN_timeEndPeriod>(GetProcAddress(hWinmm, "timeEndPeriod"));
		if (pfnBegin && pfnEnd) {
			pfnBegin(1);
			pfnEnd(1);
		}
	}
}

DWORD WINAPI NativeTray::PrefetchWorkerThreadProc(LPVOID lpParameter) {
	if (InterlockedCompareExchange(&g_prefetch_in_progress, 1, 0) != 0) {
		return 0;
	}

	HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!hKernel32) {
		InterlockedExchange(&g_prefetch_in_progress, 0);
		return 0;
	}

	PFN_PrefetchVirtualMemory pfnPrefetch = reinterpret_cast<PFN_PrefetchVirtualMemory>(
		GetProcAddress(hKernel32, "PrefetchVirtualMemory")
	);
	if (!pfnPrefetch) {
		InterlockedExchange(&g_prefetch_in_progress, 0);
		return 0;
	}

	HANDLE hProcess = GetCurrentProcess();
	SYSTEM_INFO sysInfo = {};
	GetSystemInfo(&sysInfo);

	const uint8_t *addr = static_cast<const uint8_t *>(sysInfo.lpMinimumApplicationAddress);
	const uint8_t *max_addr = static_cast<const uint8_t *>(sysInfo.lpMaximumApplicationAddress);

	constexpr size_t BATCH_SIZE = 128;
	constexpr SIZE_T MAX_PREFETCH_BYTES = 512ULL * 1024ULL * 1024ULL;
	TrayWin32MemoryRangeEntry entries[BATCH_SIZE];
	size_t entry_count = 0;
	SIZE_T total_prefetched = 0;

	MEMORY_BASIC_INFORMATION mbi = {};
	while (addr < max_addr && total_prefetched < MAX_PREFETCH_BYTES) {
		if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) {
			break;
		}

		bool is_committed = (mbi.State == MEM_COMMIT);
		bool is_valid_type = (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_IMAGE);
		bool is_accessible = (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		                                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
		bool is_guarded = (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;

		if (is_committed && is_valid_type && is_accessible && !is_guarded && mbi.RegionSize > 0) {
			entries[entry_count].VirtualAddress = mbi.BaseAddress;
			entries[entry_count].NumberOfBytes = mbi.RegionSize;
			entry_count++;
			total_prefetched += mbi.RegionSize;

			if (entry_count == BATCH_SIZE) {
				pfnPrefetch(hProcess, entry_count, entries, 0);
				entry_count = 0;
			}
		}

		const uint8_t *next_addr = static_cast<const uint8_t *>(mbi.BaseAddress) + mbi.RegionSize;
		if (next_addr <= addr) {
			break;
		}
		addr = next_addr;
	}

	if (entry_count > 0) {
		pfnPrefetch(hProcess, entry_count, entries, 0);
	}

	InterlockedExchange(&g_prefetch_in_progress, 0);
	return 0;
}

void NativeTray::prefetch_process_working_set(bool p_async) {
	if (p_async) {
		QueueUserWorkItem(&NativeTray::PrefetchWorkerThreadProc, nullptr, WT_EXECUTEDEFAULT);
	} else {
		PrefetchWorkerThreadProc(nullptr);
	}
}

void NativeTray::force_swapchain_and_dwm_refresh(HWND p_godot_hwnd) {
	if (!p_godot_hwnd) return;

	SetWindowPos(
		p_godot_hwnd,
		nullptr,
		0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
	);

	// rebind vsync to refresh swapchain state
	if (DisplayServer *ds = DisplayServer::get_singleton()) {
		DisplayServer::VSyncMode vsync = ds->window_get_vsync_mode(DisplayServer::MAIN_WINDOW_ID);
		ds->window_set_vsync_mode(vsync, DisplayServer::MAIN_WINDOW_ID);
	}

	RECT rc = {};
	if (GetClientRect(p_godot_hwnd, &rc)) {
		LONG w = rc.right - rc.left;
		LONG h = rc.bottom - rc.top;
		if (w > 0 && h > 0) {
			WPARAM size_state = IsZoomed(p_godot_hwnd) ? SIZE_MAXIMIZED : SIZE_RESTORED;
			SendMessageW(p_godot_hwnd, WM_SIZE, size_state, MAKELPARAM(w, h));
		}
	}

	RedrawWindow(
		p_godot_hwnd,
		nullptr,
		nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW
	);
	flush_dwm_composition();

	call_deferred("_deferred_post_show_refresh");
}

void NativeTray::_deferred_post_show_refresh() {
	if (Engine::get_singleton()->is_editor_hint()) return;
	if (RenderingServer *rs = RenderingServer::get_singleton()) {
		rs->force_draw(true, 0.0);
	}
}

void NativeTray::force_foreground_window(HWND p_godot_hwnd) {
	if (!p_godot_hwnd) return;

	HWND fg_hwnd = GetForegroundWindow();
	DWORD current_thread_id = GetCurrentThreadId();
	DWORD fg_thread_id = fg_hwnd ? GetWindowThreadProcessId(fg_hwnd, nullptr) : 0;

	bool attached = false;
	if (fg_thread_id != 0 && fg_thread_id != current_thread_id) {
		attached = (AttachThreadInput(fg_thread_id, current_thread_id, TRUE) != FALSE);
	}

	AllowSetForegroundWindow(ASFW_ANY);
	BringWindowToTop(p_godot_hwnd);
	SetForegroundWindow(p_godot_hwnd);
	SetActiveWindow(p_godot_hwnd);
	SetFocus(p_godot_hwnd);

	if (attached) {
		AttachThreadInput(fg_thread_id, current_thread_id, FALSE);
	}

	if (DisplayServer *ds = DisplayServer::get_singleton()) {
		ds->window_move_to_foreground(DisplayServer::MAIN_WINDOW_ID);
	}
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

	cached_icon_source = p_icon;

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

	_bind_shutdown_cleanup();
#endif
}

void NativeTray::init_tray_ico(const String &p_tooltip, const String &p_path) {
	init_tray(p_tooltip, p_path);
}

void NativeTray::update_icon(const Variant &p_icon) {
#ifdef _WIN32
	if (!hwnd || !is_active) return;

	cached_icon_source = p_icon;
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

#ifdef _WIN32
void NativeTray::cleanup_tray_resources() {
	if (is_active && hwnd) {
		Shell_NotifyIconW(NIM_DELETE, &nid);
	}
	if (current_icon) {
		DestroyIcon(current_icon);
		current_icon = nullptr;
	}
	nid.hIcon = nullptr;
	is_active = false;

	if (hwnd) {
		SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
		hwnd = nullptr;
		UnregisterClassW(L"GodotNativeTrayMessageWindow", GetModuleHandle(nullptr));
	}
}
#endif

void NativeTray::remove_tray() {
#ifdef _WIN32
	cleanup_tray_resources();
#endif
	cached_icon_source = Variant();
	disconnect_all_signals();
}

void NativeTray::recreate_tray() {
#ifdef _WIN32
	if (!is_active || !hwnd) return;

	if (cached_icon_source.get_type() != Variant::NIL) {
		HICON refreshed_icon = create_icon_from_variant(cached_icon_source);
		if (refreshed_icon) {
			if (current_icon) DestroyIcon(current_icon);
			current_icon = refreshed_icon;
			nid.hIcon = current_icon;
		}
	}

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

Rect2i NativeTray::get_tray_rect() const {
#ifdef _WIN32
	if (!is_active || !hwnd) return Rect2i();

	NOTIFYICONIDENTIFIER nii = {};
	nii.cbSize = sizeof(nii);
	nii.hWnd = hwnd;
	nii.uID = nid.uID;

	RECT rc = {};
	if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &rc))) {
		return Rect2i(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
	}
#endif
	return Rect2i();
}

void NativeTray::hide_window() {
#ifdef _WIN32
	if (Engine::get_singleton()->is_editor_hint()) return;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (!ds) return;

	int64_t handle = ds->window_get_native_handle(DisplayServer::WINDOW_HANDLE, DisplayServer::MAIN_WINDOW_ID);
	if (HWND godot_hwnd = reinterpret_cast<HWND>(handle)) {
		if (!window_hidden_by_tray && IsWindowVisible(godot_hwnd)) {
			was_maximized_before_hide = (IsZoomed(godot_hwnd) != FALSE) ||
				(ds->window_get_mode(DisplayServer::MAIN_WINDOW_ID) == DisplayServer::WINDOW_MODE_MAXIMIZED);
		}

		// minimize before hiding so
		// godot stops drawing to hidden swapchain
		set_dwm_transitions_disabled(godot_hwnd, TRUE);
		if (!IsIconic(godot_hwnd)) {
			ShowWindow(godot_hwnd, SW_MINIMIZE);
		}
		ShowWindow(godot_hwnd, SW_HIDE);

		window_hidden_by_tray = true;
		hidden_timestamp_ms = GetTickCount64();
	}
#endif
}

void NativeTray::show_window() {
#ifdef _WIN32
	if (Engine::get_singleton()->is_editor_hint()) return;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (!ds) return;

	int64_t handle = ds->window_get_native_handle(DisplayServer::WINDOW_HANDLE, DisplayServer::MAIN_WINDOW_ID);
	if (HWND godot_hwnd = reinterpret_cast<HWND>(handle)) {
		restore_process_qos_and_priority();

		ULONGLONG now_ms = GetTickCount64();
		bool long_hidden = (hidden_timestamp_ms > 0) && ((now_ms - hidden_timestamp_ms) > (5ULL * 60ULL * 1000ULL));
		if (system_resumed_while_hidden || long_hidden) {
			prefetch_process_working_set(false);
		}

		set_dwm_transitions_disabled(godot_hwnd, TRUE);
		if (!IsWindowVisible(godot_hwnd)) {
			ShowWindow(godot_hwnd, SW_SHOWNOACTIVATE);
		}

		if (was_maximized_before_hide) {
			ShowWindow(godot_hwnd, SW_SHOWMAXIMIZED);
		} else if (IsIconic(godot_hwnd)) {
			ShowWindow(godot_hwnd, SW_RESTORE);
		} else {
			ShowWindow(godot_hwnd, SW_SHOW);
		}
		set_dwm_transitions_disabled(godot_hwnd, FALSE);

		force_swapchain_and_dwm_refresh(godot_hwnd);
		force_foreground_window(godot_hwnd);

		window_hidden_by_tray = false;
		system_resumed_while_hidden = false;
	}
#endif
}

bool NativeTray::is_window_hidden() const {
#ifdef _WIN32
	if (Engine::get_singleton()->is_editor_hint()) return false;
	if (DisplayServer *ds = DisplayServer::get_singleton()) {
		int64_t handle = ds->window_get_native_handle(DisplayServer::WINDOW_HANDLE, DisplayServer::MAIN_WINDOW_ID);
		if (HWND godot_hwnd = reinterpret_cast<HWND>(handle)) {
			return !IsWindowVisible(godot_hwnd) || window_hidden_by_tray;
		}
	}
	return window_hidden_by_tray;
#else
	return false;
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
		} else if (msg == WM_POWERBROADCAST) {
			pSelf->process_power_broadcast(wParam, lParam);
			return TRUE;
		} else if (msg != 0 && msg == wm_taskbar_created) {
			pSelf->recreate_tray();
			return 0;
		}
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void NativeTray::process_power_broadcast(WPARAM wParam, LPARAM lParam) {
	switch (wParam) {
		case PBT_APMSUSPEND: {
			call_deferred("emit_signal", "on_system_suspend");
			break;
		}
		case PBT_APMRESUMEAUTOMATIC:
		case PBT_APMRESUMESUSPEND: {
			if (window_hidden_by_tray) {
				system_resumed_while_hidden = true;
			}

			restore_process_qos_and_priority();
			prefetch_process_working_set(true);

			if (is_active && hwnd) {
				if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
					Shell_NotifyIconW(NIM_ADD, &nid);
				}
			}

			if (wParam == PBT_APMRESUMEAUTOMATIC) {
				call_deferred("emit_signal", "on_system_resume");
			}
			break;
		}
	}
}

void NativeTray::process_tray_message(WPARAM wParam, LPARAM lParam) {
	switch (LOWORD(lParam)) {
		case WM_LBUTTONUP: {
			SetForegroundWindow(hwnd);
			PostMessageW(hwnd, WM_NULL, 0, 0);
			call_deferred("emit_signal", "on_tray_left_clicked");
			break;
		}
		case WM_RBUTTONUP: {
			restore_process_qos_and_priority();
			call_deferred("emit_signal", "on_tray_right_clicked");
			
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
							force_foreground_window(hw);
						}
					}
					handled_by_custom = true;
				}
			}
			
			if (!handled_by_custom && hMenu && GetMenuItemCount(hMenu) > 0) {
				POINT pt;
				GetCursorPos(&pt);
				SetForegroundWindow(hwnd);
				int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
				PostMessageW(hwnd, WM_NULL, 0, 0);
				if (cmd >= static_cast<int>(MENU_ID_OFFSET)) {
					process_menu_command(cmd);
				}
			}
			break;
		}
		case WM_LBUTTONDBLCLK: {
			SetForegroundWindow(hwnd);
			PostMessageW(hwnd, WM_NULL, 0, 0);
			call_deferred("emit_signal", "on_tray_double_clicked");
			break;
		}
	}
}

void NativeTray::process_menu_command(int command_id) {
	if (command_id >= MENU_ID_OFFSET) {
		int actual_id = command_id - MENU_ID_OFFSET;
		call_deferred("emit_signal", "on_menu_item_selected", actual_id);
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
		Ref<Texture2D> tex = p_icon;
		if (tex.is_valid()) {
			return create_icon_from_image(tex->get_image());
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
