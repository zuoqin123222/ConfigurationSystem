"""Rebuild the 38 SC01 material slots from an explicit surface-binding contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SC01_SURFACE_IDS = (
    "exterior-body-cover", "engine-bay-cover", "wheel-material", "wheel-style",
    "wheel-color", "front-caliper-color", "rear-caliper-color",
    "steering-wheel-skin", "steering-wheel-addon", "steering-center-mark",
    "ip-wings", "ip-middle", "ip-instrument-cover", "ip-upper-trim",
    "ip-lower-trim", "ip-center-mark", "a-pillar-surface", "seat-backrest",
    "seat-bolster", "seat-shell-back", "seat-headrest-mark", "door-upper",
    "door-middle", "door-armrest", "door-armrest-skin", "storage-soft-bag",
    "console-armrest-cover",
    "console-armrest-side", "handbrake", "roof-surface",
    "lower-skirt", "interior-painted-parts", "door-sill",
    "embroidered-logo", "center-panel-trim", "shift-knob",
    "brake-handle", "pedal",
)
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
SLOT_PATTERN = re.compile(r"^sc01_[a-z0-9]+(?:_[a-z0-9]+)*$")
FACE_PATTERN = re.compile(r"^(?:\*|[0-9]+(?::[0-9]+)?)$")
SHA256_PATTERN = re.compile(r"^[a-f0-9]{64}$")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_contract(path: Path) -> dict[str, Any]:
    contract = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(contract, dict), "binding: 根必须是 object")
    require(contract.get("schemaVersion") == "1.0.0", "schemaVersion: 仅支持 1.0.0")
    require(
        contract.get("kind") == "vehicle-surface-binding",
        "kind: 必须是 vehicle-surface-binding",
    )
    require(contract.get("vehicleId") == "sc01", "vehicleId: 必须是 sc01")
    require(
        isinstance(contract.get("catalogVersion"), str)
        and ID_PATTERN.fullmatch(contract["catalogVersion"]),
        "catalogVersion: 必须是稳定 ID",
    )
    source = contract.get("sourceFbx")
    require(isinstance(source, dict), "sourceFbx: 必须是 object")
    require(
        isinstance(source.get("path"), str) and source["path"].lower().endswith(".fbx"),
        "sourceFbx.path: 必须是相对 .fbx 路径",
    )
    source_path = Path(source["path"])
    require(not source_path.is_absolute() and ".." not in source_path.parts,
            "sourceFbx.path: 禁止绝对路径和 ..")
    require(
        isinstance(source.get("sha256"), str)
        and SHA256_PATTERN.fullmatch(source["sha256"]),
        "sourceFbx.sha256: 必须是 64 位小写十六进制",
    )
    require(isinstance(source.get("bytes"), int) and source["bytes"] > 0,
            "sourceFbx.bytes: 必须是正整数")

    bindings = contract.get("bindings")
    require(isinstance(bindings, list), "bindings: 必须是 array")
    actual_ids = tuple(binding.get("surfaceId") for binding in bindings
                       if isinstance(binding, dict))
    require(actual_ids == SC01_SURFACE_IDS,
            "bindings: 必须按固定顺序显式覆盖 SC01 38 surface")
    slots: set[str] = set()
    selectors: set[tuple[str, str, str]] = set()
    for index, binding in enumerate(bindings):
        slot_ids = binding.get("materialSlotIds")
        require(isinstance(slot_ids, list) and slot_ids,
                f"bindings[{index}].materialSlotIds: 必须是非空 array")
        require(len(slot_ids) == 1,
                f"bindings[{index}].materialSlotIds: Maya 面选择生产当前要求每个 surface 一个目标槽")
        for slot in slot_ids:
            require(isinstance(slot, str) and SLOT_PATTERN.fullmatch(slot),
                    f"bindings[{index}].materialSlotIds: 非法 SC01 slot ID")
            require(slot not in slots, f"bindings[{index}].materialSlotIds: 重复 {slot}")
            slots.add(slot)
        rules = binding.get("selectors")
        require(isinstance(rules, list) and rules,
                f"bindings[{index}].selectors: 必须是非空 array")
        for rule_index, selector in enumerate(rules):
            context = f"bindings[{index}].selectors[{rule_index}]"
            require(isinstance(selector, dict), f"{context}: 必须是 object")
            node = selector.get("targetNode")
            material = selector.get("sourceMaterialId")
            ranges = selector.get("faceRanges")
            require(isinstance(node, str) and node, f"{context}.targetNode: 必须非空")
            require(isinstance(material, str) and material,
                    f"{context}.sourceMaterialId: 必须非空")
            require(isinstance(ranges, list) and ranges, f"{context}.faceRanges: 必须非空")
            for face_range in ranges:
                require(isinstance(face_range, str) and FACE_PATTERN.fullmatch(face_range),
                        f"{context}.faceRanges: 非法范围 {face_range!r}")
                key = (node, material, face_range)
                require(key not in selectors, f"{context}: 选择规则重复")
                selectors.add(key)
    return contract


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_source(binding_path: Path, contract: dict[str, Any]) -> Path:
    base = binding_path.resolve().parent
    source = (base / contract["sourceFbx"]["path"]).resolve()
    require(base in source.parents, "sourceFbx.path: 解析后必须位于契约目录内")
    require(source.is_file(), f"sourceFbx.path: 文件不存在 ({source})")
    require(source.stat().st_size == contract["sourceFbx"]["bytes"],
            "sourceFbx.bytes: 与实际 FBX 不一致")
    require(sha256_file(source) == contract["sourceFbx"]["sha256"],
            "sourceFbx.sha256: 与实际 FBX 不一致")
    return source


def initialize_maya():
    import maya.cmds as cmds
    import maya.mel as mel
    import maya.standalone

    try:
        maya.standalone.initialize(name="python")
    except RuntimeError as error:
        if "already initialized" not in str(error).lower():
            raise
    version = str(cmds.about(version=True))
    require(version.startswith("2025"), f"必须使用 Maya 2025，当前版本为 {version}")
    if not cmds.pluginInfo("fbxmaya", query=True, loaded=True):
        cmds.loadPlugin("fbxmaya", quiet=True)
    return cmds, mel


def unique_node(cmds, short_name: str, node_type: str | None = None) -> str:
    matches = (
        cmds.ls(short_name, long=True, type=node_type)
        if node_type is not None
        else cmds.ls(short_name, long=True)
    ) or []
    require(len(matches) == 1, f"节点 {short_name} 必须恰好存在一次，实际 {len(matches)}")
    return matches[0]


def source_shading_engines(cmds, material_id: str) -> set[str]:
    material = unique_node(cmds, material_id)
    engines = set(cmds.listConnections(material, type="shadingEngine") or [])
    require(bool(engines), f"来源材质 {material_id} 未连接 shadingEngine")
    return engines


def selected_faces(cmds, selector: dict[str, Any]) -> list[str]:
    node = unique_node(cmds, selector["targetNode"], "transform")
    shapes = cmds.listRelatives(node, shapes=True, noIntermediate=True, fullPath=True) or []
    require(len(shapes) == 1 and cmds.nodeType(shapes[0]) == "mesh",
            f"{selector['targetNode']}: 必须恰好包含一个 mesh shape")
    count = cmds.polyEvaluate(node, face=True)
    source_engines = source_shading_engines(cmds, selector["sourceMaterialId"])
    indices: list[int] = []
    for face_range in selector["faceRanges"]:
        if face_range == "*":
            candidates = range(count)
        else:
            bounds = [int(value) for value in face_range.split(":")]
            start, end = bounds[0], bounds[-1]
            require(end >= start and end < count,
                    f"{selector['targetNode']}.{face_range}: 面范围越界")
            candidates = range(start, end + 1)
        for index in candidates:
            face = f"{node}.f[{index}]"
            assigned = set(cmds.listSets(object=face, type=1) or [])
            if assigned & source_engines:
                indices.append(index)
            elif face_range != "*":
                raise ValueError(
                    f"{face}: 当前材质不是 {selector['sourceMaterialId']}，拒绝猜测")
    require(bool(indices), f"{selector['targetNode']}: 来源材质未选中任何面")
    return [f"{node}.f[{index}]" for index in sorted(set(indices))]


def rebuild_slots(cmds, contract: dict[str, Any]) -> dict[str, int]:
    claimed: set[str] = set()
    counts: dict[str, int] = {}
    for binding in contract["bindings"]:
        slot = binding["materialSlotIds"][0]
        require(not cmds.objExists(slot) and not cmds.objExists(f"{slot}_SG"),
                f"目标材质或 shadingEngine 已存在，拒绝非幂等覆盖: {slot}")
        faces: list[str] = []
        for selector in binding["selectors"]:
            faces.extend(selected_faces(cmds, selector))
        overlap = claimed.intersection(faces)
        require(not overlap, f"{binding['surfaceId']}: 面被多个 surface 重复分配")
        claimed.update(faces)
        material = cmds.shadingNode("lambert", asShader=True, name=slot)
        engine = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=f"{slot}_SG")
        cmds.connectAttr(f"{material}.outColor", f"{engine}.surfaceShader", force=True)
        cmds.sets(faces, edit=True, forceElement=engine)
        counts[binding["surfaceId"]] = len(faces)
    return counts


def run(binding_path: Path, output: Path) -> dict[str, Any]:
    contract = load_contract(binding_path)
    source = resolve_source(binding_path, contract)
    require(not output.exists(), f"output: 已存在且禁止覆盖 ({output})")
    cmds, mel = initialize_maya()
    cmds.file(new=True, force=True)
    mel.eval("FBXResetImport;")
    cmds.file(str(source), i=True, type="FBX", ignoreVersion=True, mergeNamespacesOnClash=False)
    counts = rebuild_slots(cmds, contract)
    output.parent.mkdir(parents=True, exist_ok=True)
    mel.eval("FBXResetExport;")
    mel.eval('FBXExportFileVersion -v "FBX202000";')
    mel.eval("FBXExportInAscii -v false;")
    mel.eval("FBXExportUpAxis z;")
    roots = cmds.ls(assemblies=True, long=True) or []
    require(bool(roots), "场景没有可导出的根节点")
    cmds.select(roots, hierarchy=True, replace=True)
    mel.eval(f'FBXExport -f "{output.resolve().as_posix()}" -s;')
    return {
        "vehicleId": contract["vehicleId"],
        "catalogVersion": contract["catalogVersion"],
        "surfaceCount": len(counts),
        "faceCounts": counts,
        "output": str(output.resolve()),
        "outputBytes": output.stat().st_size,
        "outputSha256": sha256_file(output),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="按显式契约在 Maya 2025 中重建 SC01 38 surface 材质槽。")
    parser.add_argument("--binding", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(run(args.binding.resolve(), args.output.resolve()),
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"SC01 surface slot 重建失败: {error}", file=sys.stderr)
        sys.exit(1)
