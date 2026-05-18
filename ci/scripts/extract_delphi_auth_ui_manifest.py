#!/usr/bin/env python3
import argparse
import ast
import json
import re
from pathlib import Path


SCREEN_WIDTH = 800
SCREEN_HEIGHT = 600


class DfmReader:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        start = self.data.find(b"TPF0")
        if start < 0:
            raise ValueError(f"TPF0 signature not found: {path}")
        self.pos = start + 4
        self.objects = []

    def read_byte(self) -> int:
        value = self.data[self.pos]
        self.pos += 1
        return value

    def read_short_string(self) -> str:
        size = self.read_byte()
        raw = self.data[self.pos : self.pos + size]
        self.pos += size
        return raw.decode("latin1")

    def read_int(self, size: int) -> int:
        raw = self.data[self.pos : self.pos + size]
        self.pos += size
        return int.from_bytes(raw, "little", signed=True)

    def read_value(self, value_type: int):
        if value_type == 0:
            return None
        if value_type == 1:
            values = []
            while self.data[self.pos] != 0:
                values.append(self.read_value(self.read_byte()))
            self.pos += 1
            return values
        if value_type == 2:
            return self.read_int(1)
        if value_type == 3:
            return self.read_int(2)
        if value_type == 4:
            return self.read_int(4)
        if value_type == 5:
            self.pos += 10
            return "<extended>"
        if value_type in (6, 7):
            return self.read_short_string()
        if value_type == 8:
            return False
        if value_type == 9:
            return True
        if value_type == 10:
            size = int.from_bytes(self.data[self.pos : self.pos + 4], "little")
            self.pos += 4 + size
            return "<binary>"
        if value_type == 11:
            values = []
            while self.data[self.pos] != 0:
                values.append(self.read_short_string())
            self.pos += 1
            return values
        if value_type in (12, 20):
            size = int.from_bytes(self.data[self.pos : self.pos + 4], "little")
            self.pos += 4
            raw = self.data[self.pos : self.pos + size]
            self.pos += size
            return raw.decode("latin1", errors="replace")
        if value_type == 13:
            return None
        if value_type == 14:
            while self.data[self.pos] != 0:
                self.pos += 1
                while self.data[self.pos] != 0:
                    self.read_short_string()
                    self.read_value(self.read_byte())
                self.pos += 1
            self.pos += 1
            return "<collection>"
        if value_type == 15:
            self.pos += 4
            return "<single>"
        if value_type in (16, 17, 19):
            self.pos += 8
            return "<8-byte>"
        if value_type == 18:
            size = int.from_bytes(self.data[self.pos : self.pos + 4], "little")
            self.pos += 4
            raw = self.data[self.pos : self.pos + size * 2]
            self.pos += size * 2
            return raw.decode("utf-16le", errors="replace")
        raise ValueError(f"unsupported DFM value type {value_type} at {self.path}:{self.pos}")

    def parse_object(self, parent: str | None = None, depth: int = 0):
        class_name = self.read_short_string()
        name = self.read_short_string()
        props = {}
        while True:
            size = self.read_byte()
            if size == 0:
                break
            prop = self.data[self.pos : self.pos + size].decode("latin1")
            self.pos += size
            props[prop] = self.read_value(self.read_byte())

        self.objects.append(
            {
                "name": name,
                "class": class_name,
                "parent": parent,
                "depth": depth,
                "dfm_file": self.path.name,
                "props": props,
            }
        )

        while self.pos < len(self.data) and self.data[self.pos] != 0:
            self.parse_object(name, depth + 1)
        if self.pos < len(self.data) and self.data[self.pos] == 0:
            self.pos += 1

    def parse(self):
        self.parse_object()
        return self.objects


def eval_int_expression(expr: str, variables: dict[str, int]) -> int | None:
    cleaned = re.sub(r"\{[^}]*\}", "", expr)
    cleaned = cleaned.replace("div", "//")
    for name, value in variables.items():
        cleaned = re.sub(rf"\b{name}\b", str(value), cleaned)
    if re.search(r"[^0-9+\-*/() \t]", cleaned):
        return None
    try:
        tree = ast.parse(cleaned, mode="eval")
    except SyntaxError:
        return None

    allowed = (
        ast.Expression,
        ast.BinOp,
        ast.UnaryOp,
        ast.Constant,
        ast.Add,
        ast.Sub,
        ast.Mult,
        ast.FloorDiv,
        ast.USub,
        ast.UAdd,
    )
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        return None
    return int(eval(compile(tree, "<expr>", "eval"), {"__builtins__": {}}, {}))


