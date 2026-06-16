# Chat Server

一个基于 FastAPI、SQLite、React 和 Vite 的即时通讯项目。项目包含账号注册登录、好友申请、私聊、群聊、消息回执、通知、收藏、审计日志、会话搜索和 AI 摘要等功能，适合作为 IM 系统的学习、课程设计或作品集项目基础。

## 功能

- 账号注册、登录、退出
- 好友申请、同意、拒绝
- 按账号搜索并申请好友
- 好友私聊和群聊
- 群聊创建和群成员添加
- 会话置顶、静音、归档、删除
- 消息发送、编辑、撤回
- 消息幂等发送，支持 `clientId`
- 已读/送达回执和未读数量
- 消息收藏和收藏列表
- 通知中心和全部已读
- 审计日志
- 会话和消息搜索
- 简易 AI 摘要、关键词和待办提取
- SSE 实时更新和在线状态

## 技术栈

后端：

- FastAPI
- SQLite
- Pydantic
- Server-Sent Events

前端：

- React
- Vite
- lucide-react
- CSS Grid/Flex

## 项目结构

```text
app/                  FastAPI 后端
  main.py             API 路由和静态前端托管
  services.py         业务逻辑和数据库访问
  schemas.py          请求模型
  database.py         SQLite 连接
  events.py           SSE 事件中心
frontend/             React/Vite 前端
  src/main.jsx        前端应用入口
  src/styles.css      页面样式
requirements.txt      Python 依赖
.gitignore            本地数据和构建产物忽略规则
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

## API 概览

认证：

- `POST /api/register`
- `POST /api/login`
- `POST /api/logout`
- `GET /api/me`

好友：

- `GET /api/users`
- `GET /api/users/search?q=账号`
- `POST /api/friends`
- `GET /api/friend-requests`
- `POST /api/friend-requests/accept`
- `POST /api/friend-requests/reject`

会话和消息：

- `GET /api/conversations`
- `POST /api/conversations/direct`
- `POST /api/groups`
- `POST /api/groups/members`
- `GET /api/messages`
- `POST /api/messages`
- `PATCH /api/messages`
- `DELETE /api/messages`

其他：

- `GET /api/notifications`
- `POST /api/notifications/read`
- `GET /api/saved-messages`
- `GET /api/audit`
- `GET /api/search`
- `POST /api/summary`
- `GET /api/stream`
- `GET /api/health`

## 数据说明

项目不会预置演示账号或人员资料。首次启动时会自动创建空的 SQLite 数据库，用户需要在页面自行注册账号。

本地运行时数据默认写入 `data/enterprise_im.db`，该目录已加入 `.gitignore`，不会提交到仓库。日志、PID 文件、虚拟环境、前端依赖、构建产物和截图输出也已忽略。

## 验证

```bash
python3 -m compileall app
cd frontend && npm run build
```
