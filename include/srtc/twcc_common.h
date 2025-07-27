#pragma once

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-
namespace srtc::twcc
{

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
constexpr auto kCHUNK_RUN_LENGTH = 0;
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
constexpr auto kCHUNK_STATUS_VECTOR = 1;

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.1
constexpr auto kSTATUS_NOT_RECEIVED = 0;
constexpr auto kSTATUS_RECEIVED_SMALL_DELTA = 1;
constexpr auto kSTATUS_RECEIVED_LARGE_DELTA = 2;
constexpr auto kSTATUS_RECEIVED_NO_TS = 3;

} // namespace srtc::twcc