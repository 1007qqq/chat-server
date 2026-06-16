from pydantic import BaseModel, Field


class LoginRequest(BaseModel):
    username: str = Field(min_length=3)
    password: str = Field(min_length=6)


class RegisterRequest(LoginRequest):
    pass


class UserUpdateRequest(BaseModel):
    pass


class DirectRequest(BaseModel):
    targetUserId: int


class AddFriendRequest(BaseModel):
    targetUserId: int | None = None
    username: str | None = None


class FriendRequestAction(BaseModel):
    requestId: int


class GroupRequest(BaseModel):
    name: str = Field(min_length=2)
    description: str = ""
    memberIds: list[int] = []


class AddMembersRequest(BaseModel):
    conversationId: int
    memberIds: list[int]


class MessageRequest(BaseModel):
    conversationId: int
    content: str = Field(min_length=1, max_length=2000)
    clientId: str = ""
    replyToId: int | None = None


class MessageEditRequest(BaseModel):
    messageId: int
    content: str = Field(min_length=1, max_length=2000)


class MessageDeleteRequest(BaseModel):
    messageId: int


class ConversationPreferenceRequest(BaseModel):
    conversationId: int
    pinned: bool | None = None
    muted: bool | None = None
    archived: bool | None = None


class ConversationActionRequest(BaseModel):
    conversationId: int


class SaveMessageRequest(BaseModel):
    messageId: int


class NotificationReadRequest(BaseModel):
    notificationIds: list[int] | None = None


class SummaryRequest(BaseModel):
    conversationId: int


class SettingsRequest(BaseModel):
    theme: str | None = None
    density: str | None = None
    notifyDesktop: bool | None = None
    enterToSend: bool | None = None
