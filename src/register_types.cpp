#include "register_types.h"
#include "native_tray.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static NativeTray *native_tray_ptr = nullptr;

void initialize(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
		return;
	ClassDB::register_class<NativeTray>();
	native_tray_ptr = memnew(NativeTray);
	Engine::get_singleton()->register_singleton("NativeTray", native_tray_ptr);
}

void uninitialize(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
		return;
	Engine::get_singleton()->unregister_singleton("NativeTray");
	memdelete(native_tray_ptr);
	native_tray_ptr = nullptr;
}

extern "C" {
GDExtensionBool GDE_EXPORT library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize);
	init_obj.register_terminator(uninitialize);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}