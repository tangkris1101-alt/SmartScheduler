#pragma once

// 中继（api.class.feimsg.cn）的默认地址与应用令牌：AI 编排、内测门禁、日志反馈共用。
// AI 的地址/令牌可在设置页里改（存 QSettings）；门禁与反馈始终走这里的默认值，
// 免得用户改了 AI 地址后把自己锁在门外，或把日志发到别处去。
namespace relay {
inline constexpr const char *kDefaultUrl = "https://api.class.feimsg.cn";
// 应用级令牌（可轮换；服务端 .env 中为 APP_TOKEN）
inline constexpr const char *kDefaultToken =
    "2627d9f4b6ec2d20892bb682276b650ca046d6b802dd6616a154af0d3903a92c";
} // namespace relay
