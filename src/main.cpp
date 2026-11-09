#include "Application.h"
#include "Profiling.h"

int main() {
	MC_PROFILE_THREAD("Main");
	Application app;
	return app.Run();
}
