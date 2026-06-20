# 🛵 校园外卖配送系统

基于 C++ 的高性能外卖配送系统。采用 **Epoll 多路复用** 实现高并发网络服务，**MySQL** 存储业务数据，**SSE (Server-Sent Events)** 实现实时订单号推送，支持数据校验与性能基准测试。

## 🏗 系统架构

```
┌──────────────────────────────────────────────────┐
│                    前端 (Browser)                 │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ 菜单下单  │  │ 订单大屏  │  │ 订单列表/统计  │  │
│  └────┬─────┘  └────▲─────┘  └───────┬───────┘  │
│       │              │               │           │
│       │    SSE Push  │    REST API   │           │
└───────┼──────────────┼───────────────┼───────────┘
        │              │               │
┌───────┴──────────────┴───────────────┴───────────┐
│                C++ Server (Epoll)                 │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ Epoll    │  │ Thread   │  │ Push Manager  │  │
│  │ Manager  │  │ Pool(16) │  │ (SSE 推送)    │  │
│  └────┬─────┘  └────┬─────┘  └───────┬───────┘  │
│       │              │               │           │
│  ┌────┴──────────────┴───────────────┴────────┐  │
│  │           HTTP Parser + Router             │  │
│  └────────────────────┬───────────────────────┘  │
│                       │                          │
│  ┌────────────────────┴───────────────────────┐  │
│  │         Order Handler (业务逻辑)            │  │
│  └────────────────────┬───────────────────────┘  │
└───────────────────────┼──────────────────────────┘
                        │
┌───────────────────────┴──────────────────────────┐
│           MySQL 8.0 (RAII 连接池)                 │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ orders   │ │  menu    │ │ order_status_log │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
└──────────────────────────────────────────────────┘
```

## 📦 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| **网络层** | Epoll 抽象层 (水平触发 + select) | 跨平台兼容 Windows/Linux |
| **I/O 模型** | 非阻塞 I/O + 水平触发 | 高并发连接处理 |
| **HTTP** | 自实现 HTTP/1.1 解析器 | 支持 GET/POST/PUT, JSON body, query string |
| **并发** | 线程池 (std::thread + 条件变量) | 16 线程处理业务逻辑 |
| **线程安全** | 主线程响应队列 | 线程池写入 → 主线程排空发送 |
| **数据库** | MySQL 8.0 C API + RAII 连接池 | 预处理语句防 SQL 注入 |
| **实时推送** | Server-Sent Events (SSE) | 心跳保活 + 自动重连 |
| **前端** | 原生 HTML/CSS/JS | EventSource API + Notification API |
| **JSON** | 自实现轻量 JSON 解析/序列化 | 零外部依赖 |

## 📁 项目结构

```
food_delivery/
├── server/                        # C++ 后端
│   ├── include/                   # 11 个头文件
│   │   ├── server.h               #   主服务器 (事件循环, 连接管理)
│   │   ├── epoll_manager.h        #   Epoll 抽象层 (跨平台)
│   │   ├── connection.h           #   连接状态管理
│   │   ├── thread_pool.h          #   固定大小线程池
│   │   ├── db_pool.h              #   MySQL RAII 连接池
│   │   ├── http_parser.h          #   HTTP/1.1 请求解析器
│   │   ├── http_response.h        #   HTTP 响应构建器
│   │   ├── router.h               #   URL 路由分发
│   │   ├── order_handler.h        #   订单业务处理 + 数据校验
│   │   ├── push_manager.h         #   SSE 实时推送管理
│   │   ├── config.h               #   配置管理 (JSON)
│   │   └── logger.h               #   线程安全分级日志
│   ├── src/                       # 3 个源文件
│   │   ├── main.cpp               #   入口 + 信号处理
│   │   ├── server.cpp             #   事件循环 + HTTP + 静态文件
│   │   └── order_handler.cpp      #   订单 CRUD + 状态校验
│   ├── sql/init.sql               # 数据库初始化 (5表 + 12菜品)
│   ├── config.json                # 运行时配置
│   └── CMakeLists.txt
├── frontend/                      # Web 前端
│   ├── index.html                 # 主页面 (下单 + 大屏 + 列表)
│   ├── css/style.css              # 深色主题 + 动画样式
│   └── js/
│       ├── app.js                 # 主逻辑 (菜单/购物车/SSE)
│       └── notify.js              # 桌面通知 + 弹窗 + 音效
├── tests/                         # 测试套件
│   ├── test_db.cpp                # 数据库连接池测试 (6项)
│   ├── test_order.cpp             # 订单 CRUD + 数据校验测试 (8项)
│   ├── test_concurrency.cpp       # 1000 并发压力测试
│   ├── benchmark.cpp              # 性能基准 (P50/P99/QPS)
│   └── CMakeLists.txt
├── .gitignore
└── README.md
```

