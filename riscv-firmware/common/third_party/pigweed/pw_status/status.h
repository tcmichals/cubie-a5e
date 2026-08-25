#ifndef PIGWEED_PW_STATUS_H
#define PIGWEED_PW_STATUS_H

#include <stdint.h>

namespace pw {

enum class StatusCode : uint32_t {
    Ok = 0,
    Cancelled = 1,
    Unknown = 2,
    InvalidArgument = 3,
    DeadlineExceeded = 4,
    NotFound = 5,
    AlreadyExists = 6,
    PermissionDenied = 7,
    ResourceExhausted = 8,
    FailedPrecondition = 9,
    Aborted = 10,
    OutOfRange = 11,
    Unimplemented = 12,
    Internal = 13,
    Unavailable = 14,
    DataLoss = 15,
    Unauthenticated = 16,
};

class Status {
public:
    constexpr Status() : code_(StatusCode::Ok) {}
    constexpr Status(StatusCode code) : code_(code) {}

    constexpr bool ok() const { return code_ == StatusCode::Ok; }
    constexpr StatusCode code() const { return code_; }

    constexpr bool operator==(const Status& other) const { return code_ == other.code_; }
    constexpr bool operator!=(const Status& other) const { return code_ != other.code_; }

    static constexpr Status Ok() { return Status(StatusCode::Ok); }
    static constexpr Status InvalidArgument() { return Status(StatusCode::InvalidArgument); }
    static constexpr Status ResourceExhausted() { return Status(StatusCode::ResourceExhausted); }
    static constexpr Status Unavailable() { return Status(StatusCode::Unavailable); }
    static constexpr Status DeadlineExceeded() { return Status(StatusCode::DeadlineExceeded); }

private:
    StatusCode code_;
};

constexpr Status OkStatus() { return Status::Ok(); }

} // namespace pw

#endif // PIGWEED_PW_STATUS_H
