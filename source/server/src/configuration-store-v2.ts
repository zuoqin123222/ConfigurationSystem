import { createHash } from "node:crypto";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  renameSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { dirname } from "node:path";
import { RequestError } from "./data.js";
import type {
  VehicleConfiguration,
  VehiclePriceResult,
} from "./automotive-catalog-v2.js";

export interface StoredConfigurationV2 extends VehicleConfiguration {
  revision: number;
  priceResult: VehiclePriceResult;
  createdAt: string;
  updatedAt: string;
}

export interface CreateConfigurationResultV2 {
  configuration: StoredConfigurationV2;
  replayed: boolean;
}

interface IdempotencyEntry {
  fingerprint: string;
  configuration: StoredConfigurationV2;
}

function copy(value: StoredConfigurationV2): StoredConfigurationV2 {
  return structuredClone(value);
}

function fingerprint(
  configuration: VehicleConfiguration,
  priceResult: VehiclePriceResult,
): string {
  return createHash("sha256")
    .update(JSON.stringify({ configuration, priceResult }), "utf8")
    .digest("hex");
}

function configurationValue(
  configuration: StoredConfigurationV2,
): VehicleConfiguration {
  return {
    schemaVersion: configuration.schemaVersion,
    catalogVersion: configuration.catalogVersion,
    vehicleId: configuration.vehicleId,
    configurationId: configuration.configurationId,
    renderKey: configuration.renderKey,
    selections: configuration.selections,
    customizations: configuration.customizations,
  };
}

export class ConfigurationStoreV2 {
  private readonly configurations = new Map<string, StoredConfigurationV2>();
  private readonly idempotency = new Map<string, IdempotencyEntry>();

  constructor(
    private readonly now: () => Date = () => new Date(),
    private readonly snapshotPath?: string,
  ) {
    this.loadSnapshot();
  }

  private loadSnapshot(): void {
    if (!this.snapshotPath || !existsSync(this.snapshotPath)) return;
    const snapshot = JSON.parse(readFileSync(this.snapshotPath, "utf8")) as {
      schemaVersion?: unknown;
      configurations?: unknown;
      idempotency?: unknown;
    };
    if (
      snapshot.schemaVersion !== "1.0.0" ||
      !Array.isArray(snapshot.configurations)
    ) {
      throw new Error("车型目录 v2 配置存储快照结构非法");
    }
    for (const item of snapshot.configurations) {
      if (
        typeof item !== "object" ||
        item === null ||
        typeof (item as StoredConfigurationV2).configurationId !== "string"
      ) {
        throw new Error("车型目录 v2 配置存储包含非法记录");
      }
      const configuration = {
        ...(item as StoredConfigurationV2),
        customizations:
          typeof (item as Partial<StoredConfigurationV2>).customizations === "object"
            && (item as Partial<StoredConfigurationV2>).customizations !== null
            && !Array.isArray((item as Partial<StoredConfigurationV2>).customizations)
            ? (item as StoredConfigurationV2).customizations
            : {},
      };
      this.configurations.set(configuration.configurationId, copy(configuration));
    }
    if (snapshot.idempotency !== undefined && !Array.isArray(snapshot.idempotency)) {
      throw new Error("车型目录 v2 配置存储幂等索引结构非法");
    }
    for (const item of snapshot.idempotency ?? []) {
      if (
        typeof item !== "object" ||
        item === null ||
        typeof (item as { key?: unknown }).key !== "string" ||
        typeof (item as { fingerprint?: unknown }).fingerprint !== "string" ||
        typeof (item as { configurationId?: unknown }).configurationId !== "string"
      ) {
        throw new Error("车型目录 v2 配置存储包含非法幂等记录");
      }
      const entry = item as {
        key: string;
        fingerprint: string;
        configurationId: string;
      };
      const configuration = this.configurations.get(entry.configurationId);
      if (!configuration) {
        // 配置更新会生成新的稳定 ID；旧版本留下的幂等索引不能阻断服务启动。
        continue;
      }
      this.idempotency.set(entry.key, {
        fingerprint: fingerprint(
          configurationValue(configuration),
          configuration.priceResult,
        ),
        configuration: copy(configuration),
      });
    }
  }

