import hashlib
import json
import secrets
import sqlite3
import time
from datetime import datetime

from fastapi import HTTPException, status

from .config import APP_NAME, PRESENCE_TTL_SECONDS, SESSION_TTL_SECONDS
from .database import db_session, ensure_column, get_connection


def now_iso() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def now_epoch() -> float:
    return time.time()


def password_hash(username: str, password: str) -> str:
    payload = f"ai-native-im:{username.strip().lower()}:{password}".encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def parse_json(raw: str | None, default):
    if not raw:
        return default
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return default


def init_db():
    with db_session() as conn:
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT NOT NULL UNIQUE,
                display_name TEXT NOT NULL,
                password_hash TEXT NOT NULL,
                avatar_color TEXT NOT NULL,
                role TEXT NOT NULL DEFAULT 'member',
                department TEXT NOT NULL DEFAULT '',
                title TEXT NOT NULL DEFAULT '',
                status_message TEXT NOT NULL DEFAULT '',
                phone TEXT NOT NULL DEFAULT '',
                location TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                last_seen REAL NOT NULL DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS sessions (
                token TEXT PRIMARY KEY,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                created_at REAL NOT NULL,
                last_seen REAL NOT NULL
            );

            CREATE TABLE IF NOT EXISTS conversations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                type TEXT NOT NULL CHECK(type IN ('direct', 'group')),
                name TEXT NOT NULL,
                description TEXT NOT NULL DEFAULT '',
                owner_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
                direct_key TEXT UNIQUE,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS participants (
                conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                joined_at TEXT NOT NULL,
                last_read_id INTEGER NOT NULL DEFAULT 0,
                role TEXT NOT NULL DEFAULT 'member',
                is_pinned INTEGER NOT NULL DEFAULT 0,
                is_muted INTEGER NOT NULL DEFAULT 0,
                is_archived INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (conversation_id, user_id)
            );

            CREATE TABLE IF NOT EXISTS friendships (
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                friend_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                status TEXT NOT NULL DEFAULT 'accepted',
                created_at TEXT NOT NULL,
                PRIMARY KEY (user_id, friend_id),
                CHECK (user_id <> friend_id)
            );

            CREATE TABLE IF NOT EXISTS friend_requests (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                requester_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                recipient_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                status TEXT NOT NULL DEFAULT 'pending',
                created_at TEXT NOT NULL,
                responded_at TEXT,
                CHECK (requester_id <> recipient_id)
            );

            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                sender_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                content TEXT NOT NULL,
                kind TEXT NOT NULL DEFAULT 'text',
                client_id TEXT NOT NULL DEFAULT '',
                status TEXT NOT NULL DEFAULT 'sent',
                edited_at TEXT,
                deleted_at TEXT,
                reply_to_id INTEGER REFERENCES messages(id) ON DELETE SET NULL,
                created_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS message_receipts (
                message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                status TEXT NOT NULL DEFAULT 'delivered',
                delivered_at TEXT NOT NULL,
                read_at TEXT,
                PRIMARY KEY (message_id, user_id)
            );

            CREATE TABLE IF NOT EXISTS saved_messages (
                message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                saved_at TEXT NOT NULL,
                PRIMARY KEY (message_id, user_id)
            );

            CREATE TABLE IF NOT EXISTS notifications (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                type TEXT NOT NULL,
                title TEXT NOT NULL,
                body TEXT NOT NULL,
                entity_type TEXT NOT NULL DEFAULT '',
                entity_id INTEGER,
                is_read INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS summaries (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                summary TEXT NOT NULL,
                keywords TEXT NOT NULL,
                action_items TEXT NOT NULL DEFAULT '[]',
                message_count INTEGER NOT NULL,
                created_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS audit_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                actor_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
                action TEXT NOT NULL,
                entity_type TEXT NOT NULL,
                entity_id INTEGER,
                detail TEXT NOT NULL DEFAULT '{}',
                created_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS user_settings (
                user_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
                theme TEXT NOT NULL DEFAULT 'light',
                density TEXT NOT NULL DEFAULT 'comfortable',
                notify_desktop INTEGER NOT NULL DEFAULT 1,
                enter_to_send INTEGER NOT NULL DEFAULT 1,
                updated_at TEXT NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_messages_conversation ON messages(conversation_id, id);
            CREATE INDEX IF NOT EXISTS idx_messages_content ON messages(content);
            CREATE INDEX IF NOT EXISTS idx_sessions_user_last_seen ON sessions(user_id, last_seen);
            CREATE INDEX IF NOT EXISTS idx_notifications_user_read ON notifications(user_id, is_read, id);
            CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_logs(created_at, id);
            CREATE INDEX IF NOT EXISTS idx_friendships_friend ON friendships(friend_id, status);
            CREATE INDEX IF NOT EXISTS idx_friend_requests_recipient ON friend_requests(recipient_id, status, id);
            CREATE INDEX IF NOT EXISTS idx_friend_requests_requester ON friend_requests(requester_id, status, id);
            CREATE UNIQUE INDEX IF NOT EXISTS idx_friend_requests_pending_pair
                ON friend_requests(requester_id, recipient_id)
                WHERE status = 'pending';
            """
        )
        ensure_column(conn, "messages", "deleted_at", "TEXT")
        # Do not seed demo accounts. This project should be safe to publish and
        # should start with an empty user base.


def log_audit(conn: sqlite3.Connection, actor_id: int | None, action: str, entity_type: str, entity_id: int | None, detail: dict | None = None):
    conn.execute(
        """
        INSERT INTO audit_logs(actor_id, action, entity_type, entity_id, detail, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (actor_id, action, entity_type, entity_id, json.dumps(detail or {}, ensure_ascii=False), now_iso()),
    )


def create_notification(conn: sqlite3.Connection, user_id: int, title: str, body: str, entity_type: str, entity_id: int | None, kind: str = "message"):
    conn.execute(
        """
        INSERT INTO notifications(user_id, type, title, body, entity_type, entity_id, is_read, created_at)
        VALUES (?, ?, ?, ?, ?, ?, 0, ?)
        """,
        (user_id, kind, title, body, entity_type, entity_id, now_iso()),
    )


def create_user(conn: sqlite3.Connection, username: str, password: str, display_name: str, color: str, role: str = "member", department: str = "", title: str = "", status_message: str = "", phone: str = "", location: str = "") -> int:
    stamp = now_iso()
    cur = conn.execute(
        """
        INSERT INTO users(username, display_name, password_hash, avatar_color, role, department, title, status_message, phone, location, created_at, last_seen)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)
        """,
        (username, display_name, password_hash(username, password), color, role, department, title, status_message, phone, location, stamp),
    )
    user_id = int(cur.lastrowid)
    conn.execute(
        """
        INSERT INTO user_settings(user_id, theme, density, notify_desktop, enter_to_send, updated_at)
        VALUES (?, 'light', 'comfortable', 1, 1, ?)
        """,
        (user_id, stamp),
    )
    log_audit(conn, user_id, "user.created", "user", user_id, {"username": username})
    return user_id


def ensure_friendship(conn: sqlite3.Connection, user_id: int, friend_id: int):
    if user_id == friend_id:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="不能添加自己为好友")
    stamp = now_iso()
    conn.executemany(
        """
        INSERT INTO friendships(user_id, friend_id, status, created_at)
        VALUES (?, ?, 'accepted', ?)
        ON CONFLICT(user_id, friend_id) DO UPDATE SET status = 'accepted'
        """,
        [(user_id, friend_id, stamp), (friend_id, user_id, stamp)],
    )


def are_friends(conn: sqlite3.Connection, user_id: int, friend_id: int) -> bool:
    return conn.execute(
        """
        SELECT 1 FROM friendships
        WHERE user_id = ? AND friend_id = ? AND status = 'accepted'
        """,
        (user_id, friend_id),
    ).fetchone() is not None


def require_friend(conn: sqlite3.Connection, user_id: int, friend_id: int):
    if not are_friends(conn, user_id, friend_id):
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="请先添加好友")


def auth_login(username: str, password: str):
    username = username.strip().lower()
    with db_session() as conn:
        user = conn.execute("SELECT * FROM users WHERE username = ?", (username,)).fetchone()
        if not user or user["password_hash"] != password_hash(username, password):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="用户名或密码错误")
        token = secrets.token_urlsafe(32)
        ts = now_epoch()
        conn.execute("INSERT INTO sessions(token, user_id, created_at, last_seen) VALUES (?, ?, ?, ?)", (token, user["id"], ts, ts))
        conn.execute("UPDATE users SET last_seen = ? WHERE id = ?", (ts, user["id"]))
        log_audit(conn, int(user["id"]), "auth.login", "session", None, {"username": username})
        return {"token": token, "user": serialize_user(conn, user)}


