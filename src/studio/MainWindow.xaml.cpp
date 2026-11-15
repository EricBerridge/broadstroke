#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <system_error>
#include <utility>

#include "HelpWindow.xaml.h"
#include "app/scene_edit.hpp"
#include "app/version.hpp"
#include "surface/surface.hpp"

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;

using namespace bs::studio;

namespace {

// Sizes in device-independent pixels (1/96 inch).
constexpr int kInitialWidthDip = 1600, kInitialHeightDip = 880;
constexpr int kMinWidthDip = 1100, kMinHeightDip = 680;
constexpr double kPreviewPadDip = 12.0;
constexpr int kProgressRange = 1000;

// Live frames: at most one per kFrameInterval, and never more than about a
// quarter of the worker's time (see run_worker).
constexpr std::chrono::milliseconds kFrameInterval{250};
// Progress text without a frame: about 15 updates per second.
constexpr std::chrono::milliseconds kProgressInterval{66};
constexpr std::chrono::milliseconds kPollInterval{250};

// The panel beside the preview: the painting controls, or the wider editor
// of the items.
constexpr double kPaintPanelDip = 340, kEditPanelDip = 540;

// The widths of the columns of the list of items after the name, which
// takes the rest: importance, treatment and edges, then what the manner
// does. A form of the editor shows the first three or the last.
constexpr double kItemColumnWidths[] = {96, 150, 48, 240};
constexpr double kItemFontSize = 13;

void size_item_columns(muxc::Grid const& grid, bool changes) {
  const auto columns = grid.ColumnDefinitions();
  for (uint32_t i = 0; i < 4 && i + 1 < columns.Size(); ++i) {
    const bool shown = (i == 3) == changes;
    columns.GetAt(i + 1).Width(mux::GridLengthHelper::FromPixels(shown ? kItemColumnWidths[i] : 0));
  }
}

winrt::Windows::Foundation::IInspectable boxed(const std::wstring& text) { return winrt::box_value(winrt::hstring(text)); }

void add_choice(muxc::ComboBox const& box, const std::wstring& text) { box.Items().Append(boxed(text)); }

bool is_checked(muxc::CheckBox const& box) {
  const auto value = box.IsChecked();
  return value && value.Value();
}

// The first reason there is, or none.
std::wstring first_of(std::initializer_list<std::wstring> reasons) {
  for (const std::wstring& reason : reasons)
    if (!reason.empty()) return reason;
  return {};
}

muxc::TextBlock list_text(const std::wstring& text) {
  muxc::TextBlock t;
  t.Text(text);
  t.FontSize(kItemFontSize);
  t.VerticalAlignment(mux::VerticalAlignment::Center);
  t.TextTrimming(mux::TextTrimming::CharacterEllipsis);
  return t;
}

}  // namespace

