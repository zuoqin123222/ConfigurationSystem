import { resolve } from "node:path";
import { publishBakeAtomically } from "./bake.js";

const [sourceRoot, destinationRoot] = process.argv.slice(2);
if (!sourceRoot || !destinationRoot) {
  throw new Error(
    "用法：npm run publish:bake -- <包含 bake-manifest.json 的源目录> <发布根目录>",
  );
}

const destination = await publishBakeAtomically(
  resolve(sourceRoot),
  resolve(destinationRoot),
);
process.stdout.write(`${destination}\n`);
