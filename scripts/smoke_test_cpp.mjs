const base = process.env.BASE_URL || "http://127.0.0.1:8000";

async function request(path, options = {}) {
  const response = await fetch(`${base}${path}`, options);
  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(`${options.method || "GET"} ${path} -> ${response.status}: ${body.detail || JSON.stringify(body)}`);
  }
  return body;
}

function authHeaders(token) {
  return {
    "Content-Type": "application/json",
    Authorization: `Bearer ${token}`
  };
}

const suffix = Date.now();
const password = "secret123";
const aliceName = `alice${suffix}`;
const bobName = `bob${suffix}`;

const alice = await request("/api/register", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ username: aliceName, password })
});
const bob = await request("/api/register", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ username: bobName, password })
});

const aliceHeaders = authHeaders(alice.token);
const bobHeaders = authHeaders(bob.token);

const userSearch = await request(`/api/users/search?q=${encodeURIComponent(bobName)}`, { headers: aliceHeaders });
if (!userSearch.users.length) throw new Error("user search did not return bob");

const targetUserId = userSearch.users[0].id;
const friendRequest = await request("/api/friends", {
  method: "POST",
  headers: aliceHeaders,
  body: JSON.stringify({ targetUserId })
});

await request("/api/friend-requests/accept", {
  method: "POST",
  headers: bobHeaders,
  body: JSON.stringify({ requestId: friendRequest.request.id })
});

const direct = await request("/api/conversations/direct", {
  method: "POST",
  headers: aliceHeaders,
  body: JSON.stringify({ targetUserId })
});

const conversationId = direct.conversation.id;
const sent = await request("/api/messages", {
  method: "POST",
  headers: aliceHeaders,
  body: JSON.stringify({
    conversationId,
    content: "准备验证 C++ 后端消息链路",
    clientId: `smoke-${suffix}`
  })
});

await request("/api/messages/save", {
  method: "POST",
  headers: aliceHeaders,
  body: JSON.stringify({ messageId: sent.message.id })
});

const messages = await request(`/api/messages?conversationId=${conversationId}`, { headers: aliceHeaders });
const saved = await request("/api/saved-messages", { headers: aliceHeaders });
const found = await request("/api/search?q=C%2B%2B", { headers: aliceHeaders });
const summary = await request("/api/summary", {
  method: "POST",
  headers: aliceHeaders,
  body: JSON.stringify({ conversationId })
});
const health = await request("/api/health");

const result = {
  service: health.service,
  users: [alice.user.username, bob.user.username],
  friendRequestId: friendRequest.request.id,
  conversationId,
  messageId: sent.message.id,
  messageCount: messages.messages.length,
  savedCount: saved.messages.length,
  searchMessages: found.messages.length,
  summaryMessageCount: summary.summary.messageCount
};

if (result.service !== "ai-native-im-cpp") throw new Error(`unexpected service: ${result.service}`);
if (result.messageCount < 1) throw new Error("message list is empty");
if (result.savedCount < 1) throw new Error("saved message list is empty");
if (result.searchMessages < 1) throw new Error("search did not find the smoke message");
if (result.summaryMessageCount < 1) throw new Error("summary did not include messages");

console.log(JSON.stringify(result, null, 2));
