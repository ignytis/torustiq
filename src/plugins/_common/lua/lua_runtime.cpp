#include "plugins/_common/lua/lua_runtime.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace torustiq::plugins {
namespace {

const char* ConfigValue(const torustiq_config* config, const char* key) {
    return torustiq_config_get(config, key);
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open Lua file '" + path + "'");
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int64_t ParseMaxInstructions(const char* value) {
    if (value == nullptr) {
        return LuaRuntime::kDefaultMaxInstructions;
    }
    const std::string text(value);
    int64_t result = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (ec != std::errc() || end != text.data() + text.size() || result < 0) {
        throw std::runtime_error("'max_instructions' must be a non-negative integer, got '" + text +
                                 "'");
    }
    return result;
}

// Opens one standard library and leaves nothing on the stack.
void OpenLibrary(lua_State* state, const char* name, lua_CFunction open) {
    luaL_requiref(state, name, open, 1);
    lua_pop(state, 1);
}

void RemoveGlobal(lua_State* state, const char* name) {
    lua_pushnil(state);
    lua_setglobal(state, name);
}

std::string PopError(lua_State* state) {
    const char* message = lua_tostring(state, -1);
    std::string result = message != nullptr ? message : "(error object is not a string)";
    lua_pop(state, 1);
    return result;
}

} // namespace

namespace {

constexpr int kMaxDepth = 64; // nesting limit of structured payloads in Lua

// Converts the Lua value at `index` to a LuaValue. Raw accesses only (no metamethods).
// Tables: a sequence 1..n is an array, a table with string keys is a map (members sorted by key),
// an empty table is an empty map. The sentinel `null` (light userdata NULL) is a null.
bool ReadValue(lua_State* state, int index, int depth, LuaValue& out, std::string& error) {
    index = lua_absindex(state, index);
    out = LuaValue{};
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return true;
    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(state, index) == nullptr) {
            return true;
        }
        error = "unsupported value type userdata";
        return false;
    case LUA_TBOOLEAN:
        out.kind = TORUSTIQ_VALUE_BOOL;
        out.b = lua_toboolean(state, index) != 0;
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(state, index)) {
            out.kind = TORUSTIQ_VALUE_INT;
            out.i = static_cast<int64_t>(lua_tointeger(state, index));
        } else {
            out.kind = TORUSTIQ_VALUE_DOUBLE;
            out.d = static_cast<double>(lua_tonumber(state, index));
        }
        return true;
    case LUA_TSTRING: {
        size_t length = 0;
        const char* text = lua_tolstring(state, index, &length);
        out.kind = TORUSTIQ_VALUE_STRING;
        out.text.assign(text, length);
        return true;
    }
    case LUA_TTABLE:
        break;
    default:
        error =
            std::string("unsupported value type ") + lua_typename(state, lua_type(state, index));
        return false;
    }
    if (depth >= kMaxDepth || lua_checkstack(state, 4) == 0) {
        error = "value is nested too deeply (or is cyclic)";
        return false;
    }

    const lua_Unsigned length = lua_rawlen(state, index);
    if (length > 0) {
        out.kind = TORUSTIQ_VALUE_ARRAY;
        for (lua_Unsigned n = 1; n <= length; ++n) {
            lua_rawgeti(state, index, static_cast<lua_Integer>(n));
            LuaValue item;
            const bool ok = ReadValue(state, -1, depth + 1, item, error);
            lua_pop(state, 1);
            if (!ok) {
                return false;
            }
            out.items.push_back(std::move(item));
        }
        // Reject tables that have more than the sequence part.
        lua_pushnil(state);
        lua_Unsigned count = 0;
        while (lua_next(state, index) != 0) {
            lua_pop(state, 1);
            ++count;
        }
        if (count != length) {
            error = "a table mixing a sequence with other keys cannot be a value";
            return false;
        }
        return true;
    }

    out.kind = TORUSTIQ_VALUE_MAP;
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            lua_pop(state, 2);
            error = "map keys of a value must be strings";
            return false;
        }
        size_t key_length = 0;
        const char* key = lua_tolstring(state, -2, &key_length);
        LuaValue member;
        const bool ok = ReadValue(state, -1, depth + 1, member, error);
        if (!ok) {
            lua_pop(state, 2);
            return false;
        }
        out.entries.emplace_back(std::string(key, key_length), std::move(member));
        lua_pop(state, 1); // value; keep the key for lua_next
    }
    std::sort(out.entries.begin(), out.entries.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    return true;
}

} // namespace

