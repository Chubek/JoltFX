#ifndef INGAGI_MODULE_HPP
#define INGAGI_MODULE_HPP
#include "Ingagi-Module.h"
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ingagi {
class error : public std::runtime_error {
public:
    ingagi_status status;
    error(ingagi_status code, const char *message) : std::runtime_error(message), status(code) {}
};
inline ingagi_value integer(int64_t v) { return {INGAGI_INT, 0, v, 0}; }
inline ingagi_value boolean(bool v) { return {INGAGI_BOOL, 0, v ? 1 : 0, 0}; }
inline ingagi_value real(double v) { return {INGAGI_FLOAT, 0, 0, v}; }

// Move-only owning VM handle. Serialize ownership changes/close in the host;
// concurrent/reentrant calls are diagnosed by the C API. Destruction must not
// race an in-flight call. Use close() when destruction errors must be reported.
class session {
    ingagi_handle handle_ = 0;
    static void check(ingagi_status code, const char *message) {
        if (code != INGAGI_OK) throw error(code, message);
    }
public:
    explicit session(uint64_t budget = 1000000) {
        if (ingagi_abi_version() != INGAGI_ABI_VERSION) throw error(INGAGI_UNSUPPORTED, "Ingagi ABI mismatch");
        char message[4096]{};
        auto code = ingagi_create(budget, &handle_, message, sizeof(message));
        check(code, message);
    }
    ~session() { if (handle_) ingagi_destroy(handle_, nullptr, 0); }
    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&& other) noexcept : handle_(std::exchange(other.handle_, 0)) {}
    session& operator=(session&& other) {
        if (this != &other) { close(); handle_ = std::exchange(other.handle_, 0); }
        return *this;
    }
    ingagi_handle native_handle() const noexcept { return handle_; }
    void close() {
        if (!handle_) return;
        char message[4096]{};
        auto code = ingagi_destroy(handle_, message, sizeof(message));
        check(code, message); handle_ = 0;
    }
    void load(std::string_view source) {
        char message[4096]{};
        auto code = ingagi_load(handle_, source.data(), source.size(), message, sizeof(message));
        check(code, message);
    }
    ingagi_value call(std::string_view name, const std::vector<ingagi_value>& args = {}) {
        if (args.size() > INGAGI_MAX_ARGS) throw error(INGAGI_INVALID_ARGUMENT, "too many arguments");
        char message[4096]{}; ingagi_value result{};
        auto code = ingagi_call(handle_, name.data(), name.size(), args.data(),
            static_cast<uint32_t>(args.size()), &result, message, sizeof(message));
        check(code, message); return result;
    }
    void register_module(const ingagi_module& module) {
        char message[4096]{};
        auto code = ingagi_register_module(handle_, &module, message, sizeof(message));
        check(code, message);
    }
};
} // namespace ingagi
#endif
