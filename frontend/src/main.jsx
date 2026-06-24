import React, { useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  Archive,
  ArrowRight,
  Bell,
  Bookmark,
  BookmarkCheck,
  CheckCheck,
  Circle,
  Edit3,
  FileText,
  Hash,
  Info,
  Inbox,
  Lock,
  LogOut,
  MessageSquare,
  PanelRight,
  Pin,
  Plus,
  Search,
  Send,
  Settings,
  ShieldCheck,
  Sparkles,
  Star,
  Trash2,
  UserPlus,
  UserRound,
  Users,
  Volume2,
  VolumeX,
  X
} from "lucide-react";
import "./styles.css";

const initialState = {
  token: localStorage.getItem("imToken") || "",
  user: null,
  settings: null,
  users: [],
  conversations: [],
  activeConversationId: null,
  messages: [],
  notifications: { unread: 0, notifications: [] },
  friendRequests: { requests: [], incoming: [], outgoing: [] },
  audit: [],
  saved: [],
  health: null,
  search: { q: "", users: [], conversations: [], messages: [] },
  friendSearch: { q: "", users: [] },
  summary: null
};

function initials(name) {
  return (name || "?").trim().slice(0, 1).toUpperCase();
}

function cn(...items) {
  return items.filter(Boolean).join(" ");
}

function requestPeer(request) {
  return request.direction === "incoming" ? request.requester : request.recipient;
}