LuaMessageView::LuaMessageView(const LuaMessage& message) {
    headers_.reserve(message.headers.size());
    for (const auto& [key, value] : message.headers) {
        headers_.push_back({key.c_str(), value.c_str()});
    }
    message_.payload = Build(message.payload);
    message_.headers = headers_.data();
    message_.header_count = headers_.size();
}

torustiq_value LuaMessageView::Build(const LuaValue& value) {
    torustiq_value out{};
    out.kind = value.kind;
    switch (value.kind) {
    case TORUSTIQ_VALUE_BOOL:
        out.as.b = value.b ? 1 : 0;
        break;
    case TORUSTIQ_VALUE_INT:
        out.as.i = value.i;
        break;
    case TORUSTIQ_VALUE_DOUBLE:
        out.as.d = value.d;
        break;
    case TORUSTIQ_VALUE_STRING:
    case TORUSTIQ_VALUE_BYTES:
        out.as.bytes.data = reinterpret_cast<const uint8_t*>(value.text.data());
        out.as.bytes.len = value.text.size();
        break;
    case TORUSTIQ_VALUE_ARRAY: {
        auto& items = arrays_.emplace_back();
        items.reserve(value.items.size());
        for (const LuaValue& item : value.items) {
            items.push_back(Build(item));
        }
        out.as.array.items = items.data();
        out.as.array.count = items.size();
        break;
    }
    case TORUSTIQ_VALUE_MAP: {
        auto& entries = maps_.emplace_back();
        entries.reserve(value.entries.size());
        for (const auto& [key, member] : value.entries) {
            torustiq_map_entry entry{};
            entry.key.data = reinterpret_cast<const uint8_t*>(key.data());
            entry.key.len = key.size();
            entry.value = Build(member);
            entries.push_back(entry);
        }
        out.as.map.entries = entries.data();
        out.as.map.count = entries.size();
        break;
    }
    default:
        break; // NULL
    }
    return out;
}

LuaRuntime::LuaRuntime(const torustiq_config* config, const std::string& kind) {
    const char* script = ConfigValue(config, "script");
    const char* file = ConfigValue(config, "file");
    if ((script == nullptr) == (file == nullptr)) {
        throw std::runtime_error(kind + ": exactly one of 'script' or 'file' is required");
    }
    max_instructions_ = ParseMaxInstructions(ConfigValue(config, "max_instructions"));
    const std::string source = script != nullptr ? std::string(script) : ReadFile(file);
    const std::string chunk_name = script != nullptr ? "=" + kind : std::string("@") + file;

    state_ = luaL_newstate();
    if (state_ == nullptr) {
        throw std::runtime_error(kind + ": cannot create Lua state");
    }
    try {
        *static_cast<LuaRuntime**>(lua_getextraspace(state_)) = this;

        OpenLibrary(state_, LUA_GNAME, luaopen_base);
        OpenLibrary(state_, LUA_STRLIBNAME, luaopen_string);
        OpenLibrary(state_, LUA_TABLIBNAME, luaopen_table);
        OpenLibrary(state_, LUA_MATHLIBNAME, luaopen_math);
        OpenLibrary(state_, LUA_UTF8LIBNAME, luaopen_utf8);
        OpenLibrary(state_, LUA_COLIBNAME, luaopen_coroutine);
        for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require"}) {
            RemoveGlobal(state_, name);
        }

        // Plugin parameters as the global table `config`.
        lua_createtable(state_, 0, static_cast<int>(config != nullptr ? config->count : 0));
        for (size_t i = 0; config != nullptr && i < config->count; ++i) {
            lua_pushstring(state_, config->items[i].value);
            lua_setfield(state_, -2, config->items[i].key);
        }
        lua_setglobal(state_, "config");
        lua_pushlightuserdata(state_, nullptr);
        lua_setglobal(state_, "null");

        lua_sethook(state_, Hook, LUA_MASKCOUNT, kHookStep);

        // Text chunks only: precompiled bytecode is not allowed.
        if (luaL_loadbufferx(state_, source.data(), source.size(), chunk_name.c_str(), "t") !=
            LUA_OK) {
            throw std::runtime_error(kind + ": " + PopError(state_));
        }
        std::string error;
        if (!Call(0, 0, error)) {
            throw std::runtime_error(kind + ": " + error);
        }
    } catch (...) {
        lua_close(state_);
        state_ = nullptr;
        throw;
    }
}

