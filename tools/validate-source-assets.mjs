import { lstat, readdir } from "node:fs/promises";
import { relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const FORBIDDEN_SOURCE_ASSET_EXTENSIONS = Object.freeze([
  ".uasset",
  ".umap",
  ".tps"
]);

const forbiddenExtensions = new Set(FORBIDDEN_SOURCE_ASSET_EXTENSIONS);

function normalizedRelativePath(root, filePath) {
  return relative(root, filePath).split("\\").join("/");
}

export function isForbiddenSourceAsset(filePath) {
  const lowerPath = filePath.toLowerCase();
  return [...forbiddenExtensions].some((extension) => lowerPath.endsWith(extension));
}

export async function findForbiddenSourceAssets(sourceAssetsRoot) {
  const root = resolve(sourceAssetsRoot);
  const rootInfo = await lstat(root);
  if (!rootInfo.isDirectory()) {
    throw new Error(`SourceAssets 路径不是目录: ${root}`);
  }

  const violations = [];
  async function walk(directory) {
    const entries = await readdir(directory, { withFileTypes: true });
    entries.sort((left, right) => left.name.localeCompare(right.name, "en"));
    for (const entry of entries) {
      const path = resolve(directory, entry.name);
      if (entry.isSymbolicLink()) {
        continue;
      }
      if (entry.isDirectory()) {
        await walk(path);
      } else if (entry.isFile() && isForbiddenSourceAsset(entry.name)) {
        violations.push(normalizedRelativePath(root, path));
      }
    }
  }

  await walk(root);
  return violations;
}

export async function validateSourceAssets(sourceAssetsRoot) {
  try {
    const violations = await findForbiddenSourceAssets(sourceAssetsRoot);
    return {
      valid: violations.length === 0,
      root: resolve(sourceAssetsRoot),
      violations,
      errors: violations.map(
        (path) => `${path}: SourceAssets 禁止包含 Unreal/TexturePacker 二进制资产`
      )
    };
  } catch (error) {
    return {
      valid: false,
      root: resolve(sourceAssetsRoot),
      violations: [],
      errors: [error.message]
    };
  }
}

async function main(args) {
  if (args.length > 1) {
    console.error("用法: node tools/validate-source-assets.mjs [SourceAssets目录]");
    process.exitCode = 2;
    return;
  }

  const repositoryRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
  const sourceAssetsRoot = args[0]
    ? resolve(args[0])
    : resolve(repositoryRoot, "source", "clients", "ue", "SourceAssets");
  const result = await validateSourceAssets(sourceAssetsRoot);
  if (!result.valid) {
    console.error(`SourceAssets 门禁失败（${result.errors.length} 项）：`);
    for (const error of result.errors) console.error(`- ${error}`);
    process.exitCode = 1;
    return;
  }
  console.log(`SourceAssets 门禁通过：未发现 ${FORBIDDEN_SOURCE_ASSET_EXTENSIONS.join("/")}。`);
}

const isMain = process.argv[1]
  && resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  main(process.argv.slice(2)).catch((error) => {
    console.error(error.message);
    process.exitCode = 2;
  });
}
