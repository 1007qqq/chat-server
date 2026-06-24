#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace chat {

namespace {

constexpr double SESSION_TTL_SECONDS = 60.0 * 60.0 * 24.0 * 30.0;
constexpr double PRESENCE_TTL_SECONDS = 75.0;

class ApiError : public std::runtime_error {
public:
    ApiError(int status_code, const std::string &message)
        : std::runtime_error(message), status_code_(status_code) {}

    int status_code() const { return status_code_; }

private:
    int status_code_;
};

std::string trim(const std::string &value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool truthy_param(const std::string &value) {
    std::string normalized = lower(value);
    return normalized == "1" || normalized == "true" || normalized == "yes";
}

Json::array &items(Json &state, const std::string &name) {
    return state[name].as_array_mut();
}

const Json::array &items_const(const Json &state, const std::string &name) {
    return state[name].as_array();
}

std::vector<int> json_int_array(const Json &value) {
    std::vector<int> out;
    for (const auto &item : value.as_array()) {
        int id = item.as_int(0);
        if (id > 0) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Json int_array_json(const std::vector<int> &values) {
    Json::array array;
    for (int value : values) array.emplace_back(value);
    return Json(array);
}

Json nullable_int(int value) {
    return value > 0 ? Json(value) : Json(nullptr);
}

std::string join(const std::vector<std::string> &values, const std::string &separator) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << separator;
        out << values[i];
    }
    return out.str();
}

} // namespace

ChatApp::ChatApp(std::filesystem::path data_path, std::filesystem::path dist_dir)
    : data_path_(std::move(data_path)), dist_dir_(std::move(dist_dir)) {
    load();
}

HttpResponse ChatApp::handle(const HttpRequest &request) {
    try {
        if (request.path.rfind("/api/", 0) == 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            return route_api(request);
        }
        return route_static(request);
    } catch (const ApiError &error) {
        return error_response(error.status_code(), error.what());
    } catch (const std::exception &error) {
        return error_response(500, error.what());
    }
}

HttpResponse ChatApp::route_api(const HttpRequest &request) {
    const auto query = parse_query(request.query);

    if (request.method == "GET" && request.path == "/api/health") {
        return json_response(health());
    }
    if (request.method == "POST" && request.path == "/api/login") {
        return json_response(login(parse_body(request)));
    }
    if (request.method == "POST" && request.path == "/api/register") {
        return json_response(register_user(parse_body(request)));
    }
    if (request.method == "GET" && request.path == "/api/stream") {
        UserContext ctx = require_user(request);
        (void)ctx;
        HttpResponse response;
        response.content_type = "text/event-stream; charset=utf-8";
        response.headers["Cache-Control"] = "no-cache";
        response.body = "event: ready\ndata: {\"type\":\"ready\"}\n\n";
        return response;
    }

    UserContext ctx = require_user(request);
    Json payload;
    if (request.method == "POST" || request.method == "PATCH" || request.method == "DELETE") {
        payload = parse_body(request);
    }

    if (request.method == "POST" && request.path == "/api/logout") return json_response(logout(ctx));
    if (request.method == "GET" && request.path == "/api/me") return json_response(me(ctx));
    if (request.method == "PATCH" && request.path == "/api/profile") return json_response(me(ctx));
    if (request.method == "GET" && request.path == "/api/settings") return json_response(settings(ctx));
    if (request.method == "PATCH" && request.path == "/api/settings") return json_response(update_settings(ctx, payload));
    if (request.method == "GET" && request.path == "/api/users") return json_response(list_friends(ctx));
    if (request.method == "GET" && request.path == "/api/users/search") {
        auto it = query.find("q");
        return json_response(search_users(ctx, it == query.end() ? "" : it->second));
    }
    if (request.method == "POST" && request.path == "/api/friends") return json_response(create_friend_request(ctx, payload));
    if (request.method == "GET" && request.path == "/api/friend-requests") return json_response(list_friend_requests(ctx));
    if (request.method == "POST" && request.path == "/api/friend-requests/accept") return json_response(accept_friend_request(ctx, payload));
    if (request.method == "POST" && request.path == "/api/friend-requests/reject") return json_response(reject_friend_request(ctx, payload));
    if (request.method == "GET" && request.path == "/api/conversations") {
        auto it = query.find("includeArchived");
        return json_response(list_conversations(ctx, it != query.end() && truthy_param(it->second)));
    }
    if (request.method == "POST" && request.path == "/api/conversations/direct") return json_response(open_direct(ctx, payload));
    if (request.method == "POST" && request.path == "/api/groups") return json_response(open_group(ctx, payload));
    if (request.method == "POST" && request.path == "/api/groups/members") return json_response(add_group_members(ctx, payload));
    if (request.method == "POST" && request.path == "/api/conversations/preferences") return json_response(update_conversation_preferences(ctx, payload));
    if (request.method == "POST" && request.path == "/api/conversations/leave") return json_response(leave_conversation(ctx, payload));
    if (request.method == "DELETE" && request.path == "/api/conversations") return json_response(dissolve_conversation(ctx, payload));

    const std::string conv_prefix = "/api/conversations/";
    const std::string members_suffix = "/members";
    if (request.method == "GET" && request.path.rfind(conv_prefix, 0) == 0 &&
        request.path.size() > conv_prefix.size() + members_suffix.size() &&
        request.path.compare(request.path.size() - members_suffix.size(), members_suffix.size(), members_suffix) == 0) {
        std::string id_part = request.path.substr(conv_prefix.size(), request.path.size() - conv_prefix.size() - members_suffix.size());
        return json_response(conversation_members(ctx, std::stoi(id_part)));
    }

    if (request.method == "GET" && request.path == "/api/messages") {
        auto it = query.find("conversationId");
        if (it == query.end()) throw ApiError(400, "缺少 conversationId");
        return json_response(list_messages(ctx, std::stoi(it->second)));
    }
    if (request.method == "POST" && request.path == "/api/messages") return json_response(send_message(ctx, payload));
    if (request.method == "PATCH" && request.path == "/api/messages") return json_response(edit_message(ctx, payload));
    if (request.method == "DELETE" && request.path == "/api/messages") return json_response(delete_message(ctx, payload));
    if (request.method == "POST" && request.path == "/api/messages/save") return json_response(save_message(ctx, payload));

    const std::string save_prefix = "/api/messages/save/";
    if (request.method == "DELETE" && request.path.rfind(save_prefix, 0) == 0) {
        return json_response(unsave_message(ctx, std::stoi(request.path.substr(save_prefix.size()))));
    }

    if (request.method == "GET" && request.path == "/api/saved-messages") return json_response(list_saved_messages(ctx));
    if (request.method == "GET" && request.path == "/api/notifications") return json_response(list_notifications(ctx));
    if (request.method == "POST" && request.path == "/api/notifications/read") return json_response(mark_notifications_read(ctx, payload));
    if (request.method == "GET" && request.path == "/api/audit") return json_response(list_audit_logs(ctx));
    if (request.method == "GET" && request.path == "/api/search") {
        auto it = query.find("q");
        return json_response(search(ctx, it == query.end() ? "" : it->second));
    }
    if (request.method == "POST" && request.path == "/api/summary") return json_response(build_summary(ctx, payload));

    throw ApiError(404, "接口不存在");
}

HttpResponse ChatApp::route_static(const HttpRequest &request) {
    if (request.method != "GET") return error_response(404, "资源不存在");
    if (!std::filesystem::exists(dist_dir_)) {
        return json_response(Json::object{{"message", "Frontend is not built. Run npm install && npm run build in ./frontend."}});
    }

    std::string path = request.path == "/" ? "/index.html" : request.path;
    if (path.find("..") != std::string::npos) return error_response(404, "资源不存在");

    std::filesystem::path file = dist_dir_ / path.substr(1);
    if (!std::filesystem::exists(file) || std::filesystem::is_directory(file)) {
        file = dist_dir_ / "index.html";
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) return error_response(404, "资源不存在");
    std::ostringstream buffer;
    buffer << in.rdbuf();

    HttpResponse response;
    response.content_type = content_type_for(file);
    response.body = buffer.str();
    return response;
}

Json ChatApp::login(const Json &payload) {
    std::string username = normalize_username(payload["username"].as_string());
    std::string password = payload["password"].as_string();
    if (username.size() < 3 || password.size() < 6) throw ApiError(400, "账号或密码格式不正确");

    Json *user = find_user_by_username(username);
    if (!user || (*user)["passwordHash"].as_string() != password_hash(username, password)) {
        throw ApiError(401, "用户名或密码错误");
    }

    std::string token = make_token();
    double now = now_epoch();
    (*user)["lastSeen"] = now;
    items(state_, "sessions").push_back(Json::object{
        {"token", token},
        {"userId", (*user)["id"].as_int()},
        {"createdAt", now},
        {"lastSeen", now},
    });
    log_audit((*user)["id"].as_int(), "auth.login", "session", 0, Json::object{{"username", username}});
    Json response = Json::object{{"token", token}, {"user", serialize_user(*user)}};
    save();
    return response;
}

Json ChatApp::register_user(const Json &payload) {
    std::string username = normalize_username(payload["username"].as_string());
    std::string password = payload["password"].as_string();
    if (username.size() < 3) throw ApiError(400, "账号至少需要 3 个字符");
    if (password.size() < 6) throw ApiError(400, "密码至少需要 6 个字符");
    if (find_user_by_username(username)) throw ApiError(409, "用户名已存在");

    int id = next_id("users");
    std::string stamp = now_iso();
    std::string color = "#" + make_token().substr(0, 6);
    items(state_, "users").push_back(Json::object{
        {"id", id},
        {"username", username},
        {"displayName", username},
        {"passwordHash", password_hash(username, password)},
        {"avatarColor", color},
        {"role", "member"},
        {"department", ""},
        {"title", ""},
        {"statusMessage", ""},
        {"phone", ""},
        {"location", ""},
        {"createdAt", stamp},
        {"lastSeen", 0},
        {"settings", Json::object{
            {"theme", "light"},
            {"density", "comfortable"},
            {"notifyDesktop", true},
            {"enterToSend", true},
            {"updatedAt", stamp},
        }},
    });
    create_notification(id, "账号已创建", "账号已创建。", "user", id, "system");
    log_audit(id, "user.created", "user", id, Json::object{{"username", username}});
    save();
    return login(payload);
}

Json ChatApp::logout(const UserContext &ctx) {
    auto &sessions = items(state_, "sessions");
    sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [&](const Json &session) {
        return session["token"].as_string() == ctx.token;
    }), sessions.end());
    log_audit(ctx.id, "auth.logout", "session", 0, Json::object{});
    save();
    return Json::object{{"ok", true}};
}