namespace winrt::BroadstrokeStudio::implementation {

MainWindow::~MainWindow() {
  // Safety net; the normal paths join the threads in finish(), finish_tool()
  // or on_closed().
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
  tool_stop_.store(true);
  if (tool_thread_.joinable()) tool_thread_.join();
}

void MainWindow::InitializeComponent() {
  MainWindowT::InitializeComponent();
  setup_window();
  fill_controls();
  add_tips();
  update_description();
  update_medium_options();
  update_view_choices();
  check_tools();
  show_scene();
  update_controls();
  set_status(L"Open a photo to begin.");
  ready_ = true;
}

// ---------------------------------------------------------------------------
// The window

void MainWindow::setup_window() {
  dispatcher_ = DispatcherQueue();
  mux::Window self = *this;
  winrt::check_hresult(self.as<::IWindowNative>()->get_WindowHandle(&hwnd_));

  // Initial size in device-independent units, centered on and limited to
  // the work area of the monitor the window was created on.
  const int dpi = static_cast<int>(GetDpiForWindow(hwnd_));
  int w = MulDiv(kInitialWidthDip, dpi, 96), h = MulDiv(kInitialHeightDip, dpi, 96);
  int x = 0, y = 0;
  const auto area = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
      AppWindow().Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
  if (area) {
    const auto wa = area.WorkArea();
    w = std::min(w, wa.Width);
    h = std::min(h, wa.Height);
    x = wa.X + (wa.Width - w) / 2;
    y = wa.Y + (wa.Height - h) / 2;
  }
  AppWindow().MoveAndResize({x, y, w, h});
  apply_minimum_size();
  // The title bar follows the app theme, as the rest of the window does.
  AppWindow().TitleBar().PreferredTheme(Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode);

  // Changes that wait in the editor are not dropped without a word.
  AppWindow().Closing([this](auto&&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
    if (closing_ || !changes_wait()) return;
    args.Cancel(true);
    close_after_asking();
  });
  Closed([this](auto&&, auto&&) { on_closed(); });
  // The steps outside the engine may have been set up meanwhile.
  Activated([this](auto&&, mux::WindowActivatedEventArgs const& args) {
    if (ready_ && args.WindowActivationState() != mux::WindowActivationState::Deactivated && check_tools())
      update_controls();
  });

  poll_timer_ = dispatcher_.CreateTimer();
  poll_timer_.Interval(kPollInterval);
  poll_timer_.Tick([this](auto&&, auto&&) { drain_mailbox(); });
}

// The smallest size at which every control is still whole.
void MainWindow::apply_minimum_size() {
  const auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
  if (!presenter || !hwnd_) return;
  const int dpi = static_cast<int>(GetDpiForWindow(hwnd_));
  presenter.PreferredMinimumWidth(MulDiv(kMinWidthDip, dpi, 96));
  presenter.PreferredMinimumHeight(MulDiv(kMinHeightDip, dpi, 96));
}

void MainWindow::OnLoaded(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  // The scale changes when the window moves to a monitor of another DPI:
  // the preview's picture is made for the pixels it is shown on.
  root().XamlRoot().Changed([this](auto&&, auto&&) {
    apply_minimum_size();
    render_preview();
  });
  medium().Focus(mux::FocusState::Programmatic);
  if (!initial_photo_.empty()) load_photo(initial_photo_);
  show_messages();  // anything that was to be said before there was a window to say it in
}

void MainWindow::fill_controls() {
  media_ = bs::medium_names();
  int default_medium = 0;
  const std::string preferred = bs::Settings{}.medium;
  for (size_t i = 0; i < media_.size(); ++i) {
    add_choice(medium(), pretty_name(media_[i], true));
    if (media_[i] == preferred) default_medium = static_cast<int>(i);
  }
  medium().SelectedIndex(default_medium);

  // Entry 0 means "let the medium decide"; it is never changed behind the
  // user's back, so it stays selected across medium changes until the user
  // picks a specific surface.
  surfaces_ = bs::surface_names();
  add_choice(surface(), L"(default for medium)");
  for (const std::string& s : surfaces_) add_choice(surface(), pretty_name(s, true));
  surface().SelectedIndex(0);

  for (int s : kSizes) add_choice(size(), std::to_wstring(s) + L" px");
  size().SelectedIndex(kDefaultSizeIndex);

  for (const EffortChoice& e : kEfforts) add_choice(effort(), e.label);
  effort().SelectedIndex(kDefaultEffortIndex);

  skyBlend().IsChecked(bs::Settings{}.techniques.oil_blend_sky);

  // The key slider counts positions of 0.05 across the engine's range.
  key_min_pct_ = static_cast<int>(std::lround(bs::kWatercolorKeyMin * 100.f));
  key_max_pct_ = static_cast<int>(std::lround(bs::kWatercolorKeyMax * 100.f));
  const int key_default_pct = static_cast<int>(std::lround(bs::Settings{}.watercolor_key * 100.f));
  key().Minimum(0);
  key().Maximum((key_max_pct_ - key_min_pct_) / kKeyStepPct);
  key().StepFrequency(1);
  key().SmallChange(1);
  key().LargeChange(2);
  key().Value((key_default_pct - key_min_pct_) / kKeyStepPct);
  update_key_text();

  // The header of the list of items.
  set_item_columns(itemsHeader());
  const wchar_t* const titles[] = {L"Item", L"Importance", L"Treatment", L"Edges", L"Change"};
  for (int i = 0; i < 5; ++i) {
    muxc::TextBlock t = list_text(titles[i]);
    t.Opacity(0.7);
    muxc::Grid::SetColumn(t, i);
    itemsHeader().Children().Append(t);
  }

  for (const wchar_t* name : kImportanceNames) add_choice(importance(), name);
  for (const wchar_t* name : kEdgeNames) add_choice(edges(), name);

  // Painting the photo is the default (spec 2.14); a manner is chosen under
  // "An interpretation".
  whatAlone().IsChecked(true);
  for (const Manner& m : manners()) add_choice(manner(), m.label);
  manner().SelectedIndex(0);
}

// ---------------------------------------------------------------------------
// Tooltips and help

// Gives a control a tooltip, through its host (see MainWindow.xaml): the
// label beside the control shows the same tip, and the tip also shows while
// the control is disabled, which is when a user most wants to know what it
// is for and why it cannot be used.
void MainWindow::set_tip(mux::FrameworkElement const& host, const wchar_t* text) {
  for (Tip& tip : tips_)
    if (tip.host == host) {
      tip.text = text;
      tip.reason.clear();
      muxc::ToolTipService::SetToolTip(host, boxed(tip.text));
      return;
    }
  tips_.push_back({host, text, {}});
  muxc::ToolTipService::SetToolTip(host, boxed(text));
}

// Says why the host's control is disabled, before what it is for; with no
// reason the tooltip is what it was. The tooltip is only touched when the
// reason changes, so one that is showing is not disturbed.
void MainWindow::explain(mux::FrameworkElement const& host, const std::wstring& reason) {
  for (Tip& tip : tips_) {
    if (tip.host != host) continue;
    if (tip.reason == reason) return;
    tip.reason = reason;
    muxc::ToolTipService::SetToolTip(host, boxed(reason.empty() ? tip.text : reason + L"\n\n" + tip.text));
    return;
  }
  tips_.push_back({host, {}, reason});
  if (!reason.empty()) muxc::ToolTipService::SetToolTip(host, boxed(reason));
}

// A control is enabled exactly when there is no reason against it.
void MainWindow::gate(muxc::Control const& control, mux::FrameworkElement const& host, const std::wstring& reason) {
  control.IsEnabled(reason.empty());
  explain(host, reason);
}

// A menu item shows no tooltip while it is disabled, so its reason takes
// the place of its shortcut.
void MainWindow::gate_menu(muxc::MenuFlyoutItem const& item, const wchar_t* shortcut, const std::wstring& reason) {
  item.IsEnabled(reason.empty());
  item.KeyboardAcceleratorTextOverride(reason.empty() ? std::wstring(shortcut) : reason);
}

std::wstring MainWindow::busy_reason() const {
  if (working_) return L"The artwork is being made. Wait until it is finished, or press Stop.";
  if (tool_ == Tool::Observe) return L"The photo is being observed. Wait until it is finished, or press its Stop.";
  if (tool_ == Tool::Imagine) return L"The subject is being imagined. Wait until it is finished, or press its Stop.";
  return {};
}

std::wstring MainWindow::busy_word() const {
  if (working_) return L"while painting";
  if (tool_ == Tool::Observe) return L"while observing";
  if (tool_ == Tool::Imagine) return L"while imagining";
  return {};
}

// Why Start cannot be pressed; empty if it can. While the artwork is being
// made the button is Stop.
std::wstring MainWindow::start_reason() const {
  if (working_) return stop_.load() ? L"Stopping: the artist is finishing the stroke in hand." : L"";
  if (tool_ != Tool::None) return busy_reason();
  if (!photo_) return L"Open a photo first: File > Open photo, or drop one on the window.";
  // An interpretation is painted from its subject, made and up to date.
  if (what_ == What::Manner) {
    if (!scene_) return L"A manner needs the photo's scene: press \"Observe photo\" first.";
    if (!subject_) return L"Press Imagine first: the subject of this manner is not imagined yet.";
    if (subject_stale_) return L"Press \"Imagine again\" first: the imagined subject is out of date.";
  }
  return {};
}

// Why the steps outside the engine cannot be started; empty if they can.
std::wstring MainWindow::tools_reason() const {
  if (tools_ready_) return {};
  return tools_problem_ + L" Help > Guide, \"Setting up Observe and Imagine\", says how.";
}

bool MainWindow::check_tools() {
  ToolsConfig config;
  std::wstring problem, brief;
  const bool ready = read_tools_config(config, problem, &brief);
  if (ready) brief.clear();
  const bool changed = ready != tools_ready_ || brief != tools_problem_;
  tools_ready_ = ready;
  tools_problem_ = brief;
  return changed;
}

// The tip of a menu item is shown beside the menu, so that it covers neither
// the item's neighbors nor the menu bar.
void MainWindow::set_menu_tip(muxc::MenuFlyoutItem const& item, const wchar_t* text) {
  set_tip(item, text);
  muxc::ToolTipService::SetPlacement(item, muxc::Primitives::PlacementMode::Right);
}

// What each control is for, in a sentence or two. The help pages
// (help_text.cpp) say more; keep the two in step.
void MainWindow::add_tips() {
  set_menu_tip(menuOpen(),
               L"Open a photo (JPEG, PNG or BMP). You can also drop one on the window. A scene file beside the "
               L"photo is loaded with it.");
  set_menu_tip(menuSave(), L"Save the artwork as PNG, JPEG or BMP, by the ending of the file name.");
  set_menu_tip(menuShowInFolder(),
               L"Open the photo's folder in Explorer, with the photo selected. Its scene file and the files of the "
               L"manners are there.");
  set_menu_tip(menuGuide(),
               L"Open the help. F1 opens the page for the part of the window you are in. The help window stays open "
               L"beside your work.");
  set_tip(mediumTip(),
          L"What the artwork is made with. Each medium is simulated with its own tools: oil with bristle brushes, "
          L"watercolor with washes on paper, pastel with soft sticks.");
  set_tip(surfaceTip(),
          L"What the artwork is made on. The default is canvas for oil, cold-pressed paper for watercolor and pastel "
          L"paper for pastel.");
  set_tip(sizeTip(),
          L"The long side of the artwork in pixels. It is also the resolution of the simulation, so a larger size "
          L"takes longer.");
  set_tip(effortTip(), L"How far the artist takes the piece. More effort is more and finer work, and more time.");
  set_tip(seedTip(),
          L"A whole number that fixes every chance decision. The same photo, scene, settings and seed give exactly "
          L"the same artwork; another seed gives another realization.");
  set_tip(skyBlendTip(),
          L"Oil, with a scene: after each broad layer the sky and the clouds are gone over with a clean, soft brush, "
          L"so that they melt into a gradation. Off, the sky is painted like everything else.");
  set_tip(keyTip(),
          L"Watercolor: how light the picture is keyed. Lower aims the mid-tones darker, higher lighter, toward the "
          L"paper; the darks stay anchored. The default is 0.85.");
  set_tip(startTip(),
          L"Make the artwork (F5). While it is being made this button stops it, and what has been painted so far is "
          L"kept. With a manner chosen, the imagined subject is painted in place of the photo.");
  set_tip(viewTip(),
          L"What the preview shows: the photo, its scene (the items tinted over the photo), the imagined subject of "
          L"the chosen manner, or the artwork. The View menu has the same choices, with Ctrl+1 to Ctrl+4.");
  set_tip(whatSceneTip(),
          L"Paint the photo, with the artist working from its scene: finer work on what matters and, in watercolor "
          L"and pastel, each item worked on its own. It needs the photo's scene file.");
  set_tip(sceneLineTip(),
          L"The scene file says what the photo shows. It lies beside the photo and has the photo's name with "
          L".scene.json in place of the ending. \"Observe photo\" writes it by looking at the photo with models that "
          L"run on this machine, which takes a minute or two. \"Edit scene...\" shows its items.");
  set_tip(whatAloneTip(), L"Paint the photo as if it had no scene file.");
  set_tip(whatMannerTip(),
          L"Paint an interpretation of the photo. A manner changes things in the picture because of what they are; "
          L"the imagined subject is prepared first and then painted in place of the photo.");
  set_tip(mannerTip(), L"The manner the photo is interpreted in.");
  set_tip(subjectActionTip(),
          L"Prepare the imagined subject of the chosen manner: a model on this machine chooses what changes, unless "
          L"there is a choice already, and the changes are carried out on the photo. Start then paints the result.");
  set_tip(editChangesTip(), L"See and change what the manner does to each item of the scene.");
  set_tip(imagineChangesTip(),
          L"Carry out what the list says: the choice is saved and the imagined subject is made from it.");
  set_tip(discardTip(), L"Drop the changes made here that have not been imagined.");
  set_tip(itemsHeader(),
          L"The items of the scene; a part is set in below its whole. Select an item, here or by a click in the "
          L"picture, to change it below.");
  set_tip(importanceTip(),
          L"How much the item matters to the picture. The main subject and what matters get finer work; supporting "
          L"items and the background get broader work.");
  set_tip(treatmentTip(),
          L"What the item is, as one word. The painters act on skin (watercolor), hair (pastel), sky and cloud "
          L"(watercolor and oil) and foliage mass (watercolor).");
  set_tip(edgesTip(),
          L"The item's outline in watercolor and pastel: hard (crisp), soft (gradual) or lost (it merges into its "
          L"surroundings). An outline is worked softly only where both items that meet are soft or lost.");
  set_tip(mechanismTip(),
          L"What the chosen manner does to this item; \"As it is\" leaves it alone. The change waits until you press "
          L"\"Imagine with these changes\".");
  set_tip(strengthTip(),
          L"How strongly the mechanism is applied, from 1 to 3. For the solid of triangles it is how fine the "
          L"triangles are.");
}

// The help page for the part of the window that has the keyboard focus.
// Before a photo is open it is the first page, wherever the focus is.
bs::HelpTopic MainWindow::topic_of_focus() {
  if (!photo_) return bs::HelpTopic::Start;
  if (mode_ == Mode::EditChanges) return bs::HelpTopic::Manners;
  const auto focused = mux::Input::FocusManager::GetFocusedElement(root().XamlRoot()).try_as<mux::DependencyObject>();
  for (mux::DependencyObject d = focused; d; d = mux::Media::VisualTreeHelper::GetParent(d)) {
    if (d == editRedoTip()) return bs::HelpTopic::Observe;
    if (d == editPanel() || d == whatSceneTip()) return bs::HelpTopic::Scene;
    if (d == sceneLineTip()) return scene_ ? bs::HelpTopic::Scene : bs::HelpTopic::Observe;
    if (d == whatMannerTip() || d == mannerBlock()) return bs::HelpTopic::Manners;
    if (d == paintPanel()) return bs::HelpTopic::Painting;
  }
  return bs::HelpTopic::Start;
}

void MainWindow::show_help(bs::HelpTopic topic) {
  if (!help_) help_ = winrt::make<HelpWindow>();
  winrt::get_self<HelpWindow>(help_)->show(hwnd_, topic);
}

void MainWindow::OnHelp(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  show_help(bs::HelpTopic::Start);
}

void MainWindow::OnHelpKey(mux::Input::KeyboardAccelerator const&,
                           mux::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
  args.Handled(true);
  show_help(topic_of_focus());
}

// The shortcuts of the menu items and of Start. A shortcut does what its
// item or button would do now, and nothing where that is disabled. It also
// does nothing while a dialog is open: the dialog covers the item, but the
// key still arrives here.
void MainWindow::OnShortcut(mux::Input::KeyboardAccelerator const& sender,
                            mux::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
  using Windows::System::VirtualKey;
  args.Handled(true);
  if (dialog_open_) return;
  auto show = [this](View v) {
    if (view_offered(v)) set_view(v);
  };
  switch (sender.Key()) {
    case VirtualKey::O:
      if (can_open()) choose_photo();
      break;
    case VirtualKey::S:
      if (can_save()) save_artwork();
      break;
    case VirtualKey::F5:
      if (mode_ != Mode::Paint) break;  // Start is not in the editor
      if (working_) request_stop();
      else if (can_start()) start_painting();
      break;
    case VirtualKey::Number1:
      show(View::Photo);
      break;
    case VirtualKey::Number2:
      show(View::Scene);
      break;
    case VirtualKey::Number3:
      show(View::Subject);
      break;
    case VirtualKey::Number4:
      show(View::Artwork);
      break;
    default:
      break;
  }
}

void MainWindow::OnShowInFolder(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  show_photo_in_folder();
}

// Closing by the menu is closing by the title bar: work in progress is
// stopped (on_closed).
void MainWindow::OnExit(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  close_after_asking();
}

// Closes the window, after asking if changes wait in the editor.
winrt::fire_and_forget MainWindow::close_after_asking() {
  const auto keep = get_strong();
  if (!(co_await settle_changes())) co_return;
  closing_ = true;
  Close();
}

void MainWindow::OnViewMenu(Windows::Foundation::IInspectable const& sender, mux::RoutedEventArgs const&) {
  const auto item = sender.try_as<muxc::RadioMenuFlyoutItem>();
  View v = View::Photo;
  if (item == menuViewScene()) v = View::Scene;
  else if (item == menuViewSubject()) v = View::Subject;
  else if (item == menuViewArtwork()) v = View::Artwork;
  set_view(v);
}

void MainWindow::OnAbout(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) { show_about(); }

// The version, whose it is, and what this computer is, for a report of how
// long a painting took.
winrt::fire_and_forget MainWindow::show_about() {
  if (dialog_open_ || !root().XamlRoot()) co_return;
  const auto keep = get_strong();
  dialog_open_ = true;
  muxc::ContentDialog dialog = make_dialog(about_text(bs::kVersion));
  dialog.CloseButtonText(L"OK");
  dialog.DefaultButton(muxc::ContentDialogButton::Close);
  co_await dialog.ShowAsync();
  dialog_open_ = false;
  show_messages();
}

// Opens the photo's folder in Explorer with the photo selected: the scene
// file and the files of the manners lie beside it.
void MainWindow::show_photo_in_folder() {
  if (photo_path_.empty()) return;
  const PIDLIST_ABSOLUTE item = ILCreateFromPathW(photo_path_.c_str());
  if (!item) {
    tell(L"The photo is no longer where it was opened from:\n\n" + photo_path_);
    return;
  }
  const HRESULT shown = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
  ILFree(item);
  if (FAILED(shown)) tell(L"The folder of the photo could not be shown:\n\n" + directory_of(photo_path_));
}

// ---------------------------------------------------------------------------
// Messages and questions

muxc::ContentDialog MainWindow::make_dialog(const std::wstring& text) {
  muxc::ContentDialog dialog;
  dialog.XamlRoot(root().XamlRoot());
  dialog.Title(boxed(kAppTitle));
  muxc::TextBlock body;
  body.Text(text);
  body.TextWrapping(mux::TextWrapping::Wrap);
  body.IsTextSelectionEnabled(true);
  muxc::ScrollViewer scroller;
  scroller.Content(body);
  dialog.Content(scroller);
  return dialog;
}

// Says something the user has to know. Only one dialog can be open in a
// window, so a message waits for the one before it.
void MainWindow::tell(const std::wstring& text) {
  messages_.push_back(text);
  show_messages();
}

winrt::fire_and_forget MainWindow::show_messages() {
  if (messages_.empty() || dialog_open_ || !root().XamlRoot()) co_return;
  const auto keep = get_strong();
  dialog_open_ = true;
  while (!messages_.empty()) {
    const std::wstring next = std::move(messages_.front());
    messages_.pop_front();
    muxc::ContentDialog dialog = make_dialog(next);
    dialog.CloseButtonText(L"OK");
    dialog.DefaultButton(muxc::ContentDialogButton::Close);
    co_await dialog.ShowAsync();
  }
  dialog_open_ = false;
}

// Asks before something is replaced. "No" is the default.
Windows::Foundation::IAsyncOperation<bool> MainWindow::ask(std::wstring text, hstring yes, hstring no) {
  const auto keep = get_strong();
  if (dialog_open_ || !root().XamlRoot()) co_return false;
  dialog_open_ = true;
  const muxc::ContentDialog dialog = make_dialog(text);
  dialog.PrimaryButtonText(yes);
  dialog.CloseButtonText(no);
  dialog.DefaultButton(muxc::ContentDialogButton::Close);
  const muxc::ContentDialogResult result = co_await dialog.ShowAsync();
  dialog_open_ = false;
  show_messages();
  co_return result == muxc::ContentDialogResult::Primary;
}

// ---------------------------------------------------------------------------
// Events of the controls

void MainWindow::OnOpen(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) { choose_photo(); }

void MainWindow::OnMediumChanged(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (!ready_) return;
  update_description();
  update_medium_options();
}

// The seed is a whole number: nothing but digits gets into its box.
void MainWindow::OnSeedChanging(muxc::TextBox const&, muxc::TextBoxBeforeTextChangingEventArgs const& args) {
  for (const wchar_t c : args.NewText())
    if (c < L'0' || c > L'9') {
      args.Cancel(true);
      return;
    }
}

void MainWindow::OnKeyChanged(Windows::Foundation::IInspectable const&,
                              muxc::Primitives::RangeBaseValueChangedEventArgs const&) {
  if (!ready_) return;
  update_key_text();
}

void MainWindow::OnStart(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (working_) request_stop();
  else start_painting();
}

void MainWindow::OnSave(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) { save_artwork(); }

void MainWindow::OnViewChanged(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (!ready_ || filling_) return;
  update_view_menu();
  invalidate_preview();
}

void MainWindow::OnWhatChanged(Windows::Foundation::IInspectable const& sender, mux::RoutedEventArgs const&) {
  if (!ready_ || filling_) return;
  const auto button = sender.try_as<muxc::RadioButton>();
  set_what(button == whatScene() ? What::Scene : (button == whatManner() ? What::Manner : What::Alone));
}

// The button of the scene's line does what the line calls for.
void MainWindow::OnSceneAction(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (tool_ == Tool::Observe) request_tool_stop();
  else if (!scene_) start_observe();
  else set_mode(Mode::EditScene);
}

void MainWindow::OnMannerChanged(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (!ready_ || filling_) return;
  load_interpretation();
  if (!busy()) set_view(subject_frame_ ? View::Subject : View::Photo);
}

// The button of the subject's line, likewise.
void MainWindow::OnSubjectAction(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (tool_ == Tool::Imagine) request_tool_stop();
  else if (!scene_) start_observe();
  else start_imagine(false);
}

void MainWindow::OnEditChanges(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (!busy()) set_mode(Mode::EditChanges);
}

void MainWindow::OnEditBack(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) { leave_editor(); }

// "Observe again" in the scene's editor, "Ask the model again" in the
// editor of the changes. Each asks first, because it replaces what is shown.
void MainWindow::OnEditRedo(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (mode_ == Mode::EditScene) {
    if (tool_ == Tool::Observe) request_tool_stop();
    else start_observe();
  } else if (mode_ == Mode::EditChanges) {
    start_imagine(true);
  }
}

void MainWindow::OnImagineChanges(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (tool_ == Tool::Imagine) request_tool_stop();
  else imagine_with_changes();
}

void MainWindow::OnDiscardChanges(Windows::Foundation::IInspectable const&, mux::RoutedEventArgs const&) {
  if (!busy() && changes_wait()) discard_changes();
}

void MainWindow::OnItemSelected(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (!ready_ || filling_) return;
  on_item_selected();
}

void MainWindow::OnItemEdited(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (!ready_ || filling_) return;
  apply_item_edit();
}

void MainWindow::OnPickEdited(Windows::Foundation::IInspectable const&, muxc::SelectionChangedEventArgs const&) {
  if (!ready_ || filling_) return;
  apply_pick_edit();
}

void MainWindow::OnPreviewSizeChanged(Windows::Foundation::IInspectable const&, mux::SizeChangedEventArgs const&) {
  render_preview();
}

// A click on the scene view selects the item under it.
void MainWindow::OnPictureClick(Windows::Foundation::IInspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
  if (shown_view() != View::Scene || !scene_ || scene_->empty()) return;
  const double w = picture().ActualWidth(), h = picture().ActualHeight();
  if (w <= 0 || h <= 0) return;
  const auto at = args.GetCurrentPoint(picture()).Position();
  const bs::Plane<uint8_t>& layer = scene_->item;
  const int lx = std::clamp(static_cast<int>(at.X * layer.w / w), 0, layer.w - 1);
  const int ly = std::clamp(static_cast<int>(at.Y * layer.h / h), 0, layer.h - 1);
  const int id = layer(lx, ly);
  int row = -1;
  for (size_t i = 0; i < scene_->items.size(); ++i)
    if (scene_->items[i].id == id) row = static_cast<int>(i);
  select_item_row(row);
}

void MainWindow::OnDragOver(Windows::Foundation::IInspectable const&, mux::DragEventArgs const& args) {
  if (args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
    args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
}

winrt::fire_and_forget MainWindow::OnDrop(Windows::Foundation::IInspectable const&, mux::DragEventArgs const& args_in) {
  const auto keep = get_strong();
  const mux::DragEventArgs args = args_in;
  if (!args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())) co_return;
  const auto deferral = args.GetDeferral();
  std::wstring path;
  try {
    const auto dropped = co_await args.DataView().GetStorageItemsAsync();
    if (dropped.Size() > 0) path = dropped.GetAt(0).Path();
  } catch (const winrt::hresult_error&) {
    path.clear();
  }
  deferral.Complete();
  if (path.empty()) co_return;
  if (busy()) {
    MessageBeep(MB_ICONWARNING);  // the subject cannot change mid-painting, or under a step that reads it
    co_return;
  }
  SetForegroundWindow(hwnd_);
  if (!(co_await settle_changes()) || busy()) co_return;
  load_photo(path);
}

void MainWindow::on_closed() {
  if (tool_thread_.joinable()) {
    // The step outside the engine is ended with everything it started.
    AppWindow().Hide();
    tool_stop_.store(true);
    tool_thread_.join();
    tool_ = Tool::None;
  }
  if (worker_.joinable()) {
    // Hide first so the window is gone at once, then wait for the worker: it
    // stops at its next progress call, which is at most a few strokes away.
    // The worker only ever queues a wake-up (it never waits for the window),
    // so blocking the UI thread here cannot deadlock.
    AppWindow().Hide();
    stop_.store(true);
    worker_.join();
    working_ = false;
  }
  if (poll_timer_) poll_timer_.Stop();
  if (help_) {
    winrt::get_self<HelpWindow>(help_)->close_for_good();
    help_ = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Photo and artwork files

winrt::fire_and_forget MainWindow::choose_photo() {
  if (busy()) co_return;
  const auto keep = get_strong();
  if (!(co_await settle_changes()) || busy()) co_return;
  Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(AppWindow().Id());
  picker.Title(L"Open photo");
  picker.FileTypeChoices().Insert(L"Photos",
                                  winrt::single_threaded_vector<hstring>({L".jpg", L".jpeg", L".png", L".bmp"}));
  picker.FileTypeChoices().Insert(L"All files", winrt::single_threaded_vector<hstring>({L"*"}));
  std::wstring path;
  try {
    const auto picked = co_await picker.PickSingleFileAsync();
    if (picked) path = picked.Path();
  } catch (const winrt::hresult_error& e) {
    tell(L"The dialog for opening a photo could not be shown.\n\n" + std::wstring(e.message()));
    co_return;
  }
  if (!path.empty() && !busy()) load_photo(path);
}

bool MainWindow::load_photo(const std::wstring& path) {
  if (busy()) return false;
  const std::wstring full = full_path(path);

  std::string error;
  std::shared_ptr<const bs::Image> photo;
  std::unique_ptr<Frame> frame;
  try {
    // The image loader takes UTF-8 paths.
    bs::Image img = bs::load_image(to_utf8(full), &error);
    if (!img.empty()) {
      frame = make_display_frame(img);
      photo = std::make_shared<const bs::Image>(std::move(img));
    } else if (error.empty()) {
      error = "cannot read image";
    }
  } catch (const std::exception& e) {
    photo.reset();
    error = std::string("cannot load the photo: ") + e.what();
  }

  if (!photo) {
    tell(widen(error));
    return false;
  }

  photo_ = std::move(photo);
  photo_frame_ = std::move(frame);
  photo_path_ = full;
  Title(file_name_of(full) + L" - " + kAppTitle);
  fileLabel().Text(file_name_of(full) + L"  (" + std::to_wstring(photo_->w) + L" x " + std::to_wstring(photo_->h) +
                   L")");
  load_scene_for(full, photo_->w, photo_->h);
  // A photo is opened to be painted as it is, with its scene if it has one
  // (spec 2.14); an interpretation is chosen anew for each photo.
  set_mode(Mode::Paint);
  check_tools();
  set_what(scene_ ? What::Scene : What::Alone);

  // An earlier artwork stays available for saving until a new one is started;
  // switch the preview to the photo so the new subject is visible.
  update_view_choices();
  set_view(View::Photo);
  set_status(artwork_ ? L"Opened " + file_name_of(full) + L". The artwork made before can still be saved."
                      : std::wstring(L"Choose a medium and press Start."));
  update_controls();
  invalidate_preview();
  return true;
}

// A scene file beside the photo says what it shows (spec 2.13). One that
// cannot be used is reported, and the photo is used without it.
void MainWindow::load_scene_for(const std::wstring& photo_path, int photo_w, int photo_h) {
  std::shared_ptr<bs::Scene> scene;
  std::string scene_path, scene_error;
  try {
    scene_path = bs::scene_path_for(to_utf8(photo_path));
    if (bs::file_exists(scene_path)) {
      bs::Scene loaded = bs::load_scene(scene_path, &scene_error);
      if (!loaded.empty() && !loaded.fits(photo_w, photo_h)) {
        loaded = bs::Scene{};
        scene_error = "scene file '" + scene_path + "': its item layer does not have the photo's proportions";
      }
      if (!loaded.empty()) scene = std::make_shared<bs::Scene>(std::move(loaded));
    }
  } catch (const std::exception& e) {
    scene.reset();
    scene_error = std::string("cannot load the scene file: ") + e.what();
  }
  if (!scene && !scene_error.empty()) tell(widen(scene_error) + L"\n\nThe photo is opened without it.");
  scene_ = std::move(scene);
  scene_path_ = scene_ ? scene_path : std::string();
  show_scene();
}

winrt::fire_and_forget MainWindow::save_artwork() {
  if (busy() || !artwork_) co_return;
  const auto keep = get_strong();

  Microsoft::Windows::Storage::Pickers::FileSavePicker picker(AppWindow().Id());
  picker.Title(L"Save artwork");
  picker.FileTypeChoices().Insert(L"PNG image", winrt::single_threaded_vector<hstring>({L".png"}));
  picker.FileTypeChoices().Insert(L"JPEG image", winrt::single_threaded_vector<hstring>({L".jpg", L".jpeg"}));
  picker.FileTypeChoices().Insert(L"Bitmap", winrt::single_threaded_vector<hstring>({L".bmp"}));
  picker.DefaultFileExtension(L".png");
  picker.SuggestedFileName(stem_of(art_name_));
  if (!art_dir_.empty()) picker.SuggestedFolder(art_dir_);

  std::wstring out;
  try {
    const auto picked = co_await picker.PickSaveFileAsync();
    if (picked) out = picked.Path();
  } catch (const winrt::hresult_error& e) {
    tell(L"The dialog for saving the artwork could not be shown.\n\n" + std::wstring(e.message()));
    co_return;
  }
  if (out.empty() || busy() || !artwork_) co_return;

  // The writer picks the format from the extension, so make sure there is a
  // supported one.
  const std::wstring ext = extension_of(out);
  if (ext != L"png" && ext != L"jpg" && ext != L"jpeg" && ext != L"bmp") out += L".png";

  std::string error;
  bool ok = false;
  try {
    ok = bs::save_image(to_utf8(out), artwork_->image, &error);
  } catch (const std::exception& e) {
    error = std::string("cannot save the artwork: ") + e.what();
  }
  if (!ok) {
    if (error.empty()) error = "cannot save the artwork";
    tell(widen(error));
    co_return;
  }
  art_dir_ = directory_of(out);
  set_status(L"Saved " + file_name_of(out));
}

// ---------------------------------------------------------------------------
// What to paint, and the panel that is shown

// Chooses what is painted. The photo with its scene needs a scene.
void MainWindow::set_what(What what) {
  if (what == What::Scene && !scene_) what = What::Alone;
  what_ = what;
  const bool was_filling = filling_;
  filling_ = true;
  whatScene().IsChecked(what == What::Scene);
  whatAlone().IsChecked(what == What::Alone);
  whatManner().IsChecked(what == What::Manner);
  filling_ = was_filling;
  mannerBlock().Visibility(what == What::Manner ? mux::Visibility::Visible : mux::Visibility::Collapsed);
  load_interpretation();
  if (!busy() && mode_ == Mode::Paint) set_view(subject_frame_ ? View::Subject : View::Photo);
}

// Shows the painting controls, or the editor of the items in one of its two
// forms: the scene, or what the chosen manner changes. A form that has
// nothing to edit is not entered.
void MainWindow::set_mode(Mode mode) {
  if (mode == Mode::EditScene && !scene_) mode = Mode::Paint;
  if (mode == Mode::EditChanges && (!scene_ || manner_index() < 0 || !choice_.is_object())) mode = Mode::Paint;
  const Mode before = mode_;
  mode_ = mode;
  const bool editing = mode != Mode::Paint, changes = mode == Mode::EditChanges;
  const auto shown = [](bool yes) { return yes ? mux::Visibility::Visible : mux::Visibility::Collapsed; };
  paintPanel().Visibility(shown(!editing));
  editPanel().Visibility(shown(editing));
  panelColumn().Width(mux::GridLengthHelper::FromPixels(editing ? kEditPanelDip : kPaintPanelDip));
  sceneEditors().Visibility(shown(!changes));
  changeEditors().Visibility(shown(changes));
  changeButtons().Visibility(shown(changes));
  update_item_columns();

  if (changes) {
    editTitle().Text(std::wstring(L"What ") + manners()[static_cast<size_t>(manner_index())].label + L" changes in " +
                     file_name_of(photo_path_));
    set_tip(editRedoTip(),
            L"Have the model choose again what the manner changes. The choice shown is replaced, with anything you "
            L"changed in it.");
  } else if (editing) {
    editTitle().Text(L"The scene of " + file_name_of(photo_path_));
    set_tip(editRedoTip(),
            L"Write the photo's scene file again by looking at the photo. It takes a minute or two, and replaces the "
            L"scene file with the corrections made to it.");
  }
  update_controls();
  if (mode == before) return;

  if (editing) {
    // The items are selected in the picture, so the picture is the scene.
    set_view(View::Scene);
    items().Focus(mux::FocusState::Programmatic);
  } else {
    if (shown_view() == View::Scene) set_view(subject_frame_ ? View::Subject : View::Photo);
    // Back to the button that led to the editor.
    if (before == Mode::EditChanges) editChanges().Focus(mux::FocusState::Programmatic);
    else sceneAction().Focus(mux::FocusState::Programmatic);
  }
}

// Leaves the editor, after asking if changes wait in it.
winrt::fire_and_forget MainWindow::leave_editor() {
  const auto keep = get_strong();
  if (mode_ == Mode::Paint) co_return;
  if (!(co_await settle_changes())) co_return;
  set_mode(Mode::Paint);
}

// The two lines under "What to paint": what there is of the scene and of
// the imagined subject, each beside the button that does what comes next.
void MainWindow::update_lines() {
  const std::wstring busy = busy_reason(), tools = tools_reason();
  const std::wstring no_photo = photo_ ? L"" : L"Open a photo first.";
  const bool stopping = tool_stop_.load();
  const std::wstring stop_reason = stopping ? L"Stopping the step." : L"";
  const auto elapsed = [this] {
    const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - tool_started_).count();
    return std::to_wstring(seconds) + L" s";
  };

  std::wstring text, action, reason;
  if (tool_ == Tool::Observe) {
    text = stopping ? std::wstring(L"Stopping...") : L"Observing the photo, " + elapsed();
    action = L"Stop";
    reason = stop_reason;
  } else if (!photo_) {
    text = L"No photo open";
    action = L"Observe photo";
    reason = no_photo;
  } else if (!scene_) {
    text = L"Not observed yet";
    action = L"Observe photo";
    reason = first_of({busy, tools});
  } else {
    text = scene_summary();
    action = L"Edit scene...";
    reason = busy;
  }
  sceneStatus().Text(text);
  sceneAction().Content(boxed(action));
  gate(sceneAction(), sceneLineTip(), reason);

  if (tool_ == Tool::Imagine) {
    text = stopping ? std::wstring(L"Stopping...") : L"Imagining the subject, " + elapsed();
    action = L"Stop";
    reason = stop_reason;
  } else if (!scene_) {
    text = L"A manner needs the photo's scene.";
    action = L"Observe photo";
    reason = first_of({busy, no_photo, tools});
  } else if (!subject_) {
    text = L"Not imagined yet";
    action = L"Imagine";
    reason = first_of({busy, tools});
  } else if (subject_stale_) {
    text = stale_from_scene_ ? L"Out of date: the scene was edited" : L"Out of date: the changes were edited";
    action = L"Imagine again";
    reason = first_of({busy, tools});
  } else {
    text = L"Ready";
    action = L"Imagine again";
    reason = first_of({busy, tools});
  }
  subjectStatus().Text(text);
  subjectAction().Content(boxed(action));
  gate(subjectAction(), subjectActionTip(), reason);
  gate(editChanges(), editChangesTip(),
       first_of({busy, scene_ ? L"" : L"A manner needs the photo's scene.",
                 choice_.is_object() ? L"" : L"The model has not chosen for this manner yet. Press Imagine."}));
}

// ---------------------------------------------------------------------------
// The scene and its items

int MainWindow::selected_item() {
  if (!scene_) return -1;
  const int row = items().SelectedIndex();
  return (row >= 0 && row < static_cast<int>(scene_->items.size())) ? row : -1;
}

// The scene in a line: how many items, and the main subject.
std::wstring MainWindow::scene_summary() const {
  if (!scene_) return {};
  std::wstring text = std::to_wstring(scene_->items.size()) + L" items";
  if (const bs::SceneItem* main = scene_->find(scene_->main_subject)) text += L", main subject: " + widen(main->name);
  return text;
}

// The columns of the list of items, for its header and for each row: the
// name, which takes what the others leave, then importance, treatment,
// edges and what the manner does.
void MainWindow::set_item_columns(muxc::Grid const& grid) {
  grid.ColumnSpacing(6);
  muxc::ColumnDefinition name;
  name.Width(mux::GridLengthHelper::FromValueAndType(1, mux::GridUnitType::Star));
  grid.ColumnDefinitions().Append(name);
  for (int i = 0; i < 4; ++i) grid.ColumnDefinitions().Append(muxc::ColumnDefinition());
  size_item_columns(grid, mode_ == Mode::EditChanges);
}

// The scene's editor shows importance, treatment and edges; the editor of
// the changes shows what the manner does.
void MainWindow::update_item_columns() {
  const bool changes = mode_ == Mode::EditChanges;
  size_item_columns(itemsHeader(), changes);
  for (ItemRow& row : rows_) size_item_columns(row.grid, changes);
}

// One row of the list from its item.
void MainWindow::set_item_row(int row) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const bs::SceneItem& it = scene_->items[static_cast<size_t>(row)];
  ItemRow& cells = rows_[static_cast<size_t>(row)];
  cells.name.Text(widen(it.name));
  cells.importance.Text(kImportanceNames[std::clamp(it.importance, 0, 3)]);
  cells.treatment.Text(pretty_name(it.treatment, false));
  cells.edges.Text(kEdgeNames[static_cast<int>(it.edges)]);
  cells.change.Text(change_text(it.id));
}

// Selects one row, or none with row -1.
void MainWindow::select_item_row(int row) {
  items().SelectedIndex(row);
  if (row >= 0) items().ScrollIntoView(items().Items().GetAt(static_cast<uint32_t>(row)));
}

// Fills the panel from scene_: the summary, the list, the editors.
void MainWindow::show_scene() {
  const bool was_filling = filling_;
  filling_ = true;
  items().Items().Clear();
  rows_.clear();
  scene_frame_.reset();
  shown_item_ = -1;

  if (scene_) {
    const int n = static_cast<int>(scene_->items.size());
    rows_.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      const bs::SceneItem& it = scene_->items[static_cast<size_t>(i)];
      ItemRow& cells = rows_[static_cast<size_t>(i)];
      muxc::Grid grid;
      set_item_columns(grid);
      cells.grid = grid;

      // The name, behind a square of the item's color. A part is set in
      // below its whole.
      int depth = 0;
      for (const bs::SceneItem* p = scene_->find(it.parent); p && depth < 8; p = scene_->find(p->parent)) ++depth;
      muxc::Grid name_cell;
      name_cell.ColumnSpacing(6);
      muxc::ColumnDefinition swatch_column, text_column;
      swatch_column.Width(mux::GridLengthHelper::Auto());
      text_column.Width(mux::GridLengthHelper::FromValueAndType(1, mux::GridUnitType::Star));
      name_cell.ColumnDefinitions().Append(swatch_column);
      name_cell.ColumnDefinitions().Append(text_column);
      name_cell.Margin({depth * 14.0, 0, 0, 0});
      mux::Shapes::Rectangle swatch;
      swatch.Width(12);
      swatch.Height(12);
      swatch.RadiusX(2);
      swatch.RadiusY(2);
      const Rgb c = item_color(i);
      swatch.Fill(mux::Media::SolidColorBrush(Windows::UI::Color{255, c.r, c.g, c.b}));
      swatch.VerticalAlignment(mux::VerticalAlignment::Center);
      name_cell.Children().Append(swatch);
      cells.name = list_text(L"");
      muxc::Grid::SetColumn(cells.name, 1);
      name_cell.Children().Append(cells.name);
      grid.Children().Append(name_cell);

      cells.importance = list_text(L"");
      cells.treatment = list_text(L"");
      cells.edges = list_text(L"");
      cells.change = list_text(L"");
      int column = 1;
      for (const muxc::TextBlock& cell : {cells.importance, cells.treatment, cells.edges, cells.change}) {
        muxc::Grid::SetColumn(cell, column++);
        grid.Children().Append(cell);
      }

      muxc::ListViewItem item;
      item.Content(grid);
      item.MinHeight(28);
      item.Padding({8, 0, 8, 0});
      item.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
      // The row is a grid of cells; this is the name a screen reader says.
      mux::Automation::AutomationProperties::SetName(item, widen(it.name));
      items().Items().Append(item);
      set_item_row(i);
    }
  }
  filling_ = was_filling;
  show_item_editors();
}

