#pragma once

#if defined(MINECRAFT_ENABLE_TRACY)
    #include <tracy/Tracy.hpp>
    #define MC_PROFILE_ZONE(name) ZoneScopedN(name)
    #define MC_PROFILE_FRAME() FrameMark
    #define MC_PROFILE_THREAD(name) tracy::SetThreadName(name)
    #define MC_PROFILE_PLOT(name, value) TracyPlot(name, value)
#else
    #define MC_PROFILE_ZONE(name) ((void)0)
    #define MC_PROFILE_FRAME() ((void)0)
    #define MC_PROFILE_THREAD(name) ((void)0)
    #define MC_PROFILE_PLOT(name, value) ((void)0)
#endif