Json ChatApp::me(const UserContext &ctx) {
    Json *user = find_user(ctx.id);
    if (!user) throw ApiError(401, "登录已失效");
    return Json::object{{"user", serialize_user(*user)}, {"settings", get_settings_for_user(ctx.id)}};
}

Json ChatApp::settings(const UserContext &ctx) {
    return get_settings_for_user(ctx.id);
}

Json ChatApp::update_settings(const UserContext &ctx, const Json &payload) {
    Json *user = find_user(ctx.id);
    if (!user) throw ApiError(401, "登录已失效");
    Json current = get_settings_for_user(ctx.id);
    Json next = current;
    if (payload.contains("theme") && payload["theme"].is_string()) next["theme"] = payload["theme"].as_string();
    if (payload.contains("density") && payload["density"].is_string()) next["density"] = payload["density"].as_string();
    if (payload.contains("notifyDesktop") && payload["notifyDesktop"].is_bool()) next["notifyDesktop"] = payload["notifyDesktop"].as_bool();
    if (payload.contains("enterToSend") && payload["enterToSend"].is_bool()) next["enterToSend"] = payload["enterToSend"].as_bool();
    next["updatedAt"] = now_iso();
    (*user)["settings"] = next;
    log_audit(ctx.id, "settings.updated", "user_settings", ctx.id, next);
    save();
    return Json::object{{"settings", next}};
}

Json ChatApp::list_friends(const UserContext &ctx) {
    Json::array users;
    for (const auto &friendship : items_const(state_, "friendships")) {
        if (friendship["userId"].as_int() == ctx.id && friendship["status"].as_string() == "accepted") {
            const Json *user = find_user(friendship["friendId"].as_int());
            if (!user) continue;
            Json item = serialize_user_for_view(*user, ctx.id);
            item["friendCreatedAt"] = friendship["createdAt"].as_string();
            users.push_back(item);
        }
    }
    std::sort(users.begin(), users.end(), [](const Json &a, const Json &b) {
        return a["username"].as_string() < b["username"].as_string();
    });
    return Json::object{{"users", users}, {"departments", Json::array{}}};
}

Json ChatApp::search_users(const UserContext &ctx, const std::string &keyword) {
    std::string q = trim(keyword);
    Json::array users;
    if (!q.empty()) {
        for (const auto &user : items_const(state_, "users")) {
            if (user["id"].as_int() == ctx.id) continue;
            if (!contains_case_insensitive(user["username"].as_string(), q)) continue;
            users.push_back(serialize_user_for_view(user, ctx.id));
            if (users.size() >= 20) break;
        }
    }
    return Json::object{{"users", users}};
}

Json ChatApp::create_friend_request(const UserContext &ctx, const Json &payload) {
    int target_id = payload["targetUserId"].as_int(0);
    Json *target = target_id > 0 ? find_user(target_id) : find_user_by_username(normalize_username(payload["username"].as_string()));
    if (!target) throw ApiError(404, "用户不存在");
    target_id = (*target)["id"].as_int();
    if (target_id == ctx.id) throw ApiError(400, "不能添加自己为好友");
    if (are_friends(ctx.id, target_id)) throw ApiError(409, "已经是好友");

    for (auto &request : items(state_, "friendRequests")) {
        if (request["status"].as_string() != "pending") continue;
        if (request["requesterId"].as_int() == target_id && request["recipientId"].as_int() == ctx.id) {
            request["status"] = "accepted";
            request["respondedAt"] = now_iso();
            ensure_friendship(ctx.id, target_id);
            create_notification(target_id, "好友申请已通过", serialize_user(*find_user(ctx.id))["username"].as_string() + " 已通过你的好友申请。", "user", ctx.id, "friend");
            log_audit(ctx.id, "friend.accepted", "user", target_id, Json::object{{"requestId", request["id"].as_int()}});
            Json response = Json::object{{"request", serialize_friend_request(request, ctx.id)}};
            save();
            return response;
        }
        if (request["requesterId"].as_int() == ctx.id && request["recipientId"].as_int() == target_id) {
            return Json::object{{"request", serialize_friend_request(request, ctx.id)}};
        }
    }

    Json request = Json::object{
        {"id", next_id("friendRequests")},
        {"requesterId", ctx.id},
        {"recipientId", target_id},
        {"status", "pending"},
        {"createdAt", now_iso()},
        {"respondedAt", nullptr},
    };
    items(state_, "friendRequests").push_back(request);
    create_notification(target_id, "新的好友申请", serialize_user(*find_user(ctx.id))["username"].as_string() + " 请求添加你为好友。", "friend_request", request["id"].as_int(), "friend");
    log_audit(ctx.id, "friend.requested", "user", target_id, Json::object{{"username", (*target)["username"].as_string()}});
    save();
    return Json::object{{"request", serialize_friend_request(request, ctx.id)}};
}

Json ChatApp::list_friend_requests(const UserContext &ctx) {
    Json::array requests;
    for (const auto &request : items_const(state_, "friendRequests")) {
        if (request["status"].as_string() != "pending") continue;
        if (request["requesterId"].as_int() == ctx.id || request["recipientId"].as_int() == ctx.id) {
            requests.push_back(serialize_friend_request(request, ctx.id));
        }
    }
    std::sort(requests.begin(), requests.end(), [](const Json &a, const Json &b) {
        return a["id"].as_int() > b["id"].as_int();
    });
    Json::array incoming;
    Json::array outgoing;
    for (const auto &request : requests) {
        if (request["direction"].as_string() == "incoming") incoming.push_back(request);
        else outgoing.push_back(request);
    }
    return Json::object{{"requests", requests}, {"incoming", incoming}, {"outgoing", outgoing}};
}

