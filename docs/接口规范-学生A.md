# 学生 A 数据服务接口规范（A → B / A → C 集成契约）

> 版本 v1.1 · 2026-10-18
> 作用域：**仅定义学生 A 对外提供的数据接口**（对应需求 FR-07 / FR-08 / FR-09）。
> 分工边界：B 的模型服务接口、C 的 WebSocket 推送（FR-21）由各自负责，本文档不越界定义；仅在必要时说明衔接方式。
> 标注 `[TBD]` 为待与 B/C 确认项，标注 `[可选]` 为一期可裁剪项。

---

## 1. 设计原则

本契约遵循以下原则，后续任何接口增改都不得违反：

| 编号 | 原则 | 在本契约中的体现 |
|---|---|---|
| P1 | **单一职责** | 设备元数据、时序数据、批量导出分属不同资源，不混在一个接口里 |
| P2 | **资源导向（RESTful）** | URL 只用名词、不用动词；用 HTTP 方法表达动作 |
| P3 | **版本化** | 所有路径带 `/api/v1`，破坏性变更必须升 v2，v1 保留至少一个迭代周期 |
| P4 | **幂等与安全** | GET 全部幂等且无副作用；写操作（导出任务创建）返回任务 ID 供重试查询 |
| P5 | **自描述** | 每个数值都可通过元数据接口查到单位、量程、含义，不依赖口头约定 |
| P6 | **单位与精度显式化** | 数值字段一律不带单位，单位在测点元数据中声明；时间统一 ISO 8601 / UTC |
| P7 | **统一信封** | 成功与失败共用同一响应结构，前端可写一套拦截器处理 |
| P8 | **可观测** | 每个响应回传 `request_id`，便于三方联调时定位问题 |
| P9 | **向后兼容** | 只允许「新增可选字段」，禁止删除或改变已有字段语义 |
| P10 | **不过度设计** | 一期不引入 gRPC、GraphQL 等；REST + JSON 足够，降低 B/C 接入成本 |

**技术底座（已确定）**：RESTful JSON over HTTP，后端框架采用 **FastAPI**（Python 3.10+，ASGI，搭配 Uvicorn 运行）。

选型理由（三条）：

1. **自动生成 OpenAPI 文档**——B、C 无需等 A 写完文档就能看到全部接口的字段与示例，三方联调的沟通成本大幅下降，正对应设计原则 P5「自描述」。
2. **原生 Pydantic 模型校验**——请求参数与响应结构在代码层强约束，`quality`、时间格式、单位等契约约定有了落地的类型保障，不易出现「文档写 null、实际返回 -9999」这类漂移。
3. **原生异步支持**——时序查询（S5）涉及数据库 IO，异步可提升并发能力，有助于达成 NFR-2 的实时性目标。

> 本规范中的 Measurement 结构与各响应模型，在实现时应直接定义为 Pydantic `BaseModel`，做到「文档即代码」。

**标准依赖**：`fastapi`、`uvicorn[standard]`、`pydantic`。`[TBD：数据库驱动按第 9 节时序库选型结果确定]`

---

## 2. 通用约定

### 2.1 URL 与版本

```
https://{host}/api/v1/{resource}
```

- Base Path：`/api/v1`
- 交互式文档（由 FastAPI 自动生成，联调期间必须始终可访问）：

| 地址 | 内容 |
|---|---|
| `GET /docs` | Swagger UI，可直接在线试调接口 |
| `GET /redoc` | ReDoc，更适合阅读 |
| `GET /openapi.json` | OpenAPI 3.0 schema，B/C 可用它生成各自的客户端代码 |

> **约定**：这三项属于基础设施，**不得加鉴权拦截**，否则 B/C 无法自助联调。

### 2.2 统一响应信封

**成功**

```json
{
  "code": 0,
  "message": "success",
  "data": { },
  "meta": { },
  "request_id": "req_7f3a9c1e2b"
}
```

**失败**

```json
{
  "code": 40401,
  "message": "设备不存在: WT-A99",
  "data": null,
  "meta": null,
  "request_id": "req_7f3a9c1e2b"
}
```