// The editors show the selected item, or nothing.
void MainWindow::show_item_editors() {
  const bool was_filling = filling_;
  filling_ = true;
  const int i = selected_item();
  treatment().Items().Clear();
  treatment_choices_.clear();
  if (i < 0) {
    itemTitle().Text(scene_ ? L"Select an item in the list, or click it in the scene." : L"");
    importance().SelectedIndex(-1);
    edges().SelectedIndex(-1);
  } else {
    const bs::SceneItem& it = scene_->items[static_cast<size_t>(i)];
    itemTitle().Text(widen(it.name));
    importance().SelectedIndex(std::clamp(it.importance, 0, 3));
    edges().SelectedIndex(static_cast<int>(it.edges));

    // The fixed words, then whatever else the file says, so that showing an
    // item never changes it.
    treatment_choices_.push_back(std::string());
    for (const char* t : kTreatments) treatment_choices_.push_back(t);
    if (std::find(treatment_choices_.begin(), treatment_choices_.end(), it.treatment) == treatment_choices_.end())
      treatment_choices_.push_back(it.treatment);
    int current = 0;
    for (size_t k = 0; k < treatment_choices_.size(); ++k) {
      add_choice(treatment(), treatment_choices_[k].empty() ? L"(not stated)" : pretty_name(treatment_choices_[k], true));
      if (treatment_choices_[k] == it.treatment) current = static_cast<int>(k);
    }
    treatment().SelectedIndex(current);
  }

  // What the chosen manner does to the item: one of the manner's mechanisms
  // and a strength, from the choice file.
  mechanism().Items().Clear();
  strength().Items().Clear();
  const int manner_at = manner_index();
  bs::Json* pick = (i >= 0 && manner_at >= 0) ? pick_for(scene_->items[static_cast<size_t>(i)].id) : nullptr;
  if (pick) {
    const Manner& chosen_manner = manners()[static_cast<size_t>(manner_at)];
    const bs::Json* chosen_mechanism = pick->get("mechanism");
    const bs::Json* chosen_strength = pick->get("strength");
    const std::string chosen =
        (chosen_mechanism && chosen_mechanism->is_string()) ? chosen_mechanism->string : std::string(kAsItIs);
    add_choice(mechanism(), L"As it is");
    int current = 0, k = 1;
    for (const char* name : chosen_manner.mechanisms) {
      add_choice(mechanism(), pretty_name(name, true));
      if (chosen == name) current = k;
      ++k;
    }
    mechanism().SelectedIndex(current);
    // For the solid of triangles the strength is how fine the triangles are.
    const bool triangles = std::string(chosen_manner.id) == "polygonal";
    const wchar_t* const fineness[] = {L"1 coarse", L"2 medium", L"3 fine"};
    const wchar_t* const strengths[] = {L"1 slight", L"2 clear", L"3 extreme"};
    for (int level = 0; level < 3; ++level) add_choice(strength(), triangles ? fineness[level] : strengths[level]);
    const int s = (chosen_strength && chosen_strength->is_number())
                      ? std::clamp(static_cast<int>(chosen_strength->number), 1, 3)
                      : 1;
    strength().SelectedIndex(s - 1);
  }
  filling_ = was_filling;
  update_controls();
}