Json ChatApp::accept_friend_request(const UserContext &ctx, const Json &payload) {
    int request_id = payload["requestId"].as_int();
    for (auto &request : items(state_, "friendRequests")) {
        if (request["id"].as_int() != request_id) continue;
        if (request["recipientId"].as_int() != ctx.id) throw ApiError(403, "只能处理发给你的好友申请");
        if (request["status"].as_string() != "pending") throw ApiError(409, "好友申请已处理");
        int requester_id = request["requesterId"].as_int();
        request["status"] = "accepted";
        request["respondedAt"] = now_iso();
        ensure_friendship(ctx.id, requester_id);
        create_notification(requester_id, "好友申请已通过", serialize_user(*find_user(ctx.id))["username"].as_string() + " 已通过你的好友申请。", "user", ctx.id, "friend");
        log_audit(ctx.id, "friend.accepted", "user", requester_id, Json::object{{"requestId", request_id}});
        save();
        return Json::object{{"friend", serialize_user_for_view(*find_user(requester_id), ctx.id)}};
    }
    throw ApiError(404, "好友申请不存在");
}

Json ChatApp::reject_friend_request(const UserContext &ctx, const Json &payload) {
    int request_id = payload["requestId"].as_int();
    for (auto &request : items(state_, "friendRequests")) {
        if (request["id"].as_int() != request_id) continue;
        if (request["recipientId"].as_int() != ctx.id) throw ApiError(403, "只能处理发给你的好友申请");
        if (request["status"].as_string() != "pending") throw ApiError(409, "好友申请已处理");
        int requester_id = request["requesterId"].as_int();
        request["status"] = "rejected";
        request["respondedAt"] = now_iso();
        create_notification(requester_id, "好友申请未通过", "对方已拒绝你的好友申请。", "friend_request", request_id, "friend");
        log_audit(ctx.id, "friend.rejected", "user", requester_id, Json::object{{"requestId", request_id}});
        save();
        return Json::object{{"ok", true}};
    }
    throw ApiError(404, "好友申请不存在");
}

Json ChatApp::list_conversations(const UserContext &ctx, bool include_archived) {
    Json::array conversations;
    for (const auto &participant : items_const(state_, "participants")) {
        if (participant["userId"].as_int() != ctx.id) continue;
        if (!include_archived && participant["archived"].as_bool(false)) continue;
        const Json *conversation = find_conversation(participant["conversationId"].as_int());
        if (conversation) conversations.push_back(serialize_conversation(*conversation, ctx.id));
    }
    std::sort(conversations.begin(), conversations.end(), [](const Json &a, const Json &b) {
        if (a["pinned"].as_bool() != b["pinned"].as_bool()) return a["pinned"].as_bool();
        if (a["updatedAt"].as_string() != b["updatedAt"].as_string()) return a["updatedAt"].as_string() > b["updatedAt"].as_string();
        return a["id"].as_int() > b["id"].as_int();
    });
    return Json::object{{"conversations", conversations}};
}

Json ChatApp::open_direct(const UserContext &ctx, const Json &payload) {
    int target_id = payload["targetUserId"].as_int();
    if (target_id == ctx.id) throw ApiError(400, "不能和自己创建私聊");
    if (!find_user(target_id)) throw ApiError(404, "用户不存在");
    require_friend(ctx.id, target_id);
    int conversation_id = ensure_direct_conversation(ctx.id, target_id);
    if (Json *participant = find_participant(conversation_id, ctx.id)) {
        (*participant)["archived"] = false;
    }
    Json *conversation = find_conversation(conversation_id);
    save();
    return Json::object{{"conversation", serialize_conversation(*conversation, ctx.id)}};
}

Json ChatApp::open_group(const UserContext &ctx, const Json &payload) {
    std::string name = trim(payload["name"].as_string());
    if (name.empty()) throw ApiError(400, "请输入群名称");
    std::vector<int> member_ids = json_int_array(payload["memberIds"]);
    member_ids.erase(std::remove(member_ids.begin(), member_ids.end(), ctx.id), member_ids.end());
    if (member_ids.empty()) throw ApiError(400, "请至少选择一名成员");
    for (int member_id : member_ids) {
        if (!find_user(member_id)) throw ApiError(400, "存在无效成员");
        require_friend(ctx.id, member_id);
    }
    int conversation_id = create_group_internal(ctx.id, name, payload["description"].as_string(), member_ids);
    add_message_internal(conversation_id, ctx.id, "创建群聊「" + name + "」", "system", "group-created-" + std::to_string(conversation_id));
    Json *conversation = find_conversation(conversation_id);
    save();
    return Json::object{{"conversation", serialize_conversation(*conversation, ctx.id)}};
}

Json ChatApp::add_group_members(const UserContext &ctx, const Json &payload) {
    int conversation_id = payload["conversationId"].as_int();
    require_participant(ctx.id, conversation_id);
    Json *conversation = find_conversation(conversation_id);
    if (!conversation || (*conversation)["type"].as_string() != "group") throw ApiError(400, "只能向群聊添加成员");
    std::vector<int> requested = json_int_array(payload["memberIds"]);
    requested.erase(std::remove(requested.begin(), requested.end(), ctx.id), requested.end());
    if (requested.empty()) throw ApiError(400, "请至少选择一名成员");

    std::vector<int> existing = participant_ids(conversation_id);
    std::vector<int> new_ids;
    for (int member_id : requested) {
        if (!find_user(member_id)) throw ApiError(400, "存在无效成员");
        require_friend(ctx.id, member_id);
        if (std::find(existing.begin(), existing.end(), member_id) == existing.end()) new_ids.push_back(member_id);
    }
    if (new_ids.empty()) throw ApiError(400, "成员已在群聊中");

    std::string stamp = now_iso();
    for (int member_id : new_ids) {
        items(state_, "participants").push_back(Json::object{
            {"conversationId", conversation_id},
            {"userId", member_id},
            {"joinedAt", stamp},
            {"lastReadId", 0},
            {"role", "member"},
            {"pinned", false},
            {"muted", false},
            {"archived", false},
        });
    }
    add_message_internal(conversation_id, ctx.id, "添加 " + std::to_string(new_ids.size()) + " 名成员进入群聊", "system", "members-" + std::to_string(conversation_id) + "-" + std::to_string(static_cast<long long>(now_epoch())));
    (*conversation)["updatedAt"] = stamp;
    log_audit(ctx.id, "conversation.members.added", "conversation", conversation_id, Json::object{{"memberIds", int_array_json(new_ids)}});
    save();
    return Json::object{{"conversation", serialize_conversation(*conversation, ctx.id)}};
}

Json ChatApp::update_conversation_preferences(const UserContext &ctx, const Json &payload) {
    int conversation_id = payload["conversationId"].as_int();
    Json *participant = find_participant(conversation_id, ctx.id);
    if (!participant) throw ApiError(403, "无权访问该会话");
    if (payload.contains("pinned") && payload["pinned"].is_bool()) (*participant)["pinned"] = payload["pinned"].as_bool();
    if (payload.contains("muted") && payload["muted"].is_bool()) (*participant)["muted"] = payload["muted"].as_bool();
    if (payload.contains("archived") && payload["archived"].is_bool()) (*participant)["archived"] = payload["archived"].as_bool();
    Json *conversation = find_conversation(conversation_id);
    log_audit(ctx.id, "conversation.preference.updated", "conversation", conversation_id, Json::object{
        {"pinned", payload.contains("pinned") ? payload["pinned"] : Json(nullptr)},
        {"muted", payload.contains("muted") ? payload["muted"] : Json(nullptr)},
        {"archived", payload.contains("archived") ? payload["archived"] : Json(nullptr)},
    });
    save();
    return Json::object{{"conversation", serialize_conversation(*conversation, ctx.id)}};
}