  private persist(): void {
    if (!this.snapshotPath) return;
    mkdirSync(dirname(this.snapshotPath), { recursive: true });
    const temporaryPath = `${this.snapshotPath}.${process.pid}.tmp`;
    const snapshot = {
      schemaVersion: "1.0.0",
      configurations: this.list(),
      idempotency: [...this.idempotency].map(([key, entry]) => ({
        key,
        fingerprint: entry.fingerprint,
        configurationId: entry.configuration.configurationId,
      })),
    };
    writeFileSync(temporaryPath, `${JSON.stringify(snapshot, null, 2)}\n`, {
      encoding: "utf8",
      flag: "w",
    });
    try {
      renameSync(temporaryPath, this.snapshotPath);
    } catch (error) {
      rmSync(temporaryPath, { force: true });
      throw error;
    }
  }

  create(
    configuration: VehicleConfiguration,
    priceResult: VehiclePriceResult,
    idempotencyKey?: string,
  ): CreateConfigurationResultV2 {
    const requestFingerprint = fingerprint(configuration, priceResult);
    if (idempotencyKey) {
      const previous = this.idempotency.get(idempotencyKey);
      if (previous) {
        if (previous.fingerprint !== requestFingerprint) {
          throw new RequestError(
            409,
            "IDEMPOTENCY_CONFLICT",
            "同一 Idempotency-Key 不得用于不同请求",
          );
        }
        return { configuration: copy(previous.configuration), replayed: true };
      }
    }

    const existing = this.configurations.get(configuration.configurationId);
    if (existing) {
      if (idempotencyKey) {
        this.idempotency.set(idempotencyKey, {
          fingerprint: requestFingerprint,
          configuration: copy(existing),
        });
        this.persist();
      }
      return { configuration: copy(existing), replayed: true };
    }

    const timestamp = this.now().toISOString();
    const stored: StoredConfigurationV2 = {
      ...structuredClone(configuration),
      revision: 1,
      priceResult: structuredClone(priceResult),
      createdAt: timestamp,
      updatedAt: timestamp,
    };
    this.configurations.set(stored.configurationId, stored);
    if (idempotencyKey) {
      this.idempotency.set(idempotencyKey, {
        fingerprint: requestFingerprint,
        configuration: copy(stored),
      });
    }
    this.persist();
    return { configuration: copy(stored), replayed: false };
  }

  get(configurationId: string): StoredConfigurationV2 | undefined {
    const stored = this.configurations.get(configurationId);
    return stored ? copy(stored) : undefined;
  }

  list(): StoredConfigurationV2[] {
    return [...this.configurations.values()].map(copy);
  }

  update(
    configurationId: string,
    expectedRevision: number,
    configuration: VehicleConfiguration,
    priceResult: VehiclePriceResult,
  ): StoredConfigurationV2 {
    const existing = this.configurations.get(configurationId);
    if (!existing) {
      throw new RequestError(404, "CONFIGURATION_NOT_FOUND", "配置不存在");
    }
    if (existing.revision !== expectedRevision) {
      throw new RequestError(
        409,
        "REVISION_CONFLICT",
        `revision 冲突，当前 revision 为 ${existing.revision}`,
      );
    }
    if (
      configuration.configurationId !== configurationId &&
      this.configurations.has(configuration.configurationId)
    ) {
      throw new RequestError(
        409,
        "CONFIGURATION_EXISTS",
        "更新后的稳定配置已存在",
      );
    }

    const stored: StoredConfigurationV2 = {
      ...structuredClone(configuration),
      revision: existing.revision + 1,
      priceResult: structuredClone(priceResult),
      createdAt: existing.createdAt,
      updatedAt: this.now().toISOString(),
    };
    this.configurations.delete(configurationId);
    for (const [key, entry] of this.idempotency) {
      if (entry.configuration.configurationId === configurationId) {
        this.idempotency.delete(key);
      }
    }
    this.configurations.set(stored.configurationId, stored);
    this.persist();
    return copy(stored);
  }
}
