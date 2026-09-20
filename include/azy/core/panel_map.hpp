// Azy Skin — portable core: the panel map.
//
// Premiere Pro's docked panels are not windows. Its whole workspace is one
// client area that Premiere paints itself (see docs/FEASIBILITY.md §1), so there
// is no handle to ask "where is the Timeline?". What an outside process *can*
// know exactly is the client rectangle, the monitor and the DPI — and what it can
// model on top of that is a layout.
//
// This file is that model, and it is deliberately pure arithmetic: a workspace
// profile (ratios, not pixel coordinates) applied to a client rectangle. Because
// everything is a ratio, resizing, maximising, moving between monitors and
// 100–200% scaling all fall out of the same numbers, and a Premiere redesign is a
// table edit rather than a rewrite (spec §3, §32, §37).
//
// Two rules keep it safe:
//   * every rectangle is clipped to the client area and must have a positive size
//     left, or the panel is reported as unusable rather than as a negative rect;
//   * a client area too small to hold a panel simply omits it. An unplaceable
//     panel costs a missing hairline, never a broken layout (spec §34).
#pragma once

#include <string>
#include <vector>

#include "azy/core/geometry.hpp"

namespace azy {

// Which Premiere workspace the model assumes. The user picks it by hand today
// (spec §39: "manual UI profile"); automatic detection can feed the same field
// later, because every consumer only ever sees this enum.
enum class WorkspaceId {
    Auto = 0,   // not chosen: the model uses Editing
    Editing = 1,
    Color = 2,
    Audio = 3,
    Effects = 4,
    Graphics = 5,
};

const char* workspace_name(WorkspaceId workspace);
const char* workspace_key(WorkspaceId workspace);           // stable INI identifier
bool workspace_from_key(const std::string& key, WorkspaceId& out);
// The workspace the model actually uses (`Auto` resolves to Editing).
WorkspaceId resolve_workspace(WorkspaceId requested);

// The regions Azy knows how to talk about. Order is the order they are laid out
// (and the order the debug overlay draws them in, so the picture matches the list).
enum class PanelId {
    MenuBar = 0,
    ApplicationHeader,
    Toolbar,
    Project,
    EffectControls,
    SourceMonitor,
    ProgramMonitor,
    RightDock,     // Lumetri / Effects / History: whichever is docked on the right
    Timeline,
    AudioMeters,
    StatusBar,
    Count,
};

const char* panel_name(PanelId panel);

struct PanelRect {
    PanelId id = PanelId::MenuBar;
    Rect rect;
    // False when the client area was too small for this panel: the caller must
    // skip it entirely rather than draw something degenerate.
    bool usable = false;
};

// One workspace's proportions. Every field is a fraction of the client area (or a
// DIP height for the two fixed bands), never a pixel coordinate, so the same
// profile describes the layout at any window size, DPI or monitor.
struct WorkspaceProfile {
    int menu_bar_dip = 24;        // native menu bar: Windows draws it, fixed height
    int application_header_dip = 30;  // Premiere's own header/toolbar strip below it
    int status_bar_dip = 0;       // 0 = Premiere's status area lives inside the timeline band
    double left_column = 0.22;    // Project / Effect Controls / Effects
    double right_column = 0.20;   // Lumetri, History, Audio meters when docked right
    double timeline = 0.34;       // bottom band
    double left_split = 0.55;     // where the left column splits into two panels
    double center_split = 0.52;   // Source Monitor above, Program Monitor below
    // Audio meters: a narrow strip inside the right column, never narrower than
    // `audio_meters_min_px` (a 15px "panel" is not a panel, and the map reports it
    // as unusable rather than decorating something invisible).
    double audio_meters = 0.16;
    int audio_meters_min_px = 28;
};

// The profile for a workspace and a client size. `ui_scale` is the DPI factor
// (1.0 at 96 dpi): the fixed bands are given in DIP and converted here, so a
// header is the same *physical* size at every scaling factor.
WorkspaceProfile workspace_profile(WorkspaceId workspace);

// Builds the panel map for a client rectangle (physical pixels, origin at the
// client's top-left) at a given DPI.
//
// The result always contains all `PanelId` entries in enum order; `usable` says
// whether each one can be decorated. Rectangles that would be thinner than
// `min_panel_px` in either direction are reported unusable - a 3px "panel" is
// not a panel, and drawing chrome around it would look like a rendering bug.
std::vector<PanelRect> build_panel_map(const Rect& client, unsigned dpi, WorkspaceId workspace,
                                       int min_panel_px = 24);

// One line per panel, for the log and for the debug overlay: e.g.
// "Timeline  (0,656)-(1920,1080)  1920x424". Unusable panels say so.
std::string describe_panel_map(const std::vector<PanelRect>& panels);

}  // namespace azy
