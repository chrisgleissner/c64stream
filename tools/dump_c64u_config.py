#!/usr/bin/env python3
"""
C64 Stream - C64U configuration snapshot exporter
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time
from pathlib import Path
from typing import Any, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

import yaml


def _fetch_json(
    url: str,
    headers: dict[str, str],
    timeout: float,
    retries: int,
    retry_delay: float,
) -> dict[str, Any]:
    last_error: Optional[Exception] = None
    for attempt in range(retries + 1):
        request = Request(url, headers=headers)
        try:
            with urlopen(request, timeout=timeout) as response:
                payload = response.read()
            try:
                return json.loads(payload.decode("utf-8"))
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Invalid JSON from {url}: {exc}") from exc
        except HTTPError as exc:
            raise RuntimeError(f"HTTP {exc.code} for {url}: {exc.reason}") from exc
        except URLError as exc:
            last_error = exc
            if attempt < retries:
                time.sleep(retry_delay)
                continue
            raise RuntimeError(f"Failed to reach {url}: {exc.reason}") from exc
        except OSError as exc:
            last_error = exc
            if attempt < retries:
                time.sleep(retry_delay)
                continue
            raise RuntimeError(f"Failed to reach {url}: {exc}") from exc

    if last_error is not None:
        raise RuntimeError(f"Failed to reach {url}: {last_error}")
    raise RuntimeError(f"Failed to reach {url}")


def _obfuscate_if_password(item_name: str, value: Any) -> Any:
    if item_name.strip().lower() != "network password":
        return value
    if isinstance(value, str):
        return "********" if value else value
    return value


def _obfuscate_collection(item_name: str, value: Any) -> Any:
    if item_name.strip().lower() != "network password":
        return value
    if isinstance(value, list):
        return ["********" if isinstance(item, str) and item else item for item in value]
    if isinstance(value, dict):
        return {
            key: ("********" if isinstance(item, str) and item else item)
            for key, item in value.items()
        }
    return value


def _extract_menu_entry(item_name: str, current_value: Any, detail_value: Any) -> dict[str, Any]:
    selected = None
    options = None
    details: dict[str, Any] = {}

    if isinstance(detail_value, dict):
        selected = detail_value.get("current")
        if selected is None:
            selected = detail_value.get("value", detail_value.get("selected"))
        if selected is None:
            selected = detail_value.get("default")

        options = (
            detail_value.get("options")
            or detail_value.get("choices")
            or detail_value.get("enum")
            or detail_value.get("values")
            or detail_value.get("list")
        )

        for key, value in detail_value.items():
            if key in {"current", "value", "selected", "default", "options", "choices", "enum", "values", "list"}:
                continue
            details[key] = _obfuscate_if_password(item_name, value)
    else:
        selected = detail_value

    if selected is None:
        selected = current_value

    selected = _obfuscate_if_password(item_name, selected)
    entry: dict[str, Any] = {"selected": selected}
    if options is not None:
        entry["options"] = _obfuscate_collection(item_name, options)
    if details:
        entry["details"] = details
    return entry


def _scrape_category(
    base_url: str,
    headers: dict[str, str],
    timeout: float,
    retries: int,
    retry_delay: float,
    category: str,
) -> dict[str, Any]:
    category_url = f"{base_url}/v1/configs/{quote(category)}"
    category_payload = _fetch_json(category_url, headers, timeout, retries, retry_delay)
    errors = category_payload.get("errors") or []

    categories: dict[str, Any] = {}
    for cat_name, items in category_payload.items():
        if cat_name == "errors":
            continue
        if not isinstance(items, dict):
            continue
        categories[cat_name] = items

    if not categories:
        return {"errors": errors}

    menu_items: dict[str, Any] = {}
    for cat_name, items in categories.items():
        for item_name, current_value in items.items():
            item_url = f"{base_url}/v1/configs/{quote(cat_name)}/{quote(item_name)}"
            try:
                item_payload = _fetch_json(item_url, headers, timeout, retries, retry_delay)
            except RuntimeError as exc:
                menu_items[item_name] = {
                    "selected": current_value,
                    "details": {"error": str(exc)},
                }
                continue

            item_errors = item_payload.get("errors") or []
            detail_value = None
            if cat_name in item_payload:
                detail_value = item_payload.get(cat_name, {}).get(item_name)

            entry = _extract_menu_entry(item_name, current_value, detail_value)
            if item_errors:
                entry["details"] = entry.get("details", {})
                entry["details"]["errors"] = item_errors
            menu_items[item_name] = entry

    return {"items": menu_items, "errors": errors} if errors else {"items": menu_items}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Dump C64U configuration menu tree to YAML."
    )
    parser.add_argument(
        "--base-url",
        default="http://c64u",
        help="Base URL for the C64U REST API (default: http://c64u)",
    )
    parser.add_argument(
        "--password",
        default=None,
        help="Network password for X-Password header (optional)",
    )
    parser.add_argument(
        "--output",
        default="doc/c64/c64u-config.yaml",
        help="Output YAML path (default: doc/c64/c64u-config.yaml)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="HTTP timeout in seconds (default: 5)",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=2,
        help="Retry count for failed requests (default: 2)",
    )
    parser.add_argument(
        "--retry-delay",
        type=float,
        default=1.0,
        help="Seconds to wait between retries (default: 1)",
    )
    args = parser.parse_args()

    headers = {"Accept": "application/json"}
    if args.password:
        headers["X-Password"] = args.password

    base_url = args.base_url.rstrip("/")
    output_path = Path(args.output)

    config_list = _fetch_json(
        f"{base_url}/v1/configs",
        headers,
        args.timeout,
        args.retries,
        args.retry_delay,
    )
    categories = config_list.get("categories") or []
    if not categories:
        print("No configuration categories returned.", file=sys.stderr)
        return 1

    version_payload = _fetch_json(
        f"{base_url}/v1/version",
        headers,
        args.timeout,
        args.retries,
        args.retry_delay,
    )
    info_payload = _fetch_json(
        f"{base_url}/v1/info",
        headers,
        args.timeout,
        args.retries,
        args.retry_delay,
    )

    snapshot: dict[str, Any] = {
        "config": {
            "general": {
                "base_url": base_url,
                "rest_api_version": version_payload.get("version"),
                "device_type": info_payload.get("product"),
                "firmware_version": info_payload.get("firmware_version"),
                "fetched_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            },
            "categories": {},
        }
    }

    for category in categories:
        snapshot["config"]["categories"][category] = _scrape_category(
            base_url,
            headers,
            args.timeout,
            args.retries,
            args.retry_delay,
            category,
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as handle:
        yaml.safe_dump(snapshot, handle, sort_keys=False, default_flow_style=False)

    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
