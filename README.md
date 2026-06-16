# Chat Server

Chat Server 是一个可直接运行的即时通讯项目，后端使用 FastAPI + SQLite，前端使用 React + Vite。项目重点实现 IM 系统的核心业务闭环：账号注册、好友申请审批、私聊群聊、消息回执、收藏、通知、审计、搜索和摘要。

项目不会内置账号或人员资料。首次启动会创建空数据库，用户需要自行注册账号。

## 在线业务流程

1. 用户注册账号并登录。
2. 在“添加好友”弹窗中按账号搜索用户。
3. 发送好友申请，等待对方同意。
4. 对方在“好友申请”区域点击同意或拒绝。
5. 只有成为好友后才能打开私聊。
6. 好友可以被加入群聊。
7. 消息发送后会生成送达/已读回执、通知和审计记录。
8. 会话和消息可以搜索，消息可以收藏，聊天内容可以生成摘要。

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
- 只有好友可以建立私聊或被加入群聊

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
- `clientId` 幂等发送，避免重试导致重复消息
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

烟测会使用临时 SQLite 数据库，不会污染本地正式数据库。

## API 概览

认证：

- `POST /api/register`
- `POST /api/login`
- `POST /api/logout`
- `GET /api/me`
- `GET /api/settings`
- `PATCH /api/settings`

好友：

- `GET /api/users`
- `GET /api/users/search?q=账号`
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

- `GET /api/messages`
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
- `GET /api/search`
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
