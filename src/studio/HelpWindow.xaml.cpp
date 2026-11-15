#include "pch.h"

#include "HelpWindow.xaml.h"
#if __has_include("HelpWindow.g.cpp")
#include "HelpWindow.g.cpp"
#endif

#include <algorithm>
#include <string>
#include <vector>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace muxd = winrt::Microsoft::UI::Xaml::Documents;

namespace {

// Sizes in device-independent pixels (1/96 inch).
constexpr int kWidthDip = 900, kHeightDip = 680;
constexpr int kMinWidthDip = 560, kMinHeightDip = 320;

// Sizes of the page's text.
constexpr double kBodySize = 14, kCodeSize = 13.3, kHeadingSize = 16.7, kTitleSize = 22;

enum class Style { Title, Heading, Body, Item };

std::wstring widen_ascii(const std::string& s) {
  return std::wstring(s.begin(), s.end());  // the pages are plain ASCII
}

}  // namespace

namespace winrt::BroadstrokeStudio::implementation {

void HelpWindow::InitializeComponent() {
  HelpWindowT::InitializeComponent();
  mux::Window self = *this;
  winrt::check_hresult(self.as<::IWindowNative>()->get_WindowHandle(&hwnd_));

  AppWindow().TitleBar().PreferredTheme(Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode);

  filling_ = true;
  for (int i = 0; i < static_cast<int>(bs::HelpTopic::Count); ++i)
    topics().Items().Append(winrt::box_value(winrt::hstring(widen_ascii(bs::help_page(static_cast<bs::HelpTopic>(i)).title))));
  filling_ = false;

  // Closing hides it: it is kept for the next time, with its place and its
  // page.
  AppWindow().Closing([this](auto&&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
    if (closing_) return;
    args.Cancel(true);
    hide();
  });
}

// Over the middle of the studio window, within the monitor's work area, and
// owned by it: always above it, no button of its own on the taskbar.
void HelpWindow::place_over(HWND owner) {
  owner_ = owner;
  SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));

  const int dpi = static_cast<int>(GetDpiForWindow(owner));
  if (const auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
    presenter.IsMinimizable(false);
    presenter.PreferredMinimumWidth(MulDiv(kMinWidthDip, dpi, 96));
    presenter.PreferredMinimumHeight(MulDiv(kMinHeightDip, dpi, 96));
  }
  int w = MulDiv(kWidthDip, dpi, 96), h = MulDiv(kHeightDip, dpi, 96);
  RECT anchor{};
  GetWindowRect(owner, &anchor);
  int x = anchor.left + (anchor.right - anchor.left - w) / 2;
  int y = anchor.top + (anchor.bottom - anchor.top - h) / 2;
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
    const RECT& wa = mi.rcWork;
    w = std::min(w, static_cast<int>(wa.right - wa.left));
    h = std::min(h, static_cast<int>(wa.bottom - wa.top));
    x = std::clamp(x, static_cast<int>(wa.left), static_cast<int>(wa.right) - w);
    y = std::clamp(y, static_cast<int>(wa.top), static_cast<int>(wa.bottom) - h);
  }
  AppWindow().MoveAndResize({x, y, w, h});
}

void HelpWindow::show(HWND owner, bs::HelpTopic topic) {
  if (!placed_) {
    place_over(owner);
    placed_ = true;
  }
  filling_ = true;
  topics().SelectedIndex(static_cast<int>(topic));
  filling_ = false;
  if (topic != shown_) show_page(topic);
  Activate();
  SetForegroundWindow(hwnd_);
  topics().Focus(mux::FocusState::Programmatic);
}

void HelpWindow::hide() {
  if (!AppWindow().IsVisible()) return;
  AppWindow().Hide();
  if (owner_ && IsWindow(owner_)) SetForegroundWindow(owner_);
}

void HelpWindow::close_for_good() {
  closing_ = true;
  Close();
}

void HelpWindow::OnTopicChanged(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (filling_) return;
  const int i = topics().SelectedIndex();
  if (i >= 0 && i < static_cast<int>(bs::HelpTopic::Count)) show_page(static_cast<bs::HelpTopic>(i));
}

void HelpWindow::OnEscape(mux::Input::KeyboardAccelerator const&,
                          mux::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
  args.Handled(true);
  hide();
}

// Writes a topic's page: its title, then the pieces of its body, each with
// the look of its kind.
void HelpWindow::show_page(bs::HelpTopic topic) {
  shown_ = topic;
  const bs::HelpPage& help = bs::help_page(topic);
  const std::vector<bs::HelpBlock> blocks = bs::parse_help(help.body);

  page().Blocks().Clear();
  auto add = [&](const std::wstring& text, Style style) {
    muxd::Paragraph paragraph;
    paragraph.LineHeight(style == Style::Title ? 30 : 21);
    switch (style) {
      case Style::Title:
        paragraph.Margin({0, 0, 0, 14});
        break;
      case Style::Heading:
        paragraph.Margin({0, 16, 0, 6});
        break;
      case Style::Item:
        // The lines after the first hang under the text.
        paragraph.Margin({22, 0, 0, 5});
        paragraph.TextIndent(-14);
        break;
      default:
        paragraph.Margin({0, 0, 0, 10});
        break;
    }
    auto add_run = [&](const std::wstring& piece, bool typed) {
      muxd::Run run;
      run.Text(piece);
      run.FontSize(style == Style::Title ? kTitleSize
                                         : (style == Style::Heading ? kHeadingSize : (typed ? kCodeSize : kBodySize)));
      if (style == Style::Title || style == Style::Heading) run.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
      if (typed) run.FontFamily(mux::Media::FontFamily(L"Consolas"));
      paragraph.Inlines().Append(run);
    };
    if (style == Style::Item) add_run(L"•  ", false);
    // Text between backticks is shown as typed; the backticks are left out.
    bool typed = false;
    size_t at = 0;
    while (at <= text.size()) {
      size_t end = text.find(L'`', at);
      if (end == std::wstring::npos) end = text.size();
      if (end > at) add_run(text.substr(at, end - at), typed);
      typed = !typed;
      at = end + 1;
    }
    page().Blocks().Append(paragraph);
  };

  add(widen_ascii(help.title), Style::Title);
  for (const bs::HelpBlock& b : blocks)
    add(widen_ascii(b.text), b.kind == bs::HelpBlock::Kind::Heading
                                 ? Style::Heading
                                 : (b.kind == bs::HelpBlock::Kind::Item ? Style::Item : Style::Body));
  scroller().ChangeView(nullptr, 0.0, nullptr, true);
}

}  // namespace winrt::BroadstrokeStudio::implementation