def auth_register(username: str, password: str, display_name: str | None = None):
    username = username.strip().lower()
    with db_session() as conn:
        try:
            user_id = create_user(conn, username, password, username, f"#{secrets.token_hex(3)}")
        except sqlite3.IntegrityError as exc:
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="用户名已存在") from exc
        create_notification(conn, user_id, "账号已创建", "账号已创建。", "user", user_id, "system")
    return auth_login(username, password)


def get_user_by_token(token: str):
    if not token:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="需要登录后访问")
    cutoff = now_epoch() - SESSION_TTL_SECONDS
    conn = get_connection()
    try:
        row = conn.execute(
            """
            SELECT users.*
            FROM sessions
            JOIN users ON users.id = sessions.user_id
            WHERE sessions.token = ? AND sessions.last_seen >= ?
            """,
            (token, cutoff),
        ).fetchone()
        if not row:
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="登录已失效")
        ts = now_epoch()
        conn.execute("UPDATE sessions SET last_seen = ? WHERE token = ?", (ts, token))
        conn.execute("UPDATE users SET last_seen = ? WHERE id = ?", (ts, row["id"]))
        conn.commit()
        return row
    finally:
        conn.close()


def auth_logout(user_id: int, token: str):
    with db_session() as conn:
        conn.execute("DELETE FROM sessions WHERE token = ?", (token,))
        log_audit(conn, user_id, "auth.logout", "session", None, {})


def online_ids(conn: sqlite3.Connection) -> set[int]:
    cutoff = now_epoch() - PRESENCE_TTL_SECONDS
    return {int(row["user_id"]) for row in conn.execute("SELECT user_id FROM sessions WHERE last_seen >= ?", (cutoff,)).fetchall()}


def serialize_user(conn: sqlite3.Connection, row: sqlite3.Row):
    online = int(row["id"]) in online_ids(conn)
    username = row["username"]
    return {
        "id": int(row["id"]),
        "username": username,
        "displayName": username,
        "avatarColor": row["avatar_color"],
        "role": row["role"],
        "department": "",
        "title": "",
        "statusMessage": "",
        "phone": "",
        "location": "",
        "createdAt": row["created_at"],
        "online": online,
    }


def friendship_status(conn: sqlite3.Connection, viewer_id: int, target_id: int) -> str:
    if viewer_id == target_id:
        return "self"
    if are_friends(conn, viewer_id, target_id):
        return "accepted"
    pending = conn.execute(
        """
        SELECT requester_id
        FROM friend_requests
        WHERE status = 'pending'
          AND ((requester_id = ? AND recipient_id = ?)
            OR (requester_id = ? AND recipient_id = ?))
        ORDER BY id DESC
        LIMIT 1
        """,
        (viewer_id, target_id, target_id, viewer_id),
    ).fetchone()
    if not pending:
        return "none"
    return "pending_outgoing" if int(pending["requester_id"]) == viewer_id else "pending_incoming"


def serialize_user_for_view(conn: sqlite3.Connection, row: sqlite3.Row, viewer_id: int):
    user = serialize_user(conn, row)
    user["friendshipStatus"] = friendship_status(conn, viewer_id, int(row["id"]))
    user["friend"] = user["friendshipStatus"] == "accepted"
    return user


def serialize_friend_request(conn: sqlite3.Connection, row: sqlite3.Row, viewer_id: int):
    requester = conn.execute("SELECT * FROM users WHERE id = ?", (row["requester_id"],)).fetchone()
    recipient = conn.execute("SELECT * FROM users WHERE id = ?", (row["recipient_id"],)).fetchone()
    direction = "incoming" if int(row["recipient_id"]) == viewer_id else "outgoing"
    return {
        "id": int(row["id"]),
        "status": row["status"],
        "direction": direction,
        "createdAt": row["created_at"],
        "respondedAt": row["responded_at"],
        "requester": serialize_user_for_view(conn, requester, viewer_id) if requester else None,
        "recipient": serialize_user_for_view(conn, recipient, viewer_id) if recipient else None,
    }