Json ChatApp::leave_conversation(const UserContext &ctx, const Json &payload) {
    int conversation_id = payload["conversationId"].as_int();
    Json *participant = find_participant(conversation_id, ctx.id);
    if (!participant) throw ApiError(403, "无权访问该会话");
    Json *conversation = find_conversation(conversation_id);
    if (!conversation) throw ApiError(404, "会话不存在");
    if ((*conversation)["type"].as_string() == "direct") {
        (*participant)["archived"] = true;
        (*participant)["pinned"] = false;
        log_audit(ctx.id, "conversation.removed", "conversation", conversation_id, Json::object{{"mode", "direct_archived"}});
        save();
        return Json::object{{"ok", true}, {"mode", "archived"}};
    }
    if ((*participant)["role"].as_string() == "owner") throw ApiError(400, "群主请使用解散群聊");
    auto &participants = items(state_, "participants");
    participants.erase(std::remove_if(participants.begin(), participants.end(), [&](const Json &item) {
        return item["conversationId"].as_int() == conversation_id && item["userId"].as_int() == ctx.id;
    }), participants.end());
    add_message_internal(conversation_id, ctx.id, "离开群聊", "system", "leave-" + std::to_string(conversation_id) + "-" + std::to_string(ctx.id));
    log_audit(ctx.id, "conversation.left", "conversation", conversation_id, Json::object{});
    save();
    return Json::object{{"ok", true}, {"mode", "left"}};
}

Json ChatApp::dissolve_conversation(const UserContext &ctx, const Json &payload) {
    int conversation_id = payload["conversationId"].as_int();
    Json *participant = find_participant(conversation_id, ctx.id);
    if (!participant) throw ApiError(403, "无权访问该会话");
    Json *conversation = find_conversation(conversation_id);
    if (!conversation) throw ApiError(404, "会话不存在");
    if ((*conversation)["type"].as_string() != "group") throw ApiError(400, "只有群聊可以解散");
    const Json *user = find_user(ctx.id);
    if ((*participant)["role"].as_string() != "owner" && (!user || (*user)["role"].as_string() != "admin")) {
        throw ApiError(403, "只有群主或管理员可以解散群聊");
    }

    std::vector<int> message_ids;
    for (const auto &message : items_const(state_, "messages")) {
        if (message["conversationId"].as_int() == conversation_id) message_ids.push_back(message["id"].as_int());
    }
    auto has_message_id = [&](const Json &item) {
        int message_id = item["messageId"].as_int();
        return std::find(message_ids.begin(), message_ids.end(), message_id) != message_ids.end();
    };

    items(state_, "conversations").erase(std::remove_if(items(state_, "conversations").begin(), items(state_, "conversations").end(), [&](const Json &item) {
        return item["id"].as_int() == conversation_id;
    }), items(state_, "conversations").end());
    items(state_, "participants").erase(std::remove_if(items(state_, "participants").begin(), items(state_, "participants").end(), [&](const Json &item) {
        return item["conversationId"].as_int() == conversation_id;
    }), items(state_, "participants").end());
    items(state_, "messages").erase(std::remove_if(items(state_, "messages").begin(), items(state_, "messages").end(), [&](const Json &item) {
        return item["conversationId"].as_int() == conversation_id;
    }), items(state_, "messages").end());
    items(state_, "receipts").erase(std::remove_if(items(state_, "receipts").begin(), items(state_, "receipts").end(), has_message_id), items(state_, "receipts").end());
    items(state_, "savedMessages").erase(std::remove_if(items(state_, "savedMessages").begin(), items(state_, "savedMessages").end(), has_message_id), items(state_, "savedMessages").end());

    log_audit(ctx.id, "conversation.dissolved", "conversation", conversation_id, Json::object{});
    save();
    return Json::object{{"ok", true}, {"mode", "dissolved"}};
}

Json ChatApp::conversation_members(const UserContext &ctx, int conversation_id) {
    require_participant(ctx.id, conversation_id);
    return Json::object{{"members", conversation_member_list(conversation_id)}};
}

Json ChatApp::list_messages(const UserContext &ctx, int conversation_id) {
    require_participant(ctx.id, conversation_id);
    mark_read(ctx.id, conversation_id);
    Json::array messages;
    for (const auto &message : items_const(state_, "messages")) {
        if (message["conversationId"].as_int() == conversation_id) messages.push_back(serialize_message(message, ctx.id));
    }
    std::sort(messages.begin(), messages.end(), [](const Json &a, const Json &b) {
        return a["id"].as_int() < b["id"].as_int();
    });
    save();
    return Json::object{{"messages", messages}};
}

Json ChatApp::send_message(const UserContext &ctx, const Json &payload) {
    int conversation_id = payload["conversationId"].as_int();
    std::string content = trim(payload["content"].as_string());
    if (content.empty()) throw ApiError(400, "消息内容不能为空");
    require_participant(ctx.id, conversation_id);
    int message_id = add_message_internal(conversation_id, ctx.id, content, "text", payload["clientId"].as_string(), payload["replyToId"].as_int(0));
    const Json *sender = find_user(ctx.id);
    for (int uid : participant_ids(conversation_id)) {
        if (uid == ctx.id) continue;
        create_notification(uid, sender ? (*sender)["username"].as_string() + " 发来新消息" : "新消息", content.substr(0, 80), "message", message_id, "message");
    }
    Json *message = find_message(message_id);
    save();
    return Json::object{{"message", serialize_message(*message, ctx.id)}};
}

Json ChatApp::edit_message(const UserContext &ctx, const Json &payload) {
    int message_id = payload["messageId"].as_int();
    Json *message = find_message(message_id);
    if (!message || (*message)["senderId"].as_int() != ctx.id) throw ApiError(403, "只能编辑自己发送的消息");
    std::string content = trim(payload["content"].as_string());
    if (content.empty()) throw ApiError(400, "消息内容不能为空");
    (*message)["content"] = content;
    (*message)["editedAt"] = now_iso();
    log_audit(ctx.id, "message.edited", "message", message_id, Json::object{});
    save();
    return Json::object{{"message", serialize_message(*message, ctx.id)}};
}

Json ChatApp::delete_message(const UserContext &ctx, const Json &payload) {
    int message_id = payload["messageId"].as_int();
    Json *message = find_message(message_id);
    if (!message || (*message)["senderId"].as_int() != ctx.id) throw ApiError(403, "只能撤回自己发送的消息");
    (*message)["deletedAt"] = now_iso();
    (*message)["status"] = "deleted";
    log_audit(ctx.id, "message.deleted", "message", message_id, Json::object{});
    save();
    return Json::object{{"message", serialize_message(*message, ctx.id)}};
}

Json ChatApp::save_message(const UserContext &ctx, const Json &payload) {
    int message_id = payload["messageId"].as_int();
    Json *message = find_message(message_id);
    if (!message) throw ApiError(404, "消息不存在");
    require_participant(ctx.id, (*message)["conversationId"].as_int());
    for (const auto &saved : items_const(state_, "savedMessages")) {
        if (saved["messageId"].as_int() == message_id && saved["userId"].as_int() == ctx.id) {
            return Json::object{{"message", serialize_message(*message, ctx.id)}};
        }
    }
    items(state_, "savedMessages").push_back(Json::object{{"messageId", message_id}, {"userId", ctx.id}, {"savedAt", now_iso()}});
    log_audit(ctx.id, "message.saved", "message", message_id, Json::object{});
    save();
    return Json::object{{"message", serialize_message(*message, ctx.id)}};
}

Json ChatApp::unsave_message(const UserContext &ctx, int message_id) {
    auto &saved = items(state_, "savedMessages");
    saved.erase(std::remove_if(saved.begin(), saved.end(), [&](const Json &item) {
        return item["messageId"].as_int() == message_id && item["userId"].as_int() == ctx.id;
    }), saved.end());
    log_audit(ctx.id, "message.unsaved", "message", message_id, Json::object{});
    save();
    return Json::object{{"ok", true}};
}

