// SPDX-License-Identifier: MIT
#include "Startup.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* reason)
    {
        if (!condition) throw std::runtime_error(reason);
    }
}

int main()
{
    try {
        // Console fallback arms after data loads and enables only once.
        SkyEmote::InitialGameplay console;
        Check(!console.Enter(), "Enabled before data loaded");
        console.DataLoaded();
        Check(console.Waiting(), "Data load did not arm console fallback");
        Check(console.Enter() && !console.Enter(), "Console entry must enable exactly once");
        console.DataLoaded();
        Check(!console.Enter(), "Repeated data notification rearmed fallback");

        // PreLoadGame disarms fallback even if loading fails and old 3D remains.
        SkyEmote::InitialGameplay failed_load;
        failed_load.DataLoaded();
        failed_load.UseLoadLifecycle();
        Check(!failed_load.Enter(), "Failed save load enabled stale gameplay");
        failed_load.DataLoaded();
        Check(!failed_load.Waiting(), "Data notification overrode save-load ownership");

        // Normal NewGame or PostLoadGame owns startup, including unusual ordering.
        SkyEmote::InitialGameplay normal;
        normal.UseLoadLifecycle();
        normal.DataLoaded();
        Check(!normal.Enter(), "Normal startup was enabled a second time");
        std::cout << "Startup checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