def list_users():
    with db_session() as conn:
        users = [serialize_user(conn, row) for row in conn.execute("SELECT * FROM users ORDER BY username").fetchall()]
        return {"users": users, "departments": []}


def list_friends(user_id: int):
    with db_session() as conn:
        rows = conn.execute(
            """
            SELECT users.*, friendships.created_at AS friend_created_at
            FROM friendships
            JOIN users ON users.id = friendships.friend_id
            WHERE friendships.user_id = ?
              AND friendships.status = 'accepted'
            ORDER BY users.username
            """,
            (user_id,),
        ).fetchall()
        users = []
        for row in rows:
            user = serialize_user_for_view(conn, row, user_id)
            user["friendCreatedAt"] = row["friend_created_at"]
            users.append(user)
        return {"users": users, "departments": []}


def search_users_for_friend(user_id: int, keyword: str):
    keyword = keyword.strip()
    if not keyword:
        return {"users": []}
    like = f"%{keyword.lower()}%"
    with db_session() as conn:
        rows = conn.execute(
            """
            SELECT * FROM users
            WHERE id <> ?
              AND lower(username) LIKE ?
            ORDER BY
              CASE WHEN lower(username) = ? THEN 0 WHEN lower(username) LIKE ? THEN 1 ELSE 2 END,
              username
            LIMIT 20
            """,
            (user_id, like, keyword.lower(), f"{keyword.lower()}%"),
        ).fetchall()
        return {"users": [serialize_user_for_view(conn, row, user_id) for row in rows]}


def create_friend_request(user_id: int, target_user_id: int | None, username: str | None):
    if target_user_id is None and not (username or "").strip():
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="请输入账号或选择用户")
    with db_session() as conn:
        if target_user_id is not None:
            target = conn.execute("SELECT * FROM users WHERE id = ?", (target_user_id,)).fetchone()
        else:
            target = conn.execute("SELECT * FROM users WHERE username = ?", ((username or "").strip().lower(),)).fetchone()
        if not target:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="用户不存在")
        target_id = int(target["id"])
        if target_id == user_id:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="不能添加自己为好友")
        if are_friends(conn, user_id, target_id):
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="已经是好友")
        incoming = conn.execute(
            """
            SELECT * FROM friend_requests
            WHERE requester_id = ? AND recipient_id = ? AND status = 'pending'
            ORDER BY id DESC LIMIT 1
            """,
            (target_id, user_id),
        ).fetchone()
        if incoming:
            stamp = now_iso()
            conn.execute("UPDATE friend_requests SET status = 'accepted', responded_at = ? WHERE id = ?", (stamp, int(incoming["id"])))
            ensure_friendship(conn, user_id, target_id)
            create_notification(conn, target_id, "好友申请已通过", f"{serialize_user(conn, conn.execute('SELECT * FROM users WHERE id = ?', (user_id,)).fetchone())['username']} 已通过你的好友申请。", "user", user_id, "friend")
            log_audit(conn, user_id, "friend.accepted", "user", target_id, {"requestId": int(incoming["id"])})
            return {
                "friend": serialize_user_for_view(conn, target, user_id),
                "request": serialize_friend_request(conn, conn.execute("SELECT * FROM friend_requests WHERE id = ?", (int(incoming["id"]),)).fetchone(), user_id),
                "participantIds": [user_id, target_id],
            }
        existing = conn.execute(
            """
            SELECT * FROM friend_requests
            WHERE requester_id = ? AND recipient_id = ? AND status = 'pending'
            ORDER BY id DESC LIMIT 1
            """,
            (user_id, target_id),
        ).fetchone()
        if existing:
            return {"request": serialize_friend_request(conn, existing, user_id), "participantIds": [user_id, target_id]}
        stamp = now_iso()
        cur = conn.execute(
            """
            INSERT INTO friend_requests(requester_id, recipient_id, status, created_at)
            VALUES (?, ?, 'pending', ?)
            """,
            (user_id, target_id, stamp),
        )
        request = conn.execute("SELECT * FROM friend_requests WHERE id = ?", (cur.lastrowid,)).fetchone()
        create_notification(conn, target_id, "新的好友申请", f"{serialize_user(conn, conn.execute('SELECT * FROM users WHERE id = ?', (user_id,)).fetchone())['username']} 请求添加你为好友。", "friend_request", int(cur.lastrowid), "friend")
        log_audit(conn, user_id, "friend.requested", "user", target_id, {"username": target["username"]})
        return {
            "request": serialize_friend_request(conn, request, user_id),
            "participantIds": [user_id, target_id],
        }


def list_friend_requests(user_id: int):
    with db_session() as conn:
        rows = conn.execute(
            """
            SELECT *
            FROM friend_requests
            WHERE status = 'pending'
              AND (requester_id = ? OR recipient_id = ?)
            ORDER BY id DESC
            """,
            (user_id, user_id),
        ).fetchall()
        requests = [serialize_friend_request(conn, row, user_id) for row in rows]
        return {
            "requests": requests,
            "incoming": [item for item in requests if item["direction"] == "incoming"],
            "outgoing": [item for item in requests if item["direction"] == "outgoing"],
        }


def accept_friend_request(user_id: int, request_id: int):
    with db_session() as conn:
        request = conn.execute("SELECT * FROM friend_requests WHERE id = ?", (request_id,)).fetchone()
        if not request:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="好友申请不存在")
        if int(request["recipient_id"]) != user_id:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="只能处理发给你的好友申请")
        if request["status"] != "pending":
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="好友申请已处理")
        requester_id = int(request["requester_id"])
        stamp = now_iso()
        conn.execute("UPDATE friend_requests SET status = 'accepted', responded_at = ? WHERE id = ?", (stamp, request_id))
        ensure_friendship(conn, user_id, requester_id)
        requester = conn.execute("SELECT * FROM users WHERE id = ?", (requester_id,)).fetchone()
        create_notification(conn, requester_id, "好友申请已通过", f"{serialize_user(conn, conn.execute('SELECT * FROM users WHERE id = ?', (user_id,)).fetchone())['username']} 已通过你的好友申请。", "user", user_id, "friend")
        log_audit(conn, user_id, "friend.accepted", "user", requester_id, {"requestId": request_id})
        return {
            "friend": serialize_user_for_view(conn, requester, user_id),
            "request": serialize_friend_request(conn, conn.execute("SELECT * FROM friend_requests WHERE id = ?", (request_id,)).fetchone(), user_id),
            "participantIds": [user_id, requester_id],
        }


