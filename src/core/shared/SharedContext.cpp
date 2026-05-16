#include "SharedContext.h"
#include "DebuggerContext.h"

SharedContext::SharedContext() {
    debugger_context = new DebuggerContext();
	for (int i = 0; i < BUFFER_COUNT; i++) {
        buffers[i].fill(0xFF000000); // Fill with black
    }
}