# Chat Server

Chat Server 是一个可运行的即时通讯系统原型，包含后端 API、实时事件通道和 React 前端工作台。项目以“好友关系审批 + 私聊/群聊 + 消息可靠性”为核心，适合用于学习 IM 系统业务建模、课程设计、毕业设计或作品集展示。

系统默认从空数据库启动，不内置账号或人员资料。用户注册后才能使用好友申请、私聊、群聊、消息回执、通知、审计和搜索等能力。

## 设计重点

- **好友关系不是直接建立**：添加好友会先生成申请，对方同意后才会建立好友关系。
- **会话权限受好友关系约束**：私聊只能在双方是好友后创建，群聊成员也必须来自好友列表。
- **消息链路可追踪**：发送消息会记录幂等 `clientId`、送达/已读回执、通知和审计日志。
- **搜索边界清晰**：全局搜索只覆盖会话和消息；找人只在添加好友弹窗中按账号搜索。
- **隐私默认收敛**：项目不预置人员档案，前端只展示账号级信息。
- **可以独立验证**：仓库提供 `scripts/smoke_test.py`，用临时数据库验证核心后端流程。

## 核心场景

### 好友申请

用户通过账号搜索发起好友申请。申请不会自动通过，接收方需要在好友申请列表中选择同意或拒绝。同意后，双方好友列表才会出现对方。

### 私聊和群聊

私聊依赖已通过的好友关系。群聊由当前用户创建，成员只能从好友列表中选择；群聊创建后可以继续添加好友成员。会话支持置顶、静音、归档、删除、离开和解散。

### 消息和回执

消息发送支持 `clientId` 幂等处理，避免弱网重试导致重复消息。每条消息会生成送达/已读回执，前端展示回执统计；用户可以编辑、撤回和收藏消息。

### 通知、审计和摘要

系统会记录好友、会话、消息、通知等关键事件。通知中心支持全部已读；审计日志用于查看操作记录；摘要功能会基于最近聊天内容生成关键词和待办。

## 功能清单

账号和认证：

- 注册、登录、退出
- Session token 认证
- 当前账号信息查询
- 在线状态维护

好友：

- 按账号搜索用户
- 发送好友申请
- 同意好友申请
- 拒绝好友申请
- 好友列表
- 好友权限校验

会话：

- 好友私聊
- 群聊创建
- 群成员添加
- 会话置顶
- 会话静音
- 会话归档
- 私聊删除
- 群聊离开和解散

消息：

- 文本消息发送
- `clientId` 幂等发送
- 消息编辑
- 消息撤回
- 送达/已读回执
- 未读数量同步
- 消息收藏

通知、审计和搜索：

- 通知中心
- 通知全部已读
- 审计日志
- 会话搜索
- 消息搜索
- 简易摘要、关键词和待办提取

实时能力：

- Server-Sent Events 实时更新
- 登录态在线状态
- 消息、会话、好友和通知变更推送

## 技术栈

后端：

- FastAPI
- SQLite
- Pydantic
- Server-Sent Events
- 分层业务函数

前端：

- React
- Vite
- lucide-react
- CSS Grid/Flex

## 项目结构

```text
app/
  main.py             FastAPI 路由、鉴权依赖、静态文件托管
  services.py         认证、好友、会话、消息、通知、审计、摘要逻辑
  schemas.py          API 请求模型
  database.py         SQLite 连接和迁移辅助
  events.py           SSE 事件中心
  config.py           路径和运行配置

frontend/
  src/main.jsx        React 应用入口和页面组件
  src/styles.css      页面样式
  package.json        前端依赖和脚本
  vite.config.js      Vite 配置

scripts/
  smoke_test.py       后端核心链路烟测

requirements.txt      Python 依赖
.gitignore            本地数据、依赖和构建产物忽略规则
```

## 本地运行

后端：

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

前端开发：

```bash
cd frontend
npm install
npm run dev -- --host 0.0.0.0 --port 5173
```

生产构建并由 FastAPI 托管：

```bash
cd frontend
npm install
npm run build
cd ..
source .venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

访问：

```text
http://127.0.0.1:8000
```

## 验证

Python 编译检查：

```bash
python3 -m compileall app scripts
```

后端核心链路烟测：

```bash
python3 scripts/smoke_test.py
```

前端生产构建：

```bash
cd frontend
npm install
npm run build
```

烟测会使用临时 SQLite 数据库，不会污染本地正式数据库。烟测覆盖空库初始化、注册、好友申请、同意、私聊、发消息、收藏、搜索和摘要。

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

- `GET /api/users`：获取当前账号的好友列表。
- `GET /api/users/search?q=<username>`：按账号关键字搜索用户，用于发送好友申请。
- `POST /api/friends`：创建好友申请，参数可以是 `targetUserId` 或 `username`。
- `GET /api/friend-requests`：获取当前账号相关的待处理申请。
- `POST /api/friend-requests/accept`：同意好友申请。
- `POST /api/friend-requests/reject`：拒绝好友申请。

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
- `GET /api/search?q=<keyword>`：搜索当前账号可访问的会话和消息，不返回用户结果。
- `POST /api/summary`
- `GET /api/stream`
- `GET /api/health`

## 数据和隐私

- 项目不预置账号、姓名、部门、职位、手机号等人员资料。
- 用户搜索只用于添加好友，并且前端只展示账号级信息。
- 全局搜索只搜索会话和消息，不搜索人员。
- 本地运行数据库默认写入 `data/enterprise_im.db`。
- `data/`、`*.db`、日志、PID、虚拟环境、`node_modules`、`dist` 和截图输出均已加入 `.gitignore`。

## 说明

当前依赖锁定在兼容 Node 18 的 Vite 6。`npm audit` 可能建议升级到更高版本 Vite；如果要升级到新版 Vite，请先升级 Node 到新版 LTS，再调整前端依赖。
