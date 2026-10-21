#!/usr/bin/env python3
"""Select and export individually aligned frames from a sprite sheet."""

import argparse
import json
import os
import queue
import re
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageDraw, ImageTk


INITIAL_ZOOM = 3
MIN_ZOOM = 1
MAX_ZOOM = 16
PREVIEW_SIZE = 256
AUTO_SLICE_MIN_FRAME_SIZE = 3


def frame_from_drag(start, end, image_size):
    """Convert canvas coordinates into a clamped, non-empty image rectangle."""
    width, height = image_size
    x0, x1 = sorted((int(start[0]), int(end[0])))
    y0, y1 = sorted((int(start[1]), int(end[1])))
    x0 = max(0, min(x0, width - 1))
    y0 = max(0, min(y0, height - 1))
    x1 = max(x0 + 1, min(x1, width))
    y1 = max(y0 + 1, min(y1, height))
    return {"x": x0, "y": y0, "w": x1 - x0, "h": y1 - y0}


def _selection_alpha(image, selection, alpha_threshold):
    if not 0 <= alpha_threshold <= 255:
        raise ValueError("Alpha threshold must be between 0 and 255.")
    x0, y0 = selection["x"], selection["y"]
    x1, y1 = x0 + selection["w"], y0 + selection["h"]
    if (x0 < 0 or y0 < 0 or x1 > image.width or y1 > image.height
            or x1 <= x0 or y1 <= y0):
        raise ValueError("Selection is outside the source image.")
    return image.crop((x0, y0, x1, y1)).convert("RGBA")


def _dominant_border_color(image):
    pixels = image.load()
    histogram = {}
    width, height = image.size
    border_points = set()
    for x in range(width):
        border_points.add((x, 0))
        border_points.add((x, height - 1))
    for y in range(height):
        border_points.add((0, y))
        border_points.add((width - 1, y))
    for x, y in border_points:
        red, green, blue, alpha = pixels[x, y]
        if alpha == 0:
            continue
        key = (red >> 3, green >> 3, blue >> 3)
        bucket = histogram.setdefault(key, [0, 0, 0, 0])
        bucket[0] += 1
        bucket[1] += red
        bucket[2] += green
        bucket[3] += blue
    if not histogram:
        raise ValueError("Cannot estimate a background from transparent edges.")
    bucket = max(histogram.values(), key=lambda value: value[0])
    count = bucket[0]
    return tuple(round(value / count) for value in bucket[1:])


