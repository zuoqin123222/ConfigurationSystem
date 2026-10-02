import { basename, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { buildApp } from "./app.js";
import { ConfigurationStoreV2 } from "./configuration-store-v2.js";

const port = Number.parseInt(process.env.PORT ?? "8080", 10);
const host = process.env.HOST ?? "0.0.0.0";

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error("PORT 必须是 1 到 65535 的整数");
}

const moduleDirectory = dirname(fileURLToPath(import.meta.url));
const serverRoot =
  basename(dirname(moduleDirectory)) === "dist"
    ? resolve(moduleDirectory, "../..")
    : resolve(moduleDirectory, "..");
const configurationStorePath = resolve(
  process.env.CONFIGURATION_STORE_V2_PATH ??
    resolve(serverRoot, "../../package/server/configurations-v2.json"),
);
const app = buildApp({
  configurationStoreV2: new ConfigurationStoreV2(
    () => new Date(),
    configurationStorePath,
  ),
});

try {
  await app.listen({ port, host });
} catch (error) {
  app.log.error(error);
  process.exitCode = 1;
}
