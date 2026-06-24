# Chat Server

Chat Server 是一个基于 C++17 后端和 React 前端的即时通讯系统原型。项目覆盖账号认证、好友审批、私聊、群聊、消息回执、收藏、通知、审计、搜索和会话摘要等核心 IM 场景，重点展示 C++ 网络服务、业务建模和前后端协作能力。

后端不依赖 Web 框架，使用 POSIX Socket 实现 HTTP 服务，并通过轻量 JSON 组件完成请求解析、响应序列化和文件持久化。整体实现适合用于 C++ 后端学习、课程设计、毕业设计或作品集展示。

## 功能特性

- 账号注册、登录、退出和 Bearer Token 认证
- 在线状态维护和会话级权限控制
- 好友申请、同意、拒绝和好友关系校验
- 好友通过后才允许创建私聊
- 群聊创建、成员添加、离开和解散
- 消息发送、编辑、撤回、收藏和 `clientId` 幂等处理
- 消息送达/已读回执与未读数量统计
- 通知中心、审计日志和运行指标
- 会话与消息搜索
- 基于最近消息的简易会话摘要、关键词和待办提取
- React 工作台界面，支持开发模式代理和生产构建静态托管

## 系统架构

```text
Browser
  |
  | HTTP / JSON
  v
C++17 Backend
  |-- POSIX Socket HTTP Server
  |-- API Routing
  |-- Token Authentication
  |-- IM Domain Services
  |-- JSON Parser / Serializer
  |-- File Persistence: data/chat_state.json
  |
  v
React Frontend
```

C++ 服务可以直接托管 `frontend/dist` 中的前端构建产物，因此生产测试时只需要启动一个后端进程即可同时提供页面和 API。

## 技术栈

后端：

- C++17
- POSIX Socket
- Standard Library
- JSON 文件持久化
- GNU Make / CMake

前端：

- React
- Vite
- lucide-react
- CSS Grid / Flexbox

测试与构建：

- Node.js API 烟测脚本
- Makefile 构建入口
- 可选 CMake 构建入口

## 目录结构

```text
cpp_server/
  include/
    app.hpp           应用接口与领域模型声明
    http.hpp          HTTP 请求、响应和服务封装
    json.hpp          轻量 JSON 类型、解析器和序列化器
  src/
    app.cpp           认证、好友、会话、消息、通知和审计逻辑
    http.cpp          POSIX Socket HTTP 服务实现
    main.cpp          服务启动入口

frontend/
  src/main.jsx        React 应用入口
  src/styles.css      页面样式
  package.json        前端依赖和脚本
  vite.config.js      开发代理配置

scripts/
  smoke_test_cpp.mjs  后端核心链路烟测

Makefile              g++ 构建入口
CMakeLists.txt        CMake 构建配置
```

## 构建与运行

构建 C++ 后端：

```bash
make
```

启动后端：

```bash
make run
```

默认访问地址：

```text
http://127.0.0.1:8000
```

常用运行参数：

```bash
HOST=0.0.0.0 \
PORT=8000 \
DATA_FILE=data/chat_state.json \
DIST_DIR=frontend/dist \
./build/cpp/chat_server
```

## 前端开发模式

安装依赖：

```bash
cd frontend
npm install
```

启动 Vite：

```bash
npm run dev -- --host 0.0.0.0 --port 5173
```

开发模式下，Vite 会将 `/api` 请求代理到：

```text
http://127.0.0.1:8000
```

因此开发模式需要同时运行：

- C++ 后端：`8000`
- Vite 前端：`5173`

## 生产测试模式

构建前端并由 C++ 后端统一托管：

```bash
cd frontend
npm install
npm run build

cd ..
make
HOST=0.0.0.0 PORT=8000 DIST_DIR=frontend/dist ./build/cpp/chat_server
```

然后访问：

```text
http://127.0.0.1:8000
```

## 远端服务器端口转发

如果项目运行在远端服务器上，推荐转发 `8000` 端口。此模式下 C++ 服务同时提供前端页面和 API，最接近生产部署。

远端服务器执行：

```bash
cd /root/renwu
cd frontend && npm install && npm run build
cd ..
make
HOST=0.0.0.0 PORT=8000 DIST_DIR=frontend/dist ./build/cpp/chat_server
```

本地通过 SSH 转发：

```bash
ssh -L 8000:127.0.0.1:8000 <user>@<server>
```

然后在本地浏览器打开：

```text
http://127.0.0.1:8000
```

如果使用 VS Code、Cursor 或云平台自带的 Port Forward 功能，直接转发远端 `8000` 端口即可。

健康检查：

```bash
curl http://127.0.0.1:8000/api/health
```

正常响应会包含：

```json
{
  "status": "ok",
  "service": "ai-native-im-cpp"
}
```

## CMake 构建

如果环境中安装了 CMake：

```bash
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
./build/cmake/chat_server
```

## 烟测

后端启动后运行：

```bash
BASE_URL=http://127.0.0.1:8000 node scripts/smoke_test_cpp.mjs
```

烟测覆盖注册、好友申请、同意好友、创建私聊、发送消息、收藏消息、搜索、摘要和健康检查。

如需避免污染默认数据文件，可以使用临时数据文件启动服务：

```bash
DATA_FILE=/tmp/chat_server_smoke.json PORT=18080 ./build/cpp/chat_server
BASE_URL=http://127.0.0.1:18080 node scripts/smoke_test_cpp.mjs
```

## API 概览

需要登录的接口使用 Bearer Token：

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

## 数据存储

默认运行数据文件：

```text
data/chat_state.json
```

本地运行数据、日志、构建产物、前端依赖和前端构建输出均已通过 `.gitignore` 排除。
