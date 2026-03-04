#include <windows.h>

#include "fme/app.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line, int show_cmd) {
    return fme::RunApp(instance, prev_instance, cmd_line, show_cmd);
}