def reject_friend_request(user_id: int, request_id: int):
    with db_session() as conn:
        request = conn.execute("SELECT * FROM friend_requests WHERE id = ?", (request_id,)).fetchone()
        if not request:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="好友申请不存在")
        if int(request["recipient_id"]) != user_id:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="只能处理发给你的好友申请")
        if request["status"] != "pending":
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="好友申请已处理")
        requester_id = int(request["requester_id"])
        conn.execute("UPDATE friend_requests SET status = 'rejected', responded_at = ? WHERE id = ?", (now_iso(), request_id))
        create_notification(conn, requester_id, "好友申请未通过", "对方已拒绝你的好友申请。", "friend_request", request_id, "friend")
        log_audit(conn, user_id, "friend.rejected", "user", requester_id, {"requestId": request_id})
        return {"ok": True, "participantIds": [user_id, requester_id]}


def update_profile(user_id: int, data: dict):
    return get_me(user_id)


def get_me(user_id: int):
    with db_session() as conn:
        row = conn.execute("SELECT * FROM users WHERE id = ?", (user_id,)).fetchone()
        return {"user": serialize_user(conn, row), "settings": get_settings_for_conn(conn, user_id)}


def get_settings_for_conn(conn: sqlite3.Connection, user_id: int):
    row = conn.execute("SELECT * FROM user_settings WHERE user_id = ?", (user_id,)).fetchone()
    if not row:
        conn.execute("INSERT INTO user_settings(user_id, updated_at) VALUES (?, ?)", (user_id, now_iso()))
        row = conn.execute("SELECT * FROM user_settings WHERE user_id = ?", (user_id,)).fetchone()
    return {
        "theme": row["theme"],
        "density": row["density"],
        "notifyDesktop": bool(row["notify_desktop"]),
        "enterToSend": bool(row["enter_to_send"]),
        "updatedAt": row["updated_at"],
    }


def update_settings(user_id: int, data: dict):
    with db_session() as conn:
        current = get_settings_for_conn(conn, user_id)
        next_settings = {
            "theme": data.get("theme") or current["theme"],
            "density": data.get("density") or current["density"],
            "notifyDesktop": current["notifyDesktop"] if data.get("notifyDesktop") is None else bool(data.get("notifyDesktop")),
            "enterToSend": current["enterToSend"] if data.get("enterToSend") is None else bool(data.get("enterToSend")),
        }
        conn.execute(
            """
            UPDATE user_settings
            SET theme = ?, density = ?, notify_desktop = ?, enter_to_send = ?, updated_at = ?
            WHERE user_id = ?
            """,
            (next_settings["theme"], next_settings["density"], int(next_settings["notifyDesktop"]), int(next_settings["enterToSend"]), now_iso(), user_id),
        )
        log_audit(conn, user_id, "settings.updated", "user_settings", user_id, next_settings)
        return {"settings": get_settings_for_conn(conn, user_id)}


def require_participant(conn: sqlite3.Connection, user_id: int, conversation_id: int):
    row = conn.execute("SELECT * FROM participants WHERE user_id = ? AND conversation_id = ?", (user_id, conversation_id)).fetchone()
    if not row:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="无权访问该会话")
    return row


def participant_ids(conn: sqlite3.Connection, conversation_id: int) -> list[int]:
    return [int(row["user_id"]) for row in conn.execute("SELECT user_id FROM participants WHERE conversation_id = ?", (conversation_id,)).fetchall()]


def conversation_members(conn: sqlite3.Connection, conversation_id: int) -> list[dict]:
    rows = conn.execute(
        """
        SELECT users.*, participants.role AS member_role, participants.joined_at
        FROM participants
        JOIN users ON users.id = participants.user_id
        WHERE participants.conversation_id = ?
        ORDER BY participants.role DESC, users.username
        """,
        (conversation_id,),
    ).fetchall()
    members = []
    for row in rows:
        user = serialize_user(conn, row)
        user["memberRole"] = row["member_role"]
        user["joinedAt"] = row["joined_at"]
        members.append(user)
    return members


def ensure_direct_conversation(conn: sqlite3.Connection, user_a: int, user_b: int) -> int:
    low, high = sorted([int(user_a), int(user_b)])
    direct_key = f"{low}:{high}"
    existing = conn.execute("SELECT id FROM conversations WHERE direct_key = ?", (direct_key,)).fetchone()
    if existing:
        return int(existing["id"])
    stamp = now_iso()
    cur = conn.execute(
        """
        INSERT INTO conversations(type, name, description, owner_id, direct_key, created_at, updated_at)
        VALUES ('direct', ?, '', NULL, ?, ?, ?)
        """,
        (direct_key, direct_key, stamp, stamp),
    )
    conversation_id = int(cur.lastrowid)
    conn.executemany(
        """
        INSERT INTO participants(conversation_id, user_id, joined_at, last_read_id, role)
        VALUES (?, ?, ?, 0, 'member')
        """,
        [(conversation_id, low, stamp), (conversation_id, high, stamp)],
    )
    log_audit(conn, user_a, "conversation.direct.created", "conversation", conversation_id, {"targetUserId": user_b})
    return conversation_id