def _foreground_mask(image, selection, alpha_threshold, mode="alpha",
                     background_color=None, color_tolerance=48):
    if mode not in ("alpha", "auto", "color"):
        raise ValueError("Foreground mode must be alpha, auto, or color.")
    if not 0 <= color_tolerance <= 255:
        raise ValueError("Color tolerance must be between 0 and 255.")
    source = _selection_alpha(image, selection, alpha_threshold)
    alpha = source.getchannel("A")
    used_mode = mode
    if mode == "auto":
        transparent_pixels = alpha.histogram()[0]
        used_mode = (
            "alpha" if transparent_pixels >= max(1, alpha.width * alpha.height // 100)
            else "color"
        )
    if used_mode == "alpha":
        return (
            alpha.point(lambda value: 255 if value > alpha_threshold else 0),
            used_mode,
            None,
        )

    if used_mode == "color":
        if background_color is None:
            background_color = _dominant_border_color(source)
        if (len(background_color) != 3
                or any(not 0 <= channel <= 255 for channel in background_color)):
            raise ValueError("Background color must be an RGB triplet.")
    else:
        raise ValueError("Unsupported foreground detection mode.")

    color_pixels = source.load()
    mask = Image.new("L", source.size, 0)
    mask_pixels = mask.load()
    red_bg, green_bg, blue_bg = background_color
    tolerance_sq = color_tolerance * color_tolerance
    for y in range(source.height):
        for x in range(source.width):
            red, green, blue, opacity = color_pixels[x, y]
            distance_sq = (
                (red - red_bg) ** 2
                + (green - green_bg) ** 2
                + (blue - blue_bg) ** 2
            )
            if opacity > alpha_threshold and distance_sq > tolerance_sq:
                mask_pixels[x, y] = 255
    return mask, used_mode, tuple(background_color)


def _alpha_mask(image, selection, alpha_threshold):
    return _foreground_mask(
        image, selection, alpha_threshold, mode="alpha"
    )[0]


def _axis_runs(projection, merge_gap):
    runs = []
    start = None
    for position, occupied in enumerate(projection + [False]):
        if occupied and start is None:
            start = position
        elif not occupied and start is not None:
            runs.append((start, position))
            start = None

    merged = []
    for start, end in runs:
        if merged and start - merged[-1][1] <= merge_gap:
            merged[-1] = (merged[-1][0], end)
        else:
            merged.append((start, end))
    return merged


def _infer_axis_period(projection):
    length = len(projection)
    start = max(8, length // 80)
    end = min(32, length // 2)
    if end < start:
        return None
    scores = []
    for period in range(start, end + 1):
        left_energy = sum(
            value * value for value in projection[:-period]
        )
        right_energy = sum(
            value * value for value in projection[period:]
        )
        if not left_energy or not right_energy:
            continue
        overlap = sum(
            projection[index] * projection[index + period]
            for index in range(length - period)
        )
        score = overlap / (left_energy * right_energy) ** 0.5
        scores.append((period, score))
    for index in range(1, len(scores) - 1):
        period, score = scores[index]
        if (score >= 0.7
                and score >= scores[index - 1][1] + 0.005
                and score >= scores[index + 1][1] + 0.005):
            return period
    return None


def _split_periodic_runs(runs, projection):
    period = _infer_axis_period(projection)
    if period is None:
        return runs
    refined = []
    for start, end in runs:
        length = end - start
        divisions = round(length / period)
        if divisions < 2 or abs(length / divisions - period) > max(2, period * 0.25):
            refined.append((start, end))
            continue
        for index in range(divisions):
            left = start + round(index * length / divisions)
            right = start + round((index + 1) * length / divisions)
            if right > left:
                refined.append((left, right))
    return refined


def auto_slice_alpha(image, selection, merge_gap=2,
                     min_frame_size=AUTO_SLICE_MIN_FRAME_SIZE,
                     alpha_threshold=0, foreground_mode="alpha",
                     background_color=None, color_tolerance=48):
    """Find horizontally separated, non-transparent frames in one selection."""
    if merge_gap < 0:
        raise ValueError("Gap merge tolerance cannot be negative.")

    x0, y0 = selection["x"], selection["y"]
    mask, _mode, _background = _foreground_mask(
        image, selection, alpha_threshold, foreground_mode,
        background_color, color_tolerance,
    )
    pixels = mask.load()
    columns = [
        any(pixels[x, y] for y in range(mask.height))
        for x in range(mask.width)
    ]
    runs = _axis_runs(columns, merge_gap)

    frames = []
    for start, end in runs:
        bbox = mask.crop((start, 0, end, mask.height)).getbbox()
        if bbox is None:
            continue
        left, top, right, bottom = bbox
        if right - left < min_frame_size or bottom - top < min_frame_size:
            continue
        frames.append({
            "x": x0 + start + left,
            "y": y0 + top,
            "w": right - left,
            "h": bottom - top,
        })
    return frames


def auto_slice_components(image, selection, min_pixels=8,
                         min_frame_size=AUTO_SLICE_MIN_FRAME_SIZE,
                         alpha_threshold=0, connectivity=8,
                         foreground_mode="alpha", background_color=None,
                         color_tolerance=48):
    """Find separate objects using the requested pixel connectivity."""
    if min_pixels < 1:
        raise ValueError("Minimum component size must be positive.")
    if connectivity not in (4, 8):
        raise ValueError("Connectivity must be 4 or 8.")

    x0, y0 = selection["x"], selection["y"]
    mask, _mode, _background = _foreground_mask(
        image, selection, alpha_threshold, foreground_mode,
        background_color, color_tolerance,
    )
    width, height = mask.size
    pixels = mask.load()
    visited = bytearray(width * height)
    components = []

    for start_y in range(height):
        for start_x in range(width):
            start = start_y * width + start_x
            if visited[start] or pixels[start_x, start_y] == 0:
                continue
            visited[start] = 1
            pending = [start]
            left = right = start_x
            top = bottom = start_y
            pixel_count = 0
            while pending:
                index = pending.pop()
                x, y = index % width, index // width
                pixel_count += 1
                left = min(left, x)
                right = max(right, x)
                top = min(top, y)
                bottom = max(bottom, y)
                for neighbor_y in range(max(0, y - 1),
                                        min(height, y + 2)):
                    for neighbor_x in range(max(0, x - 1),
                                            min(width, x + 2)):
                        if (connectivity == 4
                                and abs(neighbor_x - x) + abs(neighbor_y - y) != 1):
                            continue
                        neighbor = neighbor_y * width + neighbor_x
                        if (not visited[neighbor]
                                and pixels[neighbor_x, neighbor_y] > 0):
                            visited[neighbor] = 1
                            pending.append(neighbor)
            if (pixel_count >= min_pixels
                    and right - left + 1 >= min_frame_size
                    and bottom - top + 1 >= min_frame_size):
                components.append({
                    "x": x0 + left,
                    "y": y0 + top,
                    "w": right - left + 1,
                    "h": bottom - top + 1,
                })

    return sorted(components, key=lambda frame: (frame["y"], frame["x"]))


def auto_slice_grid(image, selection, merge_gap=2, min_pixels=8,
                    min_frame_size=AUTO_SLICE_MIN_FRAME_SIZE,
                    alpha_threshold=0, foreground_mode="alpha",
                    background_color=None, color_tolerance=48):
    """Split a spaced sprite grid using horizontal and vertical projections."""
    if merge_gap < 0:
        raise ValueError("Gap merge tolerance cannot be negative.")
    mask, _mode, _background = _foreground_mask(
        image, selection, alpha_threshold, foreground_mode,
        background_color, color_tolerance,
    )
    return _slice_grid_from_mask(mask, selection, merge_gap, min_pixels,
                                 min_frame_size)


def _slice_grid_from_mask(mask, selection, merge_gap, min_pixels,
                          min_frame_size):
    if merge_gap < 0:
        raise ValueError("Gap merge tolerance cannot be negative.")
    if min_pixels < 1:
        raise ValueError("Minimum visible pixels must be positive.")
    x0, y0 = selection["x"], selection["y"]
    width, height = mask.size
    pixels = mask.load()
    columns = [
        any(pixels[x, y] for y in range(height))
        for x in range(width)
    ]
    rows = [
        any(pixels[x, y] for x in range(width))
        for y in range(height)
    ]
    x_runs = _axis_runs(columns, merge_gap)
    y_runs = _axis_runs(rows, merge_gap)
    x_runs = _split_periodic_runs(x_runs, columns)
    y_runs = _split_periodic_runs(y_runs, rows)
    frames = []

    for top, bottom in y_runs:
        for left, right in x_runs:
            crop = mask.crop((left, top, right, bottom))
            crop_pixels = crop.load()
            visible_pixels = sum(
                1 for y in range(crop.height)
                for x in range(crop.width) if crop_pixels[x, y]
            )
            if visible_pixels < min_pixels:
                continue
            bbox = crop.getbbox()
            if bbox is None:
                continue
            crop_left, crop_top, crop_right, crop_bottom = bbox
            frame_width = crop_right - crop_left
            frame_height = crop_bottom - crop_top
            if (frame_width < min_frame_size
                    or frame_height < min_frame_size):
                continue
            frames.append({
                "x": x0 + left + crop_left,
                "y": y0 + top + crop_top,
                "w": frame_width,
                "h": frame_height,
            })
    return frames


def add_frame_padding(frames, selection, padding, image_size):
    if padding < 0:
        raise ValueError("Frame padding cannot be negative.")
    width, height = image_size
    left_bound = selection["x"]
    top_bound = selection["y"]
    right_bound = min(width, left_bound + selection["w"])
    bottom_bound = min(height, top_bound + selection["h"])
    padded = []
    for frame in frames:
        left = max(left_bound, frame["x"] - padding)
        top = max(top_bound, frame["y"] - padding)
        right = min(right_bound, frame["x"] + frame["w"] + padding)
        bottom = min(bottom_bound, frame["y"] + frame["h"] + padding)
        padded.append({
            "x": left,
            "y": top,
            "w": right - left,
            "h": bottom - top,
        })
    return padded


def validate_frames(frames, image_size):
    width, height = image_size
    for index, frame in enumerate(frames):
        rect = (frame["x"], frame["y"], frame["w"], frame["h"])
        if any(not isinstance(value, int) for value in rect):
            raise ValueError("Frame %d coordinates must be integers." % (index + 1))
        x, y, frame_width, frame_height = rect
        if (x < 0 or y < 0 or frame_width <= 0 or frame_height <= 0
                or x + frame_width > width or y + frame_height > height):
            raise ValueError("Frame %d is outside the source image." % (index + 1))


def frame_at_point(frames, x, y):
    """Return the smallest frame containing an image-space point."""
    matches = [
        (frame["w"] * frame["h"], index)
        for index, frame in enumerate(frames)
        if frame["x"] <= x < frame["x"] + frame["w"]
        and frame["y"] <= y < frame["y"] + frame["h"]
    ]
    return min(matches)[1] if matches else None


def load_manifest(path):
    manifest_path = Path(path)
    with manifest_path.open("r", encoding="utf-8") as source:
        manifest = json.load(source)
    image_path = Path(manifest["image"])
    if not image_path.is_absolute():
        image_path = manifest_path.parent / image_path
    frames = manifest["frames"]
    return image_path, frames


class SpriteFrameSelector:
    def __init__(self, root, image_path):
        self.root = root
        self.image_path = None
        self.image = None
        self.photo = None
        self.zoom = INITIAL_ZOOM
        self.frames = []
        self.undo_stack = []
        self.selected_index = None
        self.selection_anchor = None
        self.drag_start = None
        self.drag_rectangle = None
        self.pending_rect = None
        self.right_press_position = None
        self.right_dragged = False
        self.gap_tolerance = tk.IntVar(value=2)
        self.alpha_threshold = tk.IntVar(value=0)
        self.minimum_pixels = tk.IntVar(value=8)
        self.connectivity = tk.IntVar(value=8)
        self.frame_padding = tk.IntVar(value=0)
        self.foreground_mode = tk.StringVar(value="auto")
        self.color_tolerance = tk.IntVar(value=48)
        self.background_color = None
        self.context_image_point = None
        self.ai_labeling = False
        self.ai_label_queue = None
        self.ai_label_targets = []
        self.ai_cancel_event = None
        self.ai_stage = "idle"
        self.manual_x = tk.StringVar(value="0")
        self.manual_y = tk.StringVar(value="0")
        self.manual_width = tk.StringVar(value="32")
        self.manual_height = tk.StringVar(value="32")
        self.lock_proportion = tk.BooleanVar(value=False)
        self.manual_proportion = None

        self.root.title("Sprite Frame Selector")
        self.root.geometry("1180x800")
        self.root.minsize(900, 620)
        self.root.bind("<Control-s>", lambda _event: self.save_manifest())
        self.root.bind("<Control-z>", lambda _event: self.undo())
        self.root.bind("<Control-l>",
                       lambda _event: self.label_existing_frames_with_ai())
        self.root.bind("<Delete>", lambda _event: self.delete_selected())

        self._build_ui()
        self.open_image(image_path)

    def _build_ui(self):
        toolbar = ttk.Frame(self.root, padding=6)
        toolbar.pack(side=tk.TOP, fill=tk.X)

        ttk.Button(toolbar, text="Open image", command=self.choose_image).pack(
            side=tk.LEFT
        )
        ttk.Button(toolbar, text="Open manifest", command=self.choose_manifest).pack(
            side=tk.LEFT, padx=(4, 12)
        )
        ttk.Button(toolbar, text="-", width=3, command=lambda: self.change_zoom(-1)).pack(
            side=tk.LEFT
        )
        self.zoom_label = ttk.Label(toolbar, width=6, anchor=tk.CENTER)
        self.zoom_label.pack(side=tk.LEFT)
        ttk.Button(toolbar, text="+", width=3, command=lambda: self.change_zoom(1)).pack(
            side=tk.LEFT, padx=(0, 12)
        )
        ttk.Button(toolbar, text="Save JSON", command=self.save_manifest).pack(
            side=tk.LEFT, padx=(0, 4)
        )
        ttk.Button(toolbar, text="Export PNGs", command=self.export_frames).pack(
            side=tk.LEFT
        )

        slice_toolbar = ttk.LabelFrame(
            self.root, text="1. Select, name, and edit frames",
            padding=(6, 3),
        )
        slice_toolbar.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(slice_toolbar, text="Frame name").pack(side=tk.LEFT)
        self.name_entry = ttk.Entry(slice_toolbar, width=18)
        self.name_entry.pack(side=tk.LEFT, padx=5)
        self.name_entry.bind("<KeyRelease>", self.label_selected_frame)
        self.name_entry.bind("<Return>", self.finish_labeling)
        ttk.Button(slice_toolbar, text="Add selection",
                   command=self.add_selection).pack(
            side=tk.LEFT
        )
        ttk.Label(
            slice_toolbar, text="Select a frame to rename it."
        ).pack(side=tk.LEFT, padx=(8, 4))
        ttk.Button(slice_toolbar, text="Delete selected",
                   command=self.delete_selected).pack(side=tk.LEFT, padx=4)

        auto_toolbar = ttk.LabelFrame(
            self.root, text="2. Detect frames in the drawn selection",
            padding=(6, 3),
        )
        auto_toolbar.pack(side=tk.TOP, fill=tk.X, pady=(2, 0))
        ttk.Label(auto_toolbar, text="Gap").pack(
            side=tk.LEFT, padx=(0, 2)
        )
        ttk.Spinbox(auto_toolbar, from_=0, to=12, width=3,
                    textvariable=self.gap_tolerance).pack(side=tk.LEFT)
        ttk.Button(auto_toolbar, text="Slice row",
                   command=self.auto_slice_selection).pack(
            side=tk.LEFT, padx=(8, 4)
        )
        ttk.Button(auto_toolbar, text="Find separate objects",
                   command=self.auto_slice_components_selection).pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(auto_toolbar, text="Find grid",
                   command=self.smart_slice_selection).pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(auto_toolbar, text="Analyze & slice",
                   command=self.analyze_and_slice_selection).pack(
            side=tk.LEFT, padx=(12, 4)
        )

        ai_toolbar = ttk.LabelFrame(
            self.root, text="3. Suggest names with local AI (Ctrl+L)",
            padding=(6, 3),
        )
        ai_toolbar.pack(side=tk.TOP, fill=tk.X)
        self.ai_label_button = ttk.Button(
            ai_toolbar, text="AI label generic frames",
            command=self.label_existing_frames_with_ai,
        )
        self.ai_label_button.pack(
            side=tk.LEFT
        )
        ttk.Label(
            ai_toolbar,
            text="Fills generic names; manual names are preserved.",
        ).pack(side=tk.LEFT, padx=8)
        self.replace_ai_labels = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            ai_toolbar,
            text="Replace existing labels",
            variable=self.replace_ai_labels,
        ).pack(side=tk.LEFT, padx=4)

        self.ai_progress = ttk.Progressbar(
            ai_toolbar, orient=tk.HORIZONTAL, mode="determinate",
            length=150, maximum=100,
        )
        self.ai_progress.pack(side=tk.LEFT, padx=(8, 4))
        self.ai_cancel_button = ttk.Button(
            ai_toolbar, text="Cancel", command=self.cancel_ai_labeling,
            state=tk.DISABLED,
        )
        self.ai_cancel_button.pack(side=tk.LEFT)
        self.ai_progress.pack_forget()
        self.ai_cancel_button.pack_forget()

        manual_toolbar = ttk.LabelFrame(
            self.root, text="Manual crop", padding=(6, 3)
        )
        manual_toolbar.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(manual_toolbar, text="Manual slice").pack(side=tk.LEFT)
        for label, variable in (
                ("X", self.manual_x), ("Y", self.manual_y),
                ("W", self.manual_width), ("H", self.manual_height)):
            ttk.Label(manual_toolbar, text=label).pack(
                side=tk.LEFT, padx=(8, 2)
            )
            entry = ttk.Entry(manual_toolbar, textvariable=variable, width=7)
            entry.pack(side=tk.LEFT)
            if variable in (self.manual_width, self.manual_height):
                entry.bind(
                    "<Return>",
                    lambda event, field=variable:
                    self.apply_manual_proportion(event, field),
                )
                entry.bind(
                    "<FocusOut>",
                    lambda event, field=variable:
                    self.apply_manual_proportion(event, field),
                )
        ttk.Checkbutton(
            manual_toolbar, text="Lock proportion",
            variable=self.lock_proportion,
            command=self.toggle_proportion_lock,
        ).pack(side=tk.LEFT, padx=(8, 4))
        ttk.Button(manual_toolbar, text="Add manual slice",
                   command=self.add_manual_slice).pack(side=tk.LEFT, padx=4)
        ttk.Button(manual_toolbar, text="Preview",
                   command=self.preview_manual_slice).pack(side=tk.LEFT, padx=4)
        ttk.Button(manual_toolbar, text="Apply to selected",
                   command=self.apply_manual_slice).pack(side=tk.LEFT, padx=4)
        ttk.Button(manual_toolbar, text="Undo",
                   command=self.undo).pack(side=tk.LEFT, padx=4)

        detection_toolbar = ttk.LabelFrame(
            self.root, text="Detection settings", padding=(6, 3)
        )
        detection_toolbar.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(detection_toolbar, text="Detection").pack(side=tk.LEFT)
        ttk.Label(detection_toolbar, text="Alpha threshold").pack(
            side=tk.LEFT, padx=(8, 2)
        )
        ttk.Spinbox(detection_toolbar, from_=0, to=255, width=5,
                    textvariable=self.alpha_threshold).pack(side=tk.LEFT)
        ttk.Label(detection_toolbar, text="Min pixels").pack(
            side=tk.LEFT, padx=(8, 2)
        )
        ttk.Spinbox(detection_toolbar, from_=1, to=10000, width=6,
                    textvariable=self.minimum_pixels).pack(side=tk.LEFT)
        ttk.Label(detection_toolbar, text="Connectivity").pack(
            side=tk.LEFT, padx=(8, 2)
        )
        ttk.Combobox(detection_toolbar, textvariable=self.connectivity,
                     values=(4, 8), width=3, state="readonly").pack(side=tk.LEFT)
        ttk.Label(detection_toolbar, text="Padding").pack(
            side=tk.LEFT, padx=(8, 2)
        )
        ttk.Spinbox(detection_toolbar, from_=0, to=64, width=4,
                    textvariable=self.frame_padding).pack(side=tk.LEFT)
        ttk.Label(detection_toolbar, text="Foreground").pack(
            side=tk.LEFT, padx=(8, 2)
        )
        self.foreground_combo = ttk.Combobox(
            detection_toolbar, textvariable=self.foreground_mode,
            values=("auto", "alpha", "color"), width=7, state="readonly",
        )
        self.foreground_combo.pack(side=tk.LEFT)
        self.foreground_combo.bind(
            "<<ComboboxSelected>>", self.update_background_label
        )
        ttk.Label(detection_toolbar, text="Color tolerance").pack(
            side=tk.LEFT, padx=(8, 2)
        )
        ttk.Spinbox(detection_toolbar, from_=0, to=255, width=4,
                    textvariable=self.color_tolerance).pack(side=tk.LEFT)
        self.background_label = ttk.Label(
            detection_toolbar, text="Background: auto"
        )
        self.background_label.pack(side=tk.LEFT, padx=(8, 0))
        ttk.Label(
            detection_toolbar,
            text="Threshold ignores faint pixels; padding preserves transparent margins.",
        ).pack(side=tk.LEFT, padx=(10, 0))

        self.path_label = ttk.Label(self.root, padding=(8, 0, 8, 5), anchor=tk.W)
        self.path_label.pack(side=tk.TOP, fill=tk.X)

        body = ttk.Panedwindow(self.root, orient=tk.HORIZONTAL)
        body.pack(fill=tk.BOTH, expand=True, padx=6, pady=(0, 6))

        canvas_panel = ttk.Frame(body)
        body.add(canvas_panel, weight=4)

        self.canvas = tk.Canvas(canvas_panel, background="#303030",
                                highlightthickness=0, cursor="crosshair")
        horizontal = ttk.Scrollbar(canvas_panel, orient=tk.HORIZONTAL,
                                   command=self.canvas.xview)
        vertical = ttk.Scrollbar(canvas_panel, orient=tk.VERTICAL,
                                 command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=horizontal.set,
                              yscrollcommand=vertical.set)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        vertical.grid(row=0, column=1, sticky="ns")
        horizontal.grid(row=1, column=0, sticky="ew")
        canvas_panel.rowconfigure(0, weight=1)
        canvas_panel.columnconfigure(0, weight=1)
        self.canvas.bind("<ButtonPress-1>", self.begin_selection)
        self.canvas.bind("<B1-Motion>", self.update_selection)
        self.canvas.bind("<ButtonRelease-1>", self.finish_selection)
        self.canvas.bind("<MouseWheel>", self.mousewheel_zoom)
        self.canvas.bind("<Button-4>", lambda _event: self.change_zoom(1))
        self.canvas.bind("<Button-5>", lambda _event: self.change_zoom(-1))
        self.canvas.bind("<ButtonPress-2>", self.begin_pan)
        self.canvas.bind("<B2-Motion>", self.update_pan)
        self.canvas.bind("<ButtonPress-3>", self.begin_context_action)
        self.canvas.bind("<B3-Motion>", self.update_context_action)
        self.canvas.bind("<ButtonRelease-3>", self.finish_context_action)

        self.context_menu = tk.Menu(self.root, tearoff=False)
        self.context_menu.add_command(
            label="Smart grid in selection", command=self.smart_slice_selection
        )
        self.context_menu.add_command(
            label="Analyze & slice",
            command=self.analyze_and_slice_selection,
        )
        self.context_menu.add_command(
            label="AI label frames (Ctrl+L)",
            command=self.label_existing_frames_with_ai,
        )
        self.context_menu.add_command(
            label="Auto-slice row in selection",
            command=self.auto_slice_selection,
        )
        self.context_menu.add_command(
            label="Separate connected objects",
            command=self.auto_slice_components_selection,
        )
        self.context_menu.add_command(
            label="Use clicked pixel as background",
            command=self.set_background_from_context,
        )
        self.context_menu.add_separator()
        self.context_menu.add_command(
            label="Preview manual rectangle",
            command=self.preview_manual_slice,
        )
        self.context_menu.add_command(
            label="Add manual slice", command=self.add_manual_slice
        )
        self.context_menu.add_command(
            label="Add drawn selection", command=self.add_selection
        )
        self.context_menu.add_command(
            label="Apply to selected frame", command=self.apply_manual_slice
        )
        self.context_menu.add_separator()
        self.context_menu.add_command(label="Undo (Ctrl+Z)", command=self.undo)
        self.context_menu.add_command(
            label="Delete selected frame(s)", command=self.delete_selected
        )
        self.context_menu.add_separator()
        self.context_menu.add_command(
            label="Save manifest", command=self.save_manifest
        )
        self.context_menu.add_command(
            label="Export PNGs", command=self.export_frames
        )

        side_panel = ttk.Frame(body, padding=(8, 0, 0, 0), width=290)
        body.add(side_panel, weight=1)
        ttk.Label(
            side_panel,
            text="Frames (Ctrl/Shift-click to select multiple)",
        ).pack(anchor=tk.W)
        list_panel = ttk.Frame(side_panel)
        list_panel.pack(fill=tk.BOTH, expand=True, pady=(4, 10))
        self.frame_list = tk.Listbox(
            list_panel, exportselection=False, activestyle="dotbox",
            selectmode=tk.EXTENDED,
        )
        list_scroll = ttk.Scrollbar(list_panel, orient=tk.VERTICAL,
                                    command=self.frame_list.yview)
        self.frame_list.configure(yscrollcommand=list_scroll.set)
        self.frame_list.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        list_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.frame_list.bind("<<ListboxSelect>>", self.select_frame)

        ttk.Label(side_panel, text="Crop preview (transparent checkerboard)").pack(
            anchor=tk.W
        )
        self.preview = ttk.Label(side_panel, anchor=tk.CENTER, relief=tk.SUNKEN)
        self.preview.pack(fill=tk.X, pady=(4, 8))
        self.details = ttk.Label(side_panel, justify=tk.LEFT, wraplength=270)
        self.details.pack(fill=tk.X)

        self.status = ttk.Label(self.root, padding=(8, 4), anchor=tk.W)
        self.status.pack(side=tk.BOTTOM, fill=tk.X)

    def open_image(self, path, load_sibling_manifest=True):
        if path is None:
            return
        if self.ai_labeling:
            self.status.configure(
                text="Wait for AI labeling to finish or cancel it before "
                     "opening another image."
            )
            return
        path = Path(path)
        try:
            image = Image.open(path).convert("RGBA")
        except (OSError, ValueError) as error:
            messagebox.showerror("Cannot open image", str(error), parent=self.root)
            return
        self.image_path = path.resolve()
        self.image = image
        self.frames.clear()
        self.undo_stack.clear()
        self.selected_index = None
        self.selection_anchor = None
        self.pending_rect = None
        self.background_color = None
        self.foreground_mode.set("auto")
        self.update_background_label()
        self._set_manual_rect({
            "x": 0,
            "y": 0,
            "w": min(32, image.width),
            "h": min(32, image.height),
        })
        self.path_label.configure(
            text="%s  —  %d × %d px" % (self.image_path, image.width, image.height)
        )
        self.name_entry.delete(0, tk.END)
        self._refresh_list()
        self.redraw_image()
        self.status.configure(
            text="Drag on the sheet to select a crop. Zoom with the wheel or +/-."
        )
        manifest_path = path.with_name(path.stem + ".frames.json")
        if load_sibling_manifest and manifest_path.is_file():
            self.open_manifest(manifest_path, load_sibling_manifest=False)

    def choose_image(self):
        path = filedialog.askopenfilename(
            parent=self.root,
            title="Open sprite sheet",
            filetypes=[("Images", "*.png *.webp *.bmp *.gif *.tif *.tiff"),
                       ("All files", "*.*")],
        )
        if path:
            self.open_image(path)

    def choose_manifest(self):
        path = filedialog.askopenfilename(
            parent=self.root,
            title="Open frame manifest",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        self.open_manifest(path)

    def open_manifest(self, path, load_sibling_manifest=False):
        try:
            image_path, frames = load_manifest(path)
            self.open_image(
                image_path, load_sibling_manifest=load_sibling_manifest
            )
            manifest_path = Path(path)
            with manifest_path.open("r", encoding="utf-8") as source:
                manifest = json.load(source)
            dimensions = manifest.get("image_size")
            if dimensions and (dimensions.get("width") != self.image.width
                               or dimensions.get("height") != self.image.height):
                raise ValueError("Manifest image dimensions do not match the source.")
            validate_frames(frames, self.image.size)
            self.frames = [dict(frame) for frame in frames]
            self._refresh_list()
            self.status.configure(text="Loaded %d frame(s)." % len(self.frames))
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
            messagebox.showerror("Cannot open manifest", str(error), parent=self.root)

    def redraw_image(self):
        if self.image is None:
            return
        scaled_size = (self.image.width * self.zoom, self.image.height * self.zoom)
        scaled = self.image.resize(scaled_size, Image.Resampling.NEAREST)
        self.photo = ImageTk.PhotoImage(scaled, master=self.root)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, image=self.photo, anchor=tk.NW, tags="sheet")
        self.canvas.configure(scrollregion=(0, 0, *scaled_size))
        self.zoom_label.configure(text="%d×" % self.zoom)
        self._draw_frame_rectangles()

    def _draw_frame_rectangles(self):
        if self.image is None:
            return
        self.canvas.delete("frame")
        self.canvas.delete("pending")
        selected_indices = set(self._selected_frame_indices())
        for index, frame in enumerate(self.frames):
            x0 = frame["x"] * self.zoom
            y0 = frame["y"] * self.zoom
            x1 = (frame["x"] + frame["w"]) * self.zoom
            y1 = (frame["y"] + frame["h"]) * self.zoom
            color = "#ffd740" if index in selected_indices else "#38e878"
            self.canvas.create_rectangle(x0, y0, x1, y1, outline=color,
                                         width=max(1, min(self.zoom, 3)),
                                         tags=("frame", "frame-%d" % index))
            self.canvas.create_text(x0 + 3, y0 + 2, anchor=tk.NW,
                                    text=str(index + 1), fill=color,
                                    tags=("frame", "frame-%d" % index))
        if self.pending_rect is not None:
            rect = self.pending_rect
            self.canvas.create_rectangle(
                rect["x"] * self.zoom, rect["y"] * self.zoom,
                (rect["x"] + rect["w"]) * self.zoom,
                (rect["y"] + rect["h"]) * self.zoom,
                outline="#ff5252", width=max(1, min(self.zoom, 3)),
                dash=(5, 3), tags=("pending",),
            )

    def _record_undo(self):
        self.undo_stack.append(
            (
                [dict(frame) for frame in self.frames],
                self.selected_index,
                tuple(self._selected_frame_indices()),
                self.selection_anchor,
            )
        )
        if len(self.undo_stack) > 50:
            del self.undo_stack[0]

    def undo(self):
        if not self.undo_stack:
            self.status.configure(text="Nothing to undo.")
            return
        (
            self.frames, self.selected_index, selected_indices,
            self.selection_anchor,
        ) = self.undo_stack.pop()
        self.pending_rect = None
        self._refresh_list()
        if self.selected_index is not None:
            self.frame_list.selection_clear(0, tk.END)
            for index in selected_indices:
                self.frame_list.selection_set(index)
            self.select_frame_index(
                self.selected_index, preserve_selection=True
            )
        self.status.configure(text="Last slice edit undone.")

    def change_zoom(self, amount, event=None):
        if self.image is None:
            return
        new_zoom = max(MIN_ZOOM, min(MAX_ZOOM, self.zoom + amount))
        if new_zoom == self.zoom:
            return
        if event is not None:
            image_x = self.canvas.canvasx(event.x) / self.zoom
            image_y = self.canvas.canvasy(event.y) / self.zoom
        else:
            image_x = self.canvas.canvasx(0) / self.zoom
            image_y = self.canvas.canvasy(0) / self.zoom
        self.zoom = new_zoom
        self.redraw_image()
        self.root.update_idletasks()
        scaled_width = self.image.width * self.zoom
        scaled_height = self.image.height * self.zoom
        offset_x = event.x if event is not None else 0
        offset_y = event.y if event is not None else 0
        self.canvas.xview_moveto(
            max(0, (image_x * self.zoom - offset_x) / scaled_width)
        )
        self.canvas.yview_moveto(
            max(0, (image_y * self.zoom - offset_y) / scaled_height)
        )

    def mousewheel_zoom(self, event):
        if event.delta:
            self.change_zoom(1 if event.delta > 0 else -1, event)

    def begin_pan(self, event):
        self.canvas.scan_mark(event.x, event.y)

    def update_pan(self, event):
        self.canvas.scan_dragto(event.x, event.y, gain=1)

    def begin_context_action(self, event):
        self.right_press_position = (event.x, event.y)
        self.right_dragged = False
        self.canvas.scan_mark(event.x, event.y)

    def update_context_action(self, event):
        if self.right_press_position is None:
            return
        start_x, start_y = self.right_press_position
        if max(abs(event.x - start_x), abs(event.y - start_y)) >= 4:
            self.right_dragged = True
        if self.right_dragged:
            self.canvas.scan_dragto(event.x, event.y, gain=1)

    def finish_context_action(self, event):
        if self.right_press_position is None:
            return "break"
        dragged = self.right_dragged
        self.right_press_position = None
        self.right_dragged = False
        if dragged or self.image is None:
            return "break"

        point = self._image_point(event)
        self.context_image_point = (
            min(self.image.width - 1, int(point[0])),
            min(self.image.height - 1, int(point[1])),
        )
        frame_index = frame_at_point(self.frames, *point)
        if frame_index is not None:
            self.select_frame_index(frame_index)
        else:
            x = min(self.image.width - 1, int(point[0]))
            y = min(self.image.height - 1, int(point[1]))
            self.manual_x.set(str(x))
            self.manual_y.set(str(y))
        self._post_context_menu(event)
        return "break"

    def _post_context_menu(self, event):
        has_image = self.image is not None
        has_selection = self.pending_rect is not None
        has_frames = bool(self.frames)
        has_selected = (
            self.selected_index is not None
            and 0 <= self.selected_index < len(self.frames)
        )
        states = (
            has_selection, has_selection, has_frames and not self.ai_labeling,
            has_selection, has_selection, has_image, has_image, has_image,
            has_selection, has_selected, bool(self.undo_stack), has_selected,
            has_image, has_frames,
        )
        command_index = 0
        for index in range(self.context_menu.index("end") + 1):
            if self.context_menu.type(index) != "command":
                continue
            self.context_menu.entryconfigure(
                index, state=tk.NORMAL if states[command_index] else tk.DISABLED
            )
            command_index += 1
        try:
            self.context_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.context_menu.grab_release()

    def set_background_from_context(self):
        if self.image is None or self.context_image_point is None:
            self.status.configure(text="Right-click the image to sample a background.")
            return
        x, y = self.context_image_point
        red, green, blue, _alpha = self.image.getpixel((x, y))
        self.background_color = (red, green, blue)
        self.foreground_mode.set("color")
        description = "#%02x%02x%02x" % self.background_color
        self.update_background_label()
        self.status.configure(
            text="Sampled background %s at (%d, %d); adjust color tolerance if needed."
            % (description, x, y)
        )

    def update_background_label(self, _event=None):
        mode = self.foreground_mode.get()
        if mode == "alpha":
            text = "Background: unused"
        elif mode == "auto":
            text = "Background: auto"
        elif self.background_color is None:
            text = "Background: edge estimate"
        else:
            text = "Background: #%02x%02x%02x" % self.background_color
        self.background_label.configure(text=text)

    def _image_point(self, event):
        x = self.canvas.canvasx(event.x) / self.zoom
        y = self.canvas.canvasy(event.y) / self.zoom
        return (max(0, min(self.image.width, x)),
                max(0, min(self.image.height, y)))

    def begin_selection(self, event):
        if self.image is None:
            return
        point = self._image_point(event)
        frame_index = frame_at_point(self.frames, *point)
        if frame_index is not None:
            if event.state & 0x0005:
                self._select_frame_with_modifiers(frame_index, event.state)
            else:
                self.select_frame_index(frame_index, focus_label=True)
            self.drag_start = None
            return
        self.drag_start = point
        if self.drag_rectangle is not None:
            self.canvas.delete(self.drag_rectangle)
        x, y = self.canvas.canvasx(event.x), self.canvas.canvasy(event.y)
        self.drag_rectangle = self.canvas.create_rectangle(
            x, y, x, y, outline="#ff5252", width=2
        )

    def update_selection(self, event):
        if self.drag_start is None:
            return
        start_x = self.drag_start[0] * self.zoom
        start_y = self.drag_start[1] * self.zoom
        end_x = self.canvas.canvasx(event.x)
        end_y = self.canvas.canvasy(event.y)
        self.canvas.coords(self.drag_rectangle, start_x, start_y, end_x, end_y)

    def finish_selection(self, event):
        if self.drag_start is None:
            return
        end = self._image_point(event)
        rect = frame_from_drag(self.drag_start, end, self.image.size)
        self.drag_start = None
        if self.drag_rectangle is not None:
            self.canvas.delete(self.drag_rectangle)
        self.drag_rectangle = None
        self.name_entry.delete(0, tk.END)
        self.name_entry.insert(tk.END, "frame_%03d" % (len(self.frames) + 1))
        self.pending_rect = rect
        self._set_manual_rect(rect)
        self._draw_frame_rectangles()
        self.status.configure(
            text="Selection: x=%d, y=%d, w=%d, h=%d. Name it and click Add selection."
            % (rect["x"], rect["y"], rect["w"], rect["h"])
        )
        self.show_preview(rect)

    def _manual_rect(self):
        rect = {
            "x": int(self.manual_x.get()),
            "y": int(self.manual_y.get()),
            "w": int(self.manual_width.get()),
            "h": int(self.manual_height.get()),
        }
        validate_frames([rect], self.image.size)
        return rect

    def _foreground_options(self):
        return {
            "foreground_mode": self.foreground_mode.get(),
            "background_color": self.background_color,
            "color_tolerance": self.color_tolerance.get(),
        }

    def preview_manual_slice(self):
        if self.image is None:
            self.status.configure(text="Open an image before previewing a slice.")
            return
        try:
            rect = self._manual_rect()
        except (tk.TclError, ValueError) as error:
            messagebox.showerror(
                "Invalid slice rectangle",
                "Enter integer coordinates and positive dimensions inside "
                "the image.\n\n%s" % error,
                parent=self.root,
            )
            return
        self.pending_rect = rect
        self._draw_frame_rectangles()
        self.show_preview(rect)
        self.status.configure(
            text="Preview: x=%d, y=%d, w=%d, h=%d."
            % (rect["x"], rect["y"], rect["w"], rect["h"])
        )

    def apply_manual_slice(self):
        if (self.image is None or self.selected_index is None
                or self.selected_index >= len(self.frames)):
            self.status.configure(text="Select a frame before applying edits.")
            return
        try:
            rect = self._manual_rect()
        except (tk.TclError, ValueError) as error:
            messagebox.showerror(
                "Invalid slice rectangle",
                "Enter integer coordinates and positive dimensions inside "
                "the image.\n\n%s" % error,
                parent=self.root,
            )
            return
        self._record_undo()
        frame = self.frames[self.selected_index]
        frame.update(rect)
        frame["name"] = self.name_entry.get().strip() or frame.get(
            "name", "frame_%03d" % (self.selected_index + 1)
        )
        self.pending_rect = None
        self._refresh_list()
        self.select_frame_index(self.selected_index)
        self.status.configure(text="Updated %s." % frame["name"])

    def _set_manual_rect(self, rect):
        self.manual_x.set(str(rect["x"]))
        self.manual_y.set(str(rect["y"]))
        self.manual_width.set(str(rect["w"]))
        self.manual_height.set(str(rect["h"]))
        if self.lock_proportion.get():
            self.manual_proportion = rect["w"] / rect["h"]

    def toggle_proportion_lock(self):
        self.manual_proportion = None
        if self.lock_proportion.get():
            self.apply_manual_proportion()

    def apply_manual_proportion(self, _event=None, changed=None):
        if not self.lock_proportion.get() or self.image is None:
            return
        try:
            width = int(self.manual_width.get())
            height = int(self.manual_height.get())
        except ValueError:
            return
        if width <= 0 or height <= 0:
            return
        if self.manual_proportion is None:
            self.manual_proportion = width / height
        if changed is self.manual_width:
            height = max(1, round(width / self.manual_proportion))
            self.manual_height.set(str(height))
        elif changed is self.manual_height:
            width = max(1, round(height * self.manual_proportion))
            self.manual_width.set(str(width))

    def add_manual_slice(self):
        if self.image is None:
            self.status.configure(text="Open an image before adding a slice.")
            return
        try:
            rect = self._manual_rect()
        except (tk.TclError, ValueError) as error:
            messagebox.showerror(
                "Invalid manual slice",
                "Enter integer coordinates and positive dimensions inside "
                "the image.\n\n%s" % error,
                parent=self.root,
            )
            return

        frame = dict(rect)
        frame["name"] = self.name_entry.get().strip() or (
            "frame_%03d" % (len(self.frames) + 1)
        )
        self._record_undo()
        self.frames.append(frame)
        self.pending_rect = None
        self.selected_index = len(self.frames) - 1
        self._refresh_list()
        self.frame_list.selection_set(self.selected_index)
        self.frame_list.see(self.selected_index)
        self.select_frame_index(self.selected_index)
        self.status.configure(
            text="Added %s at (%d, %d), %d × %d px."
            % (frame["name"], frame["x"], frame["y"],
               frame["w"], frame["h"])
        )

    def add_selection(self):
        rect = getattr(self, "pending_rect", None)
        if rect is None:
            self.status.configure(text="Drag a rectangle on the sheet first.")
            return
        frame = dict(rect)
        frame["name"] = self.name_entry.get().strip() or (
            "frame_%03d" % (len(self.frames) + 1)
        )
        self._record_undo()
        self.frames.append(frame)
        self.pending_rect = None
        self.selected_index = len(self.frames) - 1
        self._refresh_list()
        self.frame_list.selection_set(self.selected_index)
        self.frame_list.see(self.selected_index)
        self.select_frame_index(self.selected_index)
        self.status.configure(text="Added %s." % frame["name"])

    def auto_slice_selection(self):
        if self.image is None or self.pending_rect is None:
            self.status.configure(
                text="Drag a selection around one horizontal row of frames first."
            )
            return
        try:
            frames = auto_slice_alpha(
                self.image,
                self.pending_rect,
                merge_gap=self.gap_tolerance.get(),
                alpha_threshold=self.alpha_threshold.get(),
                **self._foreground_options()
            )
            frames = add_frame_padding(
                frames, self.pending_rect, self.frame_padding.get(),
                self.image.size,
            )
        except (tk.TclError, ValueError) as error:
            messagebox.showerror("Cannot auto-slice selection", str(error),
                                 parent=self.root)
            return
        if not frames:
            self.status.configure(
                text="No frames found. Select a single horizontal row containing "
                     "opaque pixels."
            )
            return

        self._append_detected_frames(frames, "frame", "Auto-sliced")

    def auto_slice_components_selection(self):
        if self.image is None or self.pending_rect is None:
            self.status.configure(
                text="Select an area first to auto-slice separate opaque objects."
            )
            return
        try:
            frames = auto_slice_components(
                self.image,
                self.pending_rect,
                min_pixels=self.minimum_pixels.get(),
                alpha_threshold=self.alpha_threshold.get(),
                connectivity=self.connectivity.get(),
                **self._foreground_options()
            )
            frames = add_frame_padding(
                frames, self.pending_rect, self.frame_padding.get(),
                self.image.size,
            )
        except (tk.TclError, ValueError) as error:
            messagebox.showerror("Cannot auto-slice objects", str(error),
                                 parent=self.root)
            return
        if not frames:
            self.status.configure(text="No separate opaque objects found.")
            return
        self._append_detected_frames(frames, "object", "Found")

    def smart_slice_selection(self):
        if self.image is None or self.pending_rect is None:
            self.status.configure(
                text="Select a sprite sheet area before detecting a grid."
            )
            return
        try:
            frames = auto_slice_grid(
                self.image,
                self.pending_rect,
                merge_gap=self.gap_tolerance.get(),
                min_pixels=self.minimum_pixels.get(),
                alpha_threshold=self.alpha_threshold.get(),
                **self._foreground_options()
            )
            frames = add_frame_padding(
                frames, self.pending_rect, self.frame_padding.get(),
                self.image.size,
            )
        except (tk.TclError, ValueError) as error:
            messagebox.showerror("Cannot detect sprite grid", str(error),
                                 parent=self.root)
            return
        if not frames:
            self.status.configure(
                text="No grid cells found. Lower the threshold/minimum or "
                     "select a more tightly spaced grid."
            )
            return
        self._append_detected_frames(frames, "cell", "Detected")

    def analyze_and_slice_selection(self):
        if self.ai_labeling:
            self.status.configure(
                text="Wait for current AI labeling to finish before analyzing "
                     "another selection."
            )
            return
        if self.image is None or self.pending_rect is None:
            self.status.configure(
                text="Select a sprite sheet area before running image analysis."
            )
            return
        try:
            options = self._foreground_options()
            _mask, used_mode, background = _foreground_mask(
                self.image, self.pending_rect, self.alpha_threshold.get(),
                mode=options["foreground_mode"],
                background_color=options["background_color"],
                color_tolerance=options["color_tolerance"],
            )
            max_gap = self.gap_tolerance.get()
            gap_candidates = sorted(
                {0, max_gap, max_gap // 2}, reverse=True
            )
            frames = []
            chosen_gap = max_gap
            for gap in gap_candidates:
                candidate = _slice_grid_from_mask(
                    _mask, self.pending_rect, gap, self.minimum_pixels.get(),
                    AUTO_SLICE_MIN_FRAME_SIZE,
                )
                if len(candidate) > len(frames):
                    frames = candidate
                    chosen_gap = gap
            strategy = "grid"
            if len(frames) < 2:
                components = auto_slice_components(
                    self.image,
                    self.pending_rect,
                    min_pixels=self.minimum_pixels.get(),
                    alpha_threshold=self.alpha_threshold.get(),
                    connectivity=self.connectivity.get(),
                    **options
                )
                if len(components) > len(frames):
                    frames = components
                    strategy = "connected objects"
            frames = add_frame_padding(
                frames, self.pending_rect, self.frame_padding.get(),
                self.image.size,
            )
        except (tk.TclError, ValueError) as error:
            messagebox.showerror(
                "Cannot analyze sprite sheet", str(error), parent=self.root
            )
            return
        if not frames:
            self.status.configure(
                text="No sprites found. Try another foreground mode or "
                     "adjust alpha/color tolerance and minimum pixels."
            )
            return

        detected_frames = self._append_detected_frames(
            frames, "sprite", "Analyzed"
        )
        if used_mode == "color" and background is not None:
            description = "#%02x%02x%02x" % background
            self.background_label.configure(text="Background: %s" % description)
            source = "background %s (tolerance %d)" % (
                description, self.color_tolerance.get()
            )
        else:
            source = "alpha transparency"
        self.status.configure(
            text="Analyzed with %s; detected %d slice(s) using %s (gap %d). "
                 "Generating local AI labels; review before export."
            % (source, len(frames), strategy, chosen_gap)
        )
        self._start_ai_labeling(detected_frames)

    def _append_detected_frames(self, frames, default_prefix, verb):
        prefix = self.name_entry.get().strip() or default_prefix
        first_index = len(self.frames) + 1
        detected_frames = [
            dict(frame, name="%s_%03d" % (prefix, first_index + offset))
            for offset, frame in enumerate(frames)
        ]
        self._record_undo()
        self.frames.extend(detected_frames)
        self.pending_rect = None
        self.selected_index = len(self.frames) - 1
        self._refresh_list()
        self.frame_list.selection_set(self.selected_index)
        self.frame_list.see(self.selected_index)
        self.select_frame_index(self.selected_index)
        self.status.configure(
            text="%s %d slice(s). Inspect and adjust before export."
            % (verb, len(frames))
        )
        return detected_frames

    def label_existing_frames_with_ai(self):
        if not self.frames:
            self.status.configure(text="Detect or add frames before AI labeling.")
            return
        selected_indices = self._selected_frame_indices()
        candidates = (
            [self.frames[index] for index in selected_indices]
            if len(selected_indices) > 1 else self.frames
        )
        if self.replace_ai_labels.get():
            targets = candidates
        else:
            targets = [
                frame for frame in candidates
                if re.fullmatch(
                    r"(?:frame|sprite|cell|object)_\d+(?:_\d+)*",
                    frame.get("name", ""),
                    flags=re.IGNORECASE,
                )
            ]
        if not targets:
            self.status.configure(
                text="No generic frame names to replace. Enable “Replace "
                     "existing labels” to relabel every slice."
            )
            return
        self._start_ai_labeling(targets)

    def _start_ai_labeling(self, frames):
        if self.ai_labeling:
            self.status.configure(text="AI labeling is already in progress.")
            return
        if self.image is None or not frames:
            self.status.configure(text="Detect or add frames before AI labeling.")
            return

        targets = [
            (frame, frame.get("name", ""))
            for frame in frames
        ]
        frame_crops = [dict(frame) for frame, _name in targets]
        image = self.image.copy()
        result_queue = queue.Queue()
        cancel_event = threading.Event()
        self.ai_label_queue = result_queue
        self.ai_label_targets = targets
        self.ai_cancel_event = cancel_event
        self.ai_stage = "loading"
        self.ai_labeling = True
        self.ai_label_button.configure(state=tk.DISABLED)
        self.ai_cancel_button.configure(
            text="Cancel", state=tk.NORMAL
        )
        self.ai_progress.configure(mode="indeterminate", value=0)
        self.ai_progress.pack(side=tk.LEFT, padx=(8, 4))
        self.ai_cancel_button.pack(side=tk.LEFT)
        self.ai_progress.start(12)
        self.status.configure(
            text="Loading local AI model; first use may download it…"
        )

        def label_in_background():
            try:
                if __package__:
                    from .sprite_ai_labeler import (
                        LabelingCancelled, label_frames,
                    )
                else:
                    from sprite_ai_labeler import (
                        LabelingCancelled, label_frames,
                    )
            except Exception as error:
                result_queue.put(("error", str(error)))
                return
            try:
                labels = label_frames(
                    image,
                    frame_crops,
                    progress_callback=lambda done, total: result_queue.put(
                        ("progress", done, total)
                    ),
                    stage_callback=lambda stage: result_queue.put(
                        ("stage", stage)
                    ),
                    cancel_event=cancel_event,
                )
                result_queue.put(("complete", labels))
            except LabelingCancelled:
                result_queue.put(("cancelled",))
            except Exception as error:
                result_queue.put(("error", str(error)))

        threading.Thread(target=label_in_background, daemon=True).start()
        self.root.after(100, self._poll_ai_labeling)

    def cancel_ai_labeling(self):
        if not self.ai_labeling or self.ai_cancel_event is None:
            return
        self.ai_cancel_event.set()
        self.ai_cancel_button.configure(
            text="Cancelling…", state=tk.DISABLED
        )
        if self.ai_stage == "loading":
            text = (
                "Cancel requested. Model loading/download must finish before "
                "stopping; labels will be discarded."
            )
        else:
            text = (
                "Cancelling after the current model batch; generated labels "
                "will be discarded."
            )
        self.status.configure(text=text)

    def _poll_ai_labeling(self):
        result_queue = self.ai_label_queue
        if result_queue is None:
            return
        try:
            while True:
                result = result_queue.get_nowait()
                if result[0] == "stage":
                    self.ai_stage = result[1]
                    if result[1] == "captioning":
                        self.ai_progress.stop()
                        self.ai_progress.configure(
                            mode="determinate", maximum=100, value=0
                        )
                        self.status.configure(text="Generating sprite labels…")
                elif result[0] == "progress":
                    done, total = result[1:]
                    self.ai_progress.configure(
                        value=100 * done / max(total, 1)
                    )
                    self.status.configure(
                        text="Generating AI labels: %d/%d…" % (done, total)
                    )
                elif result[0] == "complete":
                    labels = result[1]
                    try:
                        self._apply_ai_labels(labels)
                    except ValueError as error:
                        self._finish_ai_labeling()
                        self.status.configure(text="AI labeling failed.")
                        messagebox.showerror(
                            "AI labeling failed",
                            str(error),
                            parent=self.root,
                        )
                        return
                    self._finish_ai_labeling()
                    self.status.configure(
                        text="AI suggested %d label(s). Review, correct, "
                             "then save the manifest."
                        % len(labels)
                    )
                    return
                elif result[0] == "cancelled":
                    self._finish_ai_labeling()
                    self.status.configure(
                        text="AI labeling cancelled. No generated labels were "
                             "applied."
                    )
                    return
                elif result[0] == "error":
                    self._finish_ai_labeling()
                    self.status.configure(text="AI labeling failed.")
                    messagebox.showerror(
                        "AI labeling failed",
                        result[1],
                        parent=self.root,
                    )
                    return
        except queue.Empty:
            pass
        self.root.after(100, self._poll_ai_labeling)

    def _finish_ai_labeling(self):
        self.ai_progress.stop()
        self.ai_progress.pack_forget()
        self.ai_cancel_button.pack_forget()
        self.ai_cancel_button.configure(
            text="Cancel", state=tk.DISABLED
        )
        self.ai_label_button.configure(state=tk.NORMAL)
        self.ai_labeling = False
        self.ai_label_queue = None
        self.ai_cancel_event = None
        self.ai_stage = "idle"

    def _apply_ai_labels(self, labels):
        if len(labels) != len(self.ai_label_targets):
            raise ValueError(
                "The model returned an unexpected number of labels."
            )
        changed = False
        for (frame, original_name), label in zip(self.ai_label_targets, labels):
            if (any(current is frame for current in self.frames)
                    and frame.get("name", "") == original_name):
                if not changed:
                    self._record_undo()
                frame["name"] = label
                changed = True
        if changed:
            self._refresh_list()
            if self.selected_index is not None:
                self.select_frame_index(self.selected_index)

    def label_selected_frame(self, _event=None):
        if self.selected_index is None or not self.frames:
            return
        name = self.name_entry.get().strip()
        if not name:
            return
        frame = self.frames[self.selected_index]
        frame["name"] = name
        index = self.selected_index
        self.frame_list.delete(index)
        self.frame_list.insert(
            index, "%s  (%d,%d %d×%d)" %
            (name, frame["x"], frame["y"], frame["w"], frame["h"])
        )
        for selected_index in self._selected_frame_indices():
            self.frame_list.selection_set(selected_index)
        self.frame_list.see(index)
        selected_count = len(self._selected_frame_indices())
        if selected_count > 1:
            self.status.configure(
                text="Renamed selected frame %d (%d frames remain selected)."
                % (index + 1, selected_count)
            )
        else:
            self.status.configure(text="Label: %s" % name)

    def finish_labeling(self, _event=None):
        self.label_selected_frame()
        self.name_entry.selection_range(0, tk.END)
        return "break"

    def select_frame_index(self, index, focus_label=False,
                           preserve_selection=False):
        if index < 0 or index >= len(self.frames):
            return
        self.selected_index = index
        self.pending_rect = None
        if not preserve_selection:
            self.frame_list.selection_clear(0, tk.END)
            self.frame_list.selection_set(index)
            self.selection_anchor = index
        self.frame_list.see(index)
        self._draw_frame_rectangles()
        frame = self.frames[index]
        self.name_entry.delete(0, tk.END)
        self.name_entry.insert(0, frame.get("name", "frame"))
        self.show_preview(frame)
        crop = self.image.crop((
            frame["x"], frame["y"],
            frame["x"] + frame["w"], frame["y"] + frame["h"],
        ))
        visible = crop.getchannel("A").point(
            lambda value: 255 if value > self.alpha_threshold.get() else 0
        ).getbbox()
        coverage = 0
        if visible:
            visible_area = (visible[2] - visible[0]) * (visible[3] - visible[1])
            coverage = round(100 * visible_area / (frame["w"] * frame["h"]))
        self.details.configure(
            text="x=%d  y=%d\nwidth=%d  height=%d\nVisible bounds: %s\n"
                 "Bounding-box coverage: %d%%"
            % (frame["x"], frame["y"], frame["w"], frame["h"],
               "none" if visible is None else
               "%d × %d" % (visible[2] - visible[0],
                             visible[3] - visible[1]),
               coverage)
        )
        self._set_manual_rect(frame)
        if focus_label:
            self.name_entry.focus_set()
            self.name_entry.selection_range(0, tk.END)

    def select_frame(self, _event=None):
        selection = self.frame_list.curselection()
        if not selection:
            self.selected_index = None
            self.selection_anchor = None
            self._draw_frame_rectangles()
            return
        index = self.frame_list.index(tk.ACTIVE)
        if index not in selection:
            index = selection[-1]
        self.selection_anchor = index
        self.select_frame_index(index, preserve_selection=True)
        if len(selection) > 1:
            self.status.configure(
                text="%d frames selected. Rename the active frame or use "
                     "Delete / AI label." % len(selection)
            )

    def _selected_frame_indices(self):
        if hasattr(self, "frame_list"):
            selection = [
                int(index) for index in self.frame_list.curselection()
                if 0 <= int(index) < len(self.frames)
            ]
            if selection:
                return selection
        if (self.selected_index is not None
                and 0 <= self.selected_index < len(self.frames)):
            return [self.selected_index]
        return []

    def _set_frame_selection(self, indices, primary=None, anchor=None):
        valid = sorted({
            index for index in indices
            if 0 <= index < len(self.frames)
        })
        if not valid:
            self.selected_index = None
            self.selection_anchor = None
            self.frame_list.selection_clear(0, tk.END)
            self._draw_frame_rectangles()
            return
        if primary not in valid:
            primary = valid[-1]
        self.frame_list.selection_clear(0, tk.END)
        for index in valid:
            self.frame_list.selection_set(index)
        self.selected_index = primary
        self.selection_anchor = primary if anchor is None else anchor
        self.select_frame_index(primary, preserve_selection=True)
        if len(valid) > 1:
            self.status.configure(
                text="%d frames selected. Use Delete or AI label to act on "
                     "the selection." % len(valid)
            )

    def _select_frame_with_modifiers(self, index, state):
        control = bool(state & 0x0004)
        shift = bool(state & 0x0001)
        current = set(self._selected_frame_indices())
        if shift:
            anchor = self.selection_anchor
            if anchor is None:
                anchor = self.selected_index if self.selected_index is not None else index
            first, last = sorted((anchor, index))
            selected = set(range(first, last + 1))
            if control:
                selected.update(current)
            self._set_frame_selection(selected, primary=index, anchor=anchor)
        elif control:
            if index in current:
                current.remove(index)
                primary = self.selected_index
                if primary == index:
                    primary = max(current) if current else None
                if primary is None:
                    self._set_frame_selection([], anchor=None)
                else:
                    self._set_frame_selection(
                        current, primary=primary, anchor=self.selection_anchor
                    )
            else:
                current.add(index)
                self._set_frame_selection(current, primary=index, anchor=index)
        else:
            self._set_frame_selection([index], primary=index, anchor=index)

    def show_preview(self, frame):
        crop = self.image.crop((frame["x"], frame["y"],
                                frame["x"] + frame["w"],
                                frame["y"] + frame["h"]))
        checker = Image.new("RGBA", crop.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(checker)
        square = 8
        for y in range(0, crop.height, square):
            for x in range(0, crop.width, square):
                if (x // square + y // square) % 2 == 0:
                    draw.rectangle((x, y, x + square - 1, y + square - 1),
                                   fill=(100, 100, 100, 255))
        preview = Image.alpha_composite(checker, crop)
        scale = min(PREVIEW_SIZE / crop.width, PREVIEW_SIZE / crop.height)
        size = (max(1, round(crop.width * scale)),
                max(1, round(crop.height * scale)))
        preview = preview.resize(size, Image.Resampling.NEAREST)
        self.preview_photo = ImageTk.PhotoImage(preview, master=self.root)
        self.preview.configure(image=self.preview_photo, text="")

    def _refresh_list(self):
        selected_indices = self._selected_frame_indices()
        self.frame_list.delete(0, tk.END)
        for frame in self.frames:
            self.frame_list.insert(
                tk.END, "%s  (%d,%d %d×%d)" %
                (frame.get("name", "frame"), frame["x"], frame["y"],
                 frame["w"], frame["h"])
            )
        for index in selected_indices:
            if index < len(self.frames):
                self.frame_list.selection_set(index)
        self.redraw_image()

    def delete_selected(self):
        selected_indices = self._selected_frame_indices()
        if not selected_indices:
            return
        first_deleted = min(selected_indices)
        self._record_undo()
        for index in reversed(selected_indices):
            del self.frames[index]
        self.selected_index = None
        self.selection_anchor = None
        self._refresh_list()
        self.frame_list.selection_clear(0, tk.END)
        if self.frames:
            self.select_frame_index(min(first_deleted, len(self.frames) - 1))
        else:
            self.preview.configure(image="", text="")
            self.details.configure(text="")
        self.status.configure(
            text="Deleted %d selected frame(s)." % len(selected_indices)
        )

    def save_manifest(self):
        if self.image is None:
            return
        path = filedialog.asksaveasfilename(
            parent=self.root,
            title="Save frame coordinates",
            initialfile=self.image_path.stem + ".frames.json",
            defaultextension=".json",
            filetypes=[("JSON files", "*.json")],
        )
        if not path:
            return
        try:
            validate_frames(self.frames, self.image.size)
            manifest_path = Path(path).resolve()
            relative_image = os.path.relpath(
                self.image_path, manifest_path.parent
            )
            manifest = {
                "image": Path(relative_image).as_posix(),
                "image_size": {"width": self.image.width,
                               "height": self.image.height},
                "frames": self.frames,
            }
            with manifest_path.open("w", encoding="utf-8") as destination:
                json.dump(manifest, destination, ensure_ascii=False, indent=2)
                destination.write("\n")
            self.status.configure(text="Saved %d frame(s) to %s."
                                  % (len(self.frames), path))
        except (OSError, ValueError) as error:
            messagebox.showerror("Cannot save manifest", str(error), parent=self.root)

    def export_frames(self):
        if self.image is None or not self.frames:
            messagebox.showinfo("No frames", "Select and add at least one frame.",
                                parent=self.root)
            return
        directory = filedialog.askdirectory(parent=self.root,
                                            title="Export selected PNG frames")
        if not directory:
            return
        try:
            validate_frames(self.frames, self.image.size)
            output = Path(directory)
            for index, frame in enumerate(self.frames, start=1):
                name = re.sub(r"[^A-Za-z0-9_-]+", "_",
                              frame.get("name", "frame")).strip("_")
                if not name:
                    name = "frame"
                crop = self.image.crop((frame["x"], frame["y"],
                                        frame["x"] + frame["w"],
                                        frame["y"] + frame["h"]))
                crop.save(output / ("%03d_%s.png" % (index, name)))
            self.status.configure(text="Exported %d PNG frame(s) to %s."
                                  % (len(self.frames), directory))
        except (OSError, ValueError) as error:
            messagebox.showerror("Cannot export frames", str(error),
                                 parent=self.root)


def open_manifest_at_startup(selector, manifest_path):
    selector.open_manifest(manifest_path)


def main():
    parser = argparse.ArgumentParser(
        description="Select, inspect, and export individual sprite frames."
    )
    parser.add_argument("image", nargs="?", type=Path,
                        help="sprite sheet to open at startup")
    parser.add_argument("--manifest", type=Path,
                        help="load frame rectangles from a JSON manifest")
    args = parser.parse_args()
    image_path = args.image
    if args.manifest:
        image_path, _frames = load_manifest(args.manifest)
    elif image_path is None:
        image_path = (Path(__file__).resolve().parents[1]
                      / "assets" / "character" / "Characters.png")
    root = tk.Tk()
    selector = SpriteFrameSelector(root, image_path)
    if args.manifest:
        root.after_idle(open_manifest_at_startup, selector, args.manifest)
    root.mainloop()


if __name__ == "__main__":
    main()