LuaRuntime::~LuaRuntime() {
    if (state_ != nullptr) {
        lua_close(state_);
    }
}

LuaRuntime* LuaRuntime::FromState(lua_State* state) {
    return *static_cast<LuaRuntime**>(lua_getextraspace(state));
}

const char* LuaRuntime::OnHook() {
    if (stop_.load()) {
        return "stopped by host";
    }
    executed_ += kHookStep;
    if (max_instructions_ > 0 && executed_ > max_instructions_) {
        return "instruction limit exceeded (see 'max_instructions')";
    }
    return nullptr;
}

void LuaRuntime::Hook(lua_State* state, lua_Debug*) {
    const char* error = FromState(state)->OnHook();
    if (error != nullptr) {
        luaL_error(state, "%s", error); // no C++ objects with destructors in this frame
    }
}

bool LuaRuntime::PushGlobalFunction(const char* name) {
    lua_rawgeti(state_, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_pushstring(state_, name);
    lua_rawget(state_, -2);
    lua_remove(state_, -2);
    if (lua_isfunction(state_, -1)) {
        return true;
    }
    lua_pop(state_, 1);
    return false;
}

bool LuaRuntime::Call(int nargs, int nresults, std::string& error) {
    ResetBudget();
    if (lua_pcall(state_, nargs, nresults, 0) != LUA_OK) {
        error = PopError(state_);
        return false;
    }
    return true;
}

void LuaRuntime::PushValue(const torustiq_value& value, int depth) {
    switch (value.kind) {
    case TORUSTIQ_VALUE_BOOL:
        lua_pushboolean(state_, value.as.b != 0);
        break;
    case TORUSTIQ_VALUE_INT:
        lua_pushinteger(state_, static_cast<lua_Integer>(value.as.i));
        break;
    case TORUSTIQ_VALUE_DOUBLE:
        lua_pushnumber(state_, static_cast<lua_Number>(value.as.d));
        break;
    case TORUSTIQ_VALUE_STRING:
    case TORUSTIQ_VALUE_BYTES:
        lua_pushlstring(
            state_, reinterpret_cast<const char*>(value.as.bytes.data), value.as.bytes.len);
        break;
    case TORUSTIQ_VALUE_ARRAY:
    case TORUSTIQ_VALUE_MAP: {
        if (depth >= kMaxDepth || lua_checkstack(state_, 4) == 0) {
            lua_pushlightuserdata(state_, nullptr); // too deep: the host limits this already
            break;
        }
        if (value.kind == TORUSTIQ_VALUE_ARRAY) {
            lua_createtable(state_, static_cast<int>(value.as.array.count), 0);
            for (size_t n = 0; n < value.as.array.count; ++n) {
                PushValue(value.as.array.items[n], depth + 1);
                lua_rawseti(state_, -2, static_cast<lua_Integer>(n + 1));
            }
        } else {
            lua_createtable(state_, 0, static_cast<int>(value.as.map.count));
            for (size_t n = 0; n < value.as.map.count; ++n) {
                const torustiq_map_entry& entry = value.as.map.entries[n];
                lua_pushlstring(
                    state_, reinterpret_cast<const char*>(entry.key.data), entry.key.len);
                PushValue(entry.value, depth + 1);
                lua_rawset(state_, -3);
            }
        }
        break;
    }
    default: // NULL
        lua_pushlightuserdata(state_, nullptr);
        break;
    }
}

void LuaRuntime::PushMessage(const torustiq_message& message) {
    lua_createtable(state_, 0, 2);
    if (message.payload.kind == TORUSTIQ_VALUE_BYTES ||
        message.payload.kind == TORUSTIQ_VALUE_STRING) {
        lua_pushlstring(state_,
                        reinterpret_cast<const char*>(message.payload.as.bytes.data),
                        message.payload.as.bytes.len);
        lua_setfield(state_, -2, message.payload.kind == TORUSTIQ_VALUE_STRING ? "text" : "body");
    } else {
        PushValue(message.payload, 0);
        lua_setfield(state_, -2, "value");
    }
    lua_createtable(state_, 0, static_cast<int>(message.header_count));
    for (size_t i = 0; i < message.header_count; ++i) {
        lua_pushstring(state_, message.headers[i].value != nullptr ? message.headers[i].value : "");
        lua_setfield(state_, -2, message.headers[i].key);
    }
    lua_setfield(state_, -2, "headers");
}

bool LuaRuntime::ReadMessage(
    lua_State* state, int index, uint32_t payload_kind, LuaMessage& out, std::string& error) {
    index = lua_absindex(state, index);
    out = LuaMessage{};
    const int type = lua_type(state, index);
    size_t length = 0;
    const bool structured = payload_kind == TORUSTIQ_PAYLOAD_VALUE;
    const bool is_text = payload_kind == TORUSTIQ_PAYLOAD_TEXT;
    if (is_text) {
        out.payload.kind = TORUSTIQ_VALUE_STRING;
    }
    if (!structured && (type == LUA_TSTRING || type == LUA_TNUMBER)) {
        const char* text = lua_tolstring(state, index, &length);
        out.payload.text.assign(text, length);
        return true;
    }
    if (type != LUA_TTABLE) {
        error = std::string(structured ? "a message must be a table, got "
                                       : "a message must be a table or a string, got ") +
                lua_typename(state, type);
        return false;
    }

    if (structured) {
        lua_pushliteral(state, "value");
        lua_rawget(state, index);
        const bool ok = ReadValue(state, -1, 0, out.payload, error);
        lua_pop(state, 1);
        if (!ok) {
            error = "message 'value': " + error;
            return false;
        }
    } else {
        lua_pushstring(state, is_text ? "text" : "body");
        lua_rawget(state, index);
        const int body_type = lua_type(state, -1);
        if (body_type == LUA_TSTRING || body_type == LUA_TNUMBER) {
            const char* text = lua_tolstring(state, -1, &length);
            out.payload.text.assign(text, length);
        } else if (body_type != LUA_TNIL) {
            error = std::string(is_text ? "message 'text'" : "message 'body'") +
                    " must be a string, got " + lua_typename(state, body_type);
            lua_pop(state, 1);
            return false;
        }
        lua_pop(state, 1);
    }

    lua_pushliteral(state, "headers");
    lua_rawget(state, index);
    const int headers_type = lua_type(state, -1);
    if (headers_type == LUA_TTABLE) {
        const int headers_index = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, headers_index) != 0) {
            const int key_type = lua_type(state, -2);
            const int value_type = lua_type(state, -1);
            if (key_type != LUA_TSTRING ||
                (value_type != LUA_TSTRING && value_type != LUA_TNUMBER)) {
                error = "message 'headers' must map strings to strings";
                lua_pop(state, 3); // value, key, headers table
                return false;
            }
            size_t key_length = 0;
            const char* key = lua_tolstring(state, -2, &key_length);
            const char* value = lua_tolstring(state, -1, &length);
            out.headers.emplace_back(std::string(key, key_length), std::string(value, length));
            lua_pop(state, 1); // value; keep the key for lua_next
        }
        std::sort(out.headers.begin(), out.headers.end());
    } else if (headers_type != LUA_TNIL) {
        error = std::string("message 'headers' must be a table, got ") +
                lua_typename(state, headers_type);
        lua_pop(state, 1);
        return false;
    }
    lua_pop(state, 1);
    return true;
}

bool LuaRuntime::ReadMessages(lua_State* state,
                              int index,
                              uint32_t payload_kind,
                              std::vector<LuaMessage>& out,
                              std::string& error) {
    index = lua_absindex(state, index);
    out.clear();
    const int type = lua_type(state, index);
    if (type == LUA_TNIL || (type == LUA_TBOOLEAN && lua_toboolean(state, index) == 0)) {
        return true;
    }
    if (type == LUA_TTABLE && lua_rawlen(state, index) > 0) {
        const lua_Unsigned count = lua_rawlen(state, index);
        for (lua_Unsigned i = 1; i <= count; ++i) {
            lua_rawgeti(state, index, static_cast<lua_Integer>(i));
            LuaMessage message;
            const bool ok = ReadMessage(state, -1, payload_kind, message, error);
            lua_pop(state, 1);
            if (!ok) {
                return false;
            }
            out.push_back(std::move(message));
        }
        return true;
    }
    LuaMessage message;
    if (!ReadMessage(state, index, payload_kind, message, error)) {
        return false;
    }
    out.push_back(std::move(message));
    return true;
}

} // namespace torustiq::plugins
