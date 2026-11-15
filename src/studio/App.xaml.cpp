#include "pch.h"

#include "App.xaml.h"

#include <string>

#include "MainWindow.xaml.h"

namespace winrt::BroadstrokeStudio::implementation {

App::App() {}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
  // The first argument, if there is one, is a photo to open.
  std::wstring photo;
  int argc = 0;
  if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    if (argc >= 2) photo = argv[1];
    LocalFree(argv);
  }

  auto window = make_self<MainWindow>();
  window->set_initial_photo(photo);
  window_ = *window;
  window_.Activate();
}

}  // namespace winrt::BroadstrokeStudio::implementation