> **约定**：HTTP 状态码仅表达传输层语义（200/400/401/404/500），**业务结果一律看 `code`**。前端与算法侧只需判断 `code == 0`。

### 2.3 时间格式

| 场景 | 格式 | 示例 |
|---|---|---|
| 请求参数 | ISO 8601，UTC，带 `Z` | `2016-01-01T00:00:00Z` |
| 响应时间戳 | 同时提供字符串与毫秒整数 | `"ts": "2016-01-01T00:00:00Z"`, `"ts_ms": 1451606400000` |

> 提供 `ts_ms` 是为了让 B 做窗口计算、C 画横轴时不必做字符串解析。这是 P6 原则的落地。

### 2.4 分页（列表类接口统一）

**请求**：`page`（从 1 开始）、`page_size`（默认 100，上限 1000）

**响应 `meta`**：

```json
{
  "page": 1,
  "page_size": 100,
  "total": 256,
  "has_more": true
}
```

### 2.5 数值与缺失值

- **数值类型**：一律 JSON number；整型与浮点不做字符串化。
- **缺失值**：字段值为 `null`，**禁止用 `-9999`、`-1`、`""` 等哨兵值**。原始数据集中的哨兵值必须在 A 侧入库前清洗转换（见需求 FR-05），不得透传给 B/C。
- **保留小数**：不做服务端四舍五入，原样返回；需要格式化由前端处理。

### 2.6 认证

`Authorization: Bearer {token}`（JWT）。`[TBD：与需求 FR-23 的登录方案对齐，由 C 主导，`A 侧只做 token 校验`]`

> 一期若登录尚未完成，可先全通并在 `[TBD]` 标记，联调阶段关闭。

---

## 3. 数据契约核心：统一测点模型

**这是整个契约的基石。** 所有涉及测点数据的接口都返回此结构，B 和 C 只需要写一次解析逻辑。

### 3.1 Measurement（长表格式）

