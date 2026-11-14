#include "app/Application.h"
#include "diagnostics/Profiling.h"

int main() {
    MC_PROFILE_THREAD("Main");
    Application app;

    return app.Run();
}
