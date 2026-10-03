import { createReadStream, existsSync, statSync } from "node:fs";
import { realpath, stat } from "node:fs/promises";
import { basename, dirname, isAbsolute, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import Fastify, { type FastifyInstance } from "fastify";
import {
  findReadyRender,
  validateBakeManifest,
  type ValidatedBakeManifest,
} from "./bake.js";
import {
  loadContracts,
  RequestError,
  resolveConfiguration,
  type ContractData,
} from "./data.js";
import {
  ConfigurationStoreV2,
  type StoredConfigurationV2,
} from "./configuration-store-v2.js";
import {
  assertSc01Version,
  buildSc01PriceResult,
  deriveSc01Configuration,
  loadSc01V2,
  type Sc01V2Data,
} from "./sc01-v2.js";

export interface BuildAppOptions {
  contractRoot?: string;
  bakeRoot?: string;
  manifestPath?: string;
  bake?: ValidatedBakeManifest;
  data?: ContractData;
  sc01V2?: Sc01V2Data;
  configurationStoreV2?: ConfigurationStoreV2;
}

const STABLE_ID_PATTERN = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function assertFields(
  value: unknown,
  required: readonly string[],
  optional: readonly string[] = [],
): asserts value is Record<string, unknown> {
  if (!isRecord(value)) {
    throw new RequestError(400, "INVALID_REQUEST", "请求体必须是 JSON 对象");
  }
  const allowed = new Set([...required, ...optional]);
  if (
    !required.every((field) => Object.hasOwn(value, field)) ||
    Object.keys(value).some((field) => !allowed.has(field))
  ) {
    throw new RequestError(400, "INVALID_REQUEST", "请求体字段不符合接口契约");
  }
}

function readRevision(value: unknown): number {
  if (!Number.isInteger(value) || (value as number) < 1) {
    throw new RequestError(400, "INVALID_REVISION", "revision 必须是正整数");
  }
  return value as number;
}

function readIdempotencyKey(value: string | string[] | undefined): string | undefined {
  if (value === undefined) return undefined;
  if (Array.isArray(value) || value.length < 1 || value.length > 128) {
    throw new RequestError(
      400,
      "INVALID_IDEMPOTENCY_KEY",
      "Idempotency-Key 必须是 1 到 128 个字符",
    );
  }
  return value;
}

function defaultBakeRoot(publicationVersion: string): string {
  if (process.env.BAKE_ROOT) {
    return resolve(process.env.BAKE_ROOT);
  }

  const moduleDirectory = dirname(fileURLToPath(import.meta.url));
  const serverRoot =
    basename(dirname(moduleDirectory)) === "dist"
      ? resolve(moduleDirectory, "../..")
      : resolve(moduleDirectory, "..");
  const candidates = [
    resolve(serverRoot, "../../package"),
    resolve(serverRoot, "../../contracts/fixtures/bake.valid"),
  ];
  for (const candidate of candidates) {
    try {
      const rootManifest = resolve(candidate, "bake-manifest.json");
      const manifest = existsSync(rootManifest)
        ? rootManifest
        : resolve(candidate, `renders/${publicationVersion}/bake-manifest.json`);
      if (statSync(manifest).isFile()) return candidate;
    } catch {
      // 本地开发可使用实际正向 fixture，部署环境优先使用 package。
    }
  }
  return candidates[0]!;
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
  const sc01V2 = options.sc01V2 ?? loadSc01V2(options.contractRoot);
  const configurationStoreV2 =
    options.configurationStoreV2 ?? new ConfigurationStoreV2();
  const bakeRoot = resolve(
    options.bakeRoot ?? defaultBakeRoot(data.publication.publicationVersion),
  );
  const manifestPath = resolve(
    options.manifestPath ??
      (existsSync(resolve(bakeRoot, "bake-manifest.json"))
        ? resolve(bakeRoot, "bake-manifest.json")
        : resolve(
            bakeRoot,
            "renders",
            data.publication.publicationVersion,
            "bake-manifest.json",
          )),
  );
  const bake = options.bake ?? validateBakeManifest(manifestPath, bakeRoot);
  if (
    bake.manifest.catalogVersion !== data.catalog.catalogVersion ||
    bake.manifest.publicationVersion !== data.publication.publicationVersion ||
    bake.manifest.vehicleId !== data.publication.vehicleId
  ) {
    throw new Error("bake manifest 与 catalog/publication 版本不一致");
  }
  const renderRoot = resolve(bake.assetRoot, "renders");

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

  app.get("/api/v2/catalog", async () => sc01V2.catalog);

  app.post("/api/v2/configurations", async (request, reply) => {
    const body = request.body;
    assertFields(
      body,
      ["catalogVersion", "vehicleId", "selections"],
      ["customizations", "quoteRequested"],
    );
    assertSc01Version(body.catalogVersion, body.vehicleId, sc01V2);
    if (
      Object.hasOwn(body, "quoteRequested") &&
      typeof body.quoteRequested !== "boolean"
    ) {
      throw new RequestError(
        400,
        "INVALID_REQUEST",
        "quoteRequested 必须是 boolean",
      );
    }
    const configuration = deriveSc01Configuration(
      body.selections,
      sc01V2,
      body.customizations,
    );
    const priceResult = buildSc01PriceResult(configuration, sc01V2);
    if (body.quoteRequested === true) {
      return reply.status(422).send({
        code: "PRICE_UNCONFIRMED",
        message: "SC01 草案仅提供参考总价，不构成正式报价",
        details: priceResult,
      });
    }

    const idempotencyKey = readIdempotencyKey(
      request.headers["idempotency-key"],
    );
    const created = configurationStoreV2.create(
      configuration,
      priceResult,
      idempotencyKey,
    );
    return reply
      .status(created.replayed ? 200 : 201)
      .header(
        "Location",
        `/api/v2/configurations/${created.configuration.configurationId}`,
      )
      .header("ETag", `"${created.configuration.revision}"`)
      .header("Idempotency-Replayed", String(created.replayed))
      .send(created.configuration);
  });

  app.get<{
    Querystring: { configurationId?: string };
  }>("/api/v2/configurations", async (request) => {
    const { configurationId } = request.query;
    if (configurationId === undefined) {
      return { items: configurationStoreV2.list() };
    }
    const configuration = configurationStoreV2.get(configurationId);
    if (!configuration) {
      throw new RequestError(404, "CONFIGURATION_NOT_FOUND", "配置不存在");
    }
    return configuration;
  });

  app.get<{ Params: { configurationId: string } }>(
    "/api/v2/configurations/:configurationId",
    async (request, reply) => {
      const configuration = configurationStoreV2.get(
        request.params.configurationId,
      );
      if (!configuration) {
        throw new RequestError(404, "CONFIGURATION_NOT_FOUND", "配置不存在");
      }
      return reply
        .header("ETag", `"${configuration.revision}"`)
        .send(configuration);
    },
  );

  async function updateV2Configuration(
    configurationId: string,
    body: unknown,
  ): Promise<StoredConfigurationV2> {
    assertFields(body, [
      "catalogVersion",
      "vehicleId",
      "selections",
      "revision",
    ], ["configurationId", "customizations", "quoteRequested"]);
    if (
      Object.hasOwn(body, "configurationId") &&
      body.configurationId !== configurationId
    ) {
      throw new RequestError(
        400,
        "INVALID_REQUEST",
        "路径与请求体 configurationId 不一致",
      );
    }
    assertSc01Version(body.catalogVersion, body.vehicleId, sc01V2);
    const revision = readRevision(body.revision);
    const configuration = deriveSc01Configuration(
      body.selections,
      sc01V2,
      body.customizations,
    );
    const priceResult = buildSc01PriceResult(configuration, sc01V2);
    if (body.quoteRequested === true) {
      throw new RequestError(
        422,
        "PRICE_UNCONFIRMED",
        "SC01 草案价格未确认，禁止报价",
      );
    }
    return configurationStoreV2.update(
      configurationId,
      revision,
      configuration,
      priceResult,
    );
  }

  app.put("/api/v2/configurations", async (request, reply) => {
    if (!isRecord(request.body) || typeof request.body.configurationId !== "string") {
      throw new RequestError(
        400,
        "INVALID_REQUEST",
        "configurationId 必须是字符串",
      );
    }
    const updated = await updateV2Configuration(
      request.body.configurationId,
      request.body,
    );
    return reply
      .header("Location", `/api/v2/configurations/${updated.configurationId}`)
      .header("ETag", `"${updated.revision}"`)
      .send(updated);
  });

  app.put<{ Params: { configurationId: string } }>(
    "/api/v2/configurations/:configurationId",
    async (request, reply) => {
      const updated = await updateV2Configuration(
        request.params.configurationId,
        request.body,
      );
      return reply
        .header("Location", `/api/v2/configurations/${updated.configurationId}`)
        .header("ETag", `"${updated.revision}"`)
        .send(updated);
    },
  );

  app.post("/api/v2/renders/resolve", async (request) => {
    const body = request.body;
    assertFields(
      body,
      ["catalogVersion", "vehicleId", "selections"],
      ["customizations", "renderViewId"],
    );
    assertSc01Version(body.catalogVersion, body.vehicleId, sc01V2);
    if (
      Object.hasOwn(body, "renderViewId") &&
      typeof body.renderViewId !== "string"
    ) {
      throw new RequestError(
        400,
        "INVALID_REQUEST",
        "renderViewId 必须是字符串",
      );
    }
    const configuration = deriveSc01Configuration(
      body.selections,
      sc01V2,
      body.customizations,
    );
    return {
      schemaVersion: configuration.schemaVersion,
      catalogVersion: configuration.catalogVersion,
      vehicleId: configuration.vehicleId,
      configurationId: configuration.configurationId,
      renderKey: configuration.renderKey,
      ...(body.renderViewId === undefined
        ? {}
        : { renderViewId: body.renderViewId }),
    };
  });

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
    const render = findReadyRender(
      bake,
      configuration.configurationKey,
      renderViewId,
    );
    if (!render) {
      throw new RequestError(404, "RENDER_NOT_FOUND", "渲染图片不存在");
    }
    return {
      configurationKey: configuration.configurationKey,
      renderViewId,
      imageUrl: `/assets/${render.path}`,
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
      const render = findReadyRender(bake, configurationKey, renderViewId);
      if (!render) {
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
