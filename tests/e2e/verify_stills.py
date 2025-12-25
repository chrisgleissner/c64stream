#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def _compute_marker_score(img: Image.Image, area_base_px: int) -> dict:
    # Match the ROI heuristic used by tests/e2e/e2e.sh when selecting the still:
    # 1) find approximate content bounds (avoid black bars)
    # 2) use a square ROI in the content's lower-right
    # 3) score = bright_pixels(>threshold) + max(0, mean-median)*10
    g = np.asarray(img.convert("L"))
    height, width = g.shape

    # Use robust content-bound detection matching test_av_sync.py
    # (handles limited-range video, filters, CRT effects with glow/bloom)

    # Horizontal bounds: use 99th percentile per column (robust when scanlines create dark rows)
    col_hi = np.percentile(g, 99.0, axis=0)
    thr_col = max(10.0, float(np.percentile(col_hi, 90.0) * 0.20))
    content_cols = np.where(col_hi > thr_col)[0]
    if content_cols.size >= 2:
        left = int(content_cols[0])
        right = int(content_cols[-1]) + 1
    else:
        # Fallback: assume centered horizontally
        scale_factor = height / 272.0
        scaled_c64_width = int(384 * scale_factor)
        left = int((width - scaled_c64_width) // 2)
        right = int((width + scaled_c64_width) // 2)

    # Vertical bounds: same approach for rows
    row_hi = np.percentile(g, 99.0, axis=1)
    thr_row = max(10.0, float(np.percentile(row_hi, 90.0) * 0.20))
    content_rows = np.where(row_hi > thr_row)[0]
    if content_rows.size >= 2:
        top = int(content_rows[0])
        bottom = int(content_rows[-1]) + 1
    else:
        top, bottom = 0, height

    # Clamp bounds to valid image coordinates
    left = max(0, min(width, left))
    right = max(0, min(width, right))
    top = max(0, min(height, top))
    bottom = max(0, min(height, bottom))

    content_w = max(1, right - left)
    scale = float(content_w) / 384.0
    area_px = max(10, int(round(area_base_px * scale)))

    area_left = max(0, right - area_px)
    area_right = right
    area_bottom = bottom
    area_top = max(0, bottom - area_px)

    roi = g[area_top:area_bottom, area_left:area_right]
    if roi.size == 0:
        return {
            "content": {"left": left, "top": top, "right": right, "bottom": bottom},
            "roi": {"x0": area_left, "y0": area_top, "w": 0, "h": 0},
            "bright": 0,
            "contrast": 0.0,
            "score": 0,
        }

    med = float(np.median(roi))
    thr = max(50.0, med + 60.0)
    bright = int(np.sum(roi > thr))
    contrast = float(float(np.mean(roi)) - med)
    score = int(bright + max(0.0, contrast) * 10.0)

    return {
        "content": {"left": left, "top": top, "right": right, "bottom": bottom},
        "roi": {"x0": int(area_left), "y0": int(area_top), "w": int(area_right - area_left), "h": int(area_bottom - area_top)},
        "bright": bright,
        "contrast": contrast,
        "thr": thr,
        "score": score,
    }


def verify_one(path: Path, area_base_px: int, min_score: int) -> tuple[bool, dict]:
    img = Image.open(path)
    metrics = _compute_marker_score(img, area_base_px)
    ok = int(metrics["score"]) >= int(min_score)
    return ok, metrics


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify extracted still PNGs contain the sync marker.")
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "results",
        help="Path to tests/e2e/results",
    )
    parser.add_argument(
        "--area-base-px",
        type=int,
        default=80,
        help="Base ROI size in pixels at 384px content width (scaled with content width)",
    )
    parser.add_argument(
        "--min-score",
        type=int,
        default=5000,
        help="Minimum marker score (matches e2e.sh still selection threshold)",
    )
    parser.add_argument("--json", action="store_true", help="Emit JSON report")

    args = parser.parse_args()

    results_dir: Path = args.results_dir
    if not results_dir.exists():
        raise SystemExit(f"results dir not found: {results_dir}")

    failures: list[dict] = []
    checked: list[dict] = []

    for scenario_dir in sorted([p for p in results_dir.iterdir() if p.is_dir()]):
        still = scenario_dir / "c64_recording_still.png"
        if not still.exists():
            failures.append(
                {
                    "scenario": scenario_dir.name,
                    "path": str(still),
                    "error": "missing still png",
                }
            )
            continue

        ok, metrics = verify_one(still, args.area_base_px, args.min_score)
        entry = {"scenario": scenario_dir.name, "path": str(still), "ok": ok, "metrics": metrics}
        checked.append(entry)
        if not ok:
            failures.append(entry)

    summary = {
        "results_dir": str(results_dir),
        "checked": len(checked),
        "failures": len(failures),
        "params": {
            "area_base_px": args.area_base_px,
            "min_score": args.min_score,
        },
        "failures_detail": failures,
    }

    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"Checked: {summary['checked']} still(s)")
        if failures:
            print(f"FAIL: {summary['failures']} scenario(s) missing/weak marker")
            for f in failures:
                print(f"  - {f.get('scenario')} => {f.get('path')} ({f.get('error', 'weak marker')})")
        else:
            print("OK: all still PNGs show a strong marker")

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