def create_group(conn: sqlite3.Connection, owner_id: int, name: str, description: str, member_ids: list[int]) -> int:
    stamp = now_iso()
    cur = conn.execute(
        """
        INSERT INTO conversations(type, name, description, owner_id, direct_key, created_at, updated_at)
        VALUES ('group', ?, ?, ?, NULL, ?, ?)
        """,
        (name.strip(), description.strip(), owner_id, stamp, stamp),
    )
    conversation_id = int(cur.lastrowid)
    members = sorted(set([owner_id, *[int(uid) for uid in member_ids if int(uid) > 0]]))
    conn.executemany(
        """
        INSERT OR IGNORE INTO participants(conversation_id, user_id, joined_at, last_read_id, role)
        VALUES (?, ?, ?, 0, ?)
        """,
        [(conversation_id, uid, stamp, "owner" if uid == owner_id else "member") for uid in members],
    )
    log_audit(conn, owner_id, "conversation.group.created", "conversation", conversation_id, {"name": name, "memberCount": len(members)})
    return conversation_id


def serialize_conversation(conn: sqlite3.Connection, row: sqlite3.Row, user_id: int):
    pref = conn.execute("SELECT * FROM participants WHERE conversation_id = ? AND user_id = ?", (row["id"], user_id)).fetchone()
    members = conversation_members(conn, int(row["id"]))
    title = row["name"]
    if row["type"] == "direct":
        peer = next((m for m in members if m["id"] != user_id), members[0] if members else None)
        title = peer["displayName"] if peer else "私聊"
    last = conn.execute(
        """
        SELECT messages.*, users.username
        FROM messages
        JOIN users ON users.id = messages.sender_id
        WHERE conversation_id = ?
        ORDER BY messages.id DESC
        LIMIT 1
        """,
        (row["id"],),
    ).fetchone()
    unread = conn.execute(
        """
        SELECT COUNT(*) AS c
        FROM messages
        WHERE conversation_id = ?
          AND id > ?
          AND sender_id <> ?
          AND deleted_at IS NULL
        """,
        (row["id"], int(pref["last_read_id"] if pref else 0), user_id),
    ).fetchone()["c"]
    return {
        "id": int(row["id"]),
        "type": row["type"],
        "name": row["name"],
        "title": title,
        "description": row["description"],
        "ownerId": row["owner_id"],
        "updatedAt": row["updated_at"],
        "unread": int(unread),
        "pinned": bool(pref["is_pinned"]) if pref else False,
        "muted": bool(pref["is_muted"]) if pref else False,
        "archived": bool(pref["is_archived"]) if pref else False,
        "participants": members,
        "lastMessage": {
            "id": int(last["id"]),
            "senderName": last["username"],
            "content": "消息已撤回" if last["deleted_at"] else last["content"],
            "createdAt": last["created_at"],
            "status": last["status"],
        }
        if last
        else None,
    }


def list_conversations(user_id: int, include_archived: bool = False):
    with db_session() as conn:
        archived_clause = "" if include_archived else "AND participants.is_archived = 0"
        rows = conn.execute(
            f"""
            SELECT conversations.*
            FROM conversations
            JOIN participants ON participants.conversation_id = conversations.id
            WHERE participants.user_id = ?
              {archived_clause}
            ORDER BY participants.is_pinned DESC, conversations.updated_at DESC, conversations.id DESC
            """,
            (user_id,),
        ).fetchall()
        return {"conversations": [serialize_conversation(conn, row, user_id) for row in rows]}


def open_direct(user_id: int, target_user_id: int):
    if user_id == target_user_id:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="不能和自己创建私聊")
    with db_session() as conn:
        target = conn.execute("SELECT id FROM users WHERE id = ?", (target_user_id,)).fetchone()
        if not target:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="用户不存在")
        require_friend(conn, user_id, target_user_id)
        conversation_id = ensure_direct_conversation(conn, user_id, target_user_id)
        conn.execute(
            """
            UPDATE participants
            SET is_archived = 0
            WHERE conversation_id = ? AND user_id = ?
            """,
            (conversation_id, user_id),
        )
        row = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        return {"conversation": serialize_conversation(conn, row, user_id), "participantIds": participant_ids(conn, conversation_id)}


def open_group(user_id: int, name: str, description: str, member_ids: list[int]):
    requested_ids = sorted({int(uid) for uid in member_ids if int(uid) != user_id})
    if not name.strip():
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="请输入群名称")
    if not requested_ids:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="请至少选择一名成员")
    with db_session() as conn:
        valid_ids = [
            int(row["id"])
            for row in conn.execute(
                f"SELECT id FROM users WHERE id IN ({','.join('?' for _ in requested_ids)})",
                tuple(requested_ids),
            ).fetchall()
        ]
        if len(valid_ids) != len(requested_ids):
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="存在无效成员")
        for member_id in valid_ids:
            require_friend(conn, user_id, member_id)
        conversation_id = create_group(conn, user_id, name, description, valid_ids)
        add_message(conn, conversation_id, user_id, f"创建群聊「{name}」", "system", f"group-created-{conversation_id}")
        row = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        return {"conversation": serialize_conversation(conn, row, user_id), "participantIds": participant_ids(conn, conversation_id)}


def add_group_members(user_id: int, conversation_id: int, member_ids: list[int]):
    with db_session() as conn:
        require_participant(conn, user_id, conversation_id)
        conv = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        if not conv or conv["type"] != "group":
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="只能向群聊添加成员")
        requested_ids = sorted({int(uid) for uid in member_ids if int(uid) != user_id})
        if not requested_ids:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="请至少选择一名成员")
        stamp = now_iso()
        existing_ids = set(participant_ids(conn, conversation_id))
        valid_ids = [
            int(row["id"])
            for row in conn.execute(
                f"SELECT id FROM users WHERE id IN ({','.join('?' for _ in requested_ids)})",
                tuple(requested_ids),
            ).fetchall()
        ]
        if len(valid_ids) != len(requested_ids):
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="存在无效成员")
        for member_id in valid_ids:
            require_friend(conn, user_id, member_id)
        new_ids = [uid for uid in valid_ids if uid not in existing_ids]
        if not new_ids:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="成员已在群聊中")
        conn.executemany(
            """
            INSERT OR IGNORE INTO participants(conversation_id, user_id, joined_at, role)
            VALUES (?, ?, ?, 'member')
            """,
            [(conversation_id, uid, stamp) for uid in new_ids],
        )
        add_message(conn, conversation_id, user_id, f"添加 {len(new_ids)} 名成员进入群聊", "system", f"members-{conversation_id}-{int(time.time())}")
        conn.execute("UPDATE conversations SET updated_at = ? WHERE id = ?", (stamp, conversation_id))
        log_audit(conn, user_id, "conversation.members.added", "conversation", conversation_id, {"memberIds": new_ids})
        row = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        return {"conversation": serialize_conversation(conn, row, user_id), "participantIds": participant_ids(conn, conversation_id)}