## 🗄 数据库设计

### 表结构 (5 张表)

```
menu_items          orders              order_items
──────────          ──────              ───────────
id (PK)             id (PK)             id (PK)
name                order_no (UNIQUE)   order_id (FK)
category            customer_name       menu_item_id (FK)
price               phone               quantity
image               address             unit_price
description         total_price
is_available        status (ENUM)
created_at          remark              order_status_log
updated_at          created_at          ────────────────
                    updated_at          id (PK)
                                        order_id (FK)
users                                   from_status
─────                                   to_status
id (PK)                                 created_at
username (UNIQUE)
password_hash
role (ENUM)
```

### 订单状态机

```
                    ┌──────────┐
                    │ pending  │ ──────────────┐
                    └────┬─────┘               │
                         │                     │
                    ┌────▼─────┐               │
                    │confirmed │               │
                    └────┬─────┘               │
                         │                     │
                    ┌────▼─────┐               │
                    │preparing │               │
                    └────┬─────┘               │
                         │                     │
                    ┌────▼──────┐              │
                    │delivering │              │
                    └────┬──────┘              │
                         │                     │
                    ┌────▼──────┐    ┌─────────▼──┐
                    │ delivered │    │  cancelled  │ (终态)
                    └───────────┘    └────────────┘
```

