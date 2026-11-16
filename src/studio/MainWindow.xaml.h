// The studio window (spec 2.10), on WinUI 3.
//
// Nothing here changes what the engine makes: the window gathers a photo, a
// scene and settings and calls bs::create_artwork, as the command line does.
// The scene view in the preview is drawn for the screen only.
//
// Threading and ownership:
//   * The UI thread owns the window and all of its state except the Mailbox.
//   * bs::create_artwork runs on a worker thread. The worker never touches a
//     control or UI state. It writes its latest progress, its latest rendered
//     frame and, at the end, the finished Artwork into the Mailbox (guarded by
//     a mutex) and queues one wake-up, which carries no data, on the UI
//     thread's dispatcher.
//   * Wake-ups are coalesced with an atomic flag: a new one is queued only
//     after the UI thread has consumed the previous one, so the queue cannot
//     be flooded. Because nothing travels inside a wake-up, a frame can never
//     be lost in the queue; whatever is still in the Mailbox when the window
//     goes away is freed with it.
//   * The worker is always joined on the UI thread: when it reports that it
//     is done, or after a stop request when the window is closed mid-work.
//   * A step outside the engine runs on a thread of its own and reports the
//     same way.
#pragma once

#include "MainWindow.g.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "app/help_text.hpp"
#include "app/process_win32.hpp"
#include "artist/artist.hpp"
#include "core/image.hpp"
#include "core/json.hpp"
#include "support.hpp"
#include "vision/scene.hpp"

namespace winrt::BroadstrokeStudio::implementation {

struct HelpWindow;

struct MainWindow : MainWindowT<MainWindow> {
  using Clock = std::chrono::steady_clock;
  using Frame = bs::studio::Frame;

  MainWindow() = default;
  ~MainWindow();

  // Called once the XAML is loaded and the named controls exist.
  void InitializeComponent();

  // A photo to open once the window is shown (the command line's).
  void set_initial_photo(const std::wstring& path) { initial_photo_ = path; }