Json ChatApp::list_saved_messages(const UserContext &ctx) {
    Json::array messages;
    std::vector<Json> saved(items_const(state_, "savedMessages").begin(), items_const(state_, "savedMessages").end());
    std::sort(saved.begin(), saved.end(), [](const Json &a, const Json &b) {
        return a["savedAt"].as_string() > b["savedAt"].as_string();
    });
    for (const auto &item : saved) {
        if (item["userId"].as_int() != ctx.id) continue;
        const Json *message = find_message(item["messageId"].as_int());
        if (message) messages.push_back(serialize_message(*message, ctx.id));
        if (messages.size() >= 80) break;
    }
    return Json::object{{"messages", messages}};
}

Json ChatApp::list_notifications(const UserContext &ctx) {
    Json::array notifications;
    int unread = 0;
    for (const auto &notification : items_const(state_, "notifications")) {
        if (notification["userId"].as_int() != ctx.id) continue;
        if (!notification["read"].as_bool(false)) ++unread;
        notifications.push_back(serialize_notification(notification));
    }
    std::sort(notifications.begin(), notifications.end(), [](const Json &a, const Json &b) {
        return a["id"].as_int() > b["id"].as_int();
    });
    if (notifications.size() > 80) notifications.resize(80);
    return Json::object{{"unread", unread}, {"notifications", notifications}};
}

Json ChatApp::mark_notifications_read(const UserContext &ctx, const Json &payload) {
    std::unordered_set<int> ids;
    bool all = !payload.contains("notificationIds") || payload["notificationIds"].is_null();
    if (!all) {
        for (int id : json_int_array(payload["notificationIds"])) ids.insert(id);
    }
    for (auto &notification : items(state_, "notifications")) {
        if (notification["userId"].as_int() == ctx.id && (all || ids.count(notification["id"].as_int()))) {
            notification["read"] = true;
        }
    }
    log_audit(ctx.id, "notifications.read", "notification", 0, Json::object{});
    save();
    return list_notifications(ctx);
}

Json ChatApp::list_audit_logs(const UserContext &ctx) {
    const Json *user = find_user(ctx.id);
    int limit = user && ((*user)["role"].as_string() == "admin" || (*user)["role"].as_string() == "manager") ? 120 : 40;
    Json::array logs;
    std::vector<Json> rows(items_const(state_, "auditLogs").begin(), items_const(state_, "auditLogs").end());
    std::sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) {
        return a["id"].as_int() > b["id"].as_int();
    });
    for (const auto &row : rows) {
        const Json *actor = find_user(row["actorId"].as_int());
        logs.push_back(Json::object{
            {"id", row["id"].as_int()},
            {"actorId", nullable_int(row["actorId"].as_int())},
            {"actorName", actor ? (*actor)["username"].as_string() : "系统"},
            {"action", row["action"].as_string()},
            {"entityType", row["entityType"].as_string()},
            {"entityId", nullable_int(row["entityId"].as_int())},
            {"detail", row["detail"]},
            {"createdAt", row["createdAt"].as_string()},
        });
        if (static_cast<int>(logs.size()) >= limit) break;
    }
    return Json::object{{"logs", logs}};
}

Json ChatApp::search(const UserContext &ctx, const std::string &keyword) {
    std::string q = trim(keyword);
    Json::array conversations;
    Json::array messages;
    if (q.empty()) return Json::object{{"users", Json::array{}}, {"conversations", conversations}, {"messages", messages}};
    for (const auto &participant : items_const(state_, "participants")) {
        if (participant["userId"].as_int() != ctx.id) continue;
        const Json *conversation = find_conversation(participant["conversationId"].as_int());
        if (!conversation) continue;
        Json serialized = serialize_conversation(*conversation, ctx.id);
        if (contains_case_insensitive(serialized["title"].as_string(), q) ||
            contains_case_insensitive(serialized["description"].as_string(), q)) {
            conversations.push_back(serialized);
        }
    }
    for (const auto &message : items_const(state_, "messages")) {
        if (!message["deletedAt"].is_null()) continue;
        if (!contains_case_insensitive(message["content"].as_string(), q)) continue;
        if (!find_participant(message["conversationId"].as_int(), ctx.id)) continue;
        messages.push_back(serialize_message(message, ctx.id));
    }
    std::sort(messages.begin(), messages.end(), [](const Json &a, const Json &b) {
        return a["id"].as_int() > b["id"].as_int();
    });
    if (conversations.size() > 20) conversations.resize(20);
    if (messages.size() > 40) messages.resize(40);
    return Json::object{{"users", Json::array{}}, {"conversations", conversations}, {"messages", messages}};
}

Json ChatApp::build_summary(const UserContext &ctx, const Json &payload) {
    int conversation_id = payload["conversationId"].as_int();
    require_participant(ctx.id, conversation_id);
    std::vector<Json> rows;
    for (const auto &message : items_const(state_, "messages")) {
        if (message["conversationId"].as_int() == conversation_id && message["deletedAt"].is_null()) rows.push_back(message);
    }
    std::sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) {
        return a["id"].as_int() < b["id"].as_int();
    });
    if (rows.size() > 30) rows.erase(rows.begin(), rows.end() - 30);

    std::string summary;
    Json::array keywords;
    Json::array action_items;
    if (rows.empty()) {
        summary = "当前会话还没有消息，暂时无法生成摘要。";
    } else {
        std::vector<std::string> speakers;
        std::map<std::string, int> counts;
        for (const auto &message : rows) {
            const Json *sender = find_user(message["senderId"].as_int());
            std::string username = sender ? (*sender)["username"].as_string() : "unknown";
            if (std::find(speakers.begin(), speakers.end(), username) == speakers.end()) speakers.push_back(username);
            std::string text = message["content"].as_string();
            std::string word;
            for (unsigned char ch : text) {
                if (std::isalnum(ch) || ch == '_' || ch == '-') {
                    word.push_back(static_cast<char>(std::tolower(ch)));
                } else {
                    if (word.size() >= 2) counts[word]++;
                    word.clear();
                }
            }
            if (word.size() >= 2) counts[word]++;
            if (text.find("准备") != std::string::npos || text.find("建议") != std::string::npos ||
                text.find("验证") != std::string::npos || text.find("待办") != std::string::npos) {
                action_items.push_back(username + ": " + text);
            }
        }
        std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        for (const auto &item : sorted) {
            keywords.push_back(item.first);
            if (keywords.size() >= 6) break;
        }
        if (keywords.empty()) keywords.push_back("聊天内容");
        const Json &latest = rows.back();
        const Json *latest_sender = find_user(latest["senderId"].as_int());
        summary = "近 " + std::to_string(rows.size()) + " 条消息主要围绕 " + keywords.front().as_string() +
                  " 展开。参与人包括 " + join(speakers, "、") + "。最近一条由 " +
                  (latest_sender ? (*latest_sender)["username"].as_string() : "unknown") + " 在 " +
                  latest["createdAt"].as_string() + " 提出：" + latest["content"].as_string();
    }
    std::string stamp = now_iso();
    Json result = Json::object{
        {"summary", summary},
        {"keywords", keywords},
        {"actionItems", action_items},
        {"messageCount", static_cast<int>(rows.size())},
        {"createdAt", stamp},
    };
    items(state_, "summaries").push_back(Json::object{
        {"id", next_id("summaries")},
        {"conversationId", conversation_id},
        {"summary", result},
        {"createdAt", stamp},
    });
    log_audit(ctx.id, "ai.summary.created", "conversation", conversation_id, Json::object{{"messageCount", static_cast<int>(rows.size())}});
    save();
    return Json::object{{"summary", result}};
}

