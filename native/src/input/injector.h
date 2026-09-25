#pragma once

#include "input/control_message.h"

namespace srd::input {

class Injector {
public:
    void mouse_move(MouseMove value) const;
    void mouse_button(MouseButtonEvent value) const;
    void mouse_wheel(MouseWheel value) const;
    void key(KeyEvent value) const;
};

} // namespace srd::input