function App() {
  const [state, setState] = useState(initialState);
  const [authMode, setAuthMode] = useState("login");
  const [authError, setAuthError] = useState("");
  const [activePanel, setActivePanel] = useState("details");
  const [messageText, setMessageText] = useState("");
  const [modal, setModal] = useState(null);
  const [groupDraft, setGroupDraft] = useState({ name: "", description: "", memberIds: [] });
  const [groupStatus, setGroupStatus] = useState({ loading: false, error: "", success: "" });
  const [friendStatus, setFriendStatus] = useState({ loading: false, error: "", success: "" });
  const [addMemberIds, setAddMemberIds] = useState([]);
  const [addMemberStatus, setAddMemberStatus] = useState({ loading: false, error: "", success: "" });
  const [showArchived, setShowArchived] = useState(false);
  const [editingMessage, setEditingMessage] = useState(null);
  const streamRef = useRef(null);
  const tokenRef = useRef(initialState.token);
  const activeConversationIdRef = useRef(null);

  const activeConversation = useMemo(
    () => state.conversations.find((item) => item.id === state.activeConversationId) || null,
    [state.conversations, state.activeConversationId]
  );

  const api = async (path, options = {}) => {
    const headers = { "Content-Type": "application/json", ...(options.headers || {}) };
    const token = tokenRef.current || state.token;
    if (token) headers.Authorization = `Bearer ${token}`;
    const response = await fetch(path, { ...options, headers });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.detail || data.error || `请求失败 ${response.status}`);
    return data;
  };

  const patchState = (patch) => setState((prev) => ({ ...prev, ...patch }));

  async function bootstrap(token = state.token) {
    if (!token) return;
    tokenRef.current = token;
    const headers = { Authorization: `Bearer ${token}` };
    const [me, users, requests, conversations, notifications, saved, audit, health] = await Promise.all([
      fetch("/api/me", { headers }).then((r) => r.json()),
      fetch("/api/users", { headers }).then((r) => r.json()),
      fetch("/api/friend-requests", { headers }).then((r) => r.json()),
      fetch("/api/conversations", { headers }).then((r) => r.json()),
      fetch("/api/notifications", { headers }).then((r) => r.json()),
      fetch("/api/saved-messages", { headers }).then((r) => r.json()),
      fetch("/api/audit", { headers }).then((r) => r.json()),
      fetch("/api/health").then((r) => r.json())
    ]);
    setState((prev) => ({
      ...prev,
      token,
      user: me.user,
      settings: me.settings,
      users: users.users || [],
      conversations: conversations.conversations || [],
      friendRequests: normalizeRequests(requests),
      notifications,
      saved: saved.messages || [],
      audit: audit.logs || [],
      health
    }));
    openStream(token);
  }

  function normalizeRequests(data = {}) {
    return {
      requests: data.requests || [],
      incoming: data.incoming || [],
      outgoing: data.outgoing || []
    };
  }

  function openStream(token) {
    if (streamRef.current) streamRef.current.close();
    const stream = new EventSource(`/api/stream?token=${encodeURIComponent(token)}`);
    stream.addEventListener("update", async () => {
      try {
        await refreshLight();
        if (activeConversationIdRef.current) await loadMessages(activeConversationIdRef.current);
      } catch {
        // Refresh failures are surfaced by the next foreground action.
      }
    });
    streamRef.current = stream;
  }

  async function refreshLight(options = {}) {
    const includeArchived = options.includeArchived ?? showArchived;
    const [users, requests, conversations, notifications, health] = await Promise.all([
      api("/api/users"),
      api("/api/friend-requests"),
      api(`/api/conversations?includeArchived=${includeArchived ? "true" : "false"}`),
      api("/api/notifications"),
      api("/api/health")
    ]);
    patchState({
      users: users.users || [],
      conversations: conversations.conversations || [],
      friendRequests: normalizeRequests(requests),
      notifications,
      health
    });
  }

  async function loadConversations(options = {}) {
    const includeArchived = options.includeArchived ?? showArchived;
    const data = await api(`/api/conversations?includeArchived=${includeArchived ? "true" : "false"}`);
    patchState({ conversations: data.conversations || [] });
    return data.conversations || [];
  }

  async function loadMessages(conversationId) {
    const data = await api(`/api/messages?conversationId=${conversationId}`);
    const conversations = await api(`/api/conversations?includeArchived=${showArchived ? "true" : "false"}`);
    patchState({ messages: data.messages || [], conversations: conversations.conversations || [], activeConversationId: conversationId });
  }

  async function handleAuth(event) {
    event.preventDefault();
    setAuthError("");
    const form = new FormData(event.currentTarget);
    const payload = {
      username: String(form.get("username") || "").trim(),
      password: String(form.get("password") || "")
    };
    try {
      const data = await fetch(authMode === "login" ? "/api/login" : "/api/register", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      }).then(async (res) => {
        const body = await res.json();
        if (!res.ok) throw new Error(body.detail || "认证失败");
        return body;
      });
      localStorage.setItem("imToken", data.token);
      await bootstrap(data.token);
    } catch (error) {
      setAuthError(error.message);
    }
  }

  async function logout() {
    try {
      await api("/api/logout", { method: "POST", body: "{}" });
    } catch {
      // Local cleanup remains correct if the session is already gone.
    }
    if (streamRef.current) streamRef.current.close();
    localStorage.removeItem("imToken");
    tokenRef.current = "";
    setState({ ...initialState, token: "" });
  }

  function openModal(nextModal) {
    setFriendStatus({ loading: false, error: "", success: "" });
    setGroupStatus({ loading: false, error: "", success: "" });
    setAddMemberStatus({ loading: false, error: "", success: "" });
    setModal(nextModal);
  }

  function closeModal() {
    setModal(null);
    setAddMemberIds([]);
    setFriendStatus({ loading: false, error: "", success: "" });
    setGroupStatus({ loading: false, error: "", success: "" });
    setAddMemberStatus({ loading: false, error: "", success: "" });
  }

  async function startDirect(userId) {
    const data = await api("/api/conversations/direct", { method: "POST", body: JSON.stringify({ targetUserId: userId }) });
    await loadMessages(data.conversation.id);
    setActivePanel("details");
    closeModal();
  }

  async function runFriendSearch(q) {
    setFriendStatus({ loading: false, error: "", success: "" });
    patchState({ friendSearch: { ...state.friendSearch, q } });
    if (!q.trim()) {
      patchState({ friendSearch: { q, users: [] } });
      return;
    }
    const data = await api(`/api/users/search?q=${encodeURIComponent(q)}`);
    patchState({ friendSearch: { q, users: data.users || [] } });
  }

  async function requestFriend(targetUserId) {
    setFriendStatus({ loading: true, error: "", success: "" });
    try {
      await api("/api/friends", { method: "POST", body: JSON.stringify({ targetUserId }) });
      const [friends, requests] = await Promise.all([api("/api/users"), api("/api/friend-requests")]);
      patchState({
        users: friends.users || [],
        friendRequests: normalizeRequests(requests),
        friendSearch: {
          ...state.friendSearch,
          users: state.friendSearch.users.map((item) => (
            item.id === targetUserId ? { ...item, friendshipStatus: "pending_outgoing", friend: false } : item
          ))
        }
      });
      setFriendStatus({ loading: false, error: "", success: "好友申请已发送" });
    } catch (error) {
      setFriendStatus({ loading: false, error: error.message, success: "" });
    }
  }

  async function acceptFriendRequest(requestId) {
    await api("/api/friend-requests/accept", { method: "POST", body: JSON.stringify({ requestId }) });
    await refreshLight();
  }

  async function rejectFriendRequest(requestId) {
    await api("/api/friend-requests/reject", { method: "POST", body: JSON.stringify({ requestId }) });
    await refreshLight();
  }

  async function createGroup(event) {
    event.preventDefault();
    setGroupStatus({ loading: false, error: "", success: "" });
    if (!groupDraft.name.trim()) {
      setGroupStatus({ loading: false, error: "请输入群名称", success: "" });
      return;
    }
    if (groupDraft.memberIds.length === 0) {
      setGroupStatus({ loading: false, error: "请至少选择一名成员", success: "" });
      return;
    }
    setGroupStatus({ loading: true, error: "", success: "" });
    try {
      const data = await api("/api/groups", { method: "POST", body: JSON.stringify(groupDraft) });
      setGroupDraft({ name: "", description: "", memberIds: [] });
      setGroupStatus({ loading: false, error: "", success: "" });
      await loadConversations({ includeArchived: showArchived });
      await loadMessages(data.conversation.id);
      setActivePanel("details");
      closeModal();
    } catch (error) {
      setGroupStatus({ loading: false, error: error.message, success: "" });
    }
  }

  async function sendMessage(event) {
    event.preventDefault();
    if (!activeConversation || !messageText.trim()) return;
    const body = {
      conversationId: activeConversation.id,
      content: messageText.trim(),
      clientId: `web-${Date.now()}-${Math.random().toString(16).slice(2)}`
    };
    setMessageText("");
    await api("/api/messages", { method: "POST", body: JSON.stringify(body) });
    await loadMessages(activeConversation.id);
  }

  async function updatePreference(patch) {
    if (!activeConversation) return;
    await api("/api/conversations/preferences", {
      method: "POST",
      body: JSON.stringify({ conversationId: activeConversation.id, ...patch })
    });
    const conversations = await loadConversations({ includeArchived: showArchived });
    if (!conversations.some((item) => item.id === activeConversation.id)) {
      patchState({ activeConversationId: null, messages: [], summary: null });
    }
  }

  async function closeConversation() {
    patchState({ activeConversationId: null, messages: [], summary: null });
  }

  async function deleteConversation() {
    if (!activeConversation) return;
    if (activeConversation.type === "group" && activeConversation.ownerId === state.user.id) {
      await dissolveConversation();
      return;
    }
    await leaveConversation();
  }

  async function leaveConversation() {
    if (!activeConversation) return;
    const label = activeConversation.type === "group" ? "离开该群聊？" : "删除该私聊？";
    if (!window.confirm(label)) return;
    await api("/api/conversations/leave", {
      method: "POST",
      body: JSON.stringify({ conversationId: activeConversation.id })
    });
    patchState({ activeConversationId: null, messages: [], summary: null });
    await loadConversations({ includeArchived: showArchived });
  }

  async function dissolveConversation() {
    if (!activeConversation) return;
    if (!window.confirm(`确认解散「${activeConversation.title}」？该操作会删除所有成员的群聊。`)) return;
    await api("/api/conversations", {
      method: "DELETE",
      body: JSON.stringify({ conversationId: activeConversation.id })
    });
    patchState({ activeConversationId: null, messages: [], summary: null });
    await loadConversations({ includeArchived: showArchived });
  }

  async function toggleArchivedView() {
    const next = !showArchived;
    setShowArchived(next);
    const conversations = await loadConversations({ includeArchived: next });
    if (state.activeConversationId && !conversations.some((item) => item.id === state.activeConversationId)) {
      patchState({ activeConversationId: null, messages: [], summary: null });
    }
  }

  async function toggleSave(message) {
    if (message.saved) {
      await api(`/api/messages/save/${message.id}`, { method: "DELETE" });
    } else {
      await api("/api/messages/save", { method: "POST", body: JSON.stringify({ messageId: message.id }) });
    }
    const [messages, saved] = await Promise.all([
      api(`/api/messages?conversationId=${message.conversationId}`),
      api("/api/saved-messages")
    ]);
    patchState({ messages: messages.messages || [], saved: saved.messages || [] });
  }

  async function editMessage(message, nextContent) {
    await api("/api/messages", { method: "PATCH", body: JSON.stringify({ messageId: message.id, content: nextContent }) });
    await loadMessages(message.conversationId);
    setEditingMessage(null);
  }

  async function deleteMessage(message) {
    await api("/api/messages", { method: "DELETE", body: JSON.stringify({ messageId: message.id }) });
    await loadMessages(message.conversationId);
  }

  async function generateSummary() {
    if (!activeConversation) return;
    const data = await api("/api/summary", { method: "POST", body: JSON.stringify({ conversationId: activeConversation.id }) });
    patchState({ summary: data.summary });
    setActivePanel("summary");
  }

  async function runSearch(q) {
    patchState({ search: { ...state.search, q } });
    if (!q.trim()) {
      patchState({ search: { q, users: [], conversations: [], messages: [] } });
      return;
    }
    const data = await api(`/api/search?q=${encodeURIComponent(q)}`);
    patchState({ search: { q, users: [], conversations: data.conversations || [], messages: data.messages || [] } });
  }

  async function markNotificationsRead() {
    const data = await api("/api/notifications/read", { method: "POST", body: JSON.stringify({ notificationIds: null }) });
    patchState({ notifications: data });
  }

  async function addMembers(memberIds) {
    if (!activeConversation || activeConversation.type !== "group") return;
    setAddMemberStatus({ loading: false, error: "", success: "" });
    if (!memberIds.length) {
      setAddMemberStatus({ loading: false, error: "请选择要添加的成员", success: "" });
      return;
    }
    setAddMemberStatus({ loading: true, error: "", success: "" });
    try {
      await api("/api/groups/members", {
        method: "POST",
        body: JSON.stringify({ conversationId: activeConversation.id, memberIds })
      });
      setAddMemberIds([]);
      setAddMemberStatus({ loading: false, error: "", success: "" });
      await loadMessages(activeConversation.id);
      closeModal();
    } catch (error) {
      setAddMemberStatus({ loading: false, error: error.message, success: "" });
    }
  }

  useEffect(() => {
    if (state.token) {
      bootstrap(state.token).catch(() => {
        localStorage.removeItem("imToken");
        patchState({ token: "" });
      });
    }
    return () => streamRef.current?.close();
  }, []);

  useEffect(() => {
    activeConversationIdRef.current = state.activeConversationId;
    setAddMemberIds([]);
    setAddMemberStatus({ loading: false, error: "", success: "" });
  }, [state.activeConversationId]);

  if (!state.token || !state.user) {
    return (
      <AuthScreen
        authMode={authMode}
        setAuthMode={setAuthMode}
        authError={authError}
        handleAuth={handleAuth}
      />
    );
  }

  return (
    <div className="app-shell">
      <WorkspaceRail user={state.user} logout={logout} notifications={state.notifications} activePanel={activePanel} setActivePanel={setActivePanel} />
      <Sidebar
        state={state}
        activeConversation={activeConversation}
        loadMessages={loadMessages}
        startDirect={startDirect}
        runSearch={runSearch}
        acceptFriendRequest={acceptFriendRequest}
        rejectFriendRequest={rejectFriendRequest}
        showArchived={showArchived}
        toggleArchivedView={toggleArchivedView}
        openModal={openModal}
      />
      <main className={cn("conversation-stage", !activeConversation && "no-conversation")}>
        <ChatHeader
          currentUser={state.user}
          conversation={activeConversation}
          updatePreference={updatePreference}
          generateSummary={generateSummary}
          closeConversation={closeConversation}
          deleteConversation={deleteConversation}
          leaveConversation={leaveConversation}
          dissolveConversation={dissolveConversation}
        />
        <MessageList
          conversation={activeConversation}
          user={state.user}
          messages={state.messages}
          toggleSave={toggleSave}
          editMessage={editMessage}
          deleteMessage={deleteMessage}
          editingMessage={editingMessage}
          setEditingMessage={setEditingMessage}
        />
        <form className="composer" onSubmit={sendMessage}>
          <input value={messageText} onChange={(e) => setMessageText(e.target.value)} disabled={!activeConversation} placeholder={activeConversation ? "输入消息，Enter 发送" : "先选择一个会话"} />
          <button className="send-button" disabled={!activeConversation || !messageText.trim()}>
            <Send size={18} />
            发送
          </button>
        </form>
      </main>
      <Inspector
        activePanel={activePanel}
        setActivePanel={setActivePanel}
        state={state}
        activeConversation={activeConversation}
        markNotificationsRead={markNotificationsRead}
        openModal={openModal}
      />
      {modal === "friend" && (
        <AddFriendModal
          state={state}
          closeModal={closeModal}
          runFriendSearch={runFriendSearch}
          requestFriend={requestFriend}
          startDirect={startDirect}
          friendStatus={friendStatus}
        />
      )}
      {modal === "group" && (
        <CreateGroupModal
          contacts={state.users}
          groupDraft={groupDraft}
          setGroupDraft={setGroupDraft}
          createGroup={createGroup}
          groupStatus={groupStatus}
          closeModal={closeModal}
        />
      )}
      {modal === "members" && activeConversation && (
        <AddMembersModal
          conversation={activeConversation}
          users={state.users}
          currentUser={state.user}
          addMemberIds={addMemberIds}
          setAddMemberIds={setAddMemberIds}
          addMemberStatus={addMemberStatus}
          addMembers={addMembers}
          closeModal={closeModal}
        />
      )}
    </div>
  );
}

