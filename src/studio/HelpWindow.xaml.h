// The help window of the studio (spec 2.10): the pages of help_text.cpp,
// with the list of topics beside the open page.
//
// It belongs to the studio window and stays above it, but it is not modal:
// both windows take input, so a page can stay open beside the work it
// explains. Closing it hides it; it comes back at the same place with the
// same page.
#pragma once

#include "HelpWindow.g.h"

#include "app/help_text.hpp"

namespace winrt::BroadstrokeStudio::implementation {

struct HelpWindow : HelpWindowT<HelpWindow> {
  HelpWindow() = default;

  // Called once the XAML is loaded and the named controls exist.
  void InitializeComponent();

  // Shows the window with the page of `topic`, above `owner`.
  void show(HWND owner, bs::HelpTopic topic);
  void hide();
  // The studio window is going: this one goes with it.
  void close_for_good();

  // Handlers named in HelpWindow.xaml.
  void OnTopicChanged(Windows::Foundation::IInspectable const&,
                      Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnEscape(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);

 private:
  void place_over(HWND owner);
  void show_page(bs::HelpTopic topic);

  HWND hwnd_ = nullptr, owner_ = nullptr;
  bool placed_ = false;   // it has been given its first place
  bool closing_ = false;  // closing it now closes it
  bool filling_ = false;  // the list is being set; its event is not the user's
  bs::HelpTopic shown_ = bs::HelpTopic::Count;
};

}  // namespace winrt::BroadstrokeStudio::implementation

namespace winrt::BroadstrokeStudio::factory_implementation {
struct HelpWindow : HelpWindowT<HelpWindow, implementation::HelpWindow> {};
}  // namespace winrt::BroadstrokeStudio::factory_implementation
