#include "SharedContext.h"
#include "DebuggerContext.h"

SharedContext::SharedContext() {
    debugger_context = new DebuggerContext();
    buffer_1.resize(WIDTH * HEIGHT, 0xFF000000); // Fill Black
    buffer_2.resize(WIDTH * HEIGHT, 0xFF000000);

    // Assign initial pointers
    p_front_buffer = buffer_1.data();
    p_back_buffer = buffer_2.data();
}