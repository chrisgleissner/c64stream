#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-dir", required=True)
    return parser.parse_args()


def run_binary(binary, output_path):
    cmd = [
        binary,
        f"--output={output_path}",
    ]
    subprocess.check_call(cmd)


def run_ffmpeg(output_path, width, height, output_dir):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg not found")

    png_path = os.path.join(output_dir, "frame_%03d.png")
    cmd = [
        ffmpeg,
        "-y",
        "-f",
        "rawvideo",
        "-pixel_format",
        "rgba",
        "-video_size",
        f"{width}x{height}",
        "-i",
        output_path,
        png_path,
    ]
    subprocess.check_call(cmd)


def load_metadata(output_path):
    meta_path = f"{output_path}.json"
    with open(meta_path, "r", encoding="utf-8") as meta_file:
        return json.load(meta_file)


def load_raw(output_path, width, height, frames):
    frame_bytes = width * height * 4
    expected = frame_bytes * frames
    with open(output_path, "rb") as raw_file:
        data = raw_file.read()
    if len(data) != expected:
        raise RuntimeError(f"Raw size mismatch: expected {expected}, got {len(data)}")
    return data


def pixel_at(data, width, height, frame, x, y):
    frame_bytes = width * height * 4
    offset = frame * frame_bytes + (y * width + x) * 4
    r = data[offset]
    g = data[offset + 1]
    b = data[offset + 2]
    a = data[offset + 3]
    return r, g, b, a


def brightness(pixel):
    r, g, b, _ = pixel
    return (r + g + b) / 3.0


def analyze_afterglow_tail(data, width, height, frames):
    y = height // 2
    last_frame = frames - 1
    head_x = last_frame % width

    tail_values = []
    for dx in range(1, width):
        x = (head_x - dx) % width
        value = brightness(pixel_at(data, width, height, last_frame, x, y))
        if value <= 2.0:
            break
        tail_values.append(value)

    if len(tail_values) < 3:
        raise AssertionError(f"Afterglow tail too short: {len(tail_values)}")

    for i in range(1, len(tail_values)):
        if tail_values[i] > tail_values[i - 1] + 1.0:
            raise AssertionError("Afterglow tail is not monotonic")


def analyze_decay_flicker(data, width, height, frames):
    y = height // 2
    x = width // 4
    values = [brightness(pixel_at(data, width, height, f, x, y)) for f in range(frames)]
    peak_index = max(range(frames), key=lambda i: values[i])

    for i in range(peak_index + 1, frames):
        if values[i] > values[i - 1] + 1.0:
            raise AssertionError("Decay shows flicker (brightness increase)")


def analyze_scanline_stability(data, width, height, frames, scan_line_distance, scan_line_strength):
    if scan_line_distance <= 0.0 or scan_line_strength <= 0.0:
        return

    if scan_line_distance <= 0.25:
        total = 5
        scanline = 4
    elif scan_line_distance <= 0.5:
        total = 3
        scanline = 2
    elif scan_line_distance <= 1.0:
        total = 4
        scanline = 2
    else:
        total = 3
        scanline = 1

    ratios = []
    for f in range(frames):
        bright_total = 0.0
        bright_count = 0
        dark_total = 0.0
        dark_count = 0
        for y in range(height):
            row_value = 0.0
            for x in range(width):
                row_value += brightness(pixel_at(data, width, height, f, x, y))
            row_avg = row_value / width
            if (y % total) < scanline:
                bright_total += row_avg
                bright_count += 1
            else:
                dark_total += row_avg
                dark_count += 1

        if bright_count == 0 or dark_count == 0:
            continue
        bright_avg = bright_total / bright_count
        dark_avg = dark_total / dark_count
        if bright_avg <= 0.0:
            continue

        ratios.append(dark_avg / bright_avg)

    if not ratios:
        raise AssertionError("Scanline stability ratios not computed")

    expected_ratio = max(0.0, 1.0 - scan_line_strength)
    for ratio in ratios:
        if abs(ratio - expected_ratio) > 0.2:
            raise AssertionError(f"Scanline ratio drift: {ratio:.3f} vs {expected_ratio:.3f}")

    mean = sum(ratios) / len(ratios)
    variance = sum((r - mean) ** 2 for r in ratios) / len(ratios)
    if variance > 0.01:
        raise AssertionError("Scanline stability variance too high")


def main():
    args = parse_args()
    os.makedirs(args.output_dir, exist_ok=True)

    output_path = os.path.join(args.output_dir, "output.raw")
    run_binary(args.binary, output_path)

    meta = load_metadata(output_path)
    width = int(meta["width"])
    height = int(meta["height"])
    frames = int(meta["frames"])

    run_ffmpeg(output_path, width, height, args.output_dir)

    raw = load_raw(output_path, width, height, frames)
    analyze_afterglow_tail(raw, width, height, frames)
    analyze_decay_flicker(raw, width, height, frames)
    analyze_scanline_stability(
        raw,
        width,
        height,
        frames,
        float(meta["scan_line_distance"]),
        float(meta["scan_line_strength"]),
    )

    print("Pipeline analysis passed")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Pipeline analysis failed: {exc}", file=sys.stderr)
        sys.exit(1)
