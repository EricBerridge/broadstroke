// The studio program: it opens the studio window, with the photo named on
// the command line if there is one.
#pragma once

#include "App.xaml.g.h"

namespace winrt::BroadstrokeStudio::implementation {

struct App : AppT<App> {
  App();
  void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

 private:
  Microsoft::UI::Xaml::Window window_{nullptr};
};

}  // namespace winrt::BroadstrokeStudio::implementation