## 🔌 API 接口

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/` | 前端首页 |
| `GET` | `/api/menu` | 获取菜单列表 |
| `POST` | `/api/orders` | 创建订单 → **触发 SSE 推送** |
| `GET` | `/api/orders?page=1&status=pending` | 订单列表 (分页+筛选) |
| `GET` | `/api/orders/:id` | 订单详情 (含菜品明细) |
| `PUT` | `/api/orders/:id/status` | 更新订单状态 → **触发 SSE 推送** |
| `GET` | `/api/events` | SSE 实时事件流 |
| `GET` | `/api/stats` | 统计数据 (今日订单数/收入/各状态) |
| `GET` | `/css/*` `/js/*` | 静态资源 |

## ✅ 数据校验

| 校验项 | 规则 |
|--------|------|
| **手机号** | 正则 `^1[3-9]\d{9}$` |
| **必填字段** | customer_name, phone, address |
| **菜单项** | 存在性 + `is_available` 可用性检查 |
| **订单金额** | 自动从数据库价格计算, 防篡改 |
| **状态流转** | 严格状态机校验, 不可逆/不可跳转 |
| **SQL 注入** | 全部使用 MySQL C API 预处理语句 |

## 🚀 快速开始

### 环境要求

- **编译器**: MinGW g++ 8.1+ 或 MSVC 2019+
- **CMake**: 3.16+ (可选, 也支持直接 g++ 编译)
- **MySQL**: 8.0+ (已运行)
- **Windows** / Linux

### 1. 初始化数据库

```bash
mysql -u root -p < server/sql/init.sql
```

执行后创建 `food_delivery` 数据库，包含 5 张表 + 12 道菜品 + 管理员账号。

### 2. 编译

**方式一: 直接 g++ 编译 (推荐)**

```bash
cd server
mkdir -p build && cd build
g++ -std=c++17 -O2 \
  -I ../include -I "C:/Program Files/MySQL/MySQL Server 8.0/include" \
  -o food_delivery_server.exe \
  ../src/main.cpp ../src/server.cpp ../src/order_handler.cpp \
  "C:/Program Files/MySQL/MySQL Server 8.0/lib/libmysql.lib" \
  -lws2_32 -lpthread

# 复制依赖
cp "C:/Program Files/MySQL/MySQL Server 8.0/lib/libmysql.dll" .
cp -r ../../frontend .
cp ../config.json .
```

**方式二: CMake 编译**

```bash
cd server
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

### 3. 配置

编辑 `config.json` 设置数据库密码:

```json
{
    "database": {
        "password": "your_mysql_password"
    }
}
```

### 4. 启动服务

```bash
cd server/build
./food_delivery_server.exe config.json
```

### 5. 访问

浏览器打开 **http://localhost:8080**

### 6. 编译并运行测试

```bash
cd tests
mkdir -p build && cd build

# 数据库池测试
g++ -std=c++17 -O2 -I ../../server/include \
  -I "C:/Program Files/MySQL/MySQL Server 8.0/include" \
  -o test_db.exe ../test_db.cpp \
  "C:/Program Files/MySQL/MySQL Server 8.0/lib/libmysql.lib" \
  -lws2_32 -lpthread

# 订单业务测试
g++ -std=c++17 -O2 -I ../../server/include \
  -I "C:/Program Files/MySQL/MySQL Server 8.0/include" \
  -o test_order.exe ../test_order.cpp ../../server/src/order_handler.cpp \
  "C:/Program Files/MySQL/MySQL Server 8.0/lib/libmysql.lib" \
  -lws2_32 -lpthread

# 并发压力测试
g++ -std=c++17 -O2 -o test_concurrency.exe ../test_concurrency.cpp -lws2_32

# 性能基准测试
g++ -std=c++17 -O2 -o benchmark.exe ../benchmark.cpp -lws2_32

# 运行
./test_db
./test_order
./test_concurrency
./benchmark
```

## 📊 性能测试结果

**测试环境**: Windows 11, MinGW g++ 8.1.0, MySQL 8.0.41

### Benchmark (每个 API 200 次请求)

| API | QPS | Avg | P50 | P99 | 成功率 |
|-----|-----|-----|-----|-----|--------|
| GET /api/menu | 64.5 | 15.5ms | 15.5ms | 16.8ms | 100% |
| GET /api/orders | 64.4 | 15.5ms | 15.6ms | 17.0ms | 100% |
| GET /api/stats | 64.3 | 15.6ms | 15.6ms | 16.4ms | 100% |
| POST /api/orders | 64.2 | 15.6ms | 15.6ms | 16.1ms | 100% |

### Concurrency (1000 请求 / 50 并发)

- **吞吐量**: 7,852 req/s
- **P50 延迟**: 5.9ms
- **P99 延迟**: 10.6ms

## 🎯 实时推送演示

1. 打开两个浏览器窗口访问 `http://localhost:8080`
2. 窗口 A 选择菜品并下单
3. 窗口 B 的 **「实时订单大屏」** 立即弹出新订单号动画
4. 浏览器桌面通知 + 提示音同步触发
5. 在订单列表中点击 **「确认接单」→「开始备餐」→「开始配送」→「确认送达」**，状态变更实时推送到所有窗口

## 🔧 核心模块详解

### Epoll 抽象层 (`epoll_manager.h`)

自实现的跨平台 epoll 接口，在 Windows 上基于 `select()` 实现，在 Linux 上可直接替换为 `<sys/epoll.h>`。支持:

- `epoll_create1` / `epoll_ctl` / `epoll_wait` 标准 API
- `EPOLLIN` / `EPOLLOUT` / `EPOLLERR` 事件
- 水平触发 (Level-Triggered) 模式
- 零外部依赖

### 线程安全架构

```
handle_read()              ThreadPool               main loop
     │                         │                       │
     ├─ enqueue task ─────────►│                       │
     │                         ├─ process_request()    │
     │                         ├─ push pending ───────►│
     │                         │                       ├─ drain_pending()
     │                         │                       ├─ send_response()
     │                         │                       └─ handle_write()
```

- 线程池仅负责业务处理，不触碰 epoll 状态
- 响应通过 `pending_responses_` 队列 (mutex 保护) 传递
- 主线程排空队列，所有 epoll 操作在主线程完成

## 📝 License

毕业设计项目 — 仅供学习参考

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)