function AuthScreen({ authMode, setAuthMode, authError, handleAuth }) {
  return (
    <div className="auth-screen">
      <section className="auth-hero">
        <div className="brand-mark">
          <Sparkles size={22} />
          AI-Native IM
        </div>
        <h1>即时通讯工作台</h1>
        <p>连接团队、处理会话、管理消息和通知，让日常沟通保持清晰有序。</p>
        <div className="auth-illustration" aria-hidden="true">
          <div className="illustration-card card-a">
            <span />
            <strong />
          </div>
          <div className="illustration-card card-b">
            <span />
            <strong />
          </div>
          <div className="illustration-bubble">
            <i />
            <i />
            <i />
          </div>
        </div>
      </section>
      <form className="auth-card" onSubmit={handleAuth}>
        <div className="auth-tabs">
          <button type="button" className={authMode === "login" ? "active" : ""} onClick={() => setAuthMode("login")}>登录</button>
          <button type="button" className={authMode === "register" ? "active" : ""} onClick={() => setAuthMode("register")}>注册</button>
        </div>
        <label>
          <span className="field-label">账号</span>
          <span className="input-shell">
            <UserRound size={20} />
            <input name="username" autoComplete="username" minLength={3} placeholder="请输入账号" required />
          </span>
        </label>
        <label>
          <span className="field-label">密码</span>
          <span className="input-shell">
            <Lock size={20} />
            <input name="password" type="password" autoComplete={authMode === "login" ? "current-password" : "new-password"} minLength={6} placeholder="请输入密码" required />
          </span>
        </label>
        <button className="primary-action auth-submit">
          {authMode === "login" ? "进入工作台" : "创建账号"}
          <ArrowRight size={19} />
        </button>
        {authError && <p className="form-error">{authError}</p>}
      </form>
    </div>
  );
}

