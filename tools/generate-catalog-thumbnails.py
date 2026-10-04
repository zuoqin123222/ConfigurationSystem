#!/usr/bin/env python3
"""Rebuild automotive catalog thumbnails from rendered pages and a crop specification."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image, ImageOps, features
except ImportError as error:
    raise SystemExit(
        "Pillow is required. Install it with: python -m pip install Pillow"
    ) from error


DEFAULT_SIZE = 512
DEFAULT_QUALITY = 90
DEFAULT_MAX_TOTAL_BYTES = 64 * 1024 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Crop rendered automotive color-card pages into deterministic 512x512 WebP "
            "thumbnails and refresh their manifest and catalog URLs."
        )
    )
    parser.add_argument(
        "--pages-dir",
        type=Path,
        required=True,
        help="Directory containing the rendered source-page images.",
    )
    parser.add_argument(
        "--crop-spec",
        type=Path,
        required=True,
        help="JSON crop specification (a previously generated manifest is accepted).",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Directory that will contain the generated .webp files.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        required=True,
        help="Destination path for the generated crop manifest.",
    )
    parser.add_argument(
        "--catalog",
        type=Path,
        required=True,
        help="Catalog JSON whose materialVariants thumbnailUrl values are updated.",
    )
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE)
    parser.add_argument("--quality", type=int, default=DEFAULT_QUALITY)
    parser.add_argument(
        "--max-total-bytes",
        type=int,
        default=DEFAULT_MAX_TOTAL_BYTES,
        help="Fail if generated thumbnails exceed this byte budget (default: 64 MiB).",
    )
    return parser.parse_args()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_int(value: Any, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{label} must be an integer")
    return value


def source_file(item: dict[str, Any]) -> str:
    source_page = item.get("sourcePage")
    value = source_page.get("file") if isinstance(source_page, dict) else item.get("source")
    if not isinstance(value, str) or not value:
        raise ValueError(f"{item.get('variantId', '<unknown>')} has no source page")
    return value


def pixel_crop(
    item: dict[str, Any], source_width: int, source_height: int
) -> tuple[int, int, int, int]:
    crop = item.get("crop")
    if isinstance(crop, dict):
        x = require_int(crop.get("x"), "crop.x")
        y = require_int(crop.get("y"), "crop.y")
        width = require_int(crop.get("width"), "crop.width")
        height = require_int(crop.get("height"), "crop.height")
    else:
        normalized = item.get("normalizedCrop")
        if not isinstance(normalized, list) or len(normalized) != 4:
            raise ValueError(f"{item.get('variantId', '<unknown>')} has no valid crop")
        x, y, width, height = (
            round(float(normalized[0]) * source_width),
            round(float(normalized[1]) * source_height),
            round(float(normalized[2]) * source_width),
            round(float(normalized[3]) * source_height),
        )

    if x < 0 or y < 0 or width <= 0 or height <= 0:
        raise ValueError(f"Invalid crop for {item.get('variantId', '<unknown>')}")
    if x + width > source_width or y + height > source_height:
        raise ValueError(f"Crop exceeds source page for {item.get('variantId', '<unknown>')}")

    side = min(width, height)
    return x + (width - side) // 2, y + (height - side) // 2, side, side


def save_webp(source: Image.Image, box: tuple[int, int, int, int], output: Path,
              size: int, quality: int,
              output_cleanup_crop: dict[str, Any] | None = None) -> bytes:
    x, y, width, height = box
    thumbnail = source.crop((x, y, x + width, y + height))
    thumbnail = thumbnail.resize((size, size), Image.Resampling.LANCZOS)
    if output_cleanup_crop:
        cleanup_x = require_int(output_cleanup_crop.get("x"), "outputCleanupCrop.x")
        cleanup_y = require_int(output_cleanup_crop.get("y"), "outputCleanupCrop.y")
        cleanup_width = require_int(
            output_cleanup_crop.get("width"), "outputCleanupCrop.width"
        )
        cleanup_height = require_int(
            output_cleanup_crop.get("height"), "outputCleanupCrop.height"
        )
        if (
            cleanup_x < 0
            or cleanup_y < 0
            or cleanup_width <= 0
            or cleanup_height <= 0
            or cleanup_x + cleanup_width > size
            or cleanup_y + cleanup_height > size
        ):
            raise ValueError(f"Invalid outputCleanupCrop for {output.name}")
        thumbnail = thumbnail.crop((
            cleanup_x,
            cleanup_y,
            cleanup_x + cleanup_width,
            cleanup_y + cleanup_height,
        )).resize((size, size), Image.Resampling.LANCZOS)
    if thumbnail.mode not in ("RGB", "RGBA"):
        thumbnail = thumbnail.convert("RGBA" if "A" in thumbnail.getbands() else "RGB")

    save_options: dict[str, Any] = {
        "format": "WEBP",
        "quality": quality,
        "method": 6,
        "exact": True,
    }
    icc_profile = source.info.get("icc_profile")
    if icc_profile:
        save_options["icc_profile"] = icc_profile
    output.parent.mkdir(parents=True, exist_ok=True)
    thumbnail.save(output, **save_options)
    return output.read_bytes()


def update_catalog(catalog_path: Path, output_urls: dict[str, str]) -> None:
    catalog = read_json(catalog_path)
    variants = catalog.get("materialVariants")
    if not isinstance(variants, list):
        raise ValueError("catalog.materialVariants must be an array")
    catalog_ids = {item.get("variantId") for item in variants if isinstance(item, dict)}
    if catalog_ids != set(output_urls):
        missing = sorted(catalog_ids - set(output_urls))
        extra = sorted(set(output_urls) - catalog_ids)
        raise ValueError(f"Crop/catalog variant mismatch; missing={missing[:3]}, extra={extra[:3]}")
    for variant in variants:
        variant["thumbnailUrl"] = output_urls[variant["variantId"]]
    catalog_path.write_text(
        json.dumps(catalog, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def generate(args: argparse.Namespace) -> tuple[int, int]:
    if not features.check("webp"):
        raise ValueError("This Pillow build has no WebP encoder")
    if args.size <= 0 or not 0 <= args.quality <= 100:
        raise ValueError("--size must be positive and --quality must be between 0 and 100")

    spec = read_json(args.crop_spec)
    items = spec.get("items")
    if not isinstance(items, list) or not items:
        raise ValueError("crop spec items must be a non-empty array")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    generated_items: list[dict[str, Any]] = []
    output_urls: dict[str, str] = {}
    seen_ids: set[str] = set()
    source_cache: dict[Path, tuple[Image.Image, bytes]] = {}

    try:
        for item in items:
            if not isinstance(item, dict):
                raise ValueError("Every crop item must be an object")
            variant_id = item.get("variantId")
            if not isinstance(variant_id, str) or not variant_id or variant_id in seen_ids:
                raise ValueError(f"Invalid or duplicate variantId: {variant_id!r}")
            seen_ids.add(variant_id)

            page_name = source_file(item)
            page_path = args.pages_dir / page_name
            if page_path not in source_cache:
                page_bytes = page_path.read_bytes()
                image = ImageOps.exif_transpose(Image.open(page_path))
                image.load()
                source_cache[page_path] = (image, page_bytes)
            source, page_bytes = source_cache[page_path]
            source_page = item.get("sourcePage")
            expected_source_hash = (
                source_page.get("sha256")
                if isinstance(source_page, dict)
                else None
            )
            actual_source_hash = sha256(page_bytes)
            if (
                isinstance(expected_source_hash, str)
                and expected_source_hash
                and expected_source_hash != actual_source_hash
            ):
                raise ValueError(
                    f"Source page hash changed for {variant_id}; "
                    "review and update the crop specification explicitly"
                )
            crop = pixel_crop(item, source.width, source.height)

            output_name = f"{variant_id}.webp"
            output_path = args.output_dir / output_name
            output_bytes = save_webp(
                source,
                crop,
                output_path,
                args.size,
                args.quality,
                item.get("outputCleanupCrop"),
            )
            output_url = f"/sc01/thumbnails/{output_name}"
            output_urls[variant_id] = output_url
            generated_item = {
                "variantId": variant_id,
                "sourcePage": {
                    "file": page_name,
                    "width": source.width,
                    "height": source.height,
                    "sha256": actual_source_hash,
                },
                "crop": dict(zip(("x", "y", "width", "height"), crop)),
                "output": output_url,
                "outputSize": {"width": args.size, "height": args.size},
                "byteLength": len(output_bytes),
                "sha256": sha256(output_bytes),
                "replaceable": True,
                "reviewRequired": bool(item.get("reviewRequired", False)),
            }
            if isinstance(item.get("outputCleanupCrop"), dict):
                generated_item["outputCleanupCrop"] = item["outputCleanupCrop"]
            generated_items.append(generated_item)
    finally:
        for image, _ in source_cache.values():
            image.close()

    total_bytes = sum(item["byteLength"] for item in generated_items)
    if total_bytes > args.max_total_bytes:
        raise ValueError(
            f"Generated thumbnails use {total_bytes} bytes, exceeding "
            f"--max-total-bytes={args.max_total_bytes}"
        )

    manifest = {
        "schemaVersion": "2.0.0",
        "vehicleId": spec.get("vehicleId", "sc01"),
        "generatedFrom": spec.get("generatedFrom", "SC01-选配色卡.pdf"),
        "generator": "tools/generate-catalog-thumbnails.py",
        "target": {
            "width": args.size,
            "height": args.size,
            "format": "webp",
            "quality": args.quality,
            "method": 6,
            "replaceable": True,
        },
        "itemCount": len(generated_items),
        "totalBytes": total_bytes,
        "items": generated_items,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    update_catalog(args.catalog, output_urls)
    return len(generated_items), total_bytes


def main() -> int:
    args = parse_args()
    try:
        count, total_bytes = generate(args)
    except (OSError, ValueError) as error:
        print(f"Catalog thumbnail generation failed: {error}", file=sys.stderr)
        return 1
    print(
        f"Generated {count} WebP thumbnails ({total_bytes / 1024 / 1024:.2f} MiB) "
        f"and updated {args.manifest}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
