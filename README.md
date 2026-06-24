# Chat Server

Chat Server 是一个以 C++ 后端为主体的即时通讯系统原型，包含后端 API、React 前端工作台和核心链路烟测。项目以“好友关系审批 + 私聊/群聊 + 消息可靠性”为核心，适合用于学习 C++ 网络服务、业务建模和 IM 系统设计。

系统默认从空数据文件启动，不内置账号或人员资料。用户注册后才能使用好友申请、私聊、群聊、消息回执、通知、审计和搜索等能力。

## 当前后端

当前主后端在 `cpp_server/`，使用 C++17 实现：

- POSIX socket HTTP 服务
- 手写轻量 JSON 解析和序列化
- 文件型 JSON 状态存储，默认 `data/chat_state.json`
- Bearer token 登录态
- 好友、会话、消息、回执、通知、审计和摘要业务逻辑
- React 前端静态文件托管

旧的 Python/FastAPI 后端入口已移除，当前仓库的后端实现以 `cpp_server/` 为准。

## 技术栈

后端：

- C++17
- POSIX socket
- GNU Make / CMake
- JSON 文件存储
- Server-Sent Events 兼容端点

前端：

- React
- Vite
- lucide-react
- CSS Grid/Flex

## 项目结构

```text
cpp_server/
  include/json.hpp    轻量 JSON 类型、解析和序列化
  include/http.hpp    HTTP 请求/响应与服务封装
  include/app.hpp     IM 业务应用接口
  src/http.cpp        POSIX socket HTTP 服务实现
  src/app.cpp         认证、好友、会话、消息等业务逻辑
  src/main.cpp        C++ 后端启动入口

frontend/
  src/main.jsx        React 应用入口和页面组件
  src/styles.css      页面样式
  package.json        前端依赖和脚本
  vite.config.js      Vite 开发代理到 8000

scripts/
  smoke_test_cpp.mjs  C++ 后端核心链路烟测

Makefile              无 CMake 环境也能直接构建
CMakeLists.txt        标准 CMake 构建配置
```

## 本地运行

构建 C++ 后端：

```bash
make
```

启动后端：

```bash
make run
```

默认监听：

```text
http://127.0.0.1:8000
```

可用环境变量：

```bash
HOST=0.0.0.0 PORT=8000 DATA_FILE=data/chat_state.json DIST_DIR=frontend/dist ./build/cpp/chat_server
```

前端开发：

```bash
cd frontend
npm install
npm run dev -- --host 0.0.0.0 --port 5173
```

生产构建并由 C++ 后端托管：

```bash
cd frontend
npm install
npm run build
cd ..
make run
```

## CMake 构建

本机如果安装了 CMake，也可以使用标准 out-of-source 构建：

```bash
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
./build/cmake/chat_server
```

当前环境没有安装 CMake，所以仓库同时提供了 `Makefile`。

## 验证

启动 C++ 后端后运行烟测：

```bash
BASE_URL=http://127.0.0.1:8000 node scripts/smoke_test_cpp.mjs
```

烟测覆盖注册、好友申请、同意、私聊、发消息、收藏、搜索、摘要和健康检查。

## API 概览

所有需要登录的接口都使用 Bearer token：

```http
Authorization: Bearer <token>
```

认证：

- `POST /api/register`
- `POST /api/login`
- `POST /api/logout`
- `GET /api/me`
- `GET /api/settings`
- `PATCH /api/settings`

好友：

- `GET /api/users`
- `GET /api/users/search?q=<username>`
- `POST /api/friends`
- `GET /api/friend-requests`
- `POST /api/friend-requests/accept`
- `POST /api/friend-requests/reject`

会话：

- `GET /api/conversations`
- `POST /api/conversations/direct`
- `POST /api/groups`
- `POST /api/groups/members`
- `POST /api/conversations/preferences`
- `POST /api/conversations/leave`
- `DELETE /api/conversations`
- `GET /api/conversations/{conversation_id}/members`

消息：

- `GET /api/messages?conversationId=<id>`
- `POST /api/messages`
- `PATCH /api/messages`
- `DELETE /api/messages`
- `POST /api/messages/save`
- `DELETE /api/messages/save/{message_id}`
- `GET /api/saved-messages`

其他：

- `GET /api/notifications`
- `POST /api/notifications/read`
- `GET /api/audit`
- `GET /api/search?q=<keyword>`
- `POST /api/summary`
- `GET /api/stream`
- `GET /api/health`

## 数据和隐私

- 项目不预置账号、姓名、部门、职位、手机号等人员资料。
- 用户搜索只用于添加好友，并且前端只展示账号级信息。
- 全局搜索只搜索会话和消息，不搜索人员。
- 本地运行数据默认写入 `data/chat_state.json`。
- `data/`、日志、PID、虚拟环境、`node_modules`、`dist` 和截图输出均已加入 `.gitignore`。