function WorkspaceRail({ user, logout, notifications, activePanel, setActivePanel }) {
  const nav = [
    ["details", MessageSquare, "详情"],
    ["notifications", Bell, "通知"],
    ["saved", Bookmark, "收藏"],
    ["audit", ShieldCheck, "审计"],
    ["settings", Settings, "设置"]
  ];
  return (
    <aside className="workspace-rail">
      <div className="rail-logo">AI</div>
      <nav>
        {nav.map(([id, Icon, label]) => (
          <button key={id} className={activePanel === id ? "active" : ""} onClick={() => setActivePanel(id)} title={label}>
            <Icon size={20} />
            {id === "notifications" && notifications.unread > 0 && <span className="badge-dot">{notifications.unread}</span>}
          </button>
        ))}
      </nav>
      <button type="button" className="rail-avatar" title={user.username}>{initials(user.username)}</button>
      <button type="button" className="rail-logout" onClick={logout} title="退出登录"><LogOut size={18} /></button>
    </aside>
  );
}

function Sidebar({
  state,
  activeConversation,
  loadMessages,
  startDirect,
  runSearch,
  acceptFriendRequest,
  rejectFriendRequest,
  showArchived,
  toggleArchivedView,
  openModal
}) {
  const contacts = state.users.filter((u) => u.id !== state.user.id);
  const incomingCount = state.friendRequests.incoming.length;
  return (
    <aside className="sidebar">
      <div className="sidebar-top">
        <div className="sidebar-header">
          <div>
            <p>工作区</p>
            <h2>AI-Native IM</h2>
          </div>
          <button type="button" className="icon-soft" onClick={toggleArchivedView}>
            <Archive size={18} />
            {showArchived ? "隐藏归档" : "全部"}
          </button>
        </div>
        <div className="sidebar-actions">
          <button type="button" className="secondary-action compact-action" onClick={() => openModal("friend")}>
            <UserPlus size={16} />
            添加好友
          </button>
          <button type="button" className="secondary-action compact-action" onClick={() => openModal("group")}>
            <Users size={16} />
            创建群聊
          </button>
        </div>
        <div className="search-box">
          <Search size={16} />
          <input value={state.search.q} onChange={(e) => runSearch(e.target.value)} placeholder="搜索会话或消息" />
        </div>
        {state.search.q && (
          <div className="search-results">
            <strong>搜索结果</strong>
            {state.search.conversations.slice(0, 4).map((item) => (
              <button type="button" key={`conversation-${item.id}`} onClick={() => loadMessages(item.id)}>
                {item.title}
              </button>
            ))}
            {state.search.messages.slice(0, 4).map((message) => (
              <button type="button" key={message.id} onClick={() => loadMessages(message.conversationId)}>{message.content}</button>
            ))}
            {!state.search.conversations.length && !state.search.messages.length && <p className="muted compact-copy">没有匹配结果。</p>}
          </div>
        )}
      </div>
      <div className="sidebar-scroll">
        <section className="section conversation-section">
          <div className="section-title">
            <span>{showArchived ? "全部会话" : "会话"}</span>
            <small>{state.conversations.length}</small>
          </div>
          <div className="conversation-list">
            {state.conversations.map((conversation) => (
              <button key={conversation.id} className={cn("conversation-item", activeConversation?.id === conversation.id && "active", conversation.archived && "archived")} onClick={() => loadMessages(conversation.id)}>
                <div className="conversation-avatar">{conversation.type === "group" ? <Hash size={16} /> : initials(conversation.title)}</div>
                <div>
                  <strong>{conversation.title}</strong>
                  <span>{conversation.archived ? "已归档 · " : ""}{conversation.lastMessage?.content || conversation.description || "暂无消息"}</span>
                </div>
                <div className="conversation-state">
                  {conversation.pinned && <Pin size={14} />}
                  {conversation.muted && <VolumeX size={14} />}
                  {conversation.unread > 0 && <em>{conversation.unread}</em>}
                </div>
              </button>
            ))}
            {!state.conversations.length && <p className="muted compact-copy">暂无会话。</p>}
          </div>
        </section>
        <section className="section contacts-section">
          <div className="section-title"><span>好友</span><small>{contacts.length} 位</small></div>
          <div className="contacts-list">
            {contacts.length ? contacts.map((user) => (
              <div key={user.id} className="contact-row">
                <Avatar user={user} />
                <div><strong>{user.username}</strong><small>{user.online ? "在线" : "离线"}</small></div>
                <button type="button" className="contact-action" onClick={() => startDirect(user.id)}>
                  <MessageSquare size={14} />
                  聊天
                </button>
                <Circle className={user.online ? "online" : ""} size={9} fill="currentColor" />
              </div>
            )) : (
              <p className="muted compact-copy">点击“添加好友”按账号发送申请。</p>
            )}
          </div>
        </section>
        <section className="section requests-section">
          <div className="section-title"><span>好友申请</span><small>{incomingCount} 条</small></div>
          <div className="request-list">
            {state.friendRequests.incoming.length ? state.friendRequests.incoming.map((request) => {
              const peer = requestPeer(request);
              return (
                <div key={request.id} className="request-row">
                  <Avatar user={peer} />
                  <div><strong>{peer?.username}</strong><small>{request.createdAt}</small></div>
                  <button type="button" onClick={() => acceptFriendRequest(request.id)}>同意</button>
                  <button type="button" onClick={() => rejectFriendRequest(request.id)}>拒绝</button>
                </div>
              );
            }) : (
              <p className="muted compact-copy">暂无待处理申请。</p>
            )}
          </div>
        </section>
      </div>
    </aside>
  );
}