Json ChatApp::health() {
    int online = 0;
    double cutoff = now_epoch() - PRESENCE_TTL_SECONDS;
    std::unordered_set<int> online_ids;
    for (const auto &session : items_const(state_, "sessions")) {
        if (session["lastSeen"].as_number() >= cutoff) online_ids.insert(session["userId"].as_int());
    }
    online = static_cast<int>(online_ids.size());
    int message_count = 0;
    for (const auto &message : items_const(state_, "messages")) {
        if (message["deletedAt"].is_null()) ++message_count;
    }
    int friendship_count = 0;
    for (const auto &friendship : items_const(state_, "friendships")) {
        if (friendship["status"].as_string() == "accepted") ++friendship_count;
    }
    int unread_notifications = 0;
    for (const auto &notification : items_const(state_, "notifications")) {
        if (!notification["read"].as_bool(false)) ++unread_notifications;
    }
    return Json::object{
        {"status", "ok"},
        {"service", "ai-native-im-cpp"},
        {"now", now_iso()},
        {"metrics", Json::object{
            {"users", static_cast<int>(items_const(state_, "users").size())},
            {"conversations", static_cast<int>(items_const(state_, "conversations").size())},
            {"messages", message_count},
            {"online", online},
            {"friendships", friendship_count},
            {"notifications", unread_notifications},
            {"auditLogs", static_cast<int>(items_const(state_, "auditLogs").size())},
        }},
        {"modules", Json::array{"auth", "friends", "conversation", "message", "receipt", "notification", "audit", "ai-summary", "sse"}},
    };
}

std::optional<ChatApp::UserContext> ChatApp::authenticate(const HttpRequest &request) {
    std::string token = extract_token(request);
    if (token.empty()) return std::nullopt;
    double cutoff = now_epoch() - SESSION_TTL_SECONDS;
    for (auto &session : items(state_, "sessions")) {
        if (session["token"].as_string() != token) continue;
        if (session["lastSeen"].as_number() < cutoff) return std::nullopt;
        int user_id = session["userId"].as_int();
        Json *user = find_user(user_id);
        if (!user) return std::nullopt;
        double now = now_epoch();
        session["lastSeen"] = now;
        (*user)["lastSeen"] = now;
        return UserContext{user_id, token};
    }
    return std::nullopt;
}

ChatApp::UserContext ChatApp::require_user(const HttpRequest &request) {
    auto ctx = authenticate(request);
    if (!ctx) throw ApiError(401, "需要登录后访问");
    return *ctx;
}

Json ChatApp::parse_body(const HttpRequest &request) {
    if (trim(request.body).empty()) return Json::object{};
    try {
        return Json::parse(request.body);
    } catch (...) {
        throw ApiError(400, "请求 JSON 格式不正确");
    }
}

HttpResponse ChatApp::json_response(const Json &value, int status) {
    HttpResponse response;
    response.status = status;
    response.content_type = "application/json; charset=utf-8";
    response.body = value.dump();
    return response;
}

HttpResponse ChatApp::error_response(int status, const std::string &detail) {
    return json_response(Json::object{{"detail", detail}}, status);
}

void ChatApp::load() {
    if (std::filesystem::exists(data_path_)) {
        std::ifstream in(data_path_);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        try {
            state_ = Json::parse(buffer.str());
        } catch (...) {
            state_ = Json::object{};
        }
    } else {
        state_ = Json::object{};
    }
    ensure_state();
    save();
}

void ChatApp::save() {
    std::filesystem::create_directories(data_path_.parent_path());
    std::ofstream out(data_path_, std::ios::trunc);
    out << state_.dump();
}

void ChatApp::ensure_state() {
    if (!state_.is_object()) state_ = Json::object{};
    const char *array_names[] = {
        "users", "sessions", "conversations", "participants", "friendships",
        "friendRequests", "messages", "receipts", "savedMessages",
        "notifications", "auditLogs", "summaries"
    };
    for (const char *name : array_names) {
        if (!state_.contains(name) || !state_[name].is_array()) state_[name] = Json::array{};
    }
    if (!state_.contains("counters") || !state_["counters"].is_object()) state_["counters"] = Json::object{};
    const std::pair<const char *, const char *> counters[] = {
        {"users", "users"}, {"conversations", "conversations"}, {"friendRequests", "friendRequests"},
        {"messages", "messages"}, {"notifications", "notifications"}, {"auditLogs", "auditLogs"},
        {"summaries", "summaries"}
    };
    for (const auto &[counter, array_name] : counters) {
        if (state_["counters"][counter].as_int(0) > 0) continue;
        int max_id = 0;
        for (const auto &item : items_const(state_, array_name)) max_id = std::max(max_id, item["id"].as_int());
        state_["counters"][counter] = max_id + 1;
    }
}

int ChatApp::next_id(const std::string &key) {
    int id = state_["counters"][key].as_int(1);
    state_["counters"][key] = id + 1;
    return id;
}

Json *ChatApp::find_by_id(Json::array &array, int id) {
    for (auto &item : array) {
        if (item["id"].as_int() == id) return &item;
    }
    return nullptr;
}

const Json *ChatApp::find_by_id(const Json::array &array, int id) const {
    for (const auto &item : array) {
        if (item["id"].as_int() == id) return &item;
    }
    return nullptr;
}

Json *ChatApp::find_user(int id) { return find_by_id(items(state_, "users"), id); }
const Json *ChatApp::find_user(int id) const { return find_by_id(items_const(state_, "users"), id); }

Json *ChatApp::find_user_by_username(const std::string &username) {
    std::string normalized = normalize_username(username);
    for (auto &user : items(state_, "users")) {
        if (user["username"].as_string() == normalized) return &user;
    }
    return nullptr;
}

const Json *ChatApp::find_user_by_username(const std::string &username) const {
    std::string normalized = normalize_username(username);
    for (const auto &user : items_const(state_, "users")) {
        if (user["username"].as_string() == normalized) return &user;
    }
    return nullptr;
}

Json *ChatApp::find_conversation(int id) { return find_by_id(items(state_, "conversations"), id); }
const Json *ChatApp::find_conversation(int id) const { return find_by_id(items_const(state_, "conversations"), id); }

Json *ChatApp::find_participant(int conversation_id, int user_id) {
    for (auto &participant : items(state_, "participants")) {
        if (participant["conversationId"].as_int() == conversation_id && participant["userId"].as_int() == user_id) return &participant;
    }
    return nullptr;
}

const Json *ChatApp::find_participant(int conversation_id, int user_id) const {
    for (const auto &participant : items_const(state_, "participants")) {
        if (participant["conversationId"].as_int() == conversation_id && participant["userId"].as_int() == user_id) return &participant;
    }
    return nullptr;
}

Json *ChatApp::find_message(int id) { return find_by_id(items(state_, "messages"), id); }
const Json *ChatApp::find_message(int id) const { return find_by_id(items_const(state_, "messages"), id); }

bool ChatApp::are_friends(int user_id, int friend_id) const {
    for (const auto &friendship : items_const(state_, "friendships")) {
        if (friendship["userId"].as_int() == user_id && friendship["friendId"].as_int() == friend_id &&
            friendship["status"].as_string() == "accepted") {
            return true;
        }
    }
    return false;
}

void ChatApp::ensure_friendship(int user_id, int friend_id) {
    if (user_id == friend_id) throw ApiError(400, "不能添加自己为好友");
    std::string stamp = now_iso();
    auto upsert = [&](int a, int b) {
        for (auto &friendship : items(state_, "friendships")) {
            if (friendship["userId"].as_int() == a && friendship["friendId"].as_int() == b) {
                friendship["status"] = "accepted";
                return;
            }
        }
        items(state_, "friendships").push_back(Json::object{{"userId", a}, {"friendId", b}, {"status", "accepted"}, {"createdAt", stamp}});
    };
    upsert(user_id, friend_id);
    upsert(friend_id, user_id);
}

void ChatApp::require_friend(int user_id, int friend_id) const {
    if (!are_friends(user_id, friend_id)) throw ApiError(403, "请先添加好友");
}

void ChatApp::require_participant(int user_id, int conversation_id) const {
    if (!find_participant(conversation_id, user_id)) throw ApiError(403, "无权访问该会话");
}

std::vector<int> ChatApp::participant_ids(int conversation_id) const {
    std::vector<int> ids;
    for (const auto &participant : items_const(state_, "participants")) {
        if (participant["conversationId"].as_int() == conversation_id) ids.push_back(participant["userId"].as_int());
    }
    return ids;
}

