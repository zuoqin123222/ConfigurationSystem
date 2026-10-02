import { createReadStream } from "node:fs";
import { realpath, stat } from "node:fs/promises";
import { basename, dirname, isAbsolute, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import Fastify, { type FastifyInstance } from "fastify";
import {
  loadContracts,
  RequestError,
  resolveConfiguration,
  type ContractData,
} from "./data.js";

export interface BuildAppOptions {
  contractRoot?: string;
  renderRoot?: string;
  data?: ContractData;
}

const STABLE_ID_PATTERN = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function defaultRenderRoot(): string {
  if (process.env.RENDER_ROOT) {
    return resolve(process.env.RENDER_ROOT);
  }

  const moduleDirectory = dirname(fileURLToPath(import.meta.url));
  const serverRoot =
    basename(dirname(moduleDirectory)) === "dist"
      ? resolve(moduleDirectory, "../..")
      : resolve(moduleDirectory, "..");
  return resolve(serverRoot, "../../package/renders");
}

async function safeRenderPath(
  renderRoot: string,
  publicationVersion: string,
  vehicleId: string,
  configurationKey: string,
  viewId: string,
): Promise<string | undefined> {
  try {
    const root = await realpath(renderRoot);
    const candidate = await realpath(
      resolve(
        root,
        publicationVersion,
        vehicleId,
        configurationKey,
        `${viewId}.png`,
      ),
    );
    const pathFromRoot = relative(root, candidate);

    // realpath 后再次检查边界，可阻止 ..、绝对路径和目录内符号链接逃逸。
    if (
      pathFromRoot === "" ||
      pathFromRoot.startsWith(`..${process.platform === "win32" ? "\\" : "/"}`) ||
      pathFromRoot === ".." ||
      isAbsolute(pathFromRoot)
    ) {
      return undefined;
    }
    return (await stat(candidate)).isFile() ? candidate : undefined;
  } catch {
    return undefined;
  }
}

export function buildApp(options: BuildAppOptions = {}): FastifyInstance {
  const app = Fastify({
    bodyLimit: 16 * 1024,
    logger: false,
  });
  const data = options.data ?? loadContracts(options.contractRoot);
  const renderRoot = resolve(options.renderRoot ?? defaultRenderRoot());

  app.setErrorHandler((error, _request, reply) => {
    if (error instanceof RequestError) {
      return reply.status(error.statusCode).send({
        code: error.code,
        message: error.message,
      });
    }
    if (
      isRecord(error) &&
      typeof error.statusCode === "number" &&
      error.statusCode === 400
    ) {
      return reply.status(400).send({
        code: "INVALID_REQUEST",
        message: "请求 JSON 非法",
      });
    }
    app.log.error(error);
    return reply.status(500).send({
      code: "INTERNAL_ERROR",
      message: "服务内部错误",
    });
  });

  app.get("/health", async () => ({
    status: "ok",
    catalogVersion: data.catalog.catalogVersion,
    publicationVersion: data.publication.publicationVersion,
  }));

  app.get("/api/v1/catalog", async () => data.catalog);

  app.post("/api/v1/renders/resolve", async (request) => {
    const body = request.body;
    const requiredFields = [
      "catalogVersion",
      "publicationVersion",
      "vehicleId",
      "selections",
      "renderViewId",
    ] as const;
    if (
      !isRecord(body) ||
      Object.keys(body).length !== requiredFields.length ||
      !requiredFields.every((field) => Object.hasOwn(body, field))
    ) {
      throw new RequestError(
        400,
        "INVALID_REQUEST",
        "请求体字段不符合 ResolveRenderRequest",
      );
    }

    const {
      catalogVersion,
      publicationVersion,
      vehicleId,
      selections,
      renderViewId,
    } = body;
    if (
      typeof catalogVersion !== "string" ||
      typeof publicationVersion !== "string" ||
      typeof vehicleId !== "string" ||
      typeof renderViewId !== "string"
    ) {
      throw new RequestError(400, "INVALID_REQUEST", "请求字段类型非法");
    }
    if (
      !STABLE_ID_PATTERN.test(catalogVersion) ||
      !STABLE_ID_PATTERN.test(publicationVersion) ||
      !STABLE_ID_PATTERN.test(vehicleId)
    ) {
      throw new RequestError(400, "INVALID_REQUEST", "请求 ID 格式非法");
    }
    if (
      catalogVersion !== data.catalog.catalogVersion ||
      publicationVersion !== data.publication.publicationVersion
    ) {
      throw new RequestError(
        409,
        "VERSION_CONFLICT",
        "客户端版本与当前发布版本冲突",
      );
    }
    if (vehicleId !== data.publication.vehicleId) {
      throw new RequestError(404, "VEHICLE_NOT_FOUND", "车型不存在");
    }
    if (!data.publication.renderViewIds.includes(renderViewId)) {
      throw new RequestError(400, "INVALID_RENDER_VIEW", "渲染视角非法");
    }

    const configuration = resolveConfiguration(selections, data);
    return {
      configurationKey: configuration.configurationKey,
      renderViewId,
      imageUrl: `/assets/renders/${publicationVersion}/${vehicleId}/${configuration.configurationKey}/${renderViewId}.png`,
    };
  });

  app.get<{
    Params: {
      publicationVersion: string;
      vehicleId: string;
      configurationKey: string;
      renderViewId: string;
    };
  }>(
    "/assets/renders/:publicationVersion/:vehicleId/:configurationKey/:renderViewId.png",
    async (request, reply) => {
      const {
        publicationVersion,
        vehicleId,
        configurationKey,
        renderViewId,
      } = request.params;
      if (
        publicationVersion !== data.publication.publicationVersion ||
        vehicleId !== data.publication.vehicleId ||
        !data.configurations.has(configurationKey) ||
        !data.publication.renderViewIds.includes(renderViewId)
      ) {
        throw new RequestError(404, "RENDER_NOT_FOUND", "渲染图片不存在");
      }

      const filePath = await safeRenderPath(
        renderRoot,
        publicationVersion,
        vehicleId,
        configurationKey,
        renderViewId,
      );
      if (!filePath) {
        throw new RequestError(404, "RENDER_NOT_FOUND", "渲染图片不存在");
      }
      return reply
        .header("Cache-Control", "public, max-age=31536000, immutable")
        .type("image/png")
        .send(createReadStream(filePath));
    },
  );

  return app;
}