function ChatHeader({ currentUser, conversation, updatePreference, generateSummary, closeConversation, deleteConversation, leaveConversation, dissolveConversation }) {
  if (!conversation) {
    return (
      <header className="chat-header empty">
        <div>
          <p>未选择会话</p>
          <h2>选择左侧会话开始协作</h2>
        </div>
      </header>
    );
  }
  return (
    <header className="chat-header">
      <div>
        <p>{conversation.type === "group" ? "群聊" : "私聊"} · {conversation.participants.length} 名成员</p>
        <h2>{conversation.title}</h2>
      </div>
      <div className="header-actions">
        <button type="button" onClick={closeConversation}><PanelRight size={17} /> 关闭</button>
        <button type="button" onClick={() => updatePreference({ pinned: !conversation.pinned })}><Pin size={17} /> {conversation.pinned ? "取消置顶" : "置顶"}</button>
        <button type="button" onClick={() => updatePreference({ muted: !conversation.muted })}>{conversation.muted ? <Volume2 size={17} /> : <VolumeX size={17} />} {conversation.muted ? "取消静音" : "静音"}</button>
        <button type="button" onClick={() => updatePreference({ archived: true })}><Archive size={17} /> 归档</button>
        {conversation.type === "direct" && <button type="button" className="danger-soft" onClick={deleteConversation}><Trash2 size={17} /> 删除</button>}
        {conversation.type === "group" && conversation.ownerId !== currentUser.id && <button type="button" className="danger-soft" onClick={leaveConversation}><LogOut size={17} /> 离开</button>}
        {conversation.type === "group" && conversation.ownerId === currentUser.id && <button type="button" className="danger-soft" onClick={dissolveConversation}><Trash2 size={17} /> 解散</button>}
        <button type="button" className="primary-inline" onClick={generateSummary}><Sparkles size={17} /> AI 摘要</button>
      </div>
    </header>
  );
}