void MainWindow::on_item_selected() {
  show_item_editors();
  const int i = selected_item();
  if (i == shown_item_) return;
  shown_item_ = i;
  scene_frame_.reset();  // made again, with this item brought out, when next shown
  // Selecting an item is asking to see it; a painting in progress stays in
  // view.
  if (i >= 0 && !busy()) set_view(View::Scene);
  if (shown_view() == View::Scene) invalidate_preview();
}

// One of the three editors changed: the item takes the new value and the
// scene file is saved, so the file always says what will be painted.
void MainWindow::apply_item_edit() {
  const int i = selected_item();
  if (filling_ || busy() || i < 0) return;
  bs::SceneItem& it = scene_->items[static_cast<size_t>(i)];
  const bs::SceneItem before = it;
  const int new_importance = importance().SelectedIndex();
  const int new_treatment = treatment().SelectedIndex();
  const int new_edges = edges().SelectedIndex();
  if (new_importance >= 0 && new_importance <= 3) it.importance = new_importance;
  if (new_treatment >= 0 && new_treatment < static_cast<int>(treatment_choices_.size()))
    it.treatment = treatment_choices_[static_cast<size_t>(new_treatment)];
  if (new_edges >= 0 && new_edges <= 2) it.edges = static_cast<bs::Edges>(new_edges);
  if (it.importance == before.importance && it.treatment == before.treatment && it.edges == before.edges) return;

  bool kept_observed = false;
  std::string error;
  bool ok = false;
  try {
    ok = bs::save_scene_items(scene_path_, scene_->items, &kept_observed, &error);
  } catch (const std::exception& e) {
    error = std::string("cannot save the scene file: ") + e.what();
  }
  if (!ok) {
    it = before;
    show_item_editors();
    tell(widen(error) + L"\n\nThe change was not made.");
    return;
  }
  set_item_row(i);
  // A subject imagined from the scene as it was does not have the correction.
  if (subject_ && !subject_stale_) {
    subject_stale_ = true;
    stale_from_scene_ = true;
  }
  std::wstring text = L"Saved " + file_name_of(widen(scene_path_)) + L".";
  if (kept_observed)
    text += L"\nThe file as it was is kept as " + file_name_of(widen(bs::observed_copy_path(scene_path_))) + L".";
  set_status(text);
  update_controls();
}