  // Handlers named in MainWindow.xaml.
  void OnLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnOpen(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnHelp(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnHelpKey(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                 Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
  void OnShortcut(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                  Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
  void OnShowInFolder(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnShowWork(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnExit(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnViewMenu(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnAbout(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnMediumChanged(Windows::Foundation::IInspectable const&,
                       Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnSeedChanging(Microsoft::UI::Xaml::Controls::TextBox const&,
                      Microsoft::UI::Xaml::Controls::TextBoxBeforeTextChangingEventArgs const&);
  void OnKeyChanged(Windows::Foundation::IInspectable const&,
                    Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
  void OnStart(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnSave(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnViewChanged(Windows::Foundation::IInspectable const&,
                     Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnWhatChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnSceneAction(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnMannerChanged(Windows::Foundation::IInspectable const&,
                       Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnSubjectAction(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnEditChanges(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnEditBack(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnEditRedo(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnImagineChanges(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnDiscardChanges(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void OnItemSelected(Windows::Foundation::IInspectable const&,
                      Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnItemEdited(Windows::Foundation::IInspectable const&,
                    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnPickEdited(Windows::Foundation::IInspectable const&,
                    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnFacetEdited(Windows::Foundation::IInspectable const&,
                     Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
  void OnPreviewSizeChanged(Windows::Foundation::IInspectable const&,
                            Microsoft::UI::Xaml::SizeChangedEventArgs const&);
  void OnPictureClick(Windows::Foundation::IInspectable const&,
                      Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
  void OnDragOver(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
  winrt::fire_and_forget OnDrop(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);

 private:
  // What the preview shows.
  enum class View : int { Photo, Scene, Subject, Artwork };
  // The steps outside the engine that the window can start.
  enum class Tool { None, Observe, Imagine };
  // What is painted: the photo with its scene, the photo as if it had none,
  // or the subject imagined from it in a manner.
  enum class What { Scene, Alone, Manner };
  // What the panel beside the preview is: the painting controls, or the
  // editor of the items in one of its two forms.
  enum class Mode { Paint, EditScene, EditChanges };
  // The answers to "changes are waiting".
  enum class Settle : int32_t { Stay, Imagine, Discard };

  // Everything the worker thread hands to the UI thread. All fields are
  // guarded by `mutex`. A newer progress or frame simply replaces an
  // unconsumed one.
  struct Mailbox {
    std::mutex mutex;
    bool has_progress = false;
    bs::Progress progress;
    std::unique_ptr<Frame> frame;          // newest rendered frame, if not yet taken
    bool done = false;                     // the worker has finished; join it
    std::unique_ptr<bs::Artwork> artwork;  // set together with `done`
    std::string error;                     // set together with `done` on failure
    // From the thread that runs a step outside the engine.
    bool has_tool_line = false;
    std::string tool_line;       // the newest line the step printed
    bool tool_done = false;      // the step has ended; join its thread
    bs::ToolResult tool_result;  // set together with `tool_done`
    std::string tool_tail;       // the last lines it printed, for a failure report
  };

  // The text cells of one row of the list of items.
  struct ItemRow {
    Microsoft::UI::Xaml::Controls::Grid grid{nullptr};
    Microsoft::UI::Xaml::Controls::TextBlock name{nullptr}, importance{nullptr}, treatment{nullptr}, edges{nullptr},
        change{nullptr};
  };

  void setup_window();
  void fill_controls();
  void add_tips();
  void set_tip(Microsoft::UI::Xaml::FrameworkElement const& host, const wchar_t* text);
  void set_menu_tip(Microsoft::UI::Xaml::Controls::MenuFlyoutItem const& item, const wchar_t* text);

  // A control the state does not permit is disabled and says why (spec
  // 2.10): its tooltip begins with the reason; a menu item, which shows no
  // tooltip while disabled, has the reason in the place of its shortcut.
  void explain(Microsoft::UI::Xaml::FrameworkElement const& host, const std::wstring& reason);
  void gate(Microsoft::UI::Xaml::Controls::Control const& control, Microsoft::UI::Xaml::FrameworkElement const& host,
            const std::wstring& reason);
  void gate_menu(Microsoft::UI::Xaml::Controls::MenuFlyoutItem const& item, const wchar_t* shortcut,
                 const std::wstring& reason);
  std::wstring busy_reason() const;  // why nothing else can be started now, or empty
  std::wstring busy_word() const;    // the same in two words, for a menu item
  std::wstring start_reason() const;
  std::wstring tools_reason() const;
  bool check_tools();  // true if what it found has changed
  void apply_minimum_size();
  void on_closed();

  // Messages and questions. A message waits for the one before it.
  void tell(const std::wstring& text);
  winrt::fire_and_forget show_messages();
  Windows::Foundation::IAsyncOperation<bool> ask(std::wstring text, hstring yes, hstring no);
  Windows::Foundation::IAsyncOperation<int32_t> ask_about_changes();
  Microsoft::UI::Xaml::Controls::ContentDialog make_dialog(const std::wstring& text);

  bs::HelpTopic topic_of_focus();
  void show_help(bs::HelpTopic topic);
  winrt::fire_and_forget show_about();

  // What the state permits. A menu item, its shortcut and a button ask the
  // same question.
  bool can_open() const { return !busy(); }
  bool can_save() const { return !busy() && artwork_ != nullptr; }
  bool can_start() const { return start_reason().empty(); }
  void show_photo_in_folder();

  winrt::fire_and_forget choose_photo();
  bool load_photo(const std::wstring& path);
  winrt::fire_and_forget save_artwork();
  void start_painting();
  void request_stop();
  void finish(std::unique_ptr<bs::Artwork> art, const std::string& error);
  void drain_mailbox();
  void show_progress(const bs::Progress& p);
  void set_status(const std::wstring& text);
  void update_description();
  void update_medium_options();
  void update_key_text();
  void update_controls();
  const Frame* shown_frame();

  // What the preview shows: the choices offered follow what there is to show.
  View shown_view();
  void set_view(View v);
  void update_view_choices();
  bool view_offered(View v) const;
  void update_view_menu();
  void invalidate_preview();
  void render_preview();

  // What to paint, and the panel that is shown.
  void set_what(What what);
  bool use_scene() const { return what_ != What::Alone; }
  void set_mode(Mode mode);
  winrt::fire_and_forget leave_editor();
  winrt::fire_and_forget close_after_asking();
  void update_lines();

  // The scene and its items.
  void load_scene(int photo_w, int photo_h);
  std::string scene_file() const;  // UTF-8; where the open photo's scene file is or would be
  void show_scene();
  std::wstring scene_summary() const;
  void set_item_columns(Microsoft::UI::Xaml::Controls::Grid const& grid);
  void update_item_columns();
  void set_item_row(int row);
  void select_item_row(int row);
  void show_item_editors();
  void on_item_selected();
  void apply_item_edit();
  int selected_item();  // index into scene_->items, or -1

  // The steps outside the engine: observe and interpret.
  bool busy() const { return working_ || tool_ != Tool::None; }
  winrt::fire_and_forget start_observe();
  winrt::fire_and_forget start_imagine(bool ask_again);
  void begin_tool(Tool which, std::vector<bs::studio::ToolStep> steps);
  void request_tool_stop();
  void show_tool_status();
  void finish_tool(const bs::ToolResult& result, const std::string& tail);
  void run_tool_worker(std::vector<bs::studio::ToolStep> steps);  // tool thread only

  // The interpretation of the chosen manner (spec 2.14): the model's choice
  // of a mechanism per item, and the imagined subject made from it.
  int manner_index();  // index into manners(), or -1 for the photo as it is
  std::wstring interpret_dir() const;
  std::wstring choice_file(int index) const;
  std::wstring subject_file(int index) const;
  void load_interpretation();
  bs::Json* pick_for(int item_id);
  std::wstring change_text(int item_id);
  void apply_pick_edit();
  // How the solid of triangles of the selected item ends against each
  // material it meets: by default, ragged or abrupt.
  void show_pairs();
  int stated_pair(const std::string& key) const;  // 0 by default, 1 ragged, 2 abrupt
  void apply_pair_edit(const std::string& key, int which);
  // Cubism cuts all the chosen items into planes together: how many, and
  // how far apart, are settings of the manner as a whole.
  bool manner_is(const char* id);
  void show_facet_settings();
  void apply_facet_edit();
  // Changes to the choice that wait for "Imagine with these changes".
  bool changes_wait() const { return waiting_count() > 0; }
  size_t waiting_count() const { return waiting_items_.size() + waiting_settings_.size(); }
  bool write_choice();
  void discard_changes();
  // True if nothing waits any more and the caller may go on.
  Windows::Foundation::IAsyncOperation<bool> settle_changes();
  void imagine_with_changes();

  // Worker thread only.
  void run_worker(std::shared_ptr<const bs::Image> photo, std::shared_ptr<const bs::Scene> scene, bs::Settings settings);
  void notify_ui();

  HWND hwnd_ = nullptr;  // set once during setup; read-only afterwards
  Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
  // While working, the Mailbox is also polled on a slow timer so that a
  // wake-up that could not be queued cannot stall the window.
  Microsoft::UI::Dispatching::DispatcherQueueTimer poll_timer_{nullptr};
  bool ready_ = false;     // the controls are filled; their events are the user's
  bool filling_ = false;   // a panel is being filled; its events are not edits
  std::wstring initial_photo_;
  BroadstrokeStudio::HelpWindow help_{nullptr};

  // The tooltip of each host: what the control is for, and why it is
  // disabled if it is.
  struct Tip {
    Microsoft::UI::Xaml::FrameworkElement host{nullptr};
    std::wstring text, reason;
  };
  std::vector<Tip> tips_;
  // Whether the steps outside the engine can be started, and if not what is
  // missing. Looked up when a photo is opened and when the window comes to
  // the front, so that setting them up takes effect without a restart.
  bool tools_ready_ = false;
  std::wstring tools_problem_;

  bool dialog_open_ = false;
  std::deque<std::wstring> messages_;  // waiting for the dialog that is open

  std::vector<std::string> media_, surfaces_;
  int key_min_pct_ = 50, key_max_pct_ = 150;
  std::vector<View> view_choices_;  // the entries of the "Show" box and of the View menu
  std::vector<ItemRow> rows_;

  // The photo is shared with the worker for the duration of a run, so a run
  // keeps its subject alive no matter what the UI does meanwhile.
  std::shared_ptr<const bs::Image> photo_;
  // What the photo shows (spec 2.13); null if it has no scene file. It is
  // corrected in place from the scene panel, which is disabled during a run,
  // so the worker's reference never sees a change.
  std::shared_ptr<bs::Scene> scene_;
  std::string scene_path_;                      // UTF-8; the file scene_ was read from
  std::vector<std::string> treatment_choices_;  // the entries of the treatment box
  int shown_item_ = -1;                         // the item the scene view highlights
  std::unique_ptr<Frame> photo_frame_;
  std::unique_ptr<Frame> scene_frame_;  // the items tinted over the photo; made when first shown
  std::wstring photo_path_;
  // The folder of the open photo's working files and the name they have
  // (support.hpp, photo_work_files). The folder is made when something is
  // first written to it.
  std::wstring work_dir_, work_name_;

  std::unique_ptr<bs::Artwork> artwork_;  // what File > Save artwork writes
  std::unique_ptr<Frame> art_frame_;      // latest live frame, then the final image
  std::wstring art_dir_, art_name_;       // default save location and file name
  std::wstring status_text_;

  // The picture the preview shows, made for the size it is shown at.
  Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap_{nullptr};
  int bitmap_w_ = 0, bitmap_h_ = 0;

  bool working_ = false;
  std::thread worker_;
  std::atomic<bool> stop_{false};            // UI -> worker: stop at the next progress call
  std::atomic<bool> notify_pending_{false};  // a wake-up is queued and not yet consumed
  Mailbox mailbox_;

  // A step outside the engine, while it runs. Its thread hands over what the
  // step prints through the Mailbox, like the painting worker.
  Tool tool_ = Tool::None;
  std::thread tool_thread_;
  std::atomic<bool> tool_stop_{false};  // UI -> tool thread: end the step
  Clock::time_point tool_started_{};
  std::wstring tool_line_;  // the newest line the step printed

  // The interpretation of the chosen manner, read from the files of the
  // interpret step beside the photo.
  bs::Json choice_;                           // the choice file; null if there is none
  std::wstring choice_path_;
  std::shared_ptr<const bs::Image> subject_;  // the imagined subject; null if not made
  std::shared_ptr<bs::Scene> subject_scene_;  // its scene file
  std::unique_ptr<Frame> subject_frame_;
  bool subject_stale_ = false;      // the choice or the scene changed after the subject was made
  bool stale_from_scene_ = false;   // ... and it was the scene
  std::set<int> waiting_items_;     // the items whose change was edited and not yet imagined
  // Likewise what is not an item's: the pairs of materials stated ragged or
  // abrupt, and the settings of a manner as a whole.
  std::set<std::string> waiting_settings_;
  std::vector<int> plane_choices_;         // the entries of the Planes box
  int shown_planes_ = 0, shown_apart_ = 0, shown_flat_ = 0;  // what the boxes of cubism show
  struct PairRow {
    Microsoft::UI::Xaml::Controls::Grid row{nullptr};
    Microsoft::UI::Xaml::Controls::ComboBox box{nullptr};
  };
  std::vector<PairRow> pair_rows_;  // the rows of "Where the solid ends"
  What what_ = What::Alone;
  Mode mode_ = Mode::Paint;
  bool closing_ = false;            // the window may close without asking about changes
  std::wstring art_manner_;     // the manner of the artwork shown, for its file name
};

}  // namespace winrt::BroadstrokeStudio::implementation

namespace winrt::BroadstrokeStudio::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}  // namespace winrt::BroadstrokeStudio::factory_implementation
