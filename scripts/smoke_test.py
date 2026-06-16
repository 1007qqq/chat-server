import os
import sys
import tempfile
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT_DIR))

from app import config, database, services


def main():
    fd, db_path = tempfile.mkstemp(suffix=".db")
    os.close(fd)

    original_config_path = config.DB_PATH
    original_database_path = database.DB_PATH
    config.DB_PATH = db_path
    database.DB_PATH = db_path

    try:
        services.init_db()
        metrics = services.health()["metrics"]
        assert metrics["users"] == 0, metrics
        assert metrics["conversations"] == 0, metrics
        assert metrics["messages"] == 0, metrics

        user_a = services.auth_register("verify_user_a", "123456")
        user_b = services.auth_register("verify_user_b", "123456")
        user_a_id = user_a["user"]["id"]
        user_b_id = user_b["user"]["id"]

        search_result = services.search_users_for_friend(user_a_id, "verify_user_b")["users"]
        assert search_result, "expected user search result"
        assert search_result[0]["username"] == "verify_user_b"
        assert search_result[0]["friendshipStatus"] == "none"

        request = services.create_friend_request(user_a_id, user_b_id, None)["request"]
        assert request["status"] == "pending"
        with services.db_session() as conn:
            assert not services.are_friends(conn, user_a_id, user_b_id)

        incoming = services.list_friend_requests(user_b_id)["incoming"]
        assert len(incoming) == 1
        assert incoming[0]["requester"]["username"] == "verify_user_a"

        accepted = services.accept_friend_request(user_b_id, incoming[0]["id"])
        assert accepted["friend"]["username"] == "verify_user_a"
        with services.db_session() as conn:
            assert services.are_friends(conn, user_a_id, user_b_id)

        conversation = services.open_direct(user_a_id, user_b_id)["conversation"]
        assert conversation["type"] == "direct"
        assert conversation["title"] == "verify_user_b"

        message = services.send_message(
            user_a_id,
            conversation["id"],
            "hello from smoke test",
            "smoke-client-id",
            None,
        )["message"]
        assert message["content"] == "hello from smoke test"

        services.save_message(user_a_id, message["id"])
        saved = services.list_saved_messages(user_a_id)["messages"]
        assert saved and saved[0]["content"] == "hello from smoke test"

        search = services.search(user_a_id, "hello")
        assert search["users"] == []
        assert search["messages"]

        summary = services.build_summary(user_a_id, conversation["id"])["summary"]
        assert summary["messageCount"] >= 1

        print("smoke test passed")
    finally:
        config.DB_PATH = original_config_path
        database.DB_PATH = original_database_path
        for suffix in ("", "-shm", "-wal"):
            try:
                os.remove(f"{db_path}{suffix}")
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    main()