// ---------------------------------------------------------------------------
// Painting

void MainWindow::start_painting() {
  if (busy() || !photo_) return;

  // What is painted: the photo, or with a manner chosen the subject imagined
  // from it, which the engine paints like any photo (spec 2.14).
  std::shared_ptr<const bs::Image> subject = photo_;
  std::shared_ptr<const bs::Scene> subject_scene = scene_;
  const int manner_at = manner_index();
  if (manner_at >= 0) {
    // Start is disabled then, and the subject's line says why.
    if (!subject_ || subject_stale_) return;
    subject = subject_;
    subject_scene = subject_scene_;
  }

  auto selection = [](muxc::ComboBox const& box, int count, int fallback) {
    const int i = box.SelectedIndex();
    return (i >= 0 && i < count) ? i : fallback;
  };

  bs::Settings settings;
  const int chosen_medium = selection(medium(), static_cast<int>(media_.size()), 0);
  if (!media_.empty()) settings.medium = media_[static_cast<size_t>(chosen_medium)];
  const int chosen_surface = selection(surface(), static_cast<int>(surfaces_.size()) + 1, 0);
  settings.surface = chosen_surface == 0 ? std::string() : surfaces_[static_cast<size_t>(chosen_surface - 1)];
  settings.long_side_px = kSizes[selection(size(), static_cast<int>(std::size(kSizes)), kDefaultSizeIndex)];
  settings.effort = kEfforts[selection(effort(), static_cast<int>(std::size(kEfforts)), kDefaultEffortIndex)].value;
  // Each medium's own setting is passed whatever the medium is; the engine
  // uses the one that applies.
  settings.techniques.oil_blend_sky = is_checked(skyBlend());
  const int key_pct = key_min_pct_ + kKeyStepPct * static_cast<int>(std::lround(key().Value()));
  settings.watercolor_key = static_cast<float>(key_pct) / 100.f;

  const std::wstring seed_text(seed().Text());
  wchar_t* end = nullptr;
  const unsigned long seed_value = std::wcstoul(seed_text.c_str(), &end, 10);  // saturates at the 32-bit maximum
  if (end == seed_text.c_str()) {
    settings.seed = 1;
    seed().Text(L"1");
  } else {
    settings.seed = static_cast<uint32_t>(seed_value);
  }

  // The previous artwork is discarded here, not when a photo is loaded.
  artwork_.reset();
  art_frame_.reset();
  art_dir_ = directory_of(photo_path_);
  art_manner_ = manner_at >= 0 ? widen(manners()[static_cast<size_t>(manner_at)].id) : std::wstring();
  art_name_ = stem_of(photo_path_) + (art_manner_.empty() ? L"" : L"_" + art_manner_) + L"_" + widen(settings.medium) +
              L".png";

  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.has_progress = false;
    mailbox_.frame.reset();
    mailbox_.done = false;
    mailbox_.artwork.reset();
    mailbox_.error.clear();
  }
  stop_.store(false);
  working_ = true;
  update_view_choices();
  set_view(View::Artwork);
  progress().Value(0);
  set_status(L"Preparing...");
  update_controls();
  start().Focus(mux::FocusState::Programmatic);
  invalidate_preview();

  // "The photo alone" paints without the scene, as with --no-scene.
  std::shared_ptr<const bs::Scene> scene;
  if (use_scene()) scene = subject_scene;

  try {
    // The worker gets its own references to the subject and the scene, and a
    // copy of the settings.
    worker_ = std::thread([this, subject, scene, settings] { run_worker(subject, scene, settings); });
  } catch (const std::exception&) {
    working_ = false;
    update_controls();
    set_status(L"Could not start painting.");
    tell(L"Could not start the painting thread.");
    return;
  }
  poll_timer_.Start();
}

void MainWindow::request_stop() {
  if (!working_) return;
  stop_.store(true);
  set_status(L"Stopping...");
  update_controls();  // Stop is disabled until the worker has stopped
}