function MessageList({ conversation, user, messages, toggleSave, editMessage, deleteMessage, editingMessage, setEditingMessage }) {
  const [editText, setEditText] = useState("");
  if (!conversation) {
    return (
      <div className="message-list empty-state">
        <EmptyChatVisual />
        <h3>未选择会话</h3>
        <p>从左侧会话或好友发起聊天。</p>
      </div>
    );
  }
  if (!messages.length) {
    return (
      <div className="message-list empty-state">
        <EmptyChatVisual />
        <h3>暂无消息</h3>
        <p>发送第一条消息。</p>
      </div>
    );
  }
  return (
    <div className="message-list">
      {messages.map((message) => {
        const own = message.senderId === user.id;
        const editing = editingMessage === message.id;
        return (
          <article key={message.id} className={cn("message", own && "own", message.kind === "system" && "system")}>
            <Avatar user={{ username: message.senderUsername, displayName: message.senderUsername, avatarColor: message.senderColor }} />
            <div className="message-main">
              <div className="message-meta">
                <strong>{message.senderUsername}</strong>
                <span>{message.createdAt}</span>
                {message.editedAt && <span>已编辑</span>}
                <span className="receipt"><CheckCheck size={13} /> {message.receipts?.read || 0}/{Object.values(message.receipts || {}).reduce((a, b) => a + b, 0)}</span>
              </div>
              {editing ? (
                <form className="edit-box" onSubmit={(e) => { e.preventDefault(); editMessage(message, editText); }}>
                  <input value={editText} onChange={(e) => setEditText(e.target.value)} autoFocus />
                  <button type="submit">保存</button>
                  <button type="button" onClick={() => setEditingMessage(null)}>取消</button>
                </form>
              ) : (
                <p>{message.content}</p>
              )}
              {message.status !== "deleted" && (
                <div className="message-actions">
                  <button type="button" onClick={() => toggleSave(message)}>{message.saved ? <BookmarkCheck size={15} /> : <Bookmark size={15} />} {message.saved ? "已收藏" : "收藏"}</button>
                  {own && <button type="button" onClick={() => { setEditText(message.content); setEditingMessage(message.id); }}><Edit3 size={15} /> 编辑</button>}
                  {own && <button type="button" onClick={() => deleteMessage(message)}><Trash2 size={15} /> 撤回</button>}
                </div>
              )}
            </div>
          </article>
        );
      })}
    </div>
  );
}

function Inspector({ activePanel, setActivePanel, state, activeConversation, markNotificationsRead, openModal }) {
  const tabs = [
    ["details", "详情", Info],
    ["notifications", "通知", Bell],
    ["summary", "摘要", FileText],
    ["saved", "收藏", Star],
    ["audit", "审计", ShieldCheck],
    ["settings", "设置", Settings]
  ];
  return (
    <aside className="inspector">
      <div className="inspector-tabs">
        {tabs.map(([id, label, Icon]) => (
          <button type="button" key={id} className={activePanel === id ? "active" : ""} onClick={() => setActivePanel(id)}>
            <Icon size={18} />
            {label}
          </button>
        ))}
      </div>
      {activePanel === "details" && (
        <DetailsPanel
          conversation={activeConversation}
          currentUser={state.user}
          health={state.health}
          openModal={openModal}
        />
      )}
      {activePanel === "notifications" && <NotificationsPanel data={state.notifications} markRead={markNotificationsRead} />}
      {activePanel === "summary" && <SummaryPanel summary={state.summary} />}
      {activePanel === "saved" && <SavedPanel messages={state.saved} />}
      {activePanel === "audit" && <AuditPanel logs={state.audit} />}
      {activePanel === "settings" && <SettingsPanel user={state.user} settings={state.settings} />}
    </aside>
  );
}