def scan_runtime(paths: list[Path]):
    assignments = {}
    image_indices = {}
    variables = {"SCREENWIDTH": SCREEN_WIDTH, "SCREENHEIGHT": SCREEN_HEIGHT}
    set_img = re.compile(r"\b(\w+)\.SetImgIndex\s*\(\s*(W\w+)\s*,\s*(\d+)\s*\)")
    assign = re.compile(
        r"\b(\w+)\.(Left|Top|Width|Height|Visible|Floating|EnableFocus|Background)\s*:=\s*([^;]+);"
    )
    var_assign = re.compile(r"\b(dsrvtop|dsrvheight)\s*:=\s*([^;]+);")

    for path in paths:
        text = path.read_text(encoding="latin1")
        for line_no, line in enumerate(text.splitlines(), 1):
            for match in var_assign.finditer(line):
                value = eval_int_expression(match.group(2), variables)
                if value is not None:
                    variables[match.group(1)] = value
            for match in set_img.finditer(line):
                image_indices[match.group(1)] = {
                    "image_library": match.group(2),
                    "image_index": int(match.group(3)),
                    "source": f"{path.name}:{line_no}",
                }
            for match in assign.finditer(line):
                target, prop, expr = match.groups()
                prop_key = prop.lower()
                value = None
                if expr.strip() in ("TRUE", "FALSE"):
                    value = expr.strip() == "TRUE"
                else:
                    value = eval_int_expression(expr, variables)
                assignments.setdefault(target, {})[prop_key] = {
                    "expression": expr.strip(),
                    "value": value,
                    "source": f"{path.name}:{line_no}",
                }

    return assignments, image_indices


def rect(x: int, y: int, w: int, h: int):
    return {"x": x, "y": y, "w": w, "h": h}


