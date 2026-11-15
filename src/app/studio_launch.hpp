// Starting the studio window from the command line (spec 2.10). The window
// is a program of its own, broadstroke_studio.exe, in the folder studio\
// beside this one.
#pragma once

namespace bs {

// Starts the studio window with the photo named after "gui" on this
// program's command line, if there is one, and waits until it is closed.
// Returns a process exit code.
int run_studio();

}  // namespace bs
