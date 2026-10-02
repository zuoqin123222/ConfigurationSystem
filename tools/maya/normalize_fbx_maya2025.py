"""Normalize an already-exported FBX for the ConfigurationSystem UE pipeline."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any

import maya.cmds as cmds
import maya.mel as mel
import maya.standalone


ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
NODE_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*$")
SHA256_PATTERN = re.compile(r"^[a-f0-9]{64}$")
REQUIRED_USES = {"normalize", "unreal-import"}


def initialize_maya() -> None:
    try:
        maya.standalone.initialize(name="python")
    except RuntimeError as error:
        if "already initialized" not in str(error).lower():
            raise
    version = str(cmds.about(version=True))
    if not version.startswith("2025"):
        raise RuntimeError(f"必须使用 Maya 2025，当前版本为 {version}")
    if not cmds.pluginInfo("fbxmaya", query=True, loaded=True):
        cmds.loadPlugin("fbxmaya", quiet=True)


def require_object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{path}: 必须是 object")
    return value


def require_exact_keys(value: dict[str, Any], required: set[str], path: str) -> None:
    missing = required - set(value)
    extra = set(value) - required
    if missing:
        raise ValueError(f"{path}: 缺少字段 {', '.join(sorted(missing))}")
    if extra:
        raise ValueError(f"{path}: 不允许字段 {', '.join(sorted(extra))}")


def validate_relative_fbx(path_value: Any, field: str) -> str:
    if not isinstance(path_value, str) or not path_value.lower().endswith(".fbx"):
        raise ValueError(f"{field}: 必须是相对 .fbx 路径")
    path = Path(path_value)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"{field}: 禁止绝对路径和 ..")
    return path_value


def load_manifest(manifest_path: Path) -> dict[str, Any]:
    manifest = require_object(
        json.loads(manifest_path.read_text(encoding="utf-8")), "manifest"
    )
    require_exact_keys(
        manifest,
        {
            "schemaVersion", "kind", "jobId", "assetId", "provenance",
            "input", "output", "normalization",
        },
        "manifest",
    )
    if manifest["schemaVersion"] != "1.0.0":
        raise ValueError("schemaVersion: 仅支持 1.0.0")
    if manifest["kind"] != "maya-fbx-normalization":
        raise ValueError("kind: 必须是 maya-fbx-normalization")
    for field in ("jobId", "assetId"):
        if not isinstance(manifest[field], str) or not ID_PATTERN.fullmatch(manifest[field]):
            raise ValueError(f"{field}: 必须是小写 kebab-case")

    provenance = require_object(manifest["provenance"], "provenance")
    require_exact_keys(
        provenance, {"rightsHolder", "licenseId", "permittedUses"}, "provenance"
    )
    if not provenance["rightsHolder"] or not provenance["licenseId"]:
        raise ValueError("provenance: rightsHolder 和 licenseId 必须非空")
    uses = provenance["permittedUses"]
    if not isinstance(uses, list) or not REQUIRED_USES.issubset(set(uses)):
        raise ValueError("provenance.permittedUses: 必须包含 normalize 和 unreal-import")

    input_artifact = require_object(manifest["input"], "input")
    require_exact_keys(input_artifact, {"path", "sha256", "bytes"}, "input")
    validate_relative_fbx(input_artifact["path"], "input.path")
    if not isinstance(input_artifact["sha256"], str) or not SHA256_PATTERN.fullmatch(
        input_artifact["sha256"]
    ):
        raise ValueError("input.sha256: 必须是 64 位小写十六进制")
    if not isinstance(input_artifact["bytes"], int) or input_artifact["bytes"] < 1:
        raise ValueError("input.bytes: 必须是正整数")

    output = require_object(manifest["output"], "output")
    require_exact_keys(output, {"path", "overwrite"}, "output")
    validate_relative_fbx(output["path"], "output.path")
    if output["overwrite"] is not False:
        raise ValueError("output.overwrite: 必须固定为 false")
    if Path(input_artifact["path"]) == Path(output["path"]):
        raise ValueError("output.path: 不得覆盖输入 FBX")

    normalization = require_object(manifest["normalization"], "normalization")
    require_exact_keys(
        normalization,
        {
            "mayaVersion", "sceneUnit", "upAxis", "fbxFileVersion", "binary",
            "freezeRotationAndScale", "removeCamerasAndLights", "triangulate",
            "rootNode", "nodeRenames",
        },
        "normalization",
    )
    expected = {
        "mayaVersion": "2025",
        "sceneUnit": "centimeter",
        "upAxis": "+Z",
        "fbxFileVersion": "FBX202000",
        "binary": True,
        "freezeRotationAndScale": True,
        "removeCamerasAndLights": True,
    }
    for field, expected_value in expected.items():
        if normalization[field] != expected_value:
            raise ValueError(f"normalization.{field}: 必须是 {expected_value!r}")
    if not isinstance(normalization["triangulate"], bool):
        raise ValueError("normalization.triangulate: 必须是 boolean")
    if not isinstance(normalization["rootNode"], str) or not NODE_PATTERN.fullmatch(
        normalization["rootNode"]
    ):
        raise ValueError("normalization.rootNode: 节点名非法")
    if not isinstance(normalization["nodeRenames"], list):
        raise ValueError("normalization.nodeRenames: 必须是 array")
    for index, rename in enumerate(normalization["nodeRenames"]):
        rename = require_object(rename, f"normalization.nodeRenames[{index}]")
        require_exact_keys(rename, {"from", "to"}, f"normalization.nodeRenames[{index}]")
        if not all(isinstance(rename[key], str) and NODE_PATTERN.fullmatch(rename[key])
                   for key in ("from", "to")):
            raise ValueError(f"normalization.nodeRenames[{index}]: 节点名非法")
    return manifest


def resolve_job_paths(manifest_path: Path, manifest: dict[str, Any]) -> tuple[Path, Path]:
    base = manifest_path.resolve().parent
    input_path = (base / manifest["input"]["path"]).resolve()
    output_path = (base / manifest["output"]["path"]).resolve()
    if base not in input_path.parents or base not in output_path.parents:
        raise ValueError("input/output: 解析后必须位于清单目录内")
    return input_path, output_path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_input(path: Path, artifact: dict[str, Any]) -> None:
    if not path.is_file():
        raise ValueError(f"input.path: 文件不存在 ({path})")
    if path.stat().st_size != artifact["bytes"]:
        raise ValueError(
            f"input.bytes: 声明 {artifact['bytes']}，实际 {path.stat().st_size}"
        )
    actual_hash = sha256_file(path)
    if actual_hash != artifact["sha256"]:
        raise ValueError(f"input.sha256: 声明值与实际文件不匹配 ({actual_hash})")


def imported_nodes(before: set[str]) -> set[str]:
    return set(cmds.ls(long=True) or []) - before


def remove_imported_cameras_and_lights(nodes: set[str]) -> None:
    doomed: set[str] = set()
    for node in nodes:
        if not cmds.objExists(node):
            continue
        if cmds.nodeType(node) == "camera" or cmds.objectType(node, isAType="light"):
            parent = cmds.listRelatives(node, parent=True, fullPath=True) or []
            doomed.update(parent or [node])
    if doomed:
        cmds.delete(sorted(doomed, key=lambda item: item.count("|"), reverse=True))


def find_unique_node(short_name: str) -> str:
    matches = cmds.ls(short_name, long=True) or []
    if len(matches) != 1:
        raise ValueError(f"节点 {short_name} 必须恰好存在一次，实际 {len(matches)}")
    return matches[0]


def apply_renames(renames: list[dict[str, str]]) -> None:
    for rename in renames:
        source = find_unique_node(rename["from"])
        if cmds.ls(rename["to"], long=True):
            raise ValueError(f"重命名目标已存在: {rename['to']}")
        cmds.rename(source, rename["to"])


def ensure_root(root_name: str) -> str:
    matches = cmds.ls(root_name, long=True, type="transform") or []
    if len(matches) > 1:
        raise ValueError(f"根节点 {root_name} 不唯一")
    if matches:
        root = matches[0]
        if cmds.listRelatives(root, parent=True):
            root = cmds.parent(root, world=True)[0]
        return cmds.ls(root, long=True)[0]
    root = cmds.group(empty=True, name=root_name)
    assemblies = [
        node for node in (cmds.ls(assemblies=True, long=True) or [])
        if node != f"|{root_name}"
        and node.rsplit("|", 1)[-1] not in {"persp", "top", "front", "side"}
        and cmds.nodeType(node) == "transform"
    ]
    if assemblies:
        cmds.parent(assemblies, root)
    return cmds.ls(root, long=True)[0]


def normalize_scene(settings: dict[str, Any], imported: set[str]) -> str:
    cmds.currentUnit(linear="cm")
    cmds.upAxis(axis="z", rotateView=True)
    remove_imported_cameras_and_lights(imported)
    apply_renames(settings["nodeRenames"])
    root = ensure_root(settings["rootNode"])
    descendants = cmds.listRelatives(root, allDescendents=True, fullPath=True) or []
    transforms = [root] + [
        node for node in descendants if cmds.objExists(node) and cmds.nodeType(node) == "transform"
    ]
    for transform in transforms:
        cmds.makeIdentity(
            transform, apply=True, translate=False, rotate=True, scale=True, normal=False
        )
    if settings["triangulate"]:
        meshes = cmds.listRelatives(root, allDescendents=True, type="mesh", fullPath=True) or []
        for mesh in meshes:
            parents = cmds.listRelatives(mesh, parent=True, fullPath=True) or []
            if parents:
                cmds.polyTriangulate(parents[0], constructionHistory=False)
    return root


def import_fbx(input_path: Path) -> set[str]:
    before = set(cmds.ls(long=True) or [])
    mel.eval("FBXResetImport;")
    cmds.file(str(input_path), i=True, type="FBX", ignoreVersion=True, mergeNamespacesOnClash=False)
    return imported_nodes(before)


def export_fbx(output_path: Path, root: str, settings: dict[str, Any]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    mel.eval("FBXResetExport;")
    mel.eval(f'FBXExportFileVersion -v "{settings["fbxFileVersion"]}";')
    mel.eval("FBXExportInAscii -v false;")
    mel.eval("FBXExportUpAxis z;")
    mel.eval("FBXExportCameras -v false;")
    mel.eval("FBXExportLights -v false;")
    mel.eval(f'FBXExportTriangulate -v {"true" if settings["triangulate"] else "false"};')
    cmds.select(root, hierarchy=True, replace=True)
    mel.eval(f'FBXExport -f "{output_path.as_posix()}" -s;')


def run_job(manifest_path: Path) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    input_path, output_path = resolve_job_paths(manifest_path, manifest)
    verify_input(input_path, manifest["input"])
    if output_path.exists():
        raise ValueError(f"output.path: 已存在且禁止覆盖 ({output_path})")

    cmds.file(new=True, force=True)
    imported = import_fbx(input_path)
    root = normalize_scene(manifest["normalization"], imported)
    export_fbx(output_path, root, manifest["normalization"])
    return {
        "jobId": manifest["jobId"],
        "input": str(input_path),
        "output": str(output_path),
        "outputBytes": output_path.stat().st_size,
        "outputSha256": sha256_file(output_path),
        "rootNode": manifest["normalization"]["rootNode"],
    }


def run_canary(directory: Path) -> dict[str, Any]:
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "input" / "canary-exported.fbx"
    output = directory / "output" / "canary-normalized.fbx"
    manifest_path = directory / "canary-manifest.json"
    source.parent.mkdir(parents=True, exist_ok=True)

    cmds.file(new=True, force=True)
    cube, _ = cmds.polyCube(name="CanaryMesh")
    cmds.setAttr(f"{cube}.rotateY", 23)
    cmds.setAttr(f"{cube}.scaleX", 2)
    cmds.camera(name="CanaryCamera")
    cmds.pointLight(name="CanaryLight")
    cmds.select(cmds.ls(assemblies=True, long=True), hierarchy=True, replace=True)
    settings = {
        "mayaVersion": "2025",
        "sceneUnit": "centimeter",
        "upAxis": "+Z",
        "fbxFileVersion": "FBX202000",
        "binary": True,
        "freezeRotationAndScale": True,
        "removeCamerasAndLights": True,
        "triangulate": True,
        "rootNode": "Vehicle_Root",
        "nodeRenames": [],
    }
    export_fbx(source, cmds.group(cmds.ls(assemblies=True, long=True), name="CanaryRoot"), settings)
    manifest = {
        "schemaVersion": "1.0.0",
        "kind": "maya-fbx-normalization",
        "jobId": "maya2025-self-built-canary",
        "assetId": "self-built-canary",
        "provenance": {
            "rightsHolder": "ConfigurationSystem",
            "licenseId": "self-generated-canary",
            "permittedUses": ["normalize", "unreal-import"],
        },
        "input": {
            "path": "input/canary-exported.fbx",
            "sha256": sha256_file(source),
            "bytes": source.stat().st_size,
        },
        "output": {"path": "output/canary-normalized.fbx", "overwrite": False},
        "normalization": settings,
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    result = run_job(manifest_path)

    cmds.file(new=True, force=True)
    imported = import_fbx(output)
    if len(cmds.ls("Vehicle_Root", long=True, type="transform") or []) != 1:
        raise RuntimeError("canary: 规范化 FBX 缺少唯一 Vehicle_Root")
    imported_shapes = [
        node for node in imported
        if cmds.objExists(node) and cmds.nodeType(node) in {"camera", "light"}
    ]
    if imported_shapes:
        raise RuntimeError(f"canary: 输出仍包含相机或灯光 {imported_shapes}")
    result["canary"] = "passed"
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="使用 Maya 2025 规范化用户已导出的 FBX；不会发现或复制其他资产。"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--manifest", type=Path, help="规范化作业 JSON 清单")
    group.add_argument("--canary-dir", type=Path, help="运行自建几何 canary 的输出目录")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    initialize_maya()
    result = run_job(args.manifest.resolve()) if args.manifest else run_canary(args.canary_dir.resolve())
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FBX 规范化失败: {error}", file=sys.stderr)
        sys.exit(1)
