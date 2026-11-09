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

LuaMessageView::LuaMessageView(const LuaMessage& message) {
    headers_.reserve(message.headers.size());
    for (const auto& [key, value] : message.headers) {
        headers_.push_back({key.c_str(), value.c_str()});
    }
    message_.body.data = reinterpret_cast<const uint8_t*>(message.body.data());
    message_.body.len = message.body.size();
    message_.headers = headers_.data();
    message_.header_count = headers_.size();
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

void LuaRuntime::PushMessage(const torustiq_message& message) {
    lua_createtable(state_, 0, 2);
    lua_pushlstring(state_, reinterpret_cast<const char*>(message.body.data), message.body.len);
    lua_setfield(state_, -2, "body");
    lua_createtable(state_, 0, static_cast<int>(message.header_count));
    for (size_t i = 0; i < message.header_count; ++i) {
        lua_pushstring(state_, message.headers[i].value != nullptr ? message.headers[i].value : "");
        lua_setfield(state_, -2, message.headers[i].key);
    }
    lua_setfield(state_, -2, "headers");
}

bool LuaRuntime::ReadMessage(lua_State* state, int index, LuaMessage& out, std::string& error) {
    index = lua_absindex(state, index);
    out = LuaMessage{};
    const int type = lua_type(state, index);
    size_t length = 0;
    if (type == LUA_TSTRING || type == LUA_TNUMBER) {
        const char* text = lua_tolstring(state, index, &length);
        out.body.assign(text, length);
        return true;
    }
    if (type != LUA_TTABLE) {
        error =
            std::string("a message must be a table or a string, got ") + lua_typename(state, type);
        return false;
    }

    lua_pushliteral(state, "body");
    lua_rawget(state, index);
    const int body_type = lua_type(state, -1);
    if (body_type == LUA_TSTRING || body_type == LUA_TNUMBER) {
        const char* text = lua_tolstring(state, -1, &length);
        out.body.assign(text, length);
    } else if (body_type != LUA_TNIL) {
        error =
            std::string("message 'body' must be a string, got ") + lua_typename(state, body_type);
        lua_pop(state, 1);
        return false;
    }
    lua_pop(state, 1);

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
            const bool ok = ReadMessage(state, -1, message, error);
            lua_pop(state, 1);
            if (!ok) {
                return false;
            }
            out.push_back(std::move(message));
        }
        return true;
    }
    LuaMessage message;
    if (!ReadMessage(state, index, message, error)) {
        return false;
    }
    out.push_back(std::move(message));
    return true;
}

} // namespace torustiq::plugins
