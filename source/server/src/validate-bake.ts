import { resolve } from "node:path";
import { validateBakeManifest } from "./bake.js";

const [manifestPath, assetRoot, publishedPlanPath] = process.argv.slice(2);
if (!manifestPath) {
  throw new Error(
    "用法：npm run validate:bake -- <bake-manifest.json> [资产根目录] [published-configurations.json]",
  );
}

const validated = validateBakeManifest(
  resolve(manifestPath),
  assetRoot ? resolve(assetRoot) : undefined,
  publishedPlanPath ? resolve(publishedPlanPath) : undefined,
);
process.stdout.write(
  `bake manifest 校验通过：${validated.renders.size} 个 ready render\n`,
);