// Worker thread. Touches only its arguments, stop_, notify_pending_, mailbox_
// and the (immutable) window handle and dispatcher.
void MainWindow::run_worker(std::shared_ptr<const bs::Image> photo, std::shared_ptr<const bs::Scene> scene,
                            bs::Settings settings) {
  std::unique_ptr<bs::Artwork> art;
  std::unique_ptr<Frame> final_frame;
  std::string error;
  try {
    Clock::time_point next_frame = Clock::now();
    Clock::time_point last_post{};
    std::string last_stage;

    bs::Hooks hooks;
    hooks.on_progress = [&](const bs::Progress& p, const bs::Canvas& canvas) {
      // After a stop request nothing more is reported; the UI keeps the last
      // progress it saw and the final image arrives with the Artwork.
      if (stop_.load()) return false;

      const Clock::time_point now = Clock::now();
      std::unique_ptr<Frame> frame;
      // Nobody can see a frame while the window is minimized, so do not pay
      // for one. IsIconic is safe to call from any thread.
      if (now >= next_frame && !IsIconic(hwnd_)) {
        frame = make_frame(canvas.render());
        // Wait at least four times as long as the frame took, so rendering
        // stays at or below a quarter of the worker's time however large the
        // canvas is.
        const Clock::duration cost = Clock::now() - now;
        next_frame = now + std::max<Clock::duration>(kFrameInterval, cost * 4);
      }

      if (frame || p.stage != last_stage || now - last_post >= kProgressInterval) {
        last_stage = p.stage;
        last_post = now;
        {
          std::lock_guard<std::mutex> lock(mailbox_.mutex);
          mailbox_.progress = p;
          mailbox_.has_progress = true;
          if (frame) mailbox_.frame = std::move(frame);
        }
        notify_ui();
      }
      return !stop_.load();
    };

    art = std::make_unique<bs::Artwork>(bs::create_artwork(*photo, settings, hooks, scene.get()));
    if (!art->image.empty()) final_frame = make_frame(art->image);
  } catch (const std::exception& e) {
    error = e.what();
    if (error.empty()) error = "unknown error";
    art.reset();
    final_frame.reset();
  } catch (...) {
    error = "unknown error";
    art.reset();
    final_frame.reset();
  }

  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.done = true;
    mailbox_.artwork = std::move(art);
    mailbox_.error = std::move(error);
    if (final_frame) mailbox_.frame = std::move(final_frame);
  }
  notify_ui();
}

// Worker thread. Queues a wake-up unless one is already waiting to be
// handled. The window outlives both threads (they are joined before it
// goes), and a wake-up that arrives after that finds nothing to do.
void MainWindow::notify_ui() {
  if (notify_pending_.exchange(true)) return;
  bool queued = false;
  try {
    queued = dispatcher_.TryEnqueue([weak = get_weak()] {
      if (const auto self = weak.get()) self->drain_mailbox();
    });
  } catch (const winrt::hresult_error&) {
    queued = false;
  }
  if (!queued) notify_pending_.store(false);  // the poll timer picks it up
}

// UI thread: takes whatever the worker has left in the Mailbox.
void MainWindow::drain_mailbox() {
  bool has_progress = false, done = false;
  bs::Progress latest;
  std::unique_ptr<Frame> frame;
  std::unique_ptr<bs::Artwork> art;
  std::string error;
  bool has_tool_line = false, tool_done = false;
  std::string tool_line, tool_tail;
  bs::ToolResult tool_result;
  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    // Cleared under the lock: anything the worker writes from here on finds
    // the flag clear and queues a new wake-up, so no update is ever missed.
    notify_pending_.store(false);
    has_progress = std::exchange(mailbox_.has_progress, false);
    if (has_progress) latest = mailbox_.progress;
    frame = std::move(mailbox_.frame);
    done = std::exchange(mailbox_.done, false);
    art = std::move(mailbox_.artwork);
    error = std::move(mailbox_.error);
    mailbox_.error.clear();
    has_tool_line = std::exchange(mailbox_.has_tool_line, false);
    if (has_tool_line) tool_line = mailbox_.tool_line;
    tool_done = std::exchange(mailbox_.tool_done, false);
    if (tool_done) {
      tool_result = mailbox_.tool_result;
      tool_tail = std::move(mailbox_.tool_tail);
      mailbox_.tool_tail.clear();
    }
  }

  if (has_tool_line) tool_line_ = widen(tool_line);
  if (tool_ != Tool::None && !tool_done) show_tool_status();
  if (tool_done) finish_tool(tool_result, tool_tail);

  if (frame && frame->w > 0) {
    art_frame_ = std::move(frame);
    if (shown_view() == View::Artwork) invalidate_preview();
  }
  if (has_progress && working_ && !stop_.load()) show_progress(latest);
  if (done) finish(std::move(art), error);
}

void MainWindow::finish(std::unique_ptr<bs::Artwork> art, const std::string& error) {
  if (worker_.joinable()) worker_.join();  // the worker has already left its last statement
  if (tool_ == Tool::None) poll_timer_.Stop();
  working_ = false;

  const bool painted = art && !art->image.empty();
  if (painted) {
    artwork_ = std::move(art);
    wchar_t seconds[32];
    std::swprintf(seconds, std::size(seconds), L"%.1f", artwork_->seconds);
    std::wstring text = artwork_->cancelled ? L"Stopped: " : L"Finished: ";
    text += std::to_wstring(artwork_->strokes) + L" strokes in " + seconds + L" s\n";
    text += pretty_name(artwork_->medium, true) + L" on " + pretty_name(artwork_->surface, false) + L", " +
            std::to_wstring(artwork_->width) + L" x " + std::to_wstring(artwork_->height) + L" px";
    if (!artwork_->cancelled) progress().Value(kProgressRange);
    set_status(text);
  } else {
    art_frame_.reset();
    progress().Value(0);
    set_status(L"Nothing was painted.");
  }
  update_view_choices();
  update_controls();
  invalidate_preview();

  if (!painted && !error.empty()) tell(L"Painting failed: " + widen(error));
}

// ---------------------------------------------------------------------------
// The steps outside the engine

// Writes the photo's scene file by running the observe step (spec 2.13).
winrt::fire_and_forget MainWindow::start_observe() {
  if (busy() || !photo_) co_return;
  const auto keep = get_strong();
  ToolsConfig config;
  std::wstring problem;
  if (!read_tools_config(config, problem)) {
    tell(problem);
    co_return;
  }
  if (bs::file_exists(bs::scene_path_for(to_utf8(photo_path_)))) {
    const bool yes = co_await ask(L"This photo has a scene file. Observing it again writes a new one in its place; "
                                  L"corrections made to it are lost.\n\nObserve the photo again?",
                                  L"Observe again", L"Cancel");
    if (!yes || busy() || !photo_) co_return;
  }
  const std::wstring folder = config.tools + L"\\observe";
  begin_tool(Tool::Observe, {{bs::quote_argument(config.python) + L" -u " +
                                  bs::quote_argument(folder + L"\\observe.py") + L" " + bs::quote_argument(photo_path_),
                              folder}});
}

// Prepares the imagined subject of the chosen manner (spec 2.14): the model
// chooses a mechanism per item, unless a choice file is there already, and
// the choice is carried out on the photo.
winrt::fire_and_forget MainWindow::start_imagine(bool ask_again) {
  const int manner_at = manner_index();
  if (busy() || !photo_ || manner_at < 0) co_return;
  const auto keep = get_strong();
  ToolsConfig config;
  std::wstring problem;
  if (!read_tools_config(config, problem)) {
    tell(problem);
    co_return;
  }
  if (!scene_) co_return;  // the subject's line offers "Observe photo" then
  const bool have_choice = choice_.is_object();
  if (ask_again && have_choice) {
    const bool yes = co_await ask(L"The model chooses again what this manner changes. The choice shown now, with "
                                  L"anything you changed in it, is replaced.\n\nAsk the model again?",
                                  L"Ask again", L"Cancel");
    if (!yes || busy() || !photo_ || manner_index() != manner_at) co_return;
    waiting_items_.clear();  // they go with the choice that is replaced
  }

  const std::wstring folder = config.tools + L"\\interpret";
  const std::wstring out = interpret_dir();
  const std::wstring python = bs::quote_argument(config.python) + L" -u ";
  const std::wstring manner_id = widen(manners()[static_cast<size_t>(manner_at)].id);
  std::vector<ToolStep> steps;
  if (ask_again || !have_choice)
    steps.push_back({python + bs::quote_argument(folder + L"\\choose.py") + L" --out " +
                         bs::quote_argument(out + L"\\choices") + L" --manners " + manner_id + L" " +
                         bs::quote_argument(photo_path_),
                     folder});
  steps.push_back({python + bs::quote_argument(folder + L"\\imagine.py") + L" --choice " +
                       bs::quote_argument(choice_file(manner_at)) + L" --out " + bs::quote_argument(out + L"\\subjects") +
                       L" " + bs::quote_argument(photo_path_),
                   folder});
  begin_tool(Tool::Imagine, std::move(steps));
}

void MainWindow::begin_tool(Tool which, std::vector<ToolStep> steps) {
  // The steps print as they go, in UTF-8.
  SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
  SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.has_tool_line = false;
    mailbox_.tool_line.clear();
    mailbox_.tool_done = false;
    mailbox_.tool_tail.clear();
  }
  tool_stop_.store(false);
  tool_ = which;
  tool_started_ = Clock::now();
  tool_line_.clear();
  // The step does not say how far it is; the bar only shows that it runs.
  progress().IsIndeterminate(true);
  editProgress().Visibility(mux::Visibility::Visible);
  show_tool_status();
  update_controls();

  try {
    tool_thread_ = std::thread([this, steps = std::move(steps)]() mutable { run_tool_worker(std::move(steps)); });
  } catch (const std::exception&) {
    bs::ToolResult failed;
    failed.error = "cannot start a thread";
    finish_tool(failed, std::string());
    return;
  }
  poll_timer_.Start();
}

void MainWindow::request_tool_stop() {
  if (tool_ == Tool::None) return;
  tool_stop_.store(true);
  set_status(L"Stopping...");
  update_controls();  // its Stop is disabled until the step has ended
}

void MainWindow::show_tool_status() {
  if (tool_ == Tool::None || tool_stop_.load()) return;
  const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - tool_started_).count();
  std::wstring text = tool_ == Tool::Observe ? L"Observing the photo" : L"Imagining the subject";
  text += L" - " + std::to_wstring(seconds) + L" s";
  if (!tool_line_.empty()) text += L"\n" + tool_line_;
  set_status(text);
  update_lines();
}

// Tool thread. Touches only its argument, tool_stop_, notify_pending_,
// mailbox_ and the dispatcher.
void MainWindow::run_tool_worker(std::vector<ToolStep> steps) {
  bs::ToolResult result;
  std::deque<std::string> tail;
  try {
    for (const ToolStep& step : steps) {
      result = bs::run_tool(
          step.command, step.directory,
          [&](const std::string& line) {
            tail.push_back(line);
            if (tail.size() > 12) tail.pop_front();
            {
              std::lock_guard<std::mutex> lock(mailbox_.mutex);
              mailbox_.tool_line = line;
              mailbox_.has_tool_line = true;
            }
            notify_ui();
          },
          tool_stop_);
      if (!result.ok()) break;
    }
  } catch (const std::exception& e) {
    result = bs::ToolResult{};
    result.error = e.what();
  }
  std::string text;
  for (const std::string& line : tail) text += line + "\n";
  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.tool_done = true;
    mailbox_.tool_result = result;
    mailbox_.tool_tail = std::move(text);
  }
  notify_ui();
}