def create_receipts(conn: sqlite3.Connection, conversation_id: int, message_id: int, sender_id: int):
    stamp = now_iso()
    for user_id in participant_ids(conn, conversation_id):
        status_value = "read" if user_id == sender_id else "delivered"
        read_at = stamp if user_id == sender_id else None
        conn.execute(
            """
            INSERT OR REPLACE INTO message_receipts(message_id, user_id, status, delivered_at, read_at)
            VALUES (?, ?, ?, ?, ?)
            """,
            (message_id, user_id, status_value, stamp, read_at),
        )


def add_message(conn: sqlite3.Connection, conversation_id: int, sender_id: int, content: str, kind: str = "text", client_id: str = "", reply_to_id: int | None = None) -> int:
    if client_id:
        existing = conn.execute(
            """
            SELECT id FROM messages WHERE conversation_id = ? AND sender_id = ? AND client_id = ?
            """,
            (conversation_id, sender_id, client_id),
        ).fetchone()
        if existing:
            return int(existing["id"])
    stamp = now_iso()
    cur = conn.execute(
        """
        INSERT INTO messages(conversation_id, sender_id, content, kind, client_id, status, reply_to_id, created_at)
        VALUES (?, ?, ?, ?, ?, 'sent', ?, ?)
        """,
        (conversation_id, sender_id, content.strip(), kind, client_id, reply_to_id, stamp),
    )
    message_id = int(cur.lastrowid)
    create_receipts(conn, conversation_id, message_id, sender_id)
    conn.execute("UPDATE conversations SET updated_at = ? WHERE id = ?", (stamp, conversation_id))
    log_audit(conn, sender_id, "message.sent", "message", message_id, {"conversationId": conversation_id, "kind": kind})
    return message_id


def receipt_counts(conn: sqlite3.Connection, message_id: int):
    rows = conn.execute("SELECT status, COUNT(*) AS c FROM message_receipts WHERE message_id = ? GROUP BY status", (message_id,)).fetchall()
    return {row["status"]: int(row["c"]) for row in rows}


def serialize_message(conn: sqlite3.Connection, row_or_id, user_id: int):
    row = row_or_id
    if isinstance(row_or_id, int):
        row = conn.execute(
            """
            SELECT messages.*, users.username, users.avatar_color
            FROM messages
            JOIN users ON users.id = messages.sender_id
            WHERE messages.id = ?
            """,
            (row_or_id,),
        ).fetchone()
    saved = conn.execute("SELECT 1 FROM saved_messages WHERE message_id = ? AND user_id = ?", (row["id"], user_id)).fetchone()
    return {
        "id": int(row["id"]),
        "conversationId": int(row["conversation_id"]),
        "senderId": int(row["sender_id"]),
        "senderName": row["username"],
        "senderUsername": row["username"],
        "senderColor": row["avatar_color"],
        "content": "消息已撤回" if row["deleted_at"] else row["content"],
        "kind": row["kind"],
        "clientId": row["client_id"],
        "status": "deleted" if row["deleted_at"] else row["status"],
        "editedAt": row["edited_at"],
        "deletedAt": row["deleted_at"],
        "replyToId": row["reply_to_id"],
        "createdAt": row["created_at"],
        "receipts": receipt_counts(conn, int(row["id"])),
        "saved": saved is not None,
    }


def list_messages(user_id: int, conversation_id: int):
    with db_session() as conn:
        require_participant(conn, user_id, conversation_id)
        rows = conn.execute(
            """
            SELECT messages.*, users.username, users.avatar_color
            FROM messages
            JOIN users ON users.id = messages.sender_id
            WHERE messages.conversation_id = ?
            ORDER BY messages.id ASC
            LIMIT 300
            """,
            (conversation_id,),
        ).fetchall()
        mark_read(conn, user_id, conversation_id)
        return {"messages": [serialize_message(conn, row, user_id) for row in rows]}


def send_message(user_id: int, conversation_id: int, content: str, client_id: str, reply_to_id: int | None):
    with db_session() as conn:
        require_participant(conn, user_id, conversation_id)
        message_id = add_message(conn, conversation_id, user_id, content, "text", client_id, reply_to_id)
        user = conn.execute("SELECT * FROM users WHERE id = ?", (user_id,)).fetchone()
        for uid in participant_ids(conn, conversation_id):
            if uid != user_id:
                create_notification(conn, uid, f"{user['username']} 发来新消息", content[:80], "message", message_id, "message")
        message = serialize_message(conn, message_id, user_id)
        return {"message": message, "participantIds": participant_ids(conn, conversation_id)}


def edit_message(user_id: int, message_id: int, content: str):
    with db_session() as conn:
        row = conn.execute("SELECT * FROM messages WHERE id = ?", (message_id,)).fetchone()
        if not row or int(row["sender_id"]) != user_id:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="只能编辑自己发送的消息")
        conn.execute("UPDATE messages SET content = ?, edited_at = ? WHERE id = ?", (content.strip(), now_iso(), message_id))
        log_audit(conn, user_id, "message.edited", "message", message_id, {})
        return {"message": serialize_message(conn, message_id, user_id), "participantIds": participant_ids(conn, int(row["conversation_id"]))}


def delete_message(user_id: int, message_id: int):
    with db_session() as conn:
        row = conn.execute("SELECT * FROM messages WHERE id = ?", (message_id,)).fetchone()
        if not row or int(row["sender_id"]) != user_id:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="只能撤回自己发送的消息")
        conn.execute("UPDATE messages SET deleted_at = ?, status = 'deleted' WHERE id = ?", (now_iso(), message_id))
        log_audit(conn, user_id, "message.deleted", "message", message_id, {})
        return {"message": serialize_message(conn, message_id, user_id), "participantIds": participant_ids(conn, int(row["conversation_id"]))}