Json ChatApp::conversation_member_list(int conversation_id) const {
    Json::array members;
    for (const auto &participant : items_const(state_, "participants")) {
        if (participant["conversationId"].as_int() != conversation_id) continue;
        const Json *user = find_user(participant["userId"].as_int());
        if (!user) continue;
        Json serialized = serialize_user(*user);
        serialized["memberRole"] = participant["role"].as_string();
        serialized["joinedAt"] = participant["joinedAt"].as_string();
        members.push_back(serialized);
    }
    std::sort(members.begin(), members.end(), [](const Json &a, const Json &b) {
        if (a["memberRole"].as_string() != b["memberRole"].as_string()) return a["memberRole"].as_string() > b["memberRole"].as_string();
        return a["username"].as_string() < b["username"].as_string();
    });
    return Json(members);
}

Json ChatApp::serialize_user(const Json &user) const {
    bool online = false;
    double cutoff = now_epoch() - PRESENCE_TTL_SECONDS;
    int id = user["id"].as_int();
    for (const auto &session : items_const(state_, "sessions")) {
        if (session["userId"].as_int() == id && session["lastSeen"].as_number() >= cutoff) {
            online = true;
            break;
        }
    }
    return Json::object{
        {"id", id},
        {"username", user["username"].as_string()},
        {"displayName", user["displayName"].as_string(user["username"].as_string())},
        {"avatarColor", user["avatarColor"].as_string("#002FA7")},
        {"role", user["role"].as_string("member")},
        {"department", user["department"].as_string()},
        {"title", user["title"].as_string()},
        {"statusMessage", user["statusMessage"].as_string()},
        {"phone", user["phone"].as_string()},
        {"location", user["location"].as_string()},
        {"createdAt", user["createdAt"].as_string()},
        {"online", online},
    };
}

Json ChatApp::serialize_user_for_view(const Json &user, int viewer_id) const {
    Json item = serialize_user(user);
    std::string status = friendship_status(viewer_id, user["id"].as_int());
    item["friendshipStatus"] = status;
    item["friend"] = status == "accepted";
    return item;
}

Json ChatApp::serialize_friend_request(const Json &request, int viewer_id) const {
    int requester_id = request["requesterId"].as_int();
    int recipient_id = request["recipientId"].as_int();
    const Json *requester = find_user(requester_id);
    const Json *recipient = find_user(recipient_id);
    return Json::object{
        {"id", request["id"].as_int()},
        {"status", request["status"].as_string()},
        {"direction", recipient_id == viewer_id ? "incoming" : "outgoing"},
        {"createdAt", request["createdAt"].as_string()},
        {"respondedAt", request["respondedAt"]},
        {"requester", requester ? serialize_user_for_view(*requester, viewer_id) : Json(nullptr)},
        {"recipient", recipient ? serialize_user_for_view(*recipient, viewer_id) : Json(nullptr)},
    };
}

Json ChatApp::serialize_conversation(const Json &conversation, int viewer_id) const {
    int conversation_id = conversation["id"].as_int();
    const Json *pref = find_participant(conversation_id, viewer_id);
    Json members = conversation_member_list(conversation_id);
    std::string title = conversation["name"].as_string();
    if (conversation["type"].as_string() == "direct") {
        for (const auto &member : members.as_array()) {
            if (member["id"].as_int() != viewer_id) {
                title = member["displayName"].as_string(member["username"].as_string());
                break;
            }
        }
        if (title.empty()) title = "私聊";
    }

    const Json *last = nullptr;
    for (const auto &message : items_const(state_, "messages")) {
        if (message["conversationId"].as_int() != conversation_id) continue;
        if (!last || message["id"].as_int() > (*last)["id"].as_int()) last = &message;
    }

    int last_read_id = pref ? (*pref)["lastReadId"].as_int() : 0;
    int unread = 0;
    for (const auto &message : items_const(state_, "messages")) {
        if (message["conversationId"].as_int() == conversation_id &&
            message["id"].as_int() > last_read_id &&
            message["senderId"].as_int() != viewer_id &&
            message["deletedAt"].is_null()) {
            ++unread;
        }
    }

    Json last_message(nullptr);
    if (last) {
        const Json *sender = find_user((*last)["senderId"].as_int());
        last_message = Json::object{
            {"id", (*last)["id"].as_int()},
            {"senderName", sender ? (*sender)["username"].as_string() : "unknown"},
            {"content", (*last)["deletedAt"].is_null() ? (*last)["content"].as_string() : "消息已撤回"},
            {"createdAt", (*last)["createdAt"].as_string()},
            {"status", (*last)["status"].as_string()},
        };
    }

    return Json::object{
        {"id", conversation_id},
        {"type", conversation["type"].as_string()},
        {"name", conversation["name"].as_string()},
        {"title", title},
        {"description", conversation["description"].as_string()},
        {"ownerId", nullable_int(conversation["ownerId"].as_int())},
        {"updatedAt", conversation["updatedAt"].as_string()},
        {"unread", unread},
        {"pinned", pref ? (*pref)["pinned"].as_bool(false) : false},
        {"muted", pref ? (*pref)["muted"].as_bool(false) : false},
        {"archived", pref ? (*pref)["archived"].as_bool(false) : false},
        {"participants", members},
        {"lastMessage", last_message},
    };
}

Json ChatApp::serialize_message(const Json &message, int viewer_id) const {
    const Json *sender = find_user(message["senderId"].as_int());
    bool saved = false;
    for (const auto &item : items_const(state_, "savedMessages")) {
        if (item["messageId"].as_int() == message["id"].as_int() && item["userId"].as_int() == viewer_id) {
            saved = true;
            break;
        }
    }
    bool deleted = !message["deletedAt"].is_null();
    return Json::object{
        {"id", message["id"].as_int()},
        {"conversationId", message["conversationId"].as_int()},
        {"senderId", message["senderId"].as_int()},
        {"senderName", sender ? (*sender)["username"].as_string() : "unknown"},
        {"senderUsername", sender ? (*sender)["username"].as_string() : "unknown"},
        {"senderColor", sender ? (*sender)["avatarColor"].as_string("#002FA7") : "#002FA7"},
        {"content", deleted ? "消息已撤回" : message["content"].as_string()},
        {"kind", message["kind"].as_string("text")},
        {"clientId", message["clientId"].as_string()},
        {"status", deleted ? "deleted" : message["status"].as_string("sent")},
        {"editedAt", message["editedAt"]},
        {"deletedAt", message["deletedAt"]},
        {"replyToId", message["replyToId"]},
        {"createdAt", message["createdAt"].as_string()},
        {"receipts", receipt_counts(message["id"].as_int())},
        {"saved", saved},
    };
}

Json ChatApp::serialize_notification(const Json &notification) const {
    return Json::object{
        {"id", notification["id"].as_int()},
        {"type", notification["type"].as_string()},
        {"title", notification["title"].as_string()},
        {"body", notification["body"].as_string()},
        {"entityType", notification["entityType"].as_string()},
        {"entityId", nullable_int(notification["entityId"].as_int())},
        {"read", notification["read"].as_bool(false)},
        {"createdAt", notification["createdAt"].as_string()},
    };
}

Json ChatApp::get_settings_for_user(int user_id) {
    Json *user = find_user(user_id);
    if (!user) throw ApiError(401, "登录已失效");
    if (!(*user).contains("settings") || !(*user)["settings"].is_object()) {
        (*user)["settings"] = Json::object{
            {"theme", "light"},
            {"density", "comfortable"},
            {"notifyDesktop", true},
            {"enterToSend", true},
            {"updatedAt", now_iso()},
        };
    }
    return (*user)["settings"];
}

std::string ChatApp::friendship_status(int viewer_id, int target_id) const {
    if (viewer_id == target_id) return "self";
    if (are_friends(viewer_id, target_id)) return "accepted";
    for (const auto &request : items_const(state_, "friendRequests")) {
        if (request["status"].as_string() != "pending") continue;
        if (request["requesterId"].as_int() == viewer_id && request["recipientId"].as_int() == target_id) return "pending_outgoing";
        if (request["requesterId"].as_int() == target_id && request["recipientId"].as_int() == viewer_id) return "pending_incoming";
    }
    return "none";
}

