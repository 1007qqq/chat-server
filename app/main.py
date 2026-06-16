import asyncio
import json
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from .config import APP_NAME, ROOT_DIR
from .events import event_hub
from .schemas import (
    AddFriendRequest,
    ConversationActionRequest,
    AddMembersRequest,
    ConversationPreferenceRequest,
    DirectRequest,
    FriendRequestAction,
    GroupRequest,
    LoginRequest,
    MessageDeleteRequest,
    MessageEditRequest,
    MessageRequest,
    NotificationReadRequest,
    RegisterRequest,
    SaveMessageRequest,
    SettingsRequest,
    SummaryRequest,
    UserUpdateRequest,
)
from . import services


app = FastAPI(title=APP_NAME, version="2.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def startup():
    services.init_db()


def extract_token(authorization: str | None = Header(default=None), token: str | None = Query(default=None)):
    if authorization and authorization.lower().startswith("bearer "):
        return authorization.split(" ", 1)[1].strip()
    return token or ""


def current_user(token: str = Depends(extract_token)):
    return services.get_user_by_token(token)


def current_token(token: str = Depends(extract_token)):
    return token


@app.post("/api/login")
def login(payload: LoginRequest):
    return services.auth_login(payload.username, payload.password)


@app.post("/api/register")
def register(payload: RegisterRequest):
    return services.auth_register(payload.username, payload.password)


@app.post("/api/logout")
async def logout(user=Depends(current_user), token: str = Depends(current_token)):
    services.auth_logout(int(user["id"]), token)
    await event_hub.publish([int(user["id"])], {"type": "presence"})
    return {"ok": True}


@app.get("/api/me")
def me(user=Depends(current_user)):
    return services.get_me(int(user["id"]))


@app.patch("/api/profile")
def profile(payload: UserUpdateRequest, user=Depends(current_user)):
    return services.update_profile(int(user["id"]), payload.model_dump(exclude_unset=True))


@app.get("/api/settings")
def get_settings(user=Depends(current_user)):
    return services.get_me(int(user["id"]))["settings"]


@app.patch("/api/settings")
def update_settings(payload: SettingsRequest, user=Depends(current_user)):
    return services.update_settings(int(user["id"]), payload.model_dump(exclude_unset=True))


@app.get("/api/users")
def users(user=Depends(current_user)):
    return services.list_friends(int(user["id"]))


@app.get("/api/users/search")
def user_search(q: str = "", user=Depends(current_user)):
    return services.search_users_for_friend(int(user["id"]), q)


@app.post("/api/friends")
async def add_friend(payload: AddFriendRequest, user=Depends(current_user)):
    result = services.create_friend_request(int(user["id"]), payload.targetUserId, payload.username)
    await event_hub.publish(result["participantIds"], {"type": "friends"})
    return {"request": result["request"]}


@app.get("/api/friend-requests")
def friend_requests(user=Depends(current_user)):
    return services.list_friend_requests(int(user["id"]))


@app.post("/api/friend-requests/accept")
async def accept_friend_request(payload: FriendRequestAction, user=Depends(current_user)):
    result = services.accept_friend_request(int(user["id"]), payload.requestId)
    await event_hub.publish(result["participantIds"], {"type": "friends"})
    return {"friend": result["friend"]}


@app.post("/api/friend-requests/reject")
async def reject_friend_request(payload: FriendRequestAction, user=Depends(current_user)):
    result = services.reject_friend_request(int(user["id"]), payload.requestId)
    await event_hub.publish(result["participantIds"], {"type": "friends"})
    return {"ok": True}


@app.get("/api/conversations")
def conversations(includeArchived: bool = False, user=Depends(current_user)):
    return services.list_conversations(int(user["id"]), includeArchived)


@app.post("/api/conversations/direct")
async def direct(payload: DirectRequest, user=Depends(current_user)):
    result = services.open_direct(int(user["id"]), payload.targetUserId)
    await event_hub.publish(result["participantIds"], {"type": "conversation", "conversationId": result["conversation"]["id"]})
    return {"conversation": result["conversation"]}


@app.post("/api/groups")
async def group(payload: GroupRequest, user=Depends(current_user)):
    result = services.open_group(int(user["id"]), payload.name, payload.description, payload.memberIds)
    await event_hub.publish(result["participantIds"], {"type": "conversation", "conversationId": result["conversation"]["id"]})
    return {"conversation": result["conversation"]}


@app.post("/api/groups/members")
async def add_members(payload: AddMembersRequest, user=Depends(current_user)):
    result = services.add_group_members(int(user["id"]), payload.conversationId, payload.memberIds)
    await event_hub.publish(result["participantIds"], {"type": "conversation", "conversationId": payload.conversationId})
    return {"conversation": result["conversation"]}


@app.post("/api/conversations/preferences")
def conversation_preferences(payload: ConversationPreferenceRequest, user=Depends(current_user)):
    return services.update_conversation_preferences(int(user["id"]), payload.conversationId, payload.pinned, payload.muted, payload.archived)


@app.post("/api/conversations/leave")
async def leave_conversation(payload: ConversationActionRequest, user=Depends(current_user)):
    result = services.leave_conversation(int(user["id"]), payload.conversationId)
    await event_hub.publish(result["participantIds"], {"type": "conversation", "conversationId": payload.conversationId, "mode": result["mode"]})
    return {"ok": True, "mode": result["mode"]}


@app.delete("/api/conversations")
async def dissolve_conversation(payload: ConversationActionRequest, user=Depends(current_user)):
    result = services.dissolve_conversation(int(user["id"]), payload.conversationId)
    await event_hub.publish(result["participantIds"], {"type": "conversation", "conversationId": payload.conversationId, "mode": result["mode"]})
    return {"ok": True, "mode": result["mode"]}


@app.get("/api/conversations/{conversation_id}/members")
def conversation_members(conversation_id: int, user=Depends(current_user)):
    with services.db_session() as conn:
        services.require_participant(conn, int(user["id"]), conversation_id)
        return {"members": services.conversation_members(conn, conversation_id)}


@app.get("/api/messages")
def messages(conversationId: int, user=Depends(current_user)):
    return services.list_messages(int(user["id"]), conversationId)


@app.post("/api/messages")
async def send_message(payload: MessageRequest, user=Depends(current_user)):
    result = services.send_message(int(user["id"]), payload.conversationId, payload.content, payload.clientId, payload.replyToId)
    await event_hub.publish(result["participantIds"], {"type": "message", "conversationId": payload.conversationId, "message": result["message"]})
    return {"message": result["message"]}


@app.patch("/api/messages")
async def edit_message(payload: MessageEditRequest, user=Depends(current_user)):
    result = services.edit_message(int(user["id"]), payload.messageId, payload.content)
    await event_hub.publish(result["participantIds"], {"type": "message", "conversationId": result["message"]["conversationId"], "message": result["message"]})
    return {"message": result["message"]}


@app.delete("/api/messages")
async def delete_message(payload: MessageDeleteRequest, user=Depends(current_user)):
    result = services.delete_message(int(user["id"]), payload.messageId)
    await event_hub.publish(result["participantIds"], {"type": "message", "conversationId": result["message"]["conversationId"], "message": result["message"]})
    return {"message": result["message"]}


@app.post("/api/messages/save")
def save_message(payload: SaveMessageRequest, user=Depends(current_user)):
    return services.save_message(int(user["id"]), payload.messageId)


@app.delete("/api/messages/save/{message_id}")
def unsave_message(message_id: int, user=Depends(current_user)):
    return services.unsave_message(int(user["id"]), message_id)


@app.get("/api/saved-messages")
def saved_messages(user=Depends(current_user)):
    return services.list_saved_messages(int(user["id"]))


@app.get("/api/notifications")
def notifications(user=Depends(current_user)):
    return services.list_notifications(int(user["id"]))


@app.post("/api/notifications/read")
def notifications_read(payload: NotificationReadRequest, user=Depends(current_user)):
    return services.mark_notifications_read(int(user["id"]), payload.notificationIds)


@app.get("/api/audit")
def audit(user=Depends(current_user)):
    return services.list_audit_logs(int(user["id"]))


@app.get("/api/search")
def search(q: str = "", user=Depends(current_user)):
    return services.search(int(user["id"]), q)


@app.post("/api/summary")
def summary(payload: SummaryRequest, user=Depends(current_user)):
    return services.build_summary(int(user["id"]), payload.conversationId)


@app.get("/api/health")
def health():
    return services.health()


@app.get("/api/stream")
async def stream(user=Depends(current_user)):
    user_id = int(user["id"])
    queue = await event_hub.subscribe(user_id)

    async def generator():
        try:
            yield 'event: ready\ndata: {"type":"ready"}\n\n'
            while True:
                try:
                    payload = await asyncio.wait_for(queue.get(), timeout=20)
                    yield f"event: update\ndata: {payload}\n\n"
                except asyncio.TimeoutError:
                    yield 'event: ping\ndata: {"type":"ping"}\n\n'
        finally:
            await event_hub.unsubscribe(user_id, queue)

    return StreamingResponse(generator(), media_type="text/event-stream")


dist_dir = ROOT_DIR / "frontend" / "dist"
if dist_dir.exists():
    app.mount("/assets", StaticFiles(directory=dist_dir / "assets"), name="assets")


@app.get("/{full_path:path}")
def spa(full_path: str):
    if full_path.startswith("api/"):
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="接口不存在")
    index = dist_dir / "index.html"
    if index.exists():
        return FileResponse(index)
    return {"message": "Frontend is not built. Run npm install && npm run build in ./frontend."}