def mark_read(conn: sqlite3.Connection, user_id: int, conversation_id: int):
    stamp = now_iso()
    rows = conn.execute("SELECT id FROM messages WHERE conversation_id = ?", (conversation_id,)).fetchall()
    max_id = max([int(row["id"]) for row in rows], default=0)
    for row in rows:
        conn.execute(
            """
            INSERT INTO message_receipts(message_id, user_id, status, delivered_at, read_at)
            VALUES (?, ?, 'read', ?, ?)
            ON CONFLICT(message_id, user_id) DO UPDATE SET status = 'read', read_at = excluded.read_at
            """,
            (int(row["id"]), user_id, stamp, stamp),
        )
    conn.execute("UPDATE participants SET last_read_id = MAX(last_read_id, ?) WHERE conversation_id = ? AND user_id = ?", (max_id, conversation_id, user_id))


def update_conversation_preferences(user_id: int, conversation_id: int, pinned, muted, archived):
    with db_session() as conn:
        require_participant(conn, user_id, conversation_id)
        if pinned is not None:
            conn.execute("UPDATE participants SET is_pinned = ? WHERE conversation_id = ? AND user_id = ?", (int(pinned), conversation_id, user_id))
        if muted is not None:
            conn.execute("UPDATE participants SET is_muted = ? WHERE conversation_id = ? AND user_id = ?", (int(muted), conversation_id, user_id))
        if archived is not None:
            conn.execute("UPDATE participants SET is_archived = ? WHERE conversation_id = ? AND user_id = ?", (int(archived), conversation_id, user_id))
        log_audit(conn, user_id, "conversation.preference.updated", "conversation", conversation_id, {"pinned": pinned, "muted": muted, "archived": archived})
        row = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        return {"conversation": serialize_conversation(conn, row, user_id)}


def leave_conversation(user_id: int, conversation_id: int):
    with db_session() as conn:
        membership = require_participant(conn, user_id, conversation_id)
        conversation = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        if not conversation:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="会话不存在")
        if conversation["type"] == "direct":
            conn.execute(
                """
                UPDATE participants
                SET is_archived = 1, is_pinned = 0
                WHERE conversation_id = ? AND user_id = ?
                """,
                (conversation_id, user_id),
            )
            log_audit(conn, user_id, "conversation.removed", "conversation", conversation_id, {"mode": "direct_archived"})
            return {"ok": True, "conversationId": conversation_id, "participantIds": [user_id], "mode": "archived"}
        if membership["role"] == "owner":
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="群主请使用解散群聊")
        conn.execute("DELETE FROM participants WHERE conversation_id = ? AND user_id = ?", (conversation_id, user_id))
        add_message(conn, conversation_id, user_id, "离开群聊", "system", f"leave-{conversation_id}-{user_id}-{int(time.time())}")
        log_audit(conn, user_id, "conversation.left", "conversation", conversation_id, {})
        return {"ok": True, "conversationId": conversation_id, "participantIds": [user_id, *participant_ids(conn, conversation_id)], "mode": "left"}


def dissolve_conversation(user_id: int, conversation_id: int):
    with db_session() as conn:
        membership = require_participant(conn, user_id, conversation_id)
        conversation = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
        if not conversation:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="会话不存在")
        if conversation["type"] != "group":
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="只有群聊可以解散")
        current_user = conn.execute("SELECT role FROM users WHERE id = ?", (user_id,)).fetchone()
        if membership["role"] != "owner" and current_user["role"] != "admin":
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="只有群主或管理员可以解散群聊")
        ids = participant_ids(conn, conversation_id)
        log_audit(conn, user_id, "conversation.dissolved", "conversation", conversation_id, {"name": conversation["name"]})
        conn.execute("DELETE FROM conversations WHERE id = ?", (conversation_id,))
        return {"ok": True, "conversationId": conversation_id, "participantIds": ids, "mode": "dissolved"}


def save_message(user_id: int, message_id: int):
    with db_session() as conn:
        row = conn.execute("SELECT conversation_id FROM messages WHERE id = ?", (message_id,)).fetchone()
        if not row:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="消息不存在")
        require_participant(conn, user_id, int(row["conversation_id"]))
        conn.execute("INSERT OR IGNORE INTO saved_messages(message_id, user_id, saved_at) VALUES (?, ?, ?)", (message_id, user_id, now_iso()))
        log_audit(conn, user_id, "message.saved", "message", message_id, {})
        return {"message": serialize_message(conn, message_id, user_id)}


def unsave_message(user_id: int, message_id: int):
    with db_session() as conn:
        conn.execute("DELETE FROM saved_messages WHERE message_id = ? AND user_id = ?", (message_id, user_id))
        log_audit(conn, user_id, "message.unsaved", "message", message_id, {})
        return {"ok": True}


def list_saved_messages(user_id: int):
    with db_session() as conn:
        rows = conn.execute(
            """
            SELECT messages.*, users.username, users.avatar_color
            FROM saved_messages
            JOIN messages ON messages.id = saved_messages.message_id
            JOIN users ON users.id = messages.sender_id
            WHERE saved_messages.user_id = ?
            ORDER BY saved_messages.saved_at DESC
            LIMIT 80
            """,
            (user_id,),
        ).fetchall()
        return {"messages": [serialize_message(conn, row, user_id) for row in rows]}


def list_notifications(user_id: int):
    with db_session() as conn:
        rows = conn.execute("SELECT * FROM notifications WHERE user_id = ? ORDER BY id DESC LIMIT 80", (user_id,)).fetchall()
        unread = conn.execute("SELECT COUNT(*) AS c FROM notifications WHERE user_id = ? AND is_read = 0", (user_id,)).fetchone()["c"]
        return {
            "unread": int(unread),
            "notifications": [
                {
                    "id": int(row["id"]),
                    "type": row["type"],
                    "title": row["title"],
                    "body": row["body"],
                    "entityType": row["entity_type"],
                    "entityId": row["entity_id"],
                    "read": bool(row["is_read"]),
                    "createdAt": row["created_at"],
                }
                for row in rows
            ],
        }


def mark_notifications_read(user_id: int, notification_ids: list[int] | None):
    with db_session() as conn:
        if notification_ids:
            placeholders = ",".join("?" for _ in notification_ids)
            conn.execute(f"UPDATE notifications SET is_read = 1 WHERE user_id = ? AND id IN ({placeholders})", (user_id, *notification_ids))
        else:
            conn.execute("UPDATE notifications SET is_read = 1 WHERE user_id = ?", (user_id,))
        log_audit(conn, user_id, "notifications.read", "notification", None, {"notificationIds": notification_ids or "all"})
        return list_notifications(user_id)