```json
{
  "device_id": "WT-A01",
  "point_code": "wt.gen.temp_winding_a",
  "ts": "2016-01-01T00:00:00Z",
  "ts_ms": 1451606400000,
  "value": 62.5,
  "quality": 0
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `device_id` | string | 是 | 设备唯一编号，全局唯一 |
| `point_code` | string | 是 | 测点编码，命名规则见 3.3 |
| `ts` | string | 是 | ISO 8601 UTC 时间戳 |
| `ts_ms` | int | 是 | 毫秒时间戳，与 `ts` 等价，冗余提供便于计算 |
| `value` | number \| null | 是 | 测点值；缺失为 `null` |
| `quality` | int | 是 | 质量标记，取值见 3.2 |

### 3.2 质量标记 quality

| 值 | 名称 | 含义 | B 的使用建议 |
|---|---|---|---|
| 0 | good | 原始数据，正常 | 正常使用 |
| 1 | interpolated | 缺失值经插值补齐 | 训练时建议剔除，或单独打特征标记 |
| 2 | suspect | 可疑值（超量程、突变未确认） | 建议剔除 |
| 3 | missing | 缺失，未补齐 | 必须排除，不可当 0 处理 |

> **关键点（P5）**：B 拿到的每个值都能立刻知道它能不能用，不需要再去猜。这是 `quality` 字段存在的唯一目的。

### 3.3 测点编码命名规则

```
{设备类型}.{部件/域}.{信号名}
```

- 全小写，点号分隔，下划线连接单词
- 示例：`wt.gen.temp_winding_a`（风机.发电机.A 相绕组温度）、`wt.env.wind_speed`（风机.环境.风速）

预留前缀：

| 前缀 | 设备类型 |
|---|---|
| `wt.` | 风力发电机组 |
| `pv.` | 光伏逆变器 |
| `ess.` | 储能变流器/电池簇 |

### 3.4 单位表

| 物理量 | 单位 | 备注 |
|---|---|---|
| 温度 | `°C` | |
| 有功功率 | `kW` | |
| 无功功率 | `kVar` | |
| 电流 | `A` | |
| 电压 | `V` | |
| 转速 | `rpm` | |
| 风速 | `m/s` | |
| 桨距角 | `°` | |
| 频率 | `Hz` | |

> `[TBD]`：上表需按选定数据集的 `feature_description.csv` 最终校准；新增单位须同步更新本文档，属 P9 向后兼容范围内的「新增」。

---

## 4. 接口清单总览

| 编号 | 方法 | 路径 | 主用方 | 对应需求 | 说明 |
|---|---|---|---|---|---|
| S1 | GET | `/devices` | C | FR-09 | 设备列表 |
| S2 | GET | `/devices/{device_id}` | C | FR-09 | 设备详情 |
| S3 | GET | `/devices/{device_id}/points` | B、C | FR-09 | 测点元数据字典 |
| S4 | GET | `/devices/{device_id}/latest` | B、C | FR-08、FR-09 | 最新测点值快照 |
| S5 | GET | `/timeseries` | B、C | FR-08 | 时序数据查询 |
| S6 | POST | `/datasets/exports` | B | FR-07 | 创建训练数据集导出任务 |
| S7 | GET | `/datasets/exports/{task_id}` | B | FR-07 | 查询导出任务结果 |
| S8 | GET | `/health` | B、C、运维 | NFR-1 | 服务健康检查 |

---

## 5. 接口详细定义

### S1 设备列表 `GET /devices`

**请求参数**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `type` | string | 否 | 按设备类型筛选，如 `wt` |
| `status` | string | 否 | `online` / `offline` |
| `site` | string | 否 | 按场站/区域筛选 |
| `page` / `page_size` | int | 否 | 见 2.4 |

**响应 `data`**

```json
[
  {
    "device_id": "WT-A01",
    "device_name": "A 风场 01 号机组",
    "type": "wt",
    "site": "wind_farm_a",
    "status": "online",
    "last_ts": "2016-01-01T00:00:00Z",
    "point_count": 86
  }
]
```

> `last_ts` 用于前端快速判断数据新鲜度，避免 C 侧再发一次查询。

---

### S2 设备详情 `GET /devices/{device_id}`

**响应 `data`**

```json
{
  "device_id": "WT-A01",
  "device_name": "A 风场 01 号机组",
  "type": "wt",
  "site": "wind_farm_a",
  "status": "online",
  "last_ts": "2016-01-01T00:00:00Z",
  "point_count": 86,
  "points": ["wt.gen.temp_winding_a", "wt.env.wind_speed"]
}
```

---

### S3 测点元数据字典 `GET /devices/{device_id}/points`

**这是自描述原则（P5）的落地。** B 用它自动生成特征名，C 用它渲染单位与图例，**双方都不需要向 A 口头确认字段含义**。

**响应 `data`**

```json
[
  {
    "point_code": "wt.gen.temp_winding_a",
    "name": "发电机 A 相绕组温度",
    "unit": "°C",
    "category": "gen",
    "data_type": "float",
    "min": -20,
    "max": 200,
    "remark": "源自数据集 wind_speed 通道"
  }
]
```

| 字段 | 说明 |
|---|---|
| `point_code` | 测点编码，全局唯一键 |
| `name` | 中文名，前端直接用 |
| `unit` | 单位符号，见 3.4 |
| `min` / `max` | 合理量程，供 C 画纵轴、供 B 剔异常；可为 `null` |

---

### S4 最新测点值 `GET /devices/{device_id}/latest`

**B 用它算实时特征，C 用它渲染大屏卡片与实时数据流。**

**请求参数**

| 参数 | 说明 |
|---|---|
| `points` | 可选，逗号分隔指定测点；不传则返回全部 |

**响应 `data`**

```json
{
  "device_id": "WT-A01",
  "snapshot_ts": "2016-01-01T00:00:00Z",
  "values": [
    {
      "point_code": "wt.gen.temp_winding_a",
      "ts": "2016-01-01T00:00:00Z",
      "ts_ms": 1451606400000,
      "value": 62.5,
      "quality": 0
    }
  ]
}
```

> **性能约定**：单次响应 ≤ 1000 个测点；超过请改用 S5 并按测点分批。查询延迟目标见需求 NFR-2。

---

### S5 时序数据查询 `GET /timeseries`

**这是 FR-08 的核心接口**，B 做特征工程、C 画历史曲线都用它。

**请求参数**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `device_id` | string | 是 | 单设备；多设备请用 `device_ids` |
| `device_ids` | string | 否 | 逗号分隔，与 `device_id` 二选一 |
| `point_code` | string | 是 | 单测点 |
| `point_codes` | string | 否 | 逗号分隔多个测点 |
| `start_time` | string | 是 | ISO 8601 UTC |
| `end_time` | string | 是 | ISO 8601 UTC |
| `interval` | string | 否 | 采样间隔，如 `10m`、`1h`；不传返回原始粒度 |
| `quality` | string | 否 | 过滤质量，逗号分隔，如 `0` 或 `0,1`；不传不过滤 |
| `format` | string | 否 | `rows`（默认，长表）/ `columns`（列式） |

**响应 `data`（`format=rows`，默认）**

```json
{
  "device_id": "WT-A01",
  "point_code": "wt.gen.temp_winding_a",
  "interval": "10m",
  "rows": [
    {
      "ts": "2016-01-01T00:00:00Z",
      "ts_ms": 1451606400000,
      "value": 62.5,
      "quality": 0
    }
  ]
}
```

**响应 `data`（`format=columns`）**

```json
{
  "device_id": "WT-A01",
  "interval": "10m",
  "ts_ms": [1451606400000, 1451607000000],
  "series": {
    "wt.gen.temp_winding_a": [62.5, 63.1],
    "wt.env.wind_speed": [7.2, 7.5]
  }
}
```

> **为什么提供两种格式**：`rows` 结构直观，便于调试与逐点处理；`columns` 把时间戳与值各自聚合成数组，能显著减少 JSON 体积，适合 C 画多测点对比曲线。**默认给 `rows` 是为了降低 B/C 首次接入的调试成本（P10）。**

**约束**

- 单次返回行数上限 `100000`，超出返回 `42201`；请缩小时间窗口或增大 `interval`。
- `start_time` 必须早于 `end_time`。

---

### S6 创建训练数据集导出任务 `POST /datasets/exports`

**这是 FR-07 的实现。** 采用异步任务模式，避免大数据量拖垮 HTTP 连接。

**请求体**

```json
{
  "device_ids": ["WT-A01", "WT-A02"],
  "point_codes": ["wt.gen.temp_winding_a", "wt.env.wind_speed"],
  "start_time": "2016-01-01T00:00:00Z",
  "end_time": "2016-12-31T23:50:00Z",
  "include_quality": true,
  "label": true,
  "format": "csv"
}
```

| 字段 | 说明 |
|---|---|
| `include_quality` | 是否为每个测点附带 `_q` 质量列 |
| `label` | 是否附带数据集自带的故障标签列 `[TBD：需与 B 确认标签列的字段名与取值口径]` |
| `format` | `csv`（默认）/ `parquet` |

**响应 `data`**

```json
{
  "task_id": "exp_20261018_001",
  "status": "running",
  "estimated_rows": 105120
}
```

> `[可选简化]`：若团队认为异步任务过重，一期可退化为同步返回下载地址，**但必须设置行数上限与超时**，并在本文档标注该偏离。

---

### S7 查询导出任务 `GET /datasets/exports/{task_id}`

**响应 `data`**

```json
{
  "task_id": "exp_20261018_001",
  "status": "success",
  "created_at": "2026-10-18T09:00:00Z",
  "finished_at": "2026-10-18T09:00:12Z",
  "row_count": 105120,
  "column_count": 5,
  "file_size_bytes": 8381024,
  "download_url": "/api/v1/datasets/exports/exp_20261018_001/file",
  "columns": [
    "ts",
    "device_id",
    "wt.gen.temp_winding_a",
    "wt.gen.temp_winding_a_q",
    "label"
  ]
}
```

`status` 取值：`running` / `success` / `failed`。`failed` 时 `message` 给出失败原因。

**导出文件格式（CSV 宽表）**

```csv
ts,device_id,wt.gen.temp_winding_a,wt.gen.temp_winding_a_q,wt.env.wind_speed,wt.env.wind_speed_q,label
2016-01-01T00:00:00Z,WT-A01,62.5,0,7.2,0,0
```

> **为什么导出用宽表而 API 用长表**：宽表是 pandas 的原生输入形态，B 拿到后可直接 `read_csv` 进 DataFrame 训练，无需再做 pivot。这是按使用场景差异做的合理区分。

---

### S8 健康检查 `GET /health`

```json
{
  "status": "ok",
  "version": "v1.0",
  "dependencies": {
    "timeseries_db": "ok",
    "storage": "ok"
  },
  "server_time": "2026-10-18T09:00:00Z"
}
```

用于 B/C 启动时探测依赖就绪，也便于联调时快速区分「是 A 挂了还是我挂了」。

---

## 6. 错误码表

| code | HTTP | 含义 | 处理建议 |
|---|---|---|---|
| 0 | 200 | 成功 | — |
| 40001 | 400 | 请求参数校验失败 | 检查必填项与时间格式 |
| 40101 | 401 | 未认证或 token 失效 | 重新登录 |
| 40301 | 403 | 无访问权限 | 联系管理员分配角色 |
| 40401 | 404 | 设备不存在 | 核对 `device_id` |
| 40402 | 404 | 测点不存在 | 调用 S3 获取有效测点列表 |
| 40403 | 404 | 导出任务不存在 | 核对 `task_id` |
| 40901 | 409 | 导出任务尚未完成 | 轮询 S7 |
| 42201 | 422 | 查询数据量超限 | 缩小时间窗或增大 `interval` |
| 42901 | 429 | 请求频率超限 | 降低轮询频率，建议 ≥ 5s |
| 50001 | 500 | 服务内部错误 | 携带 `request_id` 反馈给 A |
| 50301 | 503 | 依赖服务（时序库）不可用 | 稍后重试 |

---

## 7. 分工边界与衔接

| 环节 | 负责方 | 说明 |
|---|---|---|
| 本文档 S1–S8 全部接口 | **A** | A 必须保证契约稳定 |
| 模型服务接口（健康度、诊断结果 FR-16） | **B** | A 不参与定义 |
| WebSocket 实时推送（FR-21） | **C** | A 不提供 WS 通道 |
| 前端如何消费 S4/S5 | **C** | 若 C 需要准实时刷新，建议用 S4 轮询（周期 ≥ 5s）；确实需要推送请先与 A 协商是否扩展 |

> **给 C 的建议**：FR-20 的「关键测点准实时刷新」，一期用 **S4 轮询**即可满足，不必等 FR-21 的 WebSocket。这样 C 可以先把大屏跑通，降低风险。是否采纳由 C 决定。

---

## 8. 版本演进规则

1. **新增字段**：属于向后兼容，直接发布，但须同步更新本文档；**接收方必须容忍未知字段**（不要反序列化失败就报错）。
2. **修改/删除字段**：破坏性变更，必须新开 `/api/v2`，v1 至少保留一个联调周期。
3. **新增错误码**：允许，属兼容范围；接收方对未知 `code` 应按「失败」处理。
4. 任何变更须在本文档追加一条变更记录。

---

## 9. 待确认项

| 编号 | 问题 | 需谁确认 |
|---|---|---|
| ~~I1~~ | ~~后端框架定 FastAPI 还是 Flask~~ | **已关闭（v1.1）**：确定采用 FastAPI + Uvicorn，见第 1 节 |
| I2 | 认证方案与 FR-23 如何对齐 | A + C |
| I3 | 导出结果是否必须带故障标签列，字段名与取值口径 | **B** |
| I4 | 单位表与测点清单如何按 `feature_description.csv` 最终校准 | A（下载数据集后落实） |
| I5 | 列式格式 `columns` 是否真的需要，还是只保留 `rows` | B + C |
| I6 | 轮询周期上限（建议 ≥ 5s）是否接受 | B + C |

---

## 10. 变更记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-10-18 | 初版，定义 S1–S8 八个接口，对应需求 FR-07 / FR-08 / FR-09 |
| v1.1 | 2026-10-18 | 确定技术底座为 FastAPI + Uvicorn；补充交互式文档地址与依赖清单；关闭待确认项 I1。接口定义本身无变更，对 B/C 已写的调用代码无影响 |
