#pragma once

#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cctype>
#include <ciso646>

#include <functional>
#include <exception>
#include <stdexcept>
#include <iterator>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <memory>
#include <tuple>
#include <new>
#include <random>
#include <chrono>
#include <type_traits>

#include <vector>
#include <array>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "utils/suppress_warnings.h"
#include "utils/visibility.h"
#include "utils/preprocessor.h"
#include "utils/doctest/doctest_proxy.h"

HA_SUPPRESS_WARNINGS

#define DYNAMIX_NO_DM_THIS
#include <dynamix/dynamix.hpp>

#include <ppk_assert.h>
#define hassert PPK_ASSERT

#ifdef _MSC_VER
// oddly enough these are not silenced by HA_SUPPRESS_WARNINGS and have to be listed explicitly
#pragma warning(disable : 4702) // unreachable code
#pragma warning(disable : 4715) // not all control paths return a value
#endif                          // _MSC_VER
#include <sajson/include/sajson.h>

#define BGFX_SHARED_LIB_USE 1
#include <bgfx/c99/bgfx.h>
#define BGFX_INVALID_HANDLE UINT16_MAX

#define IMGUI_API HA_SYMBOL_IMPORT
#include <imgui.h>

#include <yama/yama.hpp>

#include <boost/any.hpp>

#include <boost/variant.hpp>

#include <boost/mpl/for_each.hpp>

HA_SUPPRESS_WARNINGS_END

// helpers that don't expand to anything - used by the type parser
#define FIELD           // indicates the start of a field definition inside of a type
#define EXPORT          // attribute - indicates that the field should be exported
#define ATTRIBUTES(...) // list attributes and tags in a comma-separated fashion using this

#include "utils/types.h"
#include "utils/singleton.h"
#include "core/serialization/JsonData.h"
#include "core/messages/message_macros.h"

#include "core/Object.h"