int ChatApp::ensure_direct_conversation(int user_a, int user_b) {
    int low = std::min(user_a, user_b);
    int high = std::max(user_a, user_b);
    std::string direct_key = std::to_string(low) + ":" + std::to_string(high);
    for (const auto &conversation : items_const(state_, "conversations")) {
        if (conversation["directKey"].as_string() == direct_key) return conversation["id"].as_int();
    }
    std::string stamp = now_iso();
    int conversation_id = next_id("conversations");
    items(state_, "conversations").push_back(Json::object{
        {"id", conversation_id},
        {"type", "direct"},
        {"name", direct_key},
        {"description", ""},
        {"ownerId", nullptr},
        {"directKey", direct_key},
        {"createdAt", stamp},
        {"updatedAt", stamp},
    });
    for (int uid : {low, high}) {
        items(state_, "participants").push_back(Json::object{
            {"conversationId", conversation_id},
            {"userId", uid},
            {"joinedAt", stamp},
            {"lastReadId", 0},
            {"role", "member"},
            {"pinned", false},
            {"muted", false},
            {"archived", false},
        });
    }
    log_audit(user_a, "conversation.direct.created", "conversation", conversation_id, Json::object{{"targetUserId", user_b}});
    return conversation_id;
}

int ChatApp::create_group_internal(int owner_id, const std::string &name, const std::string &description, const std::vector<int> &member_ids) {
    std::string stamp = now_iso();
    int conversation_id = next_id("conversations");
    items(state_, "conversations").push_back(Json::object{
        {"id", conversation_id},
        {"type", "group"},
        {"name", trim(name)},
        {"description", trim(description)},
        {"ownerId", owner_id},
        {"directKey", ""},
        {"createdAt", stamp},
        {"updatedAt", stamp},
    });
    std::vector<int> members = member_ids;
    members.push_back(owner_id);
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    for (int uid : members) {
        items(state_, "participants").push_back(Json::object{
            {"conversationId", conversation_id},
            {"userId", uid},
            {"joinedAt", stamp},
            {"lastReadId", 0},
            {"role", uid == owner_id ? "owner" : "member"},
            {"pinned", false},
            {"muted", false},
            {"archived", false},
        });
    }
    log_audit(owner_id, "conversation.group.created", "conversation", conversation_id, Json::object{{"name", name}, {"memberCount", static_cast<int>(members.size())}});
    return conversation_id;
}

int ChatApp::add_message_internal(int conversation_id, int sender_id, const std::string &content, const std::string &kind, const std::string &client_id, int reply_to_id) {
    if (!client_id.empty()) {
        for (const auto &message : items_const(state_, "messages")) {
            if (message["conversationId"].as_int() == conversation_id &&
                message["senderId"].as_int() == sender_id &&
                message["clientId"].as_string() == client_id) {
                return message["id"].as_int();
            }
        }
    }
    std::string stamp = now_iso();
    int message_id = next_id("messages");
    items(state_, "messages").push_back(Json::object{
        {"id", message_id},
        {"conversationId", conversation_id},
        {"senderId", sender_id},
        {"content", trim(content)},
        {"kind", kind},
        {"clientId", client_id},
        {"status", "sent"},
        {"editedAt", nullptr},
        {"deletedAt", nullptr},
        {"replyToId", reply_to_id > 0 ? Json(reply_to_id) : Json(nullptr)},
        {"createdAt", stamp},
    });
    create_receipts(conversation_id, message_id, sender_id);
    if (Json *conversation = find_conversation(conversation_id)) (*conversation)["updatedAt"] = stamp;
    log_audit(sender_id, "message.sent", "message", message_id, Json::object{{"conversationId", conversation_id}, {"kind", kind}});
    return message_id;
}

void ChatApp::create_receipts(int conversation_id, int message_id, int sender_id) {
    std::string stamp = now_iso();
    for (int uid : participant_ids(conversation_id)) {
        items(state_, "receipts").push_back(Json::object{
            {"messageId", message_id},
            {"userId", uid},
            {"status", uid == sender_id ? "read" : "delivered"},
            {"deliveredAt", stamp},
            {"readAt", uid == sender_id ? Json(stamp) : Json(nullptr)},
        });
    }
}

Json ChatApp::receipt_counts(int message_id) const {
    std::map<std::string, int> counts;
    for (const auto &receipt : items_const(state_, "receipts")) {
        if (receipt["messageId"].as_int() == message_id) counts[receipt["status"].as_string()]++;
    }
    Json::object out;
    for (const auto &[key, value] : counts) out[key] = Json(value);
    return Json(out);
}

void ChatApp::mark_read(int user_id, int conversation_id) {
    std::string stamp = now_iso();
    int max_id = 0;
    for (auto &message : items(state_, "messages")) {
        if (message["conversationId"].as_int() == conversation_id) max_id = std::max(max_id, message["id"].as_int());
    }
    for (auto &receipt : items(state_, "receipts")) {
        if (receipt["userId"].as_int() == user_id) {
            const Json *message = find_message(receipt["messageId"].as_int());
            if (message && (*message)["conversationId"].as_int() == conversation_id) {
                receipt["status"] = "read";
                receipt["readAt"] = stamp;
            }
        }
    }
    if (Json *participant = find_participant(conversation_id, user_id)) {
        participant->operator[]("lastReadId") = std::max((*participant)["lastReadId"].as_int(), max_id);
    }
}

void ChatApp::create_notification(int user_id, const std::string &title, const std::string &body, const std::string &entity_type, int entity_id, const std::string &type) {
    items(state_, "notifications").push_back(Json::object{
        {"id", next_id("notifications")},
        {"userId", user_id},
        {"type", type},
        {"title", title},
        {"body", body},
        {"entityType", entity_type},
        {"entityId", entity_id},
        {"read", false},
        {"createdAt", now_iso()},
    });
}

void ChatApp::log_audit(int actor_id, const std::string &action, const std::string &entity_type, int entity_id, const Json &detail) {
    items(state_, "auditLogs").push_back(Json::object{
        {"id", next_id("auditLogs")},
        {"actorId", actor_id},
        {"action", action},
        {"entityType", entity_type},
        {"entityId", entity_id},
        {"detail", detail},
        {"createdAt", now_iso()},
    });
}

std::string ChatApp::now_iso() const {
    auto now = std::chrono::system_clock::now();
    std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&raw, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

double ChatApp::now_epoch() const {
    using clock = std::chrono::system_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::string ChatApp::make_token() const {
    static const char *hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string token;
    token.reserve(48);
    for (int i = 0; i < 48; ++i) token.push_back(hex[dist(gen)]);
    return token;
}

std::string ChatApp::password_hash(const std::string &username, const std::string &password) const {
    std::hash<std::string> hasher;
    std::ostringstream out;
    for (int i = 0; i < 4; ++i) {
        size_t value = hasher("cpp-im:" + std::to_string(i) + ":" + normalize_username(username) + ":" + password);
        out << std::hex << std::setw(sizeof(size_t) * 2) << std::setfill('0') << value;
    }
    return out.str();
}

std::string ChatApp::normalize_username(const std::string &username) const {
    return lower(trim(username));
}

bool ChatApp::contains_case_insensitive(const std::string &text, const std::string &needle) const {
    return lower(text).find(lower(needle)) != std::string::npos;
}

std::string ChatApp::extract_token(const HttpRequest &request) const {
    auto auth = request.headers.find("authorization");
    if (auth != request.headers.end()) {
        std::string value = auth->second;
        std::string prefix = "Bearer ";
        if (value.size() >= prefix.size() && lower(value.substr(0, prefix.size())) == lower(prefix)) {
            return trim(value.substr(prefix.size()));
        }
    }
    auto query = parse_query(request.query);
    auto token = query.find("token");
    return token == query.end() ? "" : token->second;
}

std::string ChatApp::content_type_for(const std::filesystem::path &path) const {
    std::string ext = lower(path.extension().string());
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

} // namespace chat