void MainWindow::finish_tool(const bs::ToolResult& result, const std::string& tail) {
  if (tool_thread_.joinable()) tool_thread_.join();  // it has already left its last statement
  if (!working_) poll_timer_.Stop();
  const Tool which = tool_;
  tool_ = Tool::None;
  tool_stop_.store(false);
  progress().IsIndeterminate(false);
  progress().Value(0);
  editProgress().Visibility(mux::Visibility::Collapsed);
  const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - tool_started_).count();

  // Whatever the step wrote is read again, also after a failure: the files
  // say what there is.
  if (which == Tool::Observe) {
    if (result.ok()) {
      // The new file is the one as observed; a copy kept of an older one
      // would mislead.
      std::error_code ec;
      std::filesystem::remove(
          std::filesystem::path(widen(bs::observed_copy_path(bs::scene_path_for(to_utf8(photo_path_))))), ec);
    }
    const bool had_scene = scene_ != nullptr;
    if (photo_) load_scene_for(photo_path_, photo_->w, photo_->h);
    // A photo observed for the first time is painted with its scene.
    if (!had_scene && scene_ && what_ == What::Alone) what_ = What::Scene;
  }
  set_what(what_);  // reads the files of the manner again
  if (!scene_) set_mode(Mode::Paint);
  update_view_choices();

  if (result.ok()) {
    if (which == Tool::Observe) {
      set_view(scene_ ? View::Scene : View::Photo);
      set_status(scene_ ? L"Observed in " + std::to_wstring(seconds) + L" s: " +
                              std::to_wstring(scene_->items.size()) + L" items."
                        : L"Observed, but no scene file was written.");
    } else {
      set_view(subject_frame_ ? View::Subject : View::Photo);
      std::wstring text = L"Imagined in " + std::to_wstring(seconds) + L" s.";
      if (!tool_line_.empty()) text += L"\n" + tool_line_;
      set_status(text);
    }
  } else if (result.stopped) {
    set_status(L"Stopped.");
  } else {
    set_status(which == Tool::Observe ? L"Observing failed." : L"Imagining failed.");
    std::wstring message = which == Tool::Observe ? L"The observe step failed." : L"The interpret step failed.";
    if (!result.error.empty()) message += L"\n\n" + widen(result.error);
    else if (!tail.empty()) message += L"\n\nIts last lines:\n" + widen(tail);
    tell(message);
  }
  update_controls();
  invalidate_preview();
}

// ---------------------------------------------------------------------------
// The interpretation

int MainWindow::manner_index() {
  if (what_ != What::Manner) return -1;  // the photo is painted
  const int i = manner().SelectedIndex();
  return (i >= 0 && i < static_cast<int>(manners().size())) ? i : -1;
}

// The files of the interpret step are kept beside the photo, in a folder of
// their own.
std::wstring MainWindow::interpret_dir() const {
  return directory_of(photo_path_) + L"\\" + stem_of(photo_path_) + L".interpret";
}

std::wstring MainWindow::choice_file(int index) const {
  return interpret_dir() + L"\\choices\\" + stem_of(photo_path_) + L"." +
         widen(manners()[static_cast<size_t>(index)].id) + L".choice.json";
}

std::wstring MainWindow::subject_file(int index) const {
  return interpret_dir() + L"\\subjects\\" + stem_of(photo_path_) + L"_" +
         widen(manners()[static_cast<size_t>(index)].id) + L".png";
}

// Reads what there is for the chosen manner: the choice and the imagined
// subject with its scene. Nothing is made here.
void MainWindow::load_interpretation() {
  choice_ = bs::Json{};
  choice_path_.clear();
  subject_.reset();
  subject_scene_.reset();
  subject_frame_.reset();
  subject_stale_ = false;
  stale_from_scene_ = false;
  waiting_items_.clear();

  const int manner_at = manner_index();
  if (manner_at >= 0 && photo_) {
    const std::wstring choice_path = choice_file(manner_at), subject_path = subject_file(manner_at);
    std::string text;
    bs::Json doc;
    if (read_text(choice_path, text) && bs::parse_json(text, doc) && doc.is_object() && doc.get("picks") &&
        doc.get("picks")->is_array()) {
      choice_ = std::move(doc);
      choice_path_ = choice_path;
    }
    if (is_file(subject_path)) {
      try {
        std::string error;
        bs::Image img = bs::load_image(to_utf8(subject_path), &error);
        if (!img.empty()) {
          subject_frame_ = make_display_frame(img);
          // The subject's own scene file, written with it: the scene with
          // the items where the manner put them.
          const std::string scene_path = bs::scene_path_for(to_utf8(subject_path));
          if (bs::file_exists(scene_path)) {
            bs::Scene loaded = bs::load_scene(scene_path, &error);
            if (!loaded.empty() && loaded.fits(img.w, img.h))
              subject_scene_ = std::make_shared<bs::Scene>(std::move(loaded));
          }
          subject_ = std::make_shared<const bs::Image>(std::move(img));
        }
      } catch (const std::exception&) {
        subject_.reset();
        subject_scene_.reset();
        subject_frame_.reset();
      }
      // A choice edited after the subject was made has not been carried out,
      // and a scene corrected after it is not the scene the subject is from.
      const bool by_choice = !choice_path_.empty() && written_after(choice_path_, subject_path);
      const bool by_scene = !scene_path_.empty() && written_after(widen(scene_path_), subject_path);
      subject_stale_ = subject_ && (by_choice || by_scene);
      stale_from_scene_ = subject_stale_ && !by_choice;
    }
  }

  if (scene_)
    for (int row = 0; row < static_cast<int>(scene_->items.size()); ++row) set_item_row(row);
  show_item_editors();
  update_view_choices();
  update_controls();
}

// The entry of the choice file for an item, or null.
bs::Json* MainWindow::pick_for(int item_id) {
  if (!choice_.is_object()) return nullptr;
  bs::Json& picks = choice_.member("picks");
  if (!picks.is_array()) return nullptr;
  for (bs::Json& pick : picks.array) {
    const bs::Json* id = pick.get("id");
    if (pick.is_object() && id && id->is_number() && id->number == static_cast<double>(item_id)) return &pick;
  }
  return nullptr;
}

// What the manner does to an item, for the list: empty if it is left alone.
std::wstring MainWindow::change_text(int item_id) {
  bs::Json* pick = manner_index() >= 0 ? pick_for(item_id) : nullptr;
  if (!pick) return {};
  const bs::Json* chosen_mechanism = pick->get("mechanism");
  const bs::Json* chosen_strength = pick->get("strength");
  if (!chosen_mechanism || !chosen_mechanism->is_string() || chosen_mechanism->string == kAsItIs) return {};
  std::wstring text = pretty_name(chosen_mechanism->string, true);
  if (chosen_strength && chosen_strength->is_number())
    text += L" " + std::to_wstring(static_cast<int>(chosen_strength->number));
  return text;
}

// The mechanism or the strength of the selected item was changed. The change
// waits: nothing is written until "Imagine with these changes", so that the
// choice file and the imagined subject stay in step.
void MainWindow::apply_pick_edit() {
  const int i = selected_item(), manner_at = manner_index();
  if (filling_ || busy() || i < 0 || manner_at < 0) return;
  const int item_id = scene_->items[static_cast<size_t>(i)].id;
  bs::Json* pick = pick_for(item_id);
  if (!pick) return;
  const int m = mechanism().SelectedIndex();
  const int s = strength().SelectedIndex();
  const std::vector<const char*>& names = manners()[static_cast<size_t>(manner_at)].mechanisms;
  if (m < 0 || m > static_cast<int>(names.size()) || s < 0 || s > 2) return;
  pick->member("mechanism") = bs::Json::of(std::string(m == 0 ? kAsItIs : names[static_cast<size_t>(m - 1)]));
  pick->member("strength") = bs::Json::of(static_cast<double>(s + 1));
  waiting_items_.insert(item_id);
  set_item_row(i);
  update_controls();
}

// Saves the choice with the changes that wait, marked as edited by hand.
// The imagined subject is from then on out of date.
bool MainWindow::write_choice() {
  if (!choice_.is_object() || choice_path_.empty()) return false;
  choice_.member("edited_by_hand") = bs::Json::of(true);
  if (!write_text(choice_path_, bs::write_json(choice_))) {
    tell(L"The choice file cannot be written:\n" + choice_path_ + L"\n\nThe changes still wait.");
    return false;
  }
  waiting_items_.clear();
  subject_stale_ = subject_ != nullptr;
  stale_from_scene_ = false;
  return true;
}

// "Imagine with these changes": the choice is saved and carried out.
void MainWindow::imagine_with_changes() {
  if (busy()) return;
  if (changes_wait() && !write_choice()) return;
  update_controls();
  start_imagine(false);
}

// Drops the changes that wait: the choice is the file's again.
void MainWindow::discard_changes() {
  load_interpretation();
  set_status(L"The changes were discarded.");
}

// Before the editor of the changes is left behind: if changes wait, asks
// what to do with them. True if nothing waits any more.
Windows::Foundation::IAsyncOperation<bool> MainWindow::settle_changes() {
  const auto keep = get_strong();
  if (!changes_wait()) co_return true;
  const Settle answer = static_cast<Settle>(co_await ask_about_changes());
  if (answer == Settle::Discard) {
    discard_changes();
    co_return true;
  }
  if (answer == Settle::Imagine) imagine_with_changes();
  co_return false;
}

Windows::Foundation::IAsyncOperation<int32_t> MainWindow::ask_about_changes() {
  const auto keep = get_strong();
  if (dialog_open_ || !root().XamlRoot()) co_return static_cast<int32_t>(Settle::Stay);
  dialog_open_ = true;
  const size_t n = waiting_items_.size();
  const muxc::ContentDialog dialog = make_dialog(
      n == 1 ? std::wstring(L"1 change to what the manner does has not been imagined yet.\n\n"
                            L"Imagine with it, or discard it?")
             : std::to_wstring(n) + L" changes to what the manner does have not been imagined yet.\n\n"
                                    L"Imagine with them, or discard them?");
  dialog.PrimaryButtonText(L"Imagine");
  dialog.SecondaryButtonText(L"Discard");
  dialog.CloseButtonText(L"Stay here");
  dialog.DefaultButton(muxc::ContentDialogButton::Close);
  const muxc::ContentDialogResult result = co_await dialog.ShowAsync();
  dialog_open_ = false;
  show_messages();
  co_return static_cast<int32_t>(result == muxc::ContentDialogResult::Primary     ? Settle::Imagine
                                 : result == muxc::ContentDialogResult::Secondary ? Settle::Discard
                                                                                  : Settle::Stay);
}

// ---------------------------------------------------------------------------
// Status and the state of the controls

void MainWindow::show_progress(const bs::Progress& p) {
  const float fraction = std::clamp(p.fraction, 0.f, 1.f);
  progress().Value(std::lround(fraction * kProgressRange));
  const int percent = static_cast<int>(fraction * 100.f);
  set_status(pretty_name(p.stage, true) + L"\n" + std::to_wstring(percent) + L"% - " + std::to_wstring(p.strokes) +
             L" strokes");
}

void MainWindow::set_status(const std::wstring& text) {
  if (text == status_text_) return;  // avoid needless work on the label
  status_text_ = text;
  status().Text(text);
  editStatus().Text(text);  // the editor says the same in its own place
}

void MainWindow::update_description() {
  const int i = medium().SelectedIndex();
  std::wstring text;
  if (i >= 0 && i < static_cast<int>(media_.size())) text = widen(bs::describe_medium(media_[static_cast<size_t>(i)]));
  description().Text(text);
}