def list_audit_logs(user_id: int):
    with db_session() as conn:
        current = conn.execute("SELECT role FROM users WHERE id = ?", (user_id,)).fetchone()
        limit = 120 if current and current["role"] in {"admin", "manager"} else 40
        rows = conn.execute(
            """
            SELECT audit_logs.*, users.username
            FROM audit_logs
            LEFT JOIN users ON users.id = audit_logs.actor_id
            ORDER BY audit_logs.id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
        return {
            "logs": [
                {
                    "id": int(row["id"]),
                    "actorId": row["actor_id"],
                    "actorName": row["username"] or "系统",
                    "action": row["action"],
                    "entityType": row["entity_type"],
                    "entityId": row["entity_id"],
                    "detail": parse_json(row["detail"], {}),
                    "createdAt": row["created_at"],
                }
                for row in rows
            ]
        }


def search(user_id: int, keyword: str):
    keyword = keyword.strip()
    if not keyword:
        return {"users": [], "conversations": [], "messages": []}
    like = f"%{keyword}%"
    with db_session() as conn:
        conversations = [
            serialize_conversation(conn, row, user_id)
            for row in conn.execute(
                """
                SELECT conversations.*
                FROM conversations
                JOIN participants ON participants.conversation_id = conversations.id
                WHERE participants.user_id = ?
                  AND (conversations.name LIKE ? OR conversations.description LIKE ?)
                ORDER BY conversations.updated_at DESC
                LIMIT 20
                """,
                (user_id, like, like),
            ).fetchall()
        ]
        messages = [
            serialize_message(conn, row, user_id)
            for row in conn.execute(
                """
                SELECT messages.*, users.username, users.avatar_color
                FROM messages
                JOIN participants ON participants.conversation_id = messages.conversation_id
                JOIN users ON users.id = messages.sender_id
                WHERE participants.user_id = ?
                  AND messages.deleted_at IS NULL
                  AND messages.content LIKE ?
                ORDER BY messages.id DESC
                LIMIT 40
                """,
                (user_id, like),
            ).fetchall()
        ]
        return {"users": [], "conversations": conversations, "messages": messages}


def build_summary(user_id: int, conversation_id: int):
    with db_session() as conn:
        require_participant(conn, user_id, conversation_id)
        rows = conn.execute(
            """
            SELECT messages.content, messages.created_at, users.username
            FROM messages
            JOIN users ON users.id = messages.sender_id
            WHERE messages.conversation_id = ? AND messages.deleted_at IS NULL
            ORDER BY messages.id DESC
            LIMIT 30
            """,
            (conversation_id,),
        ).fetchall()
        rows = list(reversed(rows))
        if not rows:
            summary = "当前会话还没有消息，暂时无法生成摘要。"
            keywords: list[str] = []
            action_items: list[str] = []
        else:
            text = "\n".join(f"{row['username']}: {row['content']}" for row in rows)
            stop_words = {"我们", "你们", "今天", "已经", "可以", "需要", "最好", "服务", "消息", "展示", "系统", "功能", "完成"}
            tokens: dict[str, int] = {}
            buf = ""
            for ch in text:
                if "\u4e00" <= ch <= "\u9fff" or ch.isalnum() or ch in ["-", "_"]:
                    buf += ch
                else:
                    if len(buf) >= 2 and buf not in stop_words:
                        tokens[buf] = tokens.get(buf, 0) + 1
                    buf = ""
            if len(buf) >= 2 and buf not in stop_words:
                tokens[buf] = tokens.get(buf, 0) + 1
            keywords = [word for word, _ in sorted(tokens.items(), key=lambda item: (-item[1], item[0]))[:6]]
            latest = rows[-1]
            speakers = sorted({row["username"] for row in rows})
            summary = f"近 {len(rows)} 条消息主要围绕 {'、'.join(keywords[:3]) if keywords else '聊天内容'} 展开。参与人包括 {'、'.join(speakers)}。最近一条由 {latest['username']} 在 {latest['created_at']} 提出：{latest['content']}"
            action_items = []
            for row in rows:
                if any(mark in row["content"] for mark in ["准备", "建议", "要", "先", "待办", "升级", "验证"]):
                    action_items.append(f"{row['username']}: {row['content']}")
                if len(action_items) >= 4:
                    break
        stamp = now_iso()
        conn.execute(
            """
            INSERT INTO summaries(conversation_id, summary, keywords, action_items, message_count, created_at)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (conversation_id, summary, json.dumps(keywords, ensure_ascii=False), json.dumps(action_items, ensure_ascii=False), len(rows), stamp),
        )
        log_audit(conn, user_id, "ai.summary.created", "conversation", conversation_id, {"messageCount": len(rows)})
        return {"summary": {"summary": summary, "keywords": keywords, "actionItems": action_items, "messageCount": len(rows), "createdAt": stamp}}


def health():
    with db_session() as conn:
        return {
            "status": "ok",
            "service": "ai-native-im-enterprise",
            "now": now_iso(),
            "metrics": {
                "users": int(conn.execute("SELECT COUNT(*) AS c FROM users").fetchone()["c"]),
                "conversations": int(conn.execute("SELECT COUNT(*) AS c FROM conversations").fetchone()["c"]),
                "messages": int(conn.execute("SELECT COUNT(*) AS c FROM messages WHERE deleted_at IS NULL").fetchone()["c"]),
                "online": len(online_ids(conn)),
                "friendships": int(conn.execute("SELECT COUNT(*) AS c FROM friendships WHERE status = 'accepted'").fetchone()["c"]),
                "notifications": int(conn.execute("SELECT COUNT(*) AS c FROM notifications WHERE is_read = 0").fetchone()["c"]),
                "auditLogs": int(conn.execute("SELECT COUNT(*) AS c FROM audit_logs").fetchone()["c"]),
            },
            "modules": ["auth", "friends", "conversation", "message", "receipt", "notification", "audit", "ai-summary", "sse"],
        }
