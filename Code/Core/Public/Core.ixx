export module Core;

export typedef signed char        int8;
export typedef short              int16;
export typedef int                int32;
export typedef long long          int64;
export typedef unsigned char      uint8;
export typedef unsigned short     uint16;
export typedef unsigned int       uint32;
export typedef unsigned long long uint64;

export constexpr size_t INT8_MIN = (-127i8 - 1);
export constexpr size_t INT16_MIN = (-32767i16 - 1);
export constexpr size_t INT32_MIN = (-2147483647i32 - 1);
export constexpr size_t INT64_MIN = (-9223372036854775807i64 - 1);
export constexpr size_t INT8_MAX = 127i8;
export constexpr size_t INT16_MAX = 32767i16;
export constexpr size_t INT32_MAX = 2147483647i32;
export constexpr size_t INT64_MAX = 9223372036854775807i64;
export constexpr size_t UINT8_MAX = 0xffui8;
export constexpr size_t UINT16_MAX = 0xffffui16;
export constexpr size_t UINT32_MAX = 0xffffffffui32;
export constexpr size_t UINT64_MAX = 0xffffffffffffffffui64;

export import <span>;
export import <vector>;
export import <array>;
export import <list>;
export import <string>;
export import <string_view>;
export import <variant>;
export import <fstream>;
export import <iostream>;
export import <filesystem>;
export import <functional>;
export import <memory>;
export import <mutex>;
export import <thread>;
export import <bitset>;
export import <coroutine>;
export import <atomic>;
export import <type_traits>;
export import <chrono>;

export import <glm/glm.hpp>;
export import <glm/gtc/matrix_transform.hpp>;