function DetailsPanel({ conversation, currentUser, health, openModal }) {
  const metricIcons = {
    auditLogs: FileText,
    conversations: MessageSquare,
    friendships: Users,
    messages: Bell,
    notifications: Bell,
    online: Circle,
    users: UserRound
  };
  return (
    <div className="panel-content">
      <h3>会话详情</h3>
      {conversation ? (
        <>
          {conversation.description && <p className="muted">{conversation.description}</p>}
          <div className="member-list">
            {conversation.participants.map((member) => (
              <div key={member.id} className="member-row">
                <Avatar user={member} />
                <div><strong>{member.username}</strong><span>{member.online ? "在线" : "离线"}</span></div>
              </div>
            ))}
          </div>
          {conversation.type === "group" && (
            <button type="button" className="secondary-action full-width" onClick={() => openModal("members")}>
              <Plus size={16} />
              添加成员
            </button>
          )}
        </>
      ) : <p className="muted">选择会话后查看成员。</p>}
      <h3>运行指标</h3>
      <div className="metric-grid">
        {Object.entries(health?.metrics || {}).map(([key, value]) => {
          const MetricIcon = metricIcons[key] || Info;
          return (
            <div key={key} className="metric-card">
              <span className={cn("metric-icon", key === "online" && "success")}>
                <MetricIcon size={21} fill={key === "online" ? "currentColor" : "none"} />
              </span>
              <strong>{value}</strong>
              <small>{key}</small>
            </div>
          );
        })}
      </div>
      <h3>当前账号</h3>
      <div className="account-card">
        <Avatar user={currentUser} />
        <div>
          <strong>{currentUser.username}</strong>
          <span>{currentUser.online ? "在线" : "离线"}</span>
        </div>
      </div>
    </div>
  );
}

function NotificationsPanel({ data, markRead }) {
  return (
    <div className="panel-content">
      <div className="panel-heading"><h3>通知中心</h3><button type="button" onClick={markRead}>全部已读</button></div>
      {data.notifications.map((item) => (
        <div key={item.id} className={cn("notice", !item.read && "unread")}>
          <strong>{item.title}</strong>
          <p>{item.body}</p>
          <span>{item.createdAt}</span>
        </div>
      ))}
      {!data.notifications.length && <p className="muted">暂无通知。</p>}
    </div>
  );
}

function SummaryPanel({ summary }) {
  return (
    <div className="panel-content">
      <h3>AI 摘要</h3>
      {summary ? (
        <>
          <p className="summary-text">{summary.summary}</p>
          <div className="tag-row">{summary.keywords.map((word) => <span key={word}>{word}</span>)}</div>
          <h4>待办</h4>
          {summary.actionItems.map((item) => <p key={item} className="task-item">{item}</p>)}
        </>
      ) : <p className="muted">点击聊天区右上角“AI 摘要”生成。</p>}
    </div>
  );
}

function SavedPanel({ messages }) {
  return (
    <div className="panel-content">
      <h3>收藏消息</h3>
      {messages.map((message) => <div key={message.id} className="saved-item"><strong>{message.senderUsername}</strong><p>{message.content}</p></div>)}
      {!messages.length && <p className="muted">暂无收藏消息。</p>}
    </div>
  );
}

function AuditPanel({ logs }) {
  return (
    <div className="panel-content">
      <h3>审计日志</h3>
      {logs.map((log) => (
        <div key={log.id} className="audit-row">
          <span>{log.createdAt}</span>
          <strong>{log.actorName}</strong>
          <p>{log.action}</p>
        </div>
      ))}
      {!logs.length && <p className="muted">暂无审计日志。</p>}
    </div>
  );
}

function SettingsPanel({ user, settings }) {
  return (
    <div className="panel-content">
      <h3>账号</h3>
      <div className="account-card">
        <Avatar user={user} />
        <div>
          <strong>{user.username}</strong>
          <span>账号信息仅用于登录和会话识别</span>
        </div>
      </div>
      <h3>偏好</h3>
      <div className="preference-list">
        <span>主题：{settings?.theme}</span>
        <span>密度：{settings?.density}</span>
        <span>桌面通知：{settings?.notifyDesktop ? "开启" : "关闭"}</span>
      </div>
    </div>
  );
}

function AddFriendModal({ state, closeModal, runFriendSearch, requestFriend, startDirect, friendStatus }) {
  return (
    <Modal title="添加好友" closeModal={closeModal}>
      <div className="modal-search-row">
        <div className="search-box modal-search">
          <Search size={18} />
          <input value={state.friendSearch.q} onChange={(e) => runFriendSearch(e.target.value)} placeholder="输入账号搜索" autoFocus />
        </div>
        <button type="button" className="primary-action modal-search-button" onClick={() => runFriendSearch(state.friendSearch.q)}>
          搜索
        </button>
      </div>
      <p className="muted compact-copy">输入账号后搜索并发送好友申请。</p>
      <div className="modal-list-heading"><strong>{state.friendSearch.q ? "搜索结果" : "好友搜索"}</strong></div>
      <div className="friend-results modal-list">
        {state.friendSearch.q && state.friendSearch.users.length ? state.friendSearch.users.map((user) => (
          <div key={user.id} className="friend-result-row">
            <Avatar user={user} />
            <div><strong>{user.username}</strong><small>{friendshipLabel(user)}</small></div>
            {user.friendshipStatus === "accepted" ? (
              <button type="button" className="contact-action" onClick={() => startDirect(user.id)}>聊天</button>
            ) : user.friendshipStatus === "pending_outgoing" ? (
              <button type="button" className="contact-action" disabled>已申请</button>
            ) : user.friendshipStatus === "pending_incoming" ? (
              <button type="button" className="contact-action" disabled>待同意</button>
            ) : (
              <button type="button" className="contact-action" disabled={friendStatus.loading} onClick={() => requestFriend(user.id)}>申请</button>
            )}
          </div>
        )) : (
          <div className="friend-empty-state">
            <Search size={34} />
            <strong>{state.friendSearch.q ? "没有找到账号" : "输入账号开始搜索"}</strong>
            <p>{state.friendSearch.q ? "请确认账号是否正确。" : "搜索结果会显示在这里，可直接发送好友申请。"}</p>
          </div>
        )}
      </div>
      {state.friendRequests.outgoing.length > 0 && (
        <div className="pending-block">
          <strong>已发送申请</strong>
          {state.friendRequests.outgoing.map((request) => {
            const peer = requestPeer(request);
            return <span key={request.id}>{peer?.username}</span>;
          })}
        </div>
      )}
      {friendStatus.error && <p className="inline-error">{friendStatus.error}</p>}
      {friendStatus.success && <p className="inline-success">{friendStatus.success}</p>}
    </Modal>
  );
}

