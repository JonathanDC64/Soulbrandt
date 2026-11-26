"""Tests for the check of recomplauncher.json (tools/release/recomplauncher.py), on made-up sources written the way
settings.cpp, settings_catalog.cpp and settings_values.hpp are.

  python -m unittest discover -s tools/release/tests
"""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomplauncher as r  # noqa: E402

SETTINGS = r'''
namespace {
constexpr Def kDefs[] = {
#if DES_DEV
    {"DES_GOD", "0", Apply::Session},          // developer builds only
#endif
    {"DES_FULLSCREEN", "0", Apply::Live},      // 1 = borderless, 2 = exclusive
#if DES_RSX_VULKAN
#if defined(_WIN32)
    {"RSX_BACKEND", "d3d12", Apply::Restart},
#else
    {"RSX_BACKEND", "vulkan", Apply::Restart},
#endif
#endif
    {"DES_FPS", "", Apply::Live},              // "" = the game's own 30 fps mode
    {"DES_DOF", "1", Apply::Live},
    {"DES_NET_SERVER", kNetServers[0].key, Apply::Restart},
    {"show_start_screen", "1", Apply::UiOnly},
    {"DES_SPEED", "1", Apply::Session},
};
'''

CATALOG = r'''
std::vector<TabDesc> make_tabs() {
    tabs.push_back({"Display", {
        {"Window", {
#ifdef _WIN32
            choice("Display mode", "DES_FULLSCREEN",
                {{"0", "Window"}, {"1", "Borderless fullscreen"}, {"2", "Exclusive fullscreen"}},
                "Borderless fills the display. {not a choice}"),
#else
            choice("Display mode", "DES_FULLSCREEN", {{"0", "Window"}, {"1", "Borderless fullscreen"}},
                "Borderless fills the display."),
#endif
            // choice("Old row", "DES_DOF", {{"7", "Seven"}}, "a comment, not a row"),
            choice("Server", "DES_NET_SERVER", server_choices(), server_help()),
        }},
        {"Frame rate", {
            {.label = "Frame rate", .key = "DES_FPS",
                .kind = ChoiceRow{
                    .choices = with_values(s::kFpsValues, {{"30 fps", true}, {"60 fps"}, {"Unlocked"}}),
                    .custom = ChoiceRow::Custom{"Custom", [] { return std::string("90"); }}},
                .help = "The game's frame rate."},
            {.label = "Custom frame rate", .key = "DES_FPS", .kind = NumberRow{.min = 30, .max = 500},
                .help = "Your own."},
            preview(toggle("Depth of field", "DES_DOF", "The game's blur on distant scenery.")),
#if DES_DEV
            toggle("God mode", "DES_GOD", "Cheats."),
#endif
            own_setting(toggle("Start screen", "show_start_screen", "Play, Settings and Quit.")),
        }},
    }});
}
'''

VALUES = 'inline constexpr const char* kFpsValues[] = {"", "60", "unlocked"};\n'


def manifest():
    return {
        "$schema": r.SCHEMA,
        "format": 1,
        "name": "Game",
        "start": {"program": ["Game.exe", "Game"]},
        "config": [{"id": "main", "format": "ini", "file": [
            {"path": "{install}/data/settings.ini", "if": "{install}/portable.txt"},
            {"path": "{config}/Game/settings.ini"}]}],
        "saves": ["{install}/gamedata/savedata"],
        "looks": {"allowed": False, "reason": "It has its own."},
        "system": {"windows": {"version": "10", "graphics": {"d3d12": "12"}}, "linux": {"glibc": "2.35"}},
        "settings": [
            {"id": "mode", "label": "Display mode", "help": "How the game fills the screen.",
             "choices": [{"label": "Window", "set": {"DES_FULLSCREEN": "0"}},
                         {"label": "Borderless", "set": {"DES_FULLSCREEN": "1"}},
                         {"label": "Exclusive", "set": {"DES_FULLSCREEN": "2"}}],
             "defaults": {"DES_FULLSCREEN": "0"}},
            {"id": "fps", "label": "Frame rate",
             "choices": [{"label": "30 fps", "set": {"DES_FPS": ""}}, {"label": "60 fps", "set": {"DES_FPS": "60"}},
                         {"label": "Unlocked", "set": {"DES_FPS": "unlocked"}}],
             "defaults": {"DES_FPS": ""}},
            {"id": "dof", "label": "Depth of field",
             "choices": [{"label": "On", "set": {"DES_DOF": "1"}}, {"label": "Off", "set": {"DES_DOF": "0"}}],
             "defaults": {"DES_DOF": "1"}},
            {"id": "start", "label": "Start screen", "group": "Start",
             "choices": [{"label": "Show", "set": {"show_start_screen": "1"}},
                         {"label": "Skip", "set": {"show_start_screen": "0"}}],
             "defaults": {"show_start_screen": "1"}},
        ],
    }


