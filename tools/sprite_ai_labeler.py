"""Generate local descriptive labels for sprite crops with BLIP."""

import contextlib
import io
import re

from PIL import Image


MODEL_ID = "Salesforce/blip-image-captioning-base"
_CAPTIONER = None


class LabelingCancelled(Exception):
    """Raised when the user cancels a batch of sprite captions."""


def _load_captioner():
    global _CAPTIONER
    if _CAPTIONER is None:
        try:
            import torch
            from transformers import BlipForConditionalGeneration, BlipProcessor
        except ImportError as error:
            raise RuntimeError(
                "AI labeling requires PyTorch and Transformers. Install them "
                "with: python -m pip install -r tools/requirements-ai.txt"
            ) from error

        with contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            processor = BlipProcessor.from_pretrained(MODEL_ID)
            model = BlipForConditionalGeneration.from_pretrained(MODEL_ID)
        device = "cuda" if torch.cuda.is_available() else "cpu"
        model.to(device)
        model.eval()
        _CAPTIONER = torch, processor, model, device
    return _CAPTIONER


def _prepare_crop(image, frame):
    crop = image.crop((
        frame["x"], frame["y"],
        frame["x"] + frame["w"], frame["y"] + frame["h"],
    )).convert("RGBA")
    background = Image.new("RGBA", crop.size, (96, 96, 96, 255))
    crop = Image.alpha_composite(background, crop).convert("RGB")
    shortest_side = min(crop.size)
    if shortest_side < 64:
        scale = min(64 / shortest_side, 1024 / max(crop.size))
        size = (max(1, round(crop.width * scale)),
                max(1, round(crop.height * scale)))
        crop = crop.resize(size, Image.Resampling.NEAREST)
    return crop


def _caption_to_label(caption):
    label = re.sub(r"\s+", " ", caption).strip(" \t\r\n.,;:!?\"'")
    label = re.sub(
        r"^(?:a|an|the)\s+(?:picture|photo|image|drawing|rendering)\s+of\s+",
        "",
        label,
        flags=re.IGNORECASE,
    )
    label = re.sub(r"^(?:a|an|the)\s+", "", label, flags=re.IGNORECASE)
    label = label.split(".", 1)[0].strip(" \t\r\n.,;:!?\"'")
    if len(label) > 72:
        label = label[:72].rsplit(" ", 1)[0]
    if not label:
        raise ValueError("The image model returned an empty label.")
    return label[:1].upper() + label[1:]


def label_frames(image, frames, progress_callback=None, stage_callback=None,
                 cancel_event=None, batch_size=4):
    """Caption sprite crops locally and report completed-frame counts."""
    if batch_size < 1:
        raise ValueError("AI labeling batch size must be positive.")
    if cancel_event is not None and cancel_event.is_set():
        raise LabelingCancelled()
    if stage_callback is not None:
        stage_callback("loading")
    torch, processor, model, device = _load_captioner()
    if cancel_event is not None and cancel_event.is_set():
        raise LabelingCancelled()
    if stage_callback is not None:
        stage_callback("captioning")
    labels = []
    for start in range(0, len(frames), batch_size):
        if cancel_event is not None and cancel_event.is_set():
            raise LabelingCancelled()
        batch = frames[start:start + batch_size]
        crops = [_prepare_crop(image, frame) for frame in batch]
        inputs = processor(images=crops, return_tensors="pt")
        inputs = {key: value.to(device) for key, value in inputs.items()}
        with torch.inference_mode():
            tokens = model.generate(**inputs, max_new_tokens=32, num_beams=3)
        captions = processor.batch_decode(tokens, skip_special_tokens=True)
        labels.extend(_caption_to_label(caption) for caption in captions)
        if progress_callback is not None:
            progress_callback(len(labels), len(frames))
    if cancel_event is not None and cancel_event.is_set():
        raise LabelingCancelled()
    return labels
