#ifndef NATIVE_TRAY_H
#define NATIVE_TRAY_H

#include <godot_cpp/classes/object.hpp>

namespace godot {

class NativeTray : public Object {
	GDCLASS(NativeTray, Object)

protected:
	static void _bind_methods();

public:
	NativeTray();
	~NativeTray();
};

} // namespace godot

#endif // NATIVE_TRAY_H
