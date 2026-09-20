#include "azy/core/panel_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "azy/core/strings.hpp"

namespace azy {
namespace {

Rect make_rect(int left, int top, int right, int bottom) {
    Rect r;
    r.left = left;
    r.top = top;
    r.right = std::max(left, right);
    r.bottom = std::max(top, bottom);
    return r;
}

// The part of a rectangle that lies inside `bounds`, or an empty rectangle.
Rect clip_to(const Rect& rect, const Rect& bounds) {
    return make_rect(std::max(rect.left, bounds.left), std::max(rect.top, bounds.top),
                     std::min(rect.right, bounds.right), std::min(rect.bottom, bounds.bottom));
}

// A band of at most `height_px` at the top of `rect` (clamped, never negative).
Rect top_band(const Rect& rect, int height_px) {
    const int height = std::max(0, std::min(height_px, rect.height()));
    return make_rect(rect.left, rect.top, rect.right, rect.top + height);
}

Rect bottom_band(const Rect& rect, int height_px) {
    const int height = std::max(0, std::min(height_px, rect.height()));
    return make_rect(rect.left, rect.bottom - height, rect.right, rect.bottom);
}

// The part below a top band of `height_px`.
Rect below_band(const Rect& rect, int height_px) {
    const int height = std::max(0, std::min(height_px, rect.height()));
    return make_rect(rect.left, rect.top + height, rect.right, rect.bottom);
}

// The part above a bottom band of `height_px`.
Rect above_band(const Rect& rect, int height_px) {
    const int height = std::max(0, std::min(height_px, rect.height()));
    return make_rect(rect.left, rect.top, rect.right, rect.bottom - height);
}

}  // namespace

const char* workspace_name(WorkspaceId workspace) {
    switch (workspace) {
        case WorkspaceId::Auto: return "Auto";
        case WorkspaceId::Editing: return "Editing";
        case WorkspaceId::Color: return "Color";
        case WorkspaceId::Audio: return "Audio";
        case WorkspaceId::Effects: return "Effects";
        case WorkspaceId::Graphics: return "Graphics";
    }
    return "Editing";
}

const char* workspace_key(WorkspaceId workspace) {
    switch (workspace) {
        case WorkspaceId::Auto: return "auto";
        case WorkspaceId::Editing: return "editing";
        case WorkspaceId::Color: return "color";
        case WorkspaceId::Audio: return "audio";
        case WorkspaceId::Effects: return "effects";
        case WorkspaceId::Graphics: return "graphics";
    }
    return "auto";
}

bool workspace_from_key(const std::string& key, WorkspaceId& out) {
    const std::string k = to_lower(trim(key));
    if (k == "auto" || k.empty()) { out = WorkspaceId::Auto; return true; }
    if (k == "editing" || k == "edit" || k == "assembly") { out = WorkspaceId::Editing; return true; }
    if (k == "color" || k == "colour" || k == "grading") { out = WorkspaceId::Color; return true; }
    if (k == "audio") { out = WorkspaceId::Audio; return true; }
    if (k == "effects") { out = WorkspaceId::Effects; return true; }
    if (k == "graphics" || k == "captions" || k == "titles") { out = WorkspaceId::Graphics; return true; }
    return false;
}

WorkspaceId resolve_workspace(WorkspaceId requested) {
    // `Auto` means "not chosen yet". Detection would replace this one line; until
    // then the Editing layout is the one every Premiere install opens with, which
    // is also the one every screenshot in the documentation shows.
    return requested == WorkspaceId::Auto ? WorkspaceId::Editing : requested;
}

const char* panel_name(PanelId panel) {
    switch (panel) {
        case PanelId::MenuBar: return "Menu bar";
        case PanelId::ApplicationHeader: return "Application header";
        case PanelId::Toolbar: return "Toolbar";
        case PanelId::Project: return "Project";
        case PanelId::EffectControls: return "Effect Controls";
        case PanelId::SourceMonitor: return "Source Monitor";
        case PanelId::ProgramMonitor: return "Program Monitor";
        case PanelId::RightDock: return "Right dock";
        case PanelId::Timeline: return "Timeline";
        case PanelId::AudioMeters: return "Audio meters";
        case PanelId::StatusBar: return "Status area";
        case PanelId::Count: break;
    }
    return "Panel";
}

WorkspaceProfile workspace_profile(WorkspaceId workspace) {
    WorkspaceProfile p;
    switch (resolve_workspace(workspace)) {
        case WorkspaceId::Editing:
            break;  // the defaults above are the Editing layout
        case WorkspaceId::Color:
            // Grading: a wider right dock (Lumetri), a taller timeline, no left
            // column split - the colorist's panels stack in the right dock.
            p.left_column = 0.18;
            p.right_column = 0.28;
            p.timeline = 0.38;
            p.left_split = 0.60;
            p.center_split = 0.50;
            p.audio_meters = 0.06;
            break;
        case WorkspaceId::Audio:
            // Audio: a wide right dock for the mixer and meters, a short timeline.
            p.left_column = 0.16;
            p.right_column = 0.34;
            p.timeline = 0.26;
            p.left_split = 0.62;
            p.center_split = 0.55;
            p.audio_meters = 0.10;
            break;
        case WorkspaceId::Effects:
            // Effects: a wide left column (Effect Controls / Effects), narrow right.
            p.left_column = 0.30;
            p.right_column = 0.16;
            p.timeline = 0.30;
            p.left_split = 0.50;
            p.center_split = 0.50;
            p.audio_meters = 0.04;
            break;
        case WorkspaceId::Graphics:
            // Graphics/captions: a wider program monitor, a short timeline.
            p.left_column = 0.20;
            p.right_column = 0.22;
            p.timeline = 0.28;
            p.left_split = 0.58;
            p.center_split = 0.45;
            p.audio_meters = 0.04;
            break;
        case WorkspaceId::Auto:
            // resolve_workspace() never returns Auto, but the switch stays total on
            // purpose: adding a workspace must be a compile error here, not a
            // silent fallback to somebody else's layout.
            p = WorkspaceProfile{};
            break;
    }
    return p;
}

std::vector<PanelRect> build_panel_map(const Rect& client, unsigned dpi, WorkspaceId workspace,
                                       int min_panel_px) {
    const WorkspaceProfile profile = workspace_profile(workspace);
    std::vector<PanelRect> panels;
    panels.reserve(static_cast<size_t>(PanelId::Count));

    auto add = [&](PanelId id, const Rect& rect) {
        PanelRect panel;
        panel.id = id;
        panel.rect = clip_to(rect, client);
        panel.usable = panel.rect.width() >= min_panel_px && panel.rect.height() >= min_panel_px;
        panels.push_back(panel);
    };

    // A client area that cannot hold anything is answered honestly: every panel is
    // listed, none is usable, nothing is drawn.
    if (client.empty()) {
        for (int i = 0; i < static_cast<int>(PanelId::Count); ++i) {
            add(static_cast<PanelId>(i), make_rect(0, 0, 0, 0));
        }
        return panels;
    }

    // The two fixed bands are DIP heights: a menu bar is the same physical size at
    // every scaling factor, which is what keeps the model honest at 100-200%.
    const int menu_px = dip_to_px(profile.menu_bar_dip, dpi);
    const int header_px = dip_to_px(profile.application_header_dip, dpi);
    const int status_px = dip_to_px(profile.status_bar_dip, dpi);

    const Rect menu = top_band(client, menu_px);
    const Rect below_menu = below_band(client, menu_px);
    const Rect status = status_px > 0 ? bottom_band(client, status_px) : make_rect(0, 0, 0, 0);
    const Rect body = above_band(below_menu, status_px);
    const Rect header = top_band(body, header_px);
    const Rect rest = below_band(body, header_px);

    // Premiere draws its tool row inside the same strip as the application header,
    // so they are reported as the same band (the debug overlay labels it once).
    const Rect toolbar = header;

    const int timeline_h = static_cast<int>(std::lround(rest.height() * profile.timeline));
    const Rect timeline = bottom_band(rest, timeline_h);
    const Rect middle = above_band(rest, timeline_h);

    const int left_w = static_cast<int>(std::lround(middle.width() * profile.left_column));
    const int right_w = static_cast<int>(std::lround(middle.width() * profile.right_column));
    const Rect left_column = make_rect(middle.left, middle.top, middle.left + left_w, middle.bottom);
    const Rect center = make_rect(middle.left + left_w, middle.top, middle.right - right_w, middle.bottom);
    const Rect right_column = make_rect(middle.right - right_w, middle.top, middle.right, middle.bottom);

    const int left_cut = static_cast<int>(std::lround(left_column.height() * profile.left_split));
    const Rect project = top_band(left_column, left_cut);
    const Rect effect_controls = below_band(left_column, left_cut);

    const int center_cut = static_cast<int>(std::lround(center.height() * profile.center_split));
    const Rect source = top_band(center, center_cut);
    const Rect program = below_band(center, center_cut);

    // The meters are usually a narrow strip on the right edge of the right dock.
    int meters_w = static_cast<int>(std::lround(right_column.width() * profile.audio_meters));
    if (meters_w < profile.audio_meters_min_px) meters_w = profile.audio_meters_min_px;
    // ... but never more than half of the dock it lives in: the model may not push
    // the dock itself below the minimum size.
    if (meters_w > right_column.width() / 2) meters_w = right_column.width() / 2;
    const Rect meters = make_rect(right_column.right - meters_w, right_column.top, right_column.right,
                                  right_column.bottom);
    const Rect right_dock = make_rect(right_column.left, right_column.top, right_column.right - meters_w,
                                      right_column.bottom);

    add(PanelId::MenuBar, menu);
    add(PanelId::ApplicationHeader, header);
    add(PanelId::Toolbar, toolbar);
    add(PanelId::Project, project);
    add(PanelId::EffectControls, effect_controls);
    add(PanelId::SourceMonitor, source);
    add(PanelId::ProgramMonitor, program);
    add(PanelId::RightDock, right_dock);
    add(PanelId::Timeline, timeline);
    add(PanelId::AudioMeters, meters);
    add(PanelId::StatusBar, status);
    return panels;
}

std::string describe_panel_map(const std::vector<PanelRect>& panels) {
    std::string out;
    for (const PanelRect& panel : panels) {
        if (!out.empty()) out += "\n";
        if (!panel.usable) {
            out += str_format("%s: not placeable in this client area", panel_name(panel.id));
            continue;
        }
        out += str_format("%s (%d,%d)-(%d,%d)  %dx%d", panel_name(panel.id), panel.rect.left, panel.rect.top,
                          panel.rect.right, panel.rect.bottom, panel.rect.width(), panel.rect.height());
    }
    return out;
}

}  // namespace azy