class SourcesTest(unittest.TestCase):
    def test_defs(self):
        defs = r.parse_defs(SETTINGS)
        self.assertEqual(defs["DES_GOD"], [("0", "Session", None, True)])
        self.assertEqual(defs["DES_FULLSCREEN"], [("0", "Live", None, False)])
        self.assertEqual(defs["RSX_BACKEND"], [("d3d12", "Restart", "windows", False),
                                               ("vulkan", "Restart", "other", False)])
        self.assertEqual(defs["DES_FPS"], [("", "Live", None, False)])
        self.assertEqual(defs["DES_NET_SERVER"], [(None, "Restart", None, False)])

    def test_no_table(self):
        with self.assertRaises(ValueError):
            r.parse_defs("int x;\n")

    def test_rows(self):
        rows = r.parse_rows(CATALOG, r.parse_value_tables(VALUES))
        self.assertEqual(rows["DES_FULLSCREEN"], [("choice", ["0", "1", "2"], "windows", False),
                                                  ("choice", ["0", "1"], "other", False)])
        self.assertEqual(rows["DES_FPS"], [("choice", ["", "60", "unlocked"], None, False),
                                           ("NumberRow", [], None, False)])
        self.assertEqual(rows["DES_DOF"], [("toggle", ["1", "0"], None, False)])   # (not the commented-out row)
        self.assertEqual(rows["DES_GOD"], [("toggle", ["1", "0"], None, True)])
        self.assertEqual(rows["DES_NET_SERVER"], [("made", [], None, False)])

    def test_folder_names(self):
        text = ('inline constexpr const char* kFolderName = "OldName";\n'
                'inline constexpr const char* kOneFolderName = "OneName";  // (the fixed folder name)\n')
        self.assertEqual(r.parse_folder_names(text), ("OldName", "OneName"))
        with self.assertRaises(ValueError):
            r.parse_folder_names(text.splitlines()[0])

    def test_values_from_an_unknown_table(self):
        with self.assertRaises(ValueError):
            r.parse_rows(CATALOG, {})


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.defs = r.parse_defs(SETTINGS)
        self.rows = r.parse_rows(CATALOG, r.parse_value_tables(VALUES))

    def problems(self, doc):
        return r.check(doc, self.defs, self.rows, "Game")

    def setting(self, doc, sid):
        return next(s for s in doc["settings"] if s["id"] == sid)

    def assertProblem(self, doc, text):
        found = self.problems(doc)
        self.assertTrue(any(text in p for p in found), f"no problem with {text!r} in {found}")

    def test_a_good_file(self):
        self.assertEqual(self.problems(manifest()), [])

    def test_a_value_the_menu_can_write_needs_a_choice(self):
        doc = manifest()
        self.setting(doc, "mode")["choices"].pop()   # (Exclusive, Windows' own)
        self.assertProblem(doc, "DES_FULLSCREEN='2', which the menu can write, has no choice")

    def test_a_value_the_menu_does_not_have(self):
        doc = manifest()
        self.setting(doc, "fps")["choices"].append({"label": "120 fps", "set": {"DES_FPS": "120"}})
        self.assertProblem(doc, "DES_FPS='120' is not one of its row's values")

    def test_the_default_is_kdefs(self):
        doc = manifest()
        self.setting(doc, "dof")["defaults"] = {"DES_DOF": "0"}
        self.assertProblem(doc, "DES_DOF's default is '1' in kDefs, not '0'")

    def test_defaults_for_the_same_keys(self):
        doc = manifest()
        self.setting(doc, "dof")["defaults"] = {}
        self.assertProblem(doc, "its defaults are not for exactly the keys its choices set")

    def test_a_key_that_is_not_a_setting(self):
        doc = manifest()
        self.setting(doc, "dof")["choices"][0]["set"] = {"DES_NOPE": "1"}
        self.setting(doc, "dof")["choices"][1]["set"] = {"DES_NOPE": "0"}
        self.assertProblem(doc, "DES_NOPE is not a setting")

    def test_a_developer_build_or_session_key(self):
        doc = manifest()
        doc["settings"].append({"id": "god", "label": "God", "defaults": {"DES_GOD": "0"},
                                "choices": [{"label": "Off", "set": {"DES_GOD": "0"}},
                                            {"label": "On", "set": {"DES_GOD": "1"}}]})
        found = self.problems(doc)
        self.assertTrue(any("a developer build's" in p for p in found), found)
        self.assertTrue(any("per session" in p for p in found), found)
        self.assertTrue(any("no choice or toggle row" in p for p in found), found)

    def test_a_default_per_system(self):
        doc = manifest()
        doc["settings"].append({"id": "renderer", "label": "Renderer", "defaults": {"RSX_BACKEND": "d3d12"},
                                "choices": [{"label": "D3D12", "set": {"RSX_BACKEND": "d3d12"}},
                                            {"label": "Vulkan", "set": {"RSX_BACKEND": "vulkan"}}]})
        self.assertProblem(doc, "RSX_BACKEND has no single default")

    def test_choices_made_as_it_runs(self):
        doc = manifest()
        doc["settings"].append({"id": "server", "label": "Server", "defaults": {"DES_NET_SERVER": "a"},
                                "choices": [{"label": "A", "set": {"DES_NET_SERVER": "a"}},
                                            {"label": "B", "set": {"DES_NET_SERVER": "b"}}]})
        self.assertProblem(doc, "makes its choices as it runs")

    def test_one_key_one_setting(self):
        doc = manifest()
        doc["settings"].append(copy.deepcopy(self.setting(doc, "dof")))
        doc["settings"][-1]["id"] = "dof2"
        self.assertProblem(doc, "DES_DOF is also 'dof''s")

    def test_choices_set_the_same_keys(self):
        doc = manifest()
        self.setting(doc, "dof")["choices"][1]["set"] = {"DES_DOF": "0", "DES_FPS": "60"}
        self.assertProblem(doc, "its choices do not all set the same keys")

    def test_values_are_strings(self):
        doc = manifest()
        self.setting(doc, "dof")["choices"][1]["set"] = {"DES_DOF": 0}
        self.assertProblem(doc, "not a string")

    def test_two_choices_one_value(self):
        doc = manifest()
        self.setting(doc, "dof")["choices"][1]["set"] = {"DES_DOF": "1"}
        self.assertProblem(doc, "two choices write the same values")

    def test_no_arguments_and_no_environment(self):
        doc = manifest()
        doc["start"]["args"] = ["--fullscreen"]
        doc["start"]["env"] = {"DES_FPS": "60"}
        found = self.problems(doc)
        self.assertEqual(sum("takes any argument as a game file" in p for p in found), 2, found)

    def test_the_program_and_the_name(self):
        doc = manifest()
        doc["name"] = "Other"
        doc["start"]["program"] = ["Game.exe"]
        found = self.problems(doc)
        self.assertTrue(any(p.startswith("name:") for p in found), found)
        self.assertTrue(any(p.startswith("start.program:") for p in found), found)

    def test_a_field_this_check_does_not_know(self):
        doc = manifest()
        doc["prepare"] = {"program": ["Game.exe"], "args": ["--prepare"]}
        doc["settings"][0]["slider"] = {"min": 0, "max": 1}
        found = self.problems(doc)
        self.assertTrue(any(p.startswith("prepare:") for p in found), found)
        self.assertTrue(any("slider is not checked here" in p for p in found), found)

    def test_the_config(self):
        doc = manifest()
        doc["config"][0]["always"] = {"DES_FPS": "60"}
        doc["config"][0]["file"][-1]["if"] = "{config}/Game"
        found = self.problems(doc)
        self.assertTrue(any(p.startswith("config.always:") for p in found), found)
        self.assertTrue(any("the last place has an if" in p for p in found), found)

    def places(self):
        doc = manifest()
        doc["config"][0]["file"] = [
            {"path": "{install}/data/settings.ini", "if": "{install}/portable.txt"},
            {"path": "{config}/Old/Game/settings.ini", "if": "{config}/Old/Game"},
            {"path": "{local}/One/settings.ini"}]
        doc["saves"] = ["{install}/gamedata/savedata", "{local}/One/gamedata/savedata"]
        return doc

    def test_the_places(self):
        def found(doc):
            return r.check(doc, self.defs, self.rows, "Game", ("Old", "One"))
        self.assertEqual(found(self.places()), [])
        doc = self.places()
        doc["config"][0]["file"][1]["if"] = "{config}/Other/Game"
        self.assertTrue(any("'{config}/Other/Game' is in neither" in p for p in found(doc)), found(doc))
        doc = self.places()
        doc["config"][0]["file"].append({"path": "{config}/Old/Game/settings.ini"})
        self.assertTrue(any("the last place is not {local}/One/settings.ini" in p for p in found(doc)), found(doc))
        doc = self.places()
        doc["saves"] = ["{install}/gamedata/savedata"]
        self.assertTrue(any(p.startswith("saves: none in the one data folder") for p in found(doc)), found(doc))
        # Without the folders (the file's other tests), the places are not checked.
        self.assertEqual(self.problems(manifest()), [])

    def test_looks_off_says_why(self):
        doc = manifest()
        del doc["looks"]["reason"]
        self.assertProblem(doc, "off without a reason")

    def test_system(self):
        doc = manifest()
        doc["system"]["linux"]["glibc"] = "new"
        doc["system"]["macos"] = {"version": "14"}
        found = self.problems(doc)
        self.assertTrue(any(p.startswith("system.linux.glibc:") for p in found), found)
        self.assertTrue(any(p.startswith("system.macos:") for p in found), found)


if __name__ == "__main__":
    unittest.main()