function friendshipLabel(user) {
  if (user.friendshipStatus === "accepted") return "已是好友";
  if (user.friendshipStatus === "pending_outgoing") return "申请待通过";
  if (user.friendshipStatus === "pending_incoming") return "对方已申请";
  return user.online ? "在线" : "离线";
}

function CreateGroupModal({ contacts, groupDraft, setGroupDraft, createGroup, groupStatus, closeModal }) {
  const toggleMember = (id) => {
    setGroupDraft((draft) => ({
      ...draft,
      memberIds: draft.memberIds.includes(id) ? draft.memberIds.filter((item) => item !== id) : [...draft.memberIds, id]
    }));
  };
  return (
    <Modal title="创建群聊" closeModal={closeModal}>
      <form className="modal-form" onSubmit={createGroup}>
        <input value={groupDraft.name} onChange={(e) => setGroupDraft({ ...groupDraft, name: e.target.value })} placeholder="群名称" autoFocus />
        <input value={groupDraft.description} onChange={(e) => setGroupDraft({ ...groupDraft, description: e.target.value })} placeholder="群描述" />
        <div className="member-picker">
          {contacts.map((user) => (
            <button type="button" key={user.id} className={groupDraft.memberIds.includes(user.id) ? "picked" : ""} onClick={() => toggleMember(user.id)}>
              {user.username}
            </button>
          ))}
        </div>
        {!contacts.length && <p className="muted compact-copy">需要先添加好友。</p>}
        {groupStatus.error && <p className="inline-error">{groupStatus.error}</p>}
        <button type="submit" className="primary-action" disabled={groupStatus.loading || !groupDraft.name.trim() || groupDraft.memberIds.length === 0}>
          <Users size={16} />
          {groupStatus.loading ? "创建中" : "创建群聊"}
        </button>
      </form>
    </Modal>
  );
}

function AddMembersModal({ conversation, users, currentUser, addMemberIds, setAddMemberIds, addMemberStatus, addMembers, closeModal }) {
  const participantIds = new Set((conversation?.participants || []).map((member) => member.id));
  const availableMembers = users.filter((user) => user.id !== currentUser.id && !participantIds.has(user.id));
  const toggleMember = (id) => {
    setAddMemberIds((items) => (items.includes(id) ? items.filter((item) => item !== id) : [...items, id]));
  };
  return (
    <Modal title="添加成员" closeModal={closeModal}>
      <form
        className="modal-form"
        onSubmit={(event) => {
          event.preventDefault();
          addMembers(addMemberIds);
        }}
      >
        {availableMembers.length ? (
          <div className="member-picker modal-picker">
            {availableMembers.map((user) => (
              <button type="button" key={user.id} className={addMemberIds.includes(user.id) ? "picked" : ""} onClick={() => toggleMember(user.id)}>
                {user.username}
              </button>
            ))}
          </div>
        ) : (
          <p className="muted compact-copy">所有好友已在群聊中。</p>
        )}
        {addMemberStatus.error && <p className="inline-error">{addMemberStatus.error}</p>}
        <button type="submit" className="primary-action" disabled={addMemberStatus.loading || addMemberIds.length === 0}>
          <Plus size={16} />
          {addMemberStatus.loading ? "添加中" : "添加成员"}
        </button>
      </form>
    </Modal>
  );
}

function Modal({ title, closeModal, children }) {
  return (
    <div className="modal-backdrop" role="presentation" onMouseDown={closeModal}>
      <section className="modal-panel" role="dialog" aria-modal="true" aria-label={title} onMouseDown={(event) => event.stopPropagation()}>
        <div className="modal-header">
          <h3>{title}</h3>
          <button type="button" className="icon-button" onClick={closeModal} title="关闭">
            <X size={18} />
          </button>
        </div>
        {children}
      </section>
    </div>
  );
}

function Avatar({ user }) {
  return <span className="avatar" style={{ "--avatar": user?.avatarColor || "#002FA7" }}>{initials(user?.username || user?.displayName)}</span>;
}

function EmptyChatVisual() {
  return (
    <div className="empty-visual" aria-hidden="true">
      <div className="empty-visual-base" />
      <div className="empty-visual-bubble">
        <span />
        <span />
        <span />
      </div>
    </div>
  );
}

createRoot(document.getElementById("root")).render(<App />);
