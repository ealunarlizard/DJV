// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/App.h>
#include <djv/Models/DeepLink.h>

#include <tlRender/UI/Init.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Error.h>

#include <iostream>

#if defined(_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#endif // _WINDOWS

FTK_MAIN()
{
    try
    {
        auto args = ftk::convert(argc, argv);
        // A review link clicked in the server's portal: the DJV already
        // running takes it, and this one has nothing more to do.
        if (2 == args.size() &&
            djv::models::isDeepLink(args[1]) &&
            djv::models::sendDeepLink(args[1]))
        {
            return 0;
        }
        auto context = ftk::Context::create();
        tl::ui::init(context);
        auto app = djv::app::App::create(context, args);
        if (app->hasCmdLineHelp())
            return 0;
        app->run();
    }
    catch(const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#if defined(_WINDOWS)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    return wmain(__argc, __wargv);
}
#endif // _WINDOWS
