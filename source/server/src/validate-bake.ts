import { resolve } from "node:path";
import { validateBakeManifest } from "./bake.js";

const [manifestPath, assetRoot] = process.argv.slice(2);
if (!manifestPath) {
  throw new Error(
    "用法：npm run validate:bake -- <bake-manifest.json> [资产根目录]",
  );
}

const validated = validateBakeManifest(
  resolve(manifestPath),
  assetRoot ? resolve(assetRoot) : undefined,
);
process.stdout.write(
  `bake manifest 校验通过：${validated.renders.size} 个 ready render\n`,
);
