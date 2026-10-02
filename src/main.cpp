// Vitals — Dear ImGui + ImPlot system monitor with a live /proc-based
// backend. See src/metrics.h/.cpp for the data source, src/app_shell.h/.cpp
// for the window/frame-loop.
#include "app_shell.h"

int main() {
    AppShell app;
    if (!app.Init()) return 1;
    return app.Run();
}
