#pragma once

#include "http.hpp"
#include "json.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace chat {

class ChatApp {
public:
    ChatApp(std::filesystem::path data_path, std::filesystem::path dist_dir);
    HttpResponse handle(const HttpRequest &request);

private:
    struct UserContext {
        int id = 0;
        std::string token;
    };

    HttpResponse route_api(const HttpRequest &request);
    HttpResponse route_static(const HttpRequest &request);

    Json login(const Json &payload);
    Json register_user(const Json &payload);
    Json logout(const UserContext &ctx);
    Json me(const UserContext &ctx);
    Json settings(const UserContext &ctx);
    Json update_settings(const UserContext &ctx, const Json &payload);
    Json list_friends(const UserContext &ctx);
    Json search_users(const UserContext &ctx, const std::string &keyword);
    Json create_friend_request(const UserContext &ctx, const Json &payload);
    Json list_friend_requests(const UserContext &ctx);
    Json accept_friend_request(const UserContext &ctx, const Json &payload);
    Json reject_friend_request(const UserContext &ctx, const Json &payload);
    Json list_conversations(const UserContext &ctx, bool include_archived);
    Json open_direct(const UserContext &ctx, const Json &payload);
    Json open_group(const UserContext &ctx, const Json &payload);
    Json add_group_members(const UserContext &ctx, const Json &payload);
    Json update_conversation_preferences(const UserContext &ctx, const Json &payload);
    Json leave_conversation(const UserContext &ctx, const Json &payload);
    Json dissolve_conversation(const UserContext &ctx, const Json &payload);
    Json conversation_members(const UserContext &ctx, int conversation_id);
    Json list_messages(const UserContext &ctx, int conversation_id);
    Json send_message(const UserContext &ctx, const Json &payload);
    Json edit_message(const UserContext &ctx, const Json &payload);
    Json delete_message(const UserContext &ctx, const Json &payload);
    Json save_message(const UserContext &ctx, const Json &payload);
    Json unsave_message(const UserContext &ctx, int message_id);
    Json list_saved_messages(const UserContext &ctx);
    Json list_notifications(const UserContext &ctx);
    Json mark_notifications_read(const UserContext &ctx, const Json &payload);
    Json list_audit_logs(const UserContext &ctx);
    Json search(const UserContext &ctx, const std::string &keyword);
    Json build_summary(const UserContext &ctx, const Json &payload);
    Json health();

    std::optional<UserContext> authenticate(const HttpRequest &request);
    UserContext require_user(const HttpRequest &request);
    Json parse_body(const HttpRequest &request);
    HttpResponse json_response(const Json &value, int status = 200);
    HttpResponse error_response(int status, const std::string &detail);

    void load();
    void save();
    void ensure_state();
    int next_id(const std::string &key);

    Json *find_by_id(Json::array &items, int id);
    const Json *find_by_id(const Json::array &items, int id) const;
    Json *find_user(int id);
    const Json *find_user(int id) const;
    Json *find_user_by_username(const std::string &username);
    const Json *find_user_by_username(const std::string &username) const;
    Json *find_conversation(int id);
    const Json *find_conversation(int id) const;
    Json *find_participant(int conversation_id, int user_id);
    const Json *find_participant(int conversation_id, int user_id) const;
    Json *find_message(int id);
    const Json *find_message(int id) const;

    bool are_friends(int user_id, int friend_id) const;
    void ensure_friendship(int user_id, int friend_id);
    void require_friend(int user_id, int friend_id) const;
    void require_participant(int user_id, int conversation_id) const;
    std::vector<int> participant_ids(int conversation_id) const;
    Json conversation_member_list(int conversation_id) const;
    Json serialize_user(const Json &user) const;
    Json serialize_user_for_view(const Json &user, int viewer_id) const;
    Json serialize_friend_request(const Json &request, int viewer_id) const;
    Json serialize_conversation(const Json &conversation, int viewer_id) const;
    Json serialize_message(const Json &message, int viewer_id) const;
    Json serialize_notification(const Json &notification) const;
    Json get_settings_for_user(int user_id);
    std::string friendship_status(int viewer_id, int target_id) const;
    int ensure_direct_conversation(int user_a, int user_b);
    int create_group_internal(int owner_id, const std::string &name, const std::string &description, const std::vector<int> &member_ids);
    int add_message_internal(int conversation_id, int sender_id, const std::string &content, const std::string &kind, const std::string &client_id, int reply_to_id = 0);
    void create_receipts(int conversation_id, int message_id, int sender_id);
    Json receipt_counts(int message_id) const;
    void mark_read(int user_id, int conversation_id);
    void create_notification(int user_id, const std::string &title, const std::string &body, const std::string &entity_type, int entity_id, const std::string &type);
    void log_audit(int actor_id, const std::string &action, const std::string &entity_type, int entity_id, const Json &detail);
    std::string now_iso() const;
    double now_epoch() const;
    std::string make_token() const;
    std::string password_hash(const std::string &username, const std::string &password) const;
    std::string normalize_username(const std::string &username) const;
    bool contains_case_insensitive(const std::string &text, const std::string &needle) const;
    std::string extract_token(const HttpRequest &request) const;
    std::string content_type_for(const std::filesystem::path &path) const;

    std::filesystem::path data_path_;
    std::filesystem::path dist_dir_;
    mutable std::mutex mutex_;
    Json state_;
};

} // namespace chat
