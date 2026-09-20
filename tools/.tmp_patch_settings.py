import pathlib

path = pathlib.Path('/home/user/azy/src/core/settings.cpp')
t = path.read_text()

edits = [
    (
        '    out += "\\n[advanced]\\n";\n    out += "experimental=" + bool_str(experimental) + "\\n";\n',
        '    out += "\\n[advanced]\\n";\n    out += "experimental=" + bool_str(experimental) + "\\n";\n'
        '    out += "debug_mode=" + bool_str(debug_mode) + "\\n";\n'
        '    out += std::string("ui_profile=") + workspace_key(ui_profile) + "\\n";\n',
    ),
    (
        '        "suspend_while_minimized", "suspend_while_inactive", "experimental", "safe_mode",\n',
        '        "suspend_while_minimized", "suspend_while_inactive", "experimental", "debug_mode",\n'
        '        "ui_profile", "safe_mode",\n',
    ),
    (
        '        } else if (key == "experimental") {\n'
        '            bool v = false;\n'
        '            if (parse_bool(value, v)) s.experimental = v;\n',
        '        } else if (key == "experimental") {\n'
        '            bool v = false;\n'
        '            if (parse_bool(value, v)) s.experimental = v;\n'
        '        } else if (key == "debug_mode") {\n'
        '            bool v = false;\n'
        '            if (parse_bool(value, v)) s.debug_mode = v;\n'
        '        } else if (key == "ui_profile") {\n'
        '            WorkspaceId workspace = WorkspaceId::Auto;\n'
        '            if (workspace_from_key(value, workspace)) {\n'
        '                s.ui_profile = workspace;\n'
        '            } else if (warnings) {\n'
        '                warnings->push_back("unknown ui_profile \'" + value + "\'; using the Editing layout");\n'
        '            }\n',
    ),
]

for old, new in edits:
    count = t.count(old)
    assert count == 1, (old[:60], count)
    t = t.replace(old, new)

path.write_text(t)
print('settings.cpp patched')