def centered(w: int, h: int):
    return rect((SCREEN_WIDTH - w) // 2, (SCREEN_HEIGHT - h) // 2, w, h)


def server_select_layout(count: int, dialog_w: int, dialog_h: int):
    dialog = centered(dialog_w, dialog_h)
    row_gap = 42
    if count <= 8:
        row_top = 235 - (row_gap * count) // 2
        close = rect(dialog["x"] + 244, dialog["y"] + 30, 24, 24)
        sprite = {"library": "WProgUse", "index": 256}
        columns = [63]
    elif count <= 16:
        row_top = 235 - (row_gap * 16 // 2) // 2
        close = rect(dialog["x"] + 348, dialog["y"] + 31, 24, 24)
        sprite = {"library": "WProgUse2", "index": 4}
        columns = [25, 195]
    else:
        row_top = 235 - (row_gap * 8) // 2
        close = rect(dialog["x"] + 527, dialog["y"] + 35, 24, 24)
        sprite = {"library": "WProgUse2", "index": 5}
        columns = [25, 195, 365]

    buttons = []
    for index in range(min(count, 24)):
        column = index // 8
        row = index % 8
        buttons.append(
            {
                "name": f"DSServer{index + 1}",
                "rect": rect(dialog["x"] + columns[column], dialog["y"] + row_top + row * row_gap, 180, 34),
                "image_library": "WProgUse2",
                "image_index": 2,
            }
        )
    return {"server_count": count, "dialog": dialog, "dialog_sprite": sprite, "close_button": close, "server_buttons": buttons}


def build_auth_contract():
    login_dialog = centered(360, 280)
    modal_dialog = centered(360, 180)
    return {
        "login": {
            "dialog": {"name": "DLogIn", "rect": login_dialog, "image_library": "WProgUse", "image_index": 60},
            "account_edit": {"name": "EdId", "rect": rect(350, 259, 137, 16), "focus_order": 1},
            "password_edit": {"name": "EdPasswd", "rect": rect(350, 291, 137, 16), "focus_order": 2},
            "create_account_button": {"name": "DLoginNew", "rect": rect(244, 367, 88, 28), "image_library": "WProgUse", "image_index": 61, "click_sound": "csStone"},
            "change_password_button": {"name": "DLoginChgPw", "rect": rect(331, 367, 88, 28), "image_library": "WProgUse", "image_index": 53, "click_sound": "csStone"},
            "login_button": {"name": "DLoginOk", "rect": rect(391, 325, 88, 28), "image_library": "WProgUse", "image_index": 62, "click_sound": "csStone"},
            "close_button": {"name": "DLoginClose", "rect": rect(472, 188, 88, 28), "image_library": "WProgUse", "image_index": 64, "click_sound": "csStone"},
        },
        "message_modal": {
            "dialog_size_0": {"dialog_sprite": {"library": "WProgUse", "index": 381}, "button_y": 36, "text_origin": {"x": 39, "y": 38}},
            "dialog_size_1": {
                "dialog": modal_dialog,
                "dialog_sprite": {"library": "WProgUse", "index": 360},
                "title_origin": rect(259, 230, 0, 0),
                "text_origin": rect(259, 248, 0, 0),
                "ok_button": rect(356, 336, 88, 28),
                "yes_button": rect(324, 336, 88, 28),
                "no_button": rect(356, 336, 88, 28),
                "cancel_button": rect(430, 336, 88, 28),
            },
            "dialog_size_2": {"dialog_sprite": {"library": "WProgUse", "index": 380}, "button_y": 305, "text_origin": {"x": 23, "y": 20}},
        },
        "server_select": {
            "1": server_select_layout(1, 300, 360),
            "8": server_select_layout(8, 300, 360),
            "16": server_select_layout(16, 404, 360),
            "24": server_select_layout(24, 584, 360),
        },
        "character_select": {
            "dialog": rect(0, 0, 800, 600),
            "background": {"library": "WProgUse", "index": 65},
            "left_button": {"name": "DscSelect1", "rect": rect(133, 453, 88, 28), "image_index": 66},
            "right_button": {"name": "DscSelect2", "rect": rect(685, 454, 88, 28), "image_index": 67},
            "start_button": {"name": "DscStart", "rect": rect(385, 456, 88, 28), "image_index": 68, "click_sound": "csNorm"},
            "new_button": {"name": "DscNewChr", "rect": rect(348, 486, 88, 28), "image_index": 69, "click_sound": "csNorm"},
            "erase_button": {"name": "DscEraseChr", "rect": rect(347, 506, 88, 28), "image_index": 70, "click_sound": "csNorm"},
            "credits_button": {"name": "DscCredits", "rect": rect(362, 527, 88, 28), "image_index": 71},
            "exit_button": {"name": "DscExit", "rect": rect(379, 547, 88, 28), "image_index": 72},
            "left_name_text": rect(117, 494, 0, 0),
            "left_level_text": rect(117, 523, 0, 0),
            "left_job_text": rect(117, 553, 0, 0),
            "right_name_text": rect(671, 496, 0, 0),
            "right_level_text": rect(671, 525, 0, 0),
            "right_job_text": rect(671, 555, 0, 0),
            "server_name_text": rect(400, 8, 0, 0),
        },
    }


def build_animation_contract():
    return {
        "login_door": {
            "background": {"library": "WChrSel", "source_expression": "102-80", "resolved_index": 22},
            "door_frame_expression": "103+CurFrame-80",
            "door_draw_origin": {"x": 152, "y": 96},
            "resolved_frame_start": 23,
            "resolved_frame_end": 32,
            "frame_count": 10,
            "advance_ms": 230,
            "advance_operator": ">",
            "advance_before_draw": True,
            "fade_trigger_frame": 9,
            "fade_index": 29,
            "next_scene": "stSelectChr",
            "sound": "s_rock_door_open",
        },
        "fade": {
            "door_fade": {
                "trigger": "login_door_frame_9",
                "set_flags": ["DoFadeOut", "DoFadeIn"],
                "initial_fade_index": 29,
                "main_paint_do_fade_out_fade_index": 1,
                "scene_change_when_fade_index_lte": 1,
                "scene_change": "stSelectChr",
            },
            "start_character_fast_fade": {
                "trigger": "SelChrStartClick",
                "set_flags": ["DoFastFadeOut"],
                "initial_fade_index": 29,
                "main_paint_do_fast_fade_out_fade_index": 1,
                "scene_change": "server_driven",
            },
            "gradient_branch": "commented_out_in_ClMain_FormPaint",
        },
        "character_select": {
            "selected_frame_count": 16,
            "freeze_frame_count": 13,
            "effect_frame_count": 14,
            "idle_frame_start": 40,
            "freeze_frame_start": 60,
            "idle_base_expression": "120-80 + job*40 + sex*120",
            "freeze_base_expression": "140-80 + job*40 + sex*120",
            "effect_base": 4,
            "effect_frame_start": 4,
            "effect_frame_end": 17,
            "selected_advance_ms": 300,
            "unfreeze_advance_ms": 120,
            "effect_advance_ms": 110,
            "freeze_advance_ms": 50,
            "advance_operator": ">",
            "draw_before_advance": True,
            "selection_sound": "s_meltstone",
            "effect_origins": [{"slot": 0, "x": 90, "y": 58}, {"slot": 1, "x": 430, "y": 60}],
            "slot_offsets": [{"slot": 0, "x_offset": 0, "y_offset": 0}, {"slot": 1, "x_offset": 340, "y_offset": 2}],
            "pose_origins": [
                {"job": 0, "sex": 0, "freeze_origin": {"x": 71, "y": 52}, "idle_origin": {"x": 71, "y": 52}},
                {"job": 0, "sex": 1, "freeze_origin": {"x": 65, "y": 55}, "idle_origin": {"x": 65, "y": 55}},
                {"job": 1, "sex": 0, "freeze_origin": {"x": 77, "y": 46}, "idle_origin": {"x": 77, "y": 46}},
                {"job": 1, "sex": 1, "freeze_origin": {"x": 171, "y": 97}, "idle_origin": {"x": 141, "y": 83}},
                {"job": 2, "sex": 0, "freeze_origin": {"x": 85, "y": 63}, "idle_origin": {"x": 85, "y": 63}},
                {"job": 2, "sex": 1, "freeze_origin": {"x": 164, "y": 103}, "idle_origin": {"x": 141, "y": 83}},
            ],
        },
    }


def build_modal_focus_audio_contract():
    return {
        "modal": {
            "mbOk": {"enter": "mrOk", "escape": "ignored", "buttons": ["ok"]},
            "mbYes": {"enter": "mrYes", "escape": "ignored", "buttons": ["yes"]},
            "mbYesNoCancel": {"enter": "ignored", "escape": "mrCancel", "buttons": ["yes", "no", "cancel"]},
            "mbOkAbort": {"enter": "mrOk", "escape": "ignored", "buttons": ["ok", "abort"], "edit_visible": True},
        },
        "focus": {
            "login_account_enter": "EdPasswd",
            "login_password_enter_success": "hide_login_edits",
            "login_password_failure": "EdId",
            "login_notice_enter": "CM_LOGINNOTICEOK",
            "login_notice_escape": "ignored",
            "character_start_without_character": "DMsgDlg",
        },
        "audio": {
            "login_open": "bmg_intro",
            "login_close": "SilenceSound",
            "login_door_open": "s_rock_door_open",
            "character_select_open": "bmg_select",
            "character_select_close": "SilenceSound",
            "character_slot_select": "s_meltstone",
            "click_sound_map": {"csNorm": "s_norm_button_click", "csStone": "s_rock_button_click", "csGlass": "s_glass_button_click"},
        },
        "known_cxx_deltas": [
            {
                "behavior": "login_password_failure_focus",
                "delphi": "EdId",
                "modern_client_pr3": "password",
                "resolution": "recorded here; fix in a focused behavior PR if 1:1 is required",
            }
        ],
    }


def control_from_object(obj, runtime_assignments, image_indices):
    props = obj["props"]
    return {
        "name": obj["name"],
        "class": obj["class"],
        "parent": obj["parent"],
        "dfm_file": obj["dfm_file"],
        "left": props.get("Left"),
        "top": props.get("Top"),
        "width": props.get("Width"),
        "height": props.get("Height"),
        "visible": props.get("Visible"),
        "floating": props.get("Floating"),
        "focusable": props.get("EnableFocus"),
        "click_handler": props.get("OnClick", ""),
        "key_handler": props.get("OnKeyDown", "") or props.get("OnKeyPress", ""),
        "direct_paint_handler": props.get("OnDirectPaint", ""),
        "click_sound": props.get("ClickCount", ""),
        "click_sound_handler": props.get("OnClickSound", ""),
        "runtime": runtime_assignments.get(obj["name"], {}),
        **image_indices.get(obj["name"], {}),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source_client = args.repo_root / "Source" / "Client"
    dfm_paths = [source_client / "FState.dfm", source_client / "ClMain.dfm"]
    pas_paths = [source_client / "FState.pas", source_client / "IntroScn.pas"]

    objects = []
    for dfm_path in dfm_paths:
        reader = DfmReader(dfm_path)
        objects.extend(reader.parse())

    runtime_assignments, image_indices = scan_runtime(pas_paths)
    controls = [control_from_object(obj, runtime_assignments, image_indices) for obj in objects]
    controls.sort(key=lambda control: (control["dfm_file"], control["name"]))

    manifest = {
        "version": 1,
        "source": {
            "dfm": [str(path.relative_to(args.repo_root)).replace("\\", "/") for path in dfm_paths],
            "pas": [str(path.relative_to(args.repo_root)).replace("\\", "/") for path in pas_paths],
        },
        "screen": {"width": SCREEN_WIDTH, "height": SCREEN_HEIGHT},
        "dfm_controls": controls,
        "layouts": build_auth_contract(),
        "animation": build_animation_contract(),
        "modal_focus_audio": build_modal_focus_audio_contract(),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, ensure_ascii=True, sort_keys=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