// Shows the one setting the chosen medium has, in the row kept for it.
void MainWindow::update_medium_options() {
  const int i = medium().SelectedIndex();
  const std::string chosen = (i >= 0 && i < static_cast<int>(media_.size())) ? media_[static_cast<size_t>(i)] : "";
  skyBlendTip().Visibility(chosen == "oil" ? mux::Visibility::Visible : mux::Visibility::Collapsed);
  keyTip().Visibility(chosen == "watercolor" ? mux::Visibility::Visible : mux::Visibility::Collapsed);
}

void MainWindow::update_key_text() {
  const int pct = key_min_pct_ + kKeyStepPct * static_cast<int>(std::lround(key().Value()));
  wchar_t text[16];
  std::swprintf(text, std::size(text), L"%.2f", pct / 100.0);
  keyValue().Text(text);
}

// Enables what the state permits. What it does not permit says why
// (gate, gate_menu), so nothing is refused only after it was pressed.
void MainWindow::update_controls() {
  const std::wstring busy = busy_reason(), tools = tools_reason();
  const std::wstring no_photo = photo_ ? L"" : L"Open a photo first.";
  const bool stopping = tool_stop_.load();
  const std::wstring stop_reason = stopping ? L"Stopping the step." : L"";

  gate_menu(menuOpen(), L"Ctrl+O", busy_word());
  gate_menu(menuSave(), L"Ctrl+S", first_of({busy_word(), artwork_ ? L"" : L"no artwork yet"}));
  gate_menu(menuShowInFolder(), L"", photo_ ? L"" : L"no photo open");

  // The settings of a run do not change during one, nor under a step.
  gate(medium(), mediumTip(), busy);
  gate(surface(), surfaceTip(), busy);
  gate(size(), sizeTip(), busy);
  gate(effort(), effortTip(), busy);
  gate(seed(), seedTip(), busy);
  // Oil blends what the scene calls sky and cloud, so without the scene the
  // box has nothing to act on. What it is set to stays as it was.
  const bool scene_used = what_ == What::Scene || (what_ == What::Manner && (!subject_ || subject_scene_));
  gate(skyBlend(), skyBlendTip(),
       first_of({busy, scene_used ? L""
                       : scene_  ? L"It needs the scene, which is not used: choose \"The photo, using its scene\"."
                                 : L"It needs the photo's scene, and the photo has none."}));
  gate(key(), keyTip(), busy);
  start().Content(boxed(working_ ? L"Stop" : L"Start"));
  gate(start(), startTip(), start_reason());
  gate(view(), viewTip(), photo_frame_ ? L"" : L"Open a photo first.");

  // What is painted is input to a run, like the settings.
  gate(whatScene(), whatSceneTip(),
       first_of({busy, no_photo, scene_ ? L"" : L"The photo has no scene. \"Observe photo\" below writes one."}));
  gate(whatAlone(), whatAloneTip(), first_of({busy, no_photo}));
  gate(whatManner(), whatMannerTip(), first_of({busy, no_photo}));
  gate(manner(), mannerTip(), first_of({busy, no_photo}));
  update_lines();

  // The editor of the items, in the form it has.
  const bool changes = mode_ == Mode::EditChanges;
  items().IsEnabled(scene_ != nullptr);
  const std::wstring edit_reason =
      first_of({busy, selected_item() >= 0 ? L"" : L"Select an item in the list, or click it in the picture."});
  gate(importance(), importanceTip(), edit_reason);
  gate(treatment(), treatmentTip(), edit_reason);
  gate(edges(), edgesTip(), edit_reason);
  const std::wstring pick_reason = first_of(
      {edit_reason, (manner_index() >= 0 && choice_.is_object()) ? L"" : L"The model has not chosen for this manner yet."});
  gate(mechanism(), mechanismTip(), pick_reason);
  gate(strength(), strengthTip(), pick_reason);

  // A step that is running is stopped with the button that started it.
  if (changes) {
    editRedo().Content(boxed(L"Ask the model again"));
    gate(editRedo(), editRedoTip(), first_of({busy, tools}));
  } else if (tool_ == Tool::Observe) {
    editRedo().Content(boxed(L"Stop"));
    gate(editRedo(), editRedoTip(), stop_reason);
  } else {
    editRedo().Content(boxed(L"Observe again"));
    gate(editRedo(), editRedoTip(), first_of({busy, tools}));
  }
  const bool ready = subject_ && !subject_stale_;
  imagineChanges().Content(boxed(tool_ == Tool::Imagine ? L"Stop"
                                 : changes_wait()      ? L"Imagine with these changes"
                                 : ready               ? L"Imagine again"
                                                       : L"Imagine"));
  gate(imagineChanges(), imagineChangesTip(), tool_ == Tool::Imagine ? stop_reason : first_of({busy, tools}));
  gate(discardChanges(), discardTip(), first_of({busy, changes_wait() ? L"" : L"No changes are waiting."}));

  std::wstring note;
  if (changes && tool_ == Tool::Imagine) {
    note = L"The subject is being imagined.";
  } else if (!changes) {
    note = L"A change is saved at once to the scene file. The painters act on the treatments skin, hair, sky, "
           L"cloud and foliage mass.";
  } else if (changes_wait()) {
    const size_t n = waiting_items_.size();
    note = (n == 1 ? std::wstring(L"1 change") : std::to_wstring(n) + L" changes") + L" not yet imagined.";
  } else if (!subject_) {
    note = L"The subject has not been imagined yet.";
  } else if (subject_stale_) {
    note = stale_from_scene_ ? L"The imagined subject is out of date: the scene was edited."
                             : L"The imagined subject is out of date: the changes were edited.";
  } else {
    note = L"The imagined subject is up to date.";
  }
  editNote().Text(note);
}

// ---------------------------------------------------------------------------
// Preview

MainWindow::View MainWindow::shown_view() {
  const int i = view().SelectedIndex();
  return (i >= 0 && i < static_cast<int>(view_choices_.size())) ? view_choices_[static_cast<size_t>(i)] : View::Photo;
}

// Chooses a view if it is on offer; otherwise the choice stays.
void MainWindow::set_view(View v) {
  const bool was_filling = filling_;
  filling_ = true;
  for (size_t i = 0; i < view_choices_.size(); ++i) {
    if (view_choices_[i] != v) continue;
    view().SelectedIndex(static_cast<int>(i));
    break;
  }
  filling_ = was_filling;
  update_view_menu();
  invalidate_preview();
}

bool MainWindow::view_offered(View v) const {
  return std::find(view_choices_.begin(), view_choices_.end(), v) != view_choices_.end();
}

// The View menu says what the "Show" box says: a view that is on offer is
// enabled, and the one shown is marked. One that is not says what it lacks.
void MainWindow::update_view_menu() {
  const View shown = shown_view();
  struct Entry {
    muxc::RadioMenuFlyoutItem item;
    View view;
    const wchar_t* shortcut;
    const wchar_t* lacks;
  };
  const Entry entries[] = {
      {menuViewPhoto(), View::Photo, L"Ctrl+1", L"no photo open"},
      {menuViewScene(), View::Scene, L"Ctrl+2", L"no scene"},
      {menuViewSubject(), View::Subject, L"Ctrl+3",
       what_ == What::Manner ? L"not imagined yet" : L"no interpretation chosen"},
      {menuViewArtwork(), View::Artwork, L"Ctrl+4", L"not started yet"}};
  for (const Entry& e : entries) {
    const bool offered = view_offered(e.view);
    gate_menu(e.item, e.shortcut, offered ? L"" : (photo_frame_ ? e.lacks : L"no photo open"));
    e.item.IsChecked(offered && e.view == shown);
  }
}

// Offers the views there is something to show for, keeping the current one
// if it is still among them.
void MainWindow::update_view_choices() {
  const View current = shown_view();
  const bool was_filling = filling_;
  filling_ = true;
  view().Items().Clear();
  view_choices_.clear();
  auto offer = [&](const wchar_t* text, View v) {
    add_choice(view(), text);
    view_choices_.push_back(v);
  };
  if (photo_frame_) offer(L"Photo", View::Photo);
  if (photo_frame_ && scene_) offer(L"Scene", View::Scene);
  if (subject_frame_) offer(L"Imagined subject", View::Subject);
  if (working_ || art_frame_) offer(L"Artwork", View::Artwork);
  if (!view_choices_.empty()) view().SelectedIndex(0);
  filling_ = was_filling;
  set_view(current);
}

const MainWindow::Frame* MainWindow::shown_frame() {
  switch (shown_view()) {
    case View::Scene:
      if (scene_frame_) return scene_frame_.get();
      break;
    case View::Subject:
      if (subject_frame_) return subject_frame_.get();
      break;
    case View::Artwork:
      if (art_frame_) return art_frame_.get();
      if (working_) return nullptr;  // the first frame of a new artwork is on its way
      break;
    default:
      break;
  }
  return photo_frame_.get();
}

void MainWindow::invalidate_preview() {
  if (shown_view() == View::Scene && !scene_frame_ && scene_ && photo_frame_)
    scene_frame_ = make_scene_frame(*photo_frame_, *scene_, selected_item());
  render_preview();
}

// Puts the picture to show into the preview, fitted to it and keeping its
// proportions. The picture is reduced here to the pixels it is shown on, by
// averaging, so that a large photo is not shown through a coarse filter;
// one that is smaller than its place is left to the element to enlarge.
void MainWindow::render_preview() {
  const Frame* frame = shown_frame();
  if (!frame || frame->w <= 0 || frame->h <= 0) {
    picture().Visibility(mux::Visibility::Collapsed);
    previewHint().Text(working_ ? L"Preparing..." : L"Open a photo from the File menu (Ctrl+O), or drop one here");
    previewHint().Visibility(mux::Visibility::Visible);
    return;
  }
  const auto xaml_root = root().XamlRoot();
  const double scale = xaml_root ? xaml_root.RasterizationScale() : 1.0;
  const int cw = static_cast<int>(std::floor(preview().ActualWidth() * scale));
  const int ch = static_cast<int>(std::floor(preview().ActualHeight() * scale));
  if (cw <= 0 || ch <= 0 || scale <= 0) return;

  // Aspect-fit inside a small margin, centered. The photo and the artwork
  // share an aspect ratio, so toggling between them keeps the picture in place.
  const int pad = std::min(static_cast<int>(std::lround(kPreviewPadDip * scale)), std::min(cw, ch) / 8);
  const double aw = cw - 2 * pad, ah = ch - 2 * pad;
  const double fit = std::min(aw / frame->w, ah / frame->h);
  const int dw = std::clamp(static_cast<int>(std::lround(frame->w * fit)), 1, cw);
  const int dh = std::clamp(static_cast<int>(std::lround(frame->h * fit)), 1, ch);

  const bool reduce = dw < frame->w && dh < frame->h;
  const int bw = reduce ? dw : frame->w, bh = reduce ? dh : frame->h;
  if (!bitmap_ || bitmap_w_ != bw || bitmap_h_ != bh) {
    bitmap_ = mux::Media::Imaging::WriteableBitmap(bw, bh);
    bitmap_w_ = bw;
    bitmap_h_ = bh;
    picture().Source(bitmap_);
  }
  uint8_t* pixels = bitmap_.PixelBuffer().data();
  if (reduce) reduce_frame(*frame, bw, bh, pixels);
  else std::memcpy(pixels, frame->bgra.data(), frame->bgra.size());
  bitmap_.Invalidate();

  picture().Width(dw / scale);
  picture().Height(dh / scale);
  picture().Visibility(mux::Visibility::Visible);
  previewHint().Visibility(mux::Visibility::Collapsed);
}

}  // namespace winrt::BroadstrokeStudio::implementation
