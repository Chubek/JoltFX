/**
 * @file DSLtk.hpp
 * @brief Header-only DSL construction toolkit for modern C++ (C++20)
 *
 * ============================================================
 *  OVERVIEW
 * ============================================================
 *
 * DSLtk.hpp provides a set of composable "feature mixins" that you
 * attach to a CRTP base class to build Domain-Specific Languages entirely
 * in headers, with zero runtime overhead where possible.
 *
 * Core idea:
 *
 *   struct MyDSL : dsl::DSL<MyDSL, Feature1, Feature2, ...> { ... };
 *
 * Available features:
 *   - dsl::Pipeline       — operator| chaining (pipe stages)
 *   - dsl::Operators      — composable predicates (&, |, !)
 *   - dsl::PatternMatch   — compile-time pattern matching
 * (match/when/otherwise)
 *   - dsl::AST            — value-type AST nodes (leaf/node/dump)
 *   - dsl::Rewrite        — AST rewrite rules
 *   - dsl::ExprTemplates  — lazy expression trees with fused eval
 *   - dsl::CustomLiterals — static constexpr literal suffix registration
 *   - dsl::EnumReflection — magic_enum-compatible enum reflection and utilities
 *
 * ============================================================
 *  QUICK START
 * ============================================================
 *
 * Step 1 — Pick your features:
 *
 *   struct MathDSL : dsl::DSL<MathDSL, dsl::Pipeline, dsl::Operators> {};
 * Step 2 — Use it:
 *
 *   MathDSL m;
 *   auto result = m.eval(5.0);
 *
 * Step 3 — Expose to users via a thin wrapper header (mydsl.hpp):
 *
 *   #include "DSLtk.hpp"
 *   struct MathDSL : dsl::DSL<MathDSL, dsl::Pipeline, dsl::Operators> { ... };
 *
 * Users only include mydsl.hpp — they never see DSLtk.hpp directly.
 *
 * ============================================================
 *  FEATURE REFERENCE
 * ============================================================
 *
 * --- Pipeline ---
 *
 *   Adds operator| so values can flow through a chain of transformations.
 *
 *   Usage:
 *     auto result = value | dsl::pipe(f1) | dsl::pipe(f2);
 *     // equivalent to: f2(f1(value))
 *
 *   Example:
 *     struct Proc : dsl::DSL<Proc, dsl::Pipeline> {};
 *     Proc p;
 *     auto r = p.wrap(10)
 *             | dsl::pipe([](int x){ return x * 2; })
 *             | dsl::pipe([](int x){ return x + 1; });
 *     // r == 21
 *
 * --- Operators ---
 *
 *   Adds &, |, ! for composing predicates/validators.
 *
 *   Usage:
 *     auto combined = pred_a & pred_b;   // both must pass
 *     auto either   = pred_a | pred_b;   // at least one must pass
 *     auto negated  = !pred_a;
 *
 *   Example:
 *     auto is_positive = dsl::predicate([](int x){ return x > 0; });
 *     auto is_even     = dsl::predicate([](int x){ return x % 2 == 0; });
 *     auto rule = is_positive & is_even;
 *     rule(4);   // true
 *     rule(-2);  // false
 *
 * --- PatternMatch ---
 *
 *   Compile-time dispatch table built from when<>/otherwise clauses.
 *
 *   Usage:
 *     auto table = dsl::match(
 *         dsl::when<Key1>(handler1),
 *         dsl::when<Key2>(handler2),
 *         dsl::otherwise(fallback)
 *     );
 *     auto result = table(key, args...);
 *
 *   For string patterns (CTRE-style, limited):
 *     auto table = dsl::match(
 *         dsl::when<dsl::pattern<"[0-9]+">>(handler),
 *         dsl::otherwise(fallback)
 *     );
 *
 *   Example:
 *     enum class Color { Red, Green, Blue };
 *     auto name = dsl::match(
 *         dsl::when<Color::Red>  ([](auto){ return "red";   }),
 *         dsl::when<Color::Green>([](auto){ return "green"; }),
 *         dsl::otherwise         ([](auto){ return "other"; })
 *     );
 *     name(Color::Red, 0);  // "red"
 *
 * --- AST ---
 *
 *   Value-type AST nodes. Leaves hold data; inner nodes hold children.
 *
 *   Usage:
 *     auto leaf_node  = dsl::leaf<"name">(value);
 *     auto inner_node = dsl::node<"op">(child1, child2);
 *     std::cout << node.dump();   // S-expression string
 *
 *   Example:
 *     auto expr = dsl::node<"add">(
 *         dsl::leaf<"num">(1),
 *         dsl::leaf<"num">(2)
 *     );
 *     expr.dump();  // "(add (num 1) (num 2))"
 *
 * --- Rewrite ---
 *
 *   Apply transformation rules to an AST until a fixpoint is reached.
 *
 *   Usage:
 *     auto rules = dsl::rewrite_set(
 *         dsl::rule<"name">(predicate, transformer)
 *     );
 *     auto new_tree = rules.apply(tree);
 *
 *   Example:
 *     auto rules = dsl::rewrite_set(
 *         dsl::rule<"double-neg">(
 *             [](auto& t){ return t.tag() == "not" && t.child(0).tag() ==
 * "not"; },
 *             [](auto& t){ return t.child(0).child(0); }  // remove double
 * negation
 *         )
 *     );
 *
 * --- ExprTemplates ---
 *
 *   Lazy expression trees. Operators build a tree; .eval() fuses it.
 *
 *   Usage:
 *     auto expr = a + b * c;   // no computation yet
 *     auto val  = expr.eval(); // single fused pass
 *
 *   Example:
 *     struct Vec : dsl::DSL<Vec, dsl::ExprTemplates> {
 *         std::vector<float> data;
 *     };
 *     Vec a{{1,2,3}}, b{{4,5,6}}, c{{7,8,9}};
 *     auto expr = a + b * c;
 *     auto result = expr.eval();  // {1+4*7, 2+5*8, 3+6*9} = {29, 42, 57}
 *
 * --- CustomLiterals ---
 *
 *   Register constexpr literal suffixes via a static table.
 *
 *   Usage:
 *     static constexpr auto literals = dsl::literal_set(
 *         dsl::lit<"_km">([](long double v){ return v * 1000.0L; }),
 *         dsl::lit<"_m"> ([](long double v){ return v; })
 *     );
 *     auto d = MyDSL::parse_literal("3.5_km");  // 3500.0
 *
 *   For native C++ UDL syntax (3.5_km), define operator"" in an
 *   inline namespace in your own header — see examples below.
 *
 * --- Memoization ---
 *
 *   Caches expensive pure-function results by hashable key.
 *
 *   Usage:
 *     auto f = dsl::memoize<int, int>([](int x){ return x * x; });
 *
 *   Example:
 *     auto fib = dsl::memoize<int, long long>([](int n){ return n < 2 ? n :
 * 1LL; }); auto v = fib(20);
 *
 * --- Lazy ---
 *
 *   Defers one computation until first get()/force() and caches it.
 *
 *   Usage:
 *     dsl::Lazy<int> v([]{ return 42; });
 *
 *   Example:
 *     dsl::Lazy<std::string> cfg([]{ return std::string("ready"); });
 *     auto s = cfg.get();
 *
 * --- Monadic ---
 *
 *   Optional-aware map/flat_map/filter/or_else for Maybe and std::optional.
 *
 *   Usage:
 *     auto out = dsl::Maybe<int>(5).map([](int x){ return x + 1;
 * }).or_else(0);
 *
 *   Example:
 *     std::optional<int> v = 3;
 *     auto out = dsl::map(v, [](int x){ return x * 2; });
 *
 * --- Result ---
 *
 *   Explicit success/error flow modeled as Result<T,E>.
 *
 *   Usage:
 *     auto r = dsl::Result<int,std::string>::from_ok(7);
 *
 *   Example:
 *     auto r = dsl::Result<int,std::string>::from_ok(4).map([](int x){ return
 * x+1; });
 *
 * --- CombinatorParser ---
 *
 *   Functional parser core + combinators (`|`, `&`, `*`, optional()).
 *
 *   Usage:
 *     auto p = dsl::parser([](dsl::ParsecInput&
 * in)->dsl::ExpectedResult<char>{...});
 *
 *   Example:
 *     auto seq = p1 & p2;
 *
 * --- TaskPipeline ---
 *
 *   Composes tasks with `|` and executes with shared TaskState.
 *
 *   Usage:
 *     auto chain = t1 | t2; dsl::run(state, chain);
 *
 *   Example:
 *     dsl::TaskState st; dsl::run(st, chain);
 *
 * --- EnumReflection ---
 *
 *   Static enum reflection, casts, flags, constexpr dispatch, fusion,
 *   enum-indexed containers, stream operators, and optional formatters.
 *   All magic_enum v0.9.8 public APIs are included in this single header.
 *
 *   Usage:
 *     enum class Color { Red = -10, Green = 0, Blue = 10 };
 *     static_assert(dsl::enum_name(Color::Red) == "Red");
 *     constexpr auto color = dsl::enum_cast<Color>("blue", dsl::case_insensitive);
 *     constexpr auto values = dsl::enum_values<Color>();
 *
 *   The dsl::enum_reflection namespace aliases magic_enum. Customize ranges
 *   and names through dsl::customize (an alias of magic_enum::customize).
 *   The default search range is [-128, 127]; enum_name<Value>() is unbounded.
 *   See manual/ch29-enum-reflection.md for the complete API and configuration.
 *
 * ============================================================
 *  FULL EXAMPLES
 * ============================================================
 *
 * Example A — UnitsDSL (CustomLiterals):
 *
 *   struct UnitsDSL : dsl::DSL<UnitsDSL, dsl::CustomLiterals> {
 *       static constexpr auto literals = dsl::literal_set(
 *           dsl::lit<"_km">([](long double v){ return v * 1000.0L; }),
 *           dsl::lit<"_m"> ([](long double v){ return v; }),
 *           dsl::lit<"_cm">([](long double v){ return v / 100.0L; })
 *       );
 *   };
 *   inline namespace unit_literals {
 *       constexpr long double operator""_km(long double v){ return v *
 * 1000.0L; } constexpr long double operator""_m (long double v){ return v; }
 *   }
 *   // Usage:
 *   using namespace unit_literals;
 *   auto d = 3.5_km;  // 3500.0
 *
 * Example B — StateMachineDSL (Pipeline + PatternMatch):
 *
 *   struct SM : dsl::DSL<SM, dsl::Pipeline, dsl::PatternMatch> {
 *       enum class S { Idle, Running, Done };
 *       static constexpr auto transitions = dsl::match(
 *           dsl::when<S::Idle>   ([](auto e){ return
 * e=="start"?S::Running:S::Idle; }), dsl::when<S::Running>([](auto e){ return
 * e=="stop" ?S::Done   :S::Running; }), dsl::otherwise       ([](auto  ){
 * return S::Done; })
 *       );
 *       S state = S::Idle;
 *       void send(std::string_view e){ state = transitions(state, e); }
 *   };
 *   // Usage:
 *   SM sm;
 *   sm.send("start");  // state == Running
 *   sm.send("stop");   // state == Done
 *
 * Example C — VecDSL (ExprTemplates):
 *
 *   struct VecDSL : dsl::DSL<VecDSL, dsl::ExprTemplates> {
 *       std::vector<float> data;
 *   };
 *   // Usage:
 *   VecDSL a{{1,2,3}}, b{{4,5,6}};
 *   auto expr   = a + b;          // lazy
 *   auto result = expr.eval();    // {5, 7, 9}
 *
 * Example D — QueryDSL (AST + Rewrite):
 *
 *   struct QueryDSL : dsl::DSL<QueryDSL, dsl::AST, dsl::Rewrite> {
 *       auto select(auto... cols) { return
 * dsl::node<"select">(dsl::leaf<"col">(cols)...); } auto from  (auto table)  {
 * return dsl::leaf<"from">(table); }
 *   };
 *   // Usage:
 *   QueryDSL q;
 *   auto tree = q.select("name","age") | q.from("users");
 *   std::cout << tree.dump();
 *   // "(select (col name) (col age) (from users))"
 *
 * Example E — Memoization:
 *   auto sq = dsl::memoize<int,int>([](int x){ return x*x; });
 *   auto v = sq(12);
 *
 * Example F — Lazy + Monadic:
 *   dsl::Lazy<dsl::Maybe<int>> lazy([]{ return dsl::Maybe<int>(8); });
 *   auto out = lazy.get().map([](int x){ return x+1; }).or_else(0);
 *
 * Example G — Result:
 *   auto r = dsl::Result<int,std::string>::from_ok(9).map([](int x){ return
 * x*2; });
 *
 * Example H — Parser + TaskPipeline:
 *   dsl::ParsecInput in{"abc",0};
 *   auto p = dsl::parser([](dsl::ParsecInput& i)->dsl::ExpectedResult<char>{
 * return i.consume(); }); dsl::Task t{"noop", [](dsl::TaskState&){ return
 * dsl::Result<std::string,std::string>::from_ok("ok"); }};
 *
 * ============================================================
 *  DESIGN NOTES
 * ============================================================
 *
 * 1. CRTP avoids virtual dispatch — all polymorphism is compile-time.
 * 2. Features are orthogonal: adding Pipeline does not affect PatternMatch.
 * 3. NTTPs (non-type template parameters) are used for string tags and
 *    pattern strings, enabling constexpr matching with zero overhead.
 * 4. AST nodes are value types (copyable, movable) — no heap allocation
 *    for small trees (uses std::variant + std::vector internally).
 * 5. ExprTemplates use recursive template structs; .eval() is the only
 *    point where actual computation happens.
 * 6. CustomLiterals::parse_literal does a linear scan of the literal_set
 *    at runtime; for native UDL syntax prefer operator"" in your header.
 *
 * ============================================================
 *  REQUIREMENTS
 * ============================================================
 *
 *   - C++20 or later (uses concepts, NTTPs, constexpr lambdas)
 *   - No external dependencies
 *   - Header-only: just #include "DSLtk.hpp"
 * ============================================================
 */

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// BEGIN DSLTK EMBEDDED MAGIC_ENUM
// Generated by scripts/update-magic-enum.py; do not edit this block.
// Source: https://github.com/Neargye/magic_enum/tree/v0.9.8
// Public-header SHA256: 8e39051ab6167a80897f9b67c24da77f29f108209b11f5ec47ab8d74964611d6
// Upstream include guards and MIT notices are retained.
// Embedded upstream header: magic_enum_all.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_ALL_HPP
#define NEARGYE_MAGIC_ENUM_ALL_HPP

// Embedded upstream header: magic_enum.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_HPP
#define NEARGYE_MAGIC_ENUM_HPP

#define MAGIC_ENUM_VERSION_MAJOR 0
#define MAGIC_ENUM_VERSION_MINOR 9
#define MAGIC_ENUM_VERSION_PATCH 8

#ifndef MAGIC_ENUM_USE_STD_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>
#endif

#if defined(MAGIC_ENUM_CONFIG_FILE)
#  include MAGIC_ENUM_CONFIG_FILE
#endif

#ifndef MAGIC_ENUM_USE_STD_MODULE
#if !defined(MAGIC_ENUM_USING_ALIAS_OPTIONAL)
#  include <optional>
#endif
#if !defined(MAGIC_ENUM_USING_ALIAS_STRING)
#  include <string>
#endif
#if !defined(MAGIC_ENUM_USING_ALIAS_STRING_VIEW)
#  include <string_view>
#endif
#endif

#if defined(MAGIC_ENUM_NO_ASSERT)
#  define MAGIC_ENUM_ASSERT(...) static_cast<void>(0)
#elif !defined(MAGIC_ENUM_ASSERT)
#  include <cassert>
#  define MAGIC_ENUM_ASSERT(...) assert((__VA_ARGS__))
#endif

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunknown-warning-option"
#  pragma clang diagnostic ignored "-Wenum-constexpr-conversion"
#  pragma clang diagnostic ignored "-Wuseless-cast" // suppresses 'static_cast<char_type>('\0')' for char_type = char (common on Linux).
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized" // May be used uninitialized 'return {};'.
#  pragma GCC diagnostic ignored "-Wuseless-cast" // suppresses 'static_cast<char_type>('\0')' for char_type = char (common on Linux).
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 26495) // Variable 'static_str<N>::chars_' is uninitialized.
#  pragma warning(disable : 28020) // Arithmetic overflow: Using operator '-' on a 4 byte value and then casting the result to a 8 byte value.
#  pragma warning(disable : 26451) // The expression '0<=_Param_(1)&&_Param_(1)<=1-1' is not true at this call.
#  pragma warning(disable : 4514) // Unreferenced inline function has been removed.
#endif

// Checks magic_enum compiler compatibility.
#if defined(__clang__) && __clang_major__ >= 5 || defined(__GNUC__) && __GNUC__ >= 9 || defined(_MSC_VER) && _MSC_VER >= 1910 || defined(__RESHARPER__)
#  undef  MAGIC_ENUM_SUPPORTED
#  define MAGIC_ENUM_SUPPORTED 1
#endif

// Checks magic_enum compiler aliases compatibility.
#if defined(__clang__) && __clang_major__ >= 5 || defined(__GNUC__) && __GNUC__ >= 9 || defined(_MSC_VER) && _MSC_VER >= 1920
#  undef  MAGIC_ENUM_SUPPORTED_ALIASES
#  define MAGIC_ENUM_SUPPORTED_ALIASES 1
#endif

// Specify the calling convention for compilers that need it in order to get reliable mangled names under different
// compiler flags. In particular, MSVC allows changing the default calling convention on x86.
#if defined(__clang__) || defined(__GNUC__)
#define MAGIC_ENUM_CALLING_CONVENTION
#elif defined(_MSC_VER)
#define MAGIC_ENUM_CALLING_CONVENTION __cdecl
#endif

// Enum value must be greater or equals than MAGIC_ENUM_RANGE_MIN. By default MAGIC_ENUM_RANGE_MIN = -128.
// If need another min range for all enum types by default, redefine the macro MAGIC_ENUM_RANGE_MIN.
#if !defined(MAGIC_ENUM_RANGE_MIN)
#  define MAGIC_ENUM_RANGE_MIN -128
#endif

// Enum value must be less or equals than MAGIC_ENUM_RANGE_MAX. By default MAGIC_ENUM_RANGE_MAX = 127.
// If need another max range for all enum types by default, redefine the macro MAGIC_ENUM_RANGE_MAX.
#if !defined(MAGIC_ENUM_RANGE_MAX)
#  define MAGIC_ENUM_RANGE_MAX 127
#endif

// Improve ReSharper C++ intellisense performance with builtins, avoiding unnecessary template instantiations.
#if defined(__RESHARPER__)
#  undef MAGIC_ENUM_GET_ENUM_NAME_BUILTIN
#  undef MAGIC_ENUM_GET_TYPE_NAME_BUILTIN
#  if __RESHARPER__ >= 20230100
#    define MAGIC_ENUM_GET_ENUM_NAME_BUILTIN(V) __rscpp_enumerator_name(V)
#    define MAGIC_ENUM_GET_TYPE_NAME_BUILTIN(T) __rscpp_type_name<T>()
#  else
#    define MAGIC_ENUM_GET_ENUM_NAME_BUILTIN(V) nullptr
#    define MAGIC_ENUM_GET_TYPE_NAME_BUILTIN(T) nullptr
#  endif
#endif

namespace magic_enum {

// If need another optional type, define the macro MAGIC_ENUM_USING_ALIAS_OPTIONAL.
#if defined(MAGIC_ENUM_USING_ALIAS_OPTIONAL)
MAGIC_ENUM_USING_ALIAS_OPTIONAL
#else
using std::optional;
#endif

// If need another string_view type, define the macro MAGIC_ENUM_USING_ALIAS_STRING_VIEW.
#if defined(MAGIC_ENUM_USING_ALIAS_STRING_VIEW)
MAGIC_ENUM_USING_ALIAS_STRING_VIEW
#else
using std::string_view;
#endif

// If need another string type, define the macro MAGIC_ENUM_USING_ALIAS_STRING.
#if defined(MAGIC_ENUM_USING_ALIAS_STRING)
MAGIC_ENUM_USING_ALIAS_STRING
#else
using std::string;
#endif

using char_type = string_view::value_type;
static_assert(std::is_same_v<string_view::value_type, string::value_type>, "magic_enum::customize requires same string_view::value_type and string::value_type");
static_assert([] {
  if constexpr (std::is_same_v<char_type, wchar_t>) {
    constexpr const char     c[] =  "abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789|";
    constexpr const wchar_t wc[] = L"abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789|";
    static_assert(std::size(c) == std::size(wc), "magic_enum::customize identifier characters are multichars in wchar_t.");

    for (std::size_t i = 0; i < std::size(c); ++i) {
      if (c[i] != wc[i]) {
        return false;
      }
    }
  }
  return true;
} (), "magic_enum::customize wchar_t is not compatible with ASCII.");

namespace customize {
  template <typename E, typename = void>
  struct enum_range;
}

namespace detail {
  template<typename E, typename = void>
  constexpr inline std::size_t prefix_length_or_zero = 0;

  template<typename E>
  constexpr inline auto prefix_length_or_zero<E, std::void_t<decltype(customize::enum_range<E>::prefix_length)>> = std::size_t{customize::enum_range<E>::prefix_length};
}

namespace customize {

template <bool IsFlags = false, int Min = MAGIC_ENUM_RANGE_MIN, int Max = MAGIC_ENUM_RANGE_MAX, std::size_t PrefixLength = 0>
struct adl_info_holder {
  constexpr static int max = Max;
  constexpr static int min = Min;
  constexpr static bool is_flags = IsFlags;
  constexpr static std::size_t prefix_length = PrefixLength;

  template<int min, int max>
  constexpr static adl_info_holder<IsFlags, min, max, PrefixLength> minmax() { return {}; }

  template<bool is_flag>
  constexpr static adl_info_holder<is_flag, Min, Max, PrefixLength> flag() { return {}; }

  template<std::size_t prefix_len>
  constexpr static adl_info_holder<IsFlags, Min, Max, prefix_len> prefix() { return {}; }
};

constexpr adl_info_holder<> adl_info() { return {}; }

// Enum value must be in range [MAGIC_ENUM_RANGE_MIN, MAGIC_ENUM_RANGE_MAX]. By default MAGIC_ENUM_RANGE_MIN = -128, MAGIC_ENUM_RANGE_MAX = 127.
// If need another range for all enum types by default, redefine the macro MAGIC_ENUM_RANGE_MIN and MAGIC_ENUM_RANGE_MAX.
// If need another range for specific enum type, add specialization enum_range for necessary enum type.
template <typename E, typename>
struct enum_range {
  static constexpr int min = MAGIC_ENUM_RANGE_MIN;
  static constexpr int max = MAGIC_ENUM_RANGE_MAX;
};

template <typename E>
struct enum_range<E, decltype(void(magic_enum_define_range_adl(E{})))> : decltype(magic_enum_define_range_adl(E{})) {};

static_assert(MAGIC_ENUM_RANGE_MAX > MAGIC_ENUM_RANGE_MIN, "MAGIC_ENUM_RANGE_MAX must be greater than MAGIC_ENUM_RANGE_MIN.");

namespace detail {

enum class customize_tag {
  default_tag,
  invalid_tag,
  custom_tag
};

} // namespace magic_enum::customize::detail

class customize_t : public std::pair<detail::customize_tag, string_view> {
 public:
  constexpr customize_t(string_view srt) : std::pair<detail::customize_tag, string_view>{detail::customize_tag::custom_tag, srt} {}
  constexpr customize_t(const char_type* srt) : customize_t{string_view{srt}} {}
  constexpr customize_t(detail::customize_tag tag) : std::pair<detail::customize_tag, string_view>{tag, string_view{}} {
    MAGIC_ENUM_ASSERT(tag != detail::customize_tag::custom_tag);
  }
};

// Default customize.
inline constexpr auto default_tag = customize_t{detail::customize_tag::default_tag};
// Invalid customize.
inline constexpr auto invalid_tag = customize_t{detail::customize_tag::invalid_tag};

// If need custom names for enum, add specialization enum_name for necessary enum type.
template <typename E>
constexpr customize_t enum_name(E) noexcept {
  return default_tag;
}

// If need custom type name for enum, add specialization enum_type_name for necessary enum type.
template <typename E>
constexpr customize_t enum_type_name() noexcept {
  return default_tag;
}

} // namespace magic_enum::customize

namespace detail {

template <typename T>
struct supported
#if defined(MAGIC_ENUM_SUPPORTED) || defined(MAGIC_ENUM_NO_CHECK_SUPPORT)
  : std::true_type {};
#else
  : std::false_type {};
#endif

template <auto V, typename E = std::decay_t<decltype(V)>, std::enable_if_t<std::is_enum_v<E>, int> = 0>
using enum_constant = std::integral_constant<E, V>;

template <typename... T>
inline constexpr bool always_false_v = false;

template <typename T, typename = void>
struct has_is_flags : std::false_type {};

template <typename T>
struct has_is_flags<T, std::void_t<decltype(customize::enum_range<T>::is_flags)>> : std::bool_constant<std::is_same_v<bool, std::decay_t<decltype(customize::enum_range<T>::is_flags)>>> {};

template <typename T, typename = void>
struct range_min : std::integral_constant<int, MAGIC_ENUM_RANGE_MIN> {};

template <typename T>
struct range_min<T, std::void_t<decltype(customize::enum_range<T>::min)>> : std::integral_constant<decltype(customize::enum_range<T>::min), customize::enum_range<T>::min> {};

template <typename T, typename = void>
struct range_max : std::integral_constant<int, MAGIC_ENUM_RANGE_MAX> {};

template <typename T>
struct range_max<T, std::void_t<decltype(customize::enum_range<T>::max)>> : std::integral_constant<decltype(customize::enum_range<T>::max), customize::enum_range<T>::max> {};

struct str_view {
  const char* str_ = nullptr;
  std::size_t size_ = 0;
};

template <std::uint16_t N>
class static_str {
 public:
  constexpr explicit static_str(str_view str) noexcept : static_str{str.str_, std::make_integer_sequence<std::uint16_t, N>{}} {
    MAGIC_ENUM_ASSERT(str.size_ == N);
  }

  constexpr explicit static_str(const char* const str) noexcept : static_str{ str, std::make_integer_sequence<std::uint16_t, N>{} } {
  }

  constexpr explicit static_str(string_view str) noexcept : static_str{str.data(), std::make_integer_sequence<std::uint16_t, N>{}} {
    MAGIC_ENUM_ASSERT(str.size() == N);
  }

  constexpr const char_type* data() const noexcept { return chars_; }

  constexpr std::uint16_t size() const noexcept { return N; }

  constexpr string_view str() const noexcept { return string_view(data(), size()); }

 private:
  template <std::uint16_t... I>
  constexpr static_str(const char* str, std::integer_sequence<std::uint16_t, I...>) noexcept : chars_{static_cast<char_type>(str[I])..., static_cast<char_type>('\0')} {}

  template <std::uint16_t... I>
  constexpr static_str(string_view str, std::integer_sequence<std::uint16_t, I...>) noexcept : chars_{str[I]..., static_cast<char_type>('\0')} {}

  char_type chars_[static_cast<std::size_t>(N) + 1];
};

template <>
class static_str<0> {
 public:
  constexpr static_str() noexcept = default;

  constexpr static_str(str_view) noexcept {}

  constexpr static_str(string_view) noexcept {}

  constexpr const char_type* data() const noexcept { return chars_; }

  constexpr std::uint16_t size() const noexcept { return 0; }

  constexpr string_view str() const noexcept { return string_view(data(), size()); }

private:
  static constexpr char_type chars_[1] = {};
};

template <typename Op = std::equal_to<>>
class case_insensitive {
  static constexpr char_type to_lower(char_type c) noexcept {
    return (c >= static_cast<char_type>('A') && c <= static_cast<char_type>('Z')) ? static_cast<char_type>(c + (static_cast<char_type>('a') - static_cast<char_type>('A'))) : c;
  }

 public:
  template <typename L, typename R>
  constexpr auto operator()(L lhs, R rhs) const noexcept -> std::enable_if_t<std::is_same_v<std::decay_t<L>, char_type> && std::is_same_v<std::decay_t<R>, char_type>, bool> {
    return Op{}(to_lower(lhs), to_lower(rhs));
  }
};

constexpr std::size_t find(string_view str, char_type c) noexcept {
#if defined(__clang__) && __clang_major__ < 9 && defined(__GLIBCXX__) || defined(_MSC_VER) && _MSC_VER < 1920 && !defined(__clang__)
// https://stackoverflow.com/questions/56484834/constexpr-stdstring-viewfind-last-of-doesnt-work-on-clang-8-with-libstdc
// https://developercommunity.visualstudio.com/content/problem/360432/vs20178-regression-c-failed-in-test.html
  constexpr bool workaround = true;
#else
  constexpr bool workaround = false;
#endif

  if constexpr (workaround) {
    for (std::size_t i = 0; i < str.size(); ++i) {
      if (str[i] == c) {
        return i;
      }
    }

    return string_view::npos;
  } else {
    return str.find(c);
  }
}

template <typename BinaryPredicate>
inline constexpr bool is_default_predicate_v = std::is_same_v<std::decay_t<BinaryPredicate>, std::equal_to<string_view::value_type>> || std::is_same_v<std::decay_t<BinaryPredicate>, std::equal_to<>>;


template <typename BinaryPredicate>
inline constexpr bool is_nothrow_invocable_v = is_default_predicate_v<BinaryPredicate> || std::is_nothrow_invocable_r_v<bool, BinaryPredicate, char_type, char_type>;


template <typename BinaryPredicate>
constexpr bool cmp_equal(string_view lhs, string_view rhs, [[maybe_unused]] BinaryPredicate&& p) noexcept(is_nothrow_invocable_v<BinaryPredicate>) {
#if defined(_MSC_VER) && _MSC_VER < 1920 && !defined(__clang__)
  // https://developercommunity.visualstudio.com/content/problem/360432/vs20178-regression-c-failed-in-test.html
  // https://developercommunity.visualstudio.com/content/problem/232218/c-constexpr-string-view.html
  constexpr bool workaround = true;
#else
  constexpr bool workaround = false;
#endif

  if constexpr (!is_default_predicate_v<BinaryPredicate> || workaround) {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    const auto size = lhs.size();
    for (std::size_t i = 0; i < size; ++i) {
      if (!p(lhs[i], rhs[i])) {
        return false;
      }
    }

    return true;
  } else {
    return lhs == rhs;
  }
}

template <typename L, typename R>
constexpr bool cmp_less(L lhs, R rhs) noexcept {
  static_assert(std::is_integral_v<L> && std::is_integral_v<R>, "magic_enum::detail::cmp_less requires integral type.");

  if constexpr (std::is_signed_v<L> == std::is_signed_v<R>) {
    // If same signedness (both signed or both unsigned).
    return lhs < rhs;
  } else if constexpr (std::is_same_v<L, bool>) { // bool special case
      return static_cast<R>(lhs) < rhs;
  } else if constexpr (std::is_same_v<R, bool>) { // bool special case
      return lhs < static_cast<L>(rhs);
  } else if constexpr (std::is_signed_v<R>) {
    // If 'right' is negative, then result is 'false', otherwise cast & compare.
    return rhs > 0 && lhs < static_cast<std::make_unsigned_t<R>>(rhs);
  } else {
    // If 'left' is negative, then result is 'true', otherwise cast & compare.
    return lhs < 0 || static_cast<std::make_unsigned_t<L>>(lhs) < rhs;
  }
}

template <typename I>
constexpr I log2(I value) noexcept {
  static_assert(std::is_integral_v<I>, "magic_enum::detail::log2 requires integral type.");

  if constexpr (std::is_same_v<I, bool>) { // bool special case
    return MAGIC_ENUM_ASSERT(false), value;
  } else {
    auto ret = I{0};
    for (; value > I{1}; value >>= I{1}, ++ret) {}

    return ret;
  }
}

#if defined(__cpp_lib_array_constexpr) && __cpp_lib_array_constexpr >= 201603L
#  define MAGIC_ENUM_ARRAY_CONSTEXPR 1
#else
template <typename T, std::size_t N, std::size_t... I>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N], std::index_sequence<I...>) noexcept {
  return {{a[I]...}};
}
#endif

template <typename T>
inline constexpr bool is_enum_v = std::is_enum_v<T> && std::is_same_v<T, std::decay_t<T>>;

template <typename E>
constexpr auto MAGIC_ENUM_CALLING_CONVENTION n() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::n requires enum type.");

  if constexpr (supported<E>::value) {
#if defined(MAGIC_ENUM_GET_TYPE_NAME_BUILTIN)
    constexpr auto name_ptr = MAGIC_ENUM_GET_TYPE_NAME_BUILTIN(E);
    constexpr auto name = name_ptr ? str_view{name_ptr, std::char_traits<char>::length(name_ptr)} : str_view{};
#elif defined(__clang__)
    str_view name;
    if constexpr (sizeof(__PRETTY_FUNCTION__) == sizeof(__FUNCTION__)) {
      static_assert(always_false_v<E>, "magic_enum::detail::n requires __PRETTY_FUNCTION__.");
      return str_view{};
    } else {
      name.size_ = sizeof(__PRETTY_FUNCTION__) - 36;
      name.str_ = __PRETTY_FUNCTION__ + 34;
    }
#elif defined(__GNUC__)
    auto name = str_view{__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 1};
    if constexpr (sizeof(__PRETTY_FUNCTION__) == sizeof(__FUNCTION__)) {
      static_assert(always_false_v<E>, "magic_enum::detail::n requires __PRETTY_FUNCTION__.");
      return str_view{};
    } else if (name.str_[name.size_ - 1] == ']') {
      name.size_ -= 50;
      name.str_ += 49;
    } else {
      name.size_ -= 40;
      name.str_ += 37;
    }
#elif defined(_MSC_VER)
    // CLI/C++ workaround (see https://github.com/Neargye/magic_enum/issues/284).
    str_view name;
    name.str_ = __FUNCSIG__;
    name.str_ += 40;
    name.size_ += sizeof(__FUNCSIG__) - 57;
#else
    auto name = str_view{};
#endif
    std::size_t p = 0;
    for (std::size_t i = name.size_; i > 0; --i) {
      if (name.str_[i] == ':') {
        p = i + 1;
        break;
      }
    }
    if (p > 0) {
      name.size_ -= p;
      name.str_ += p;
    }
    return name;
  } else {
    return str_view{}; // Unsupported compiler or Invalid customize.
  }
}

template <typename E>
constexpr auto type_name() noexcept {
  [[maybe_unused]] constexpr auto custom = customize::enum_type_name<E>();
  static_assert(std::is_same_v<std::decay_t<decltype(custom)>, customize::customize_t>, "magic_enum::customize requires customize_t type.");
  if constexpr (custom.first == customize::detail::customize_tag::custom_tag) {
    constexpr auto name = custom.second;
    static_assert(!name.empty(), "magic_enum::customize requires not empty string.");
    return static_str<name.size()>{name};
  } else if constexpr (custom.first == customize::detail::customize_tag::invalid_tag) {
    return static_str<0>{};
  } else if constexpr (custom.first == customize::detail::customize_tag::default_tag) {
    constexpr auto name = n<E>();
    return static_str<name.size_>{name};
  } else {
    static_assert(always_false_v<E>, "magic_enum::customize invalid.");
  }
}

template <typename E>
inline constexpr auto type_name_v = type_name<E>();

template <auto V>
constexpr auto MAGIC_ENUM_CALLING_CONVENTION n() noexcept {
  static_assert(is_enum_v<decltype(V)>, "magic_enum::detail::n requires enum type.");

  if constexpr (supported<decltype(V)>::value) {
#if defined(MAGIC_ENUM_GET_ENUM_NAME_BUILTIN)
    constexpr auto name_ptr = MAGIC_ENUM_GET_ENUM_NAME_BUILTIN(V);
    auto name = name_ptr ? str_view{name_ptr, std::char_traits<char>::length(name_ptr)} : str_view{};
#elif defined(__clang__)
    str_view name;
    if constexpr (sizeof(__PRETTY_FUNCTION__) == sizeof(__FUNCTION__)) {
      static_assert(always_false_v<decltype(V)>, "magic_enum::detail::n requires __PRETTY_FUNCTION__.");
      return str_view{};
    } else {
      name.size_ = sizeof(__PRETTY_FUNCTION__) - 36;
      name.str_ = __PRETTY_FUNCTION__ + 34;
    }
    if (name.size_ > 22 && name.str_[0] == '(' && name.str_[1] == 'a' && name.str_[10] == ' ' && name.str_[22] == ':') {
      name.size_ -= 23;
      name.str_ += 23;
    }
    if (name.str_[0] == '(' || name.str_[0] == '-' || (name.str_[0] >= '0' && name.str_[0] <= '9')) {
      name = str_view{};
    }
#elif defined(__GNUC__)
    auto name = str_view{__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 1};
    if constexpr (sizeof(__PRETTY_FUNCTION__) == sizeof(__FUNCTION__)) {
      static_assert(always_false_v<decltype(V)>, "magic_enum::detail::n requires __PRETTY_FUNCTION__.");
      return str_view{};
    } else if (name.str_[name.size_ - 1] == ']') {
      name.size_ -= 55;
      name.str_ += 54;
    } else {
      name.size_ -= 40;
      name.str_ += 37;
    }
    if (name.str_[0] == '(') {
      name = str_view{};
    }
#elif defined(_MSC_VER)
    str_view name;
    if ((__FUNCSIG__[5] == '_' && __FUNCSIG__[35] != '(') || (__FUNCSIG__[5] == 'c' && __FUNCSIG__[41] != '(')) {
      // CLI/C++ workaround (see https://github.com/Neargye/magic_enum/issues/284).
      name.str_ = __FUNCSIG__;
      name.str_ += 35;
      name.size_ = sizeof(__FUNCSIG__) - 52;
    }
#else
    auto name = str_view{};
#endif
    std::size_t p = 0;
    for (std::size_t i = name.size_; i > 0; --i) {
      if (name.str_[i] == ':') {
        p = i + 1;
        break;
      }
    }
    if (p > 0) {
      name.size_ -= p;
      name.str_ += p;
    }
    return name;
  } else {
    return str_view{}; // Unsupported compiler or Invalid customize.
  }
}

#if defined(_MSC_VER) && !defined(__clang__) && _MSC_VER < 1920
#  define MAGIC_ENUM_VS_2017_WORKAROUND 1
#endif

#if defined(MAGIC_ENUM_VS_2017_WORKAROUND)
template <typename E, E V>
constexpr auto MAGIC_ENUM_CALLING_CONVENTION n() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::n requires enum type.");

#  if defined(MAGIC_ENUM_GET_ENUM_NAME_BUILTIN)
  constexpr auto name_ptr = MAGIC_ENUM_GET_ENUM_NAME_BUILTIN(V);
  auto name = name_ptr ? str_view{name_ptr, std::char_traits<char>::length(name_ptr)} : str_view{};
#  else
  // CLI/C++ workaround (see https://github.com/Neargye/magic_enum/issues/284).
  str_view name;
  name.str_ = __FUNCSIG__;
  name.size_ = sizeof(__FUNCSIG__) - 17;
  std::size_t p = 0;
  for (std::size_t i = name.size_; i > 0; --i) {
    if (name.str_[i] == ',' || name.str_[i] == ':') {
      p = i + 1;
      break;
    }
  }
  if (p > 0) {
    name.size_ -= p;
    name.str_ += p;
  }
  if (name.str_[0] == '(' || name.str_[0] == '-' || (name.str_[0] >= '0' && name.str_[0] <= '9')) {
    name = str_view{};
  }
  return name;
#  endif
}
#endif

template <typename E, E V>
constexpr auto enum_name() noexcept {
  [[maybe_unused]] constexpr auto custom = customize::enum_name<E>(V);
  static_assert(std::is_same_v<std::decay_t<decltype(custom)>, customize::customize_t>, "magic_enum::customize requires customize_t type.");
  if constexpr (custom.first == customize::detail::customize_tag::custom_tag) {
    constexpr auto name = custom.second;
    static_assert(!name.empty(), "magic_enum::customize requires not empty string.");
    return static_str<name.size()>{name};
  } else if constexpr (custom.first == customize::detail::customize_tag::invalid_tag) {
    return static_str<0>{};
  } else if constexpr (custom.first == customize::detail::customize_tag::default_tag) {
#if defined(MAGIC_ENUM_VS_2017_WORKAROUND)
    constexpr auto name = n<E, V>();
#else
    constexpr auto name = n<V>();
#endif
    return static_str<name.size_ - prefix_length_or_zero<E>>{name.str_ + prefix_length_or_zero<E>};
  } else {
    static_assert(always_false_v<E>, "magic_enum::customize invalid.");
  }
}

template <typename E, E V>
inline constexpr auto enum_name_v = enum_name<E, V>();

// CWG1766: Values outside the range of the values of an enumeration
// https://reviews.llvm.org/D130058, https://reviews.llvm.org/D131307
#if defined(__clang__) && __clang_major__ >= 16
template <typename E, auto V, typename = void>
inline constexpr bool is_enum_constexpr_static_cast_valid = false;
template <typename E, auto V>
inline constexpr bool is_enum_constexpr_static_cast_valid<E, V, std::void_t<std::integral_constant<E, static_cast<E>(V)>>> = true;
#else
template <typename, auto>
inline constexpr bool is_enum_constexpr_static_cast_valid = true;
#endif

template <typename E, auto V>
constexpr bool is_valid() noexcept {
  if constexpr (is_enum_constexpr_static_cast_valid<E, V>) {
    constexpr E v = static_cast<E>(V);
    [[maybe_unused]] constexpr auto custom = customize::enum_name<E>(v);
    static_assert(std::is_same_v<std::decay_t<decltype(custom)>, customize::customize_t>, "magic_enum::customize requires customize_t type.");
    if constexpr (custom.first == customize::detail::customize_tag::custom_tag) {
      constexpr auto name = custom.second;
      static_assert(!name.empty(), "magic_enum::customize requires not empty string.");
      return name.size() != 0;
    } else if constexpr (custom.first == customize::detail::customize_tag::default_tag) {
#if defined(MAGIC_ENUM_VS_2017_WORKAROUND)
      return n<E, v>().size_ != 0;
#else
      return n<v>().size_ != 0;
#endif
    } else {
      return false;
    }
  } else {
    return false;
  }
}

enum class enum_subtype {
  common,
  flags
};

template <typename E, int O, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr U ualue(std::size_t i) noexcept {
  if constexpr (std::is_same_v<U, bool>) { // bool special case
    static_assert(O == 0, "magic_enum::detail::ualue requires valid offset.");

    return static_cast<U>(i);
  } else if constexpr (S == enum_subtype::flags) {
    return static_cast<U>(U{1} << static_cast<U>(static_cast<int>(i) + O));
  } else {
    return static_cast<U>(static_cast<int>(i) + O);
  }
}

template <typename E, int O, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr E value(std::size_t i) noexcept {
  return static_cast<E>(ualue<E, O, S>(i));
}

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr int reflected_min() noexcept {
  if constexpr (S == enum_subtype::flags) {
    return 0;
  } else {
    constexpr auto lhs = range_min<E>::value;
    constexpr auto rhs = (std::numeric_limits<U>::min)();

    if constexpr (cmp_less(rhs, lhs)) {
      return lhs;
    } else {
      return rhs;
    }
  }
}

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr int reflected_max() noexcept {
  if constexpr (S == enum_subtype::flags) {
    return std::numeric_limits<U>::digits - 1;
  } else {
    constexpr auto lhs = range_max<E>::value;
    constexpr auto rhs = (std::numeric_limits<U>::max)();

    if constexpr (cmp_less(lhs, rhs)) {
      return lhs;
    } else {
      return rhs;
    }
  }
}

#define MAGIC_ENUM_FOR_EACH_256(T)                                                                                                                                                                 \
  T(  0)T(  1)T(  2)T(  3)T(  4)T(  5)T(  6)T(  7)T(  8)T(  9)T( 10)T( 11)T( 12)T( 13)T( 14)T( 15)T( 16)T( 17)T( 18)T( 19)T( 20)T( 21)T( 22)T( 23)T( 24)T( 25)T( 26)T( 27)T( 28)T( 29)T( 30)T( 31) \
  T( 32)T( 33)T( 34)T( 35)T( 36)T( 37)T( 38)T( 39)T( 40)T( 41)T( 42)T( 43)T( 44)T( 45)T( 46)T( 47)T( 48)T( 49)T( 50)T( 51)T( 52)T( 53)T( 54)T( 55)T( 56)T( 57)T( 58)T( 59)T( 60)T( 61)T( 62)T( 63) \
  T( 64)T( 65)T( 66)T( 67)T( 68)T( 69)T( 70)T( 71)T( 72)T( 73)T( 74)T( 75)T( 76)T( 77)T( 78)T( 79)T( 80)T( 81)T( 82)T( 83)T( 84)T( 85)T( 86)T( 87)T( 88)T( 89)T( 90)T( 91)T( 92)T( 93)T( 94)T( 95) \
  T( 96)T( 97)T( 98)T( 99)T(100)T(101)T(102)T(103)T(104)T(105)T(106)T(107)T(108)T(109)T(110)T(111)T(112)T(113)T(114)T(115)T(116)T(117)T(118)T(119)T(120)T(121)T(122)T(123)T(124)T(125)T(126)T(127) \
  T(128)T(129)T(130)T(131)T(132)T(133)T(134)T(135)T(136)T(137)T(138)T(139)T(140)T(141)T(142)T(143)T(144)T(145)T(146)T(147)T(148)T(149)T(150)T(151)T(152)T(153)T(154)T(155)T(156)T(157)T(158)T(159) \
  T(160)T(161)T(162)T(163)T(164)T(165)T(166)T(167)T(168)T(169)T(170)T(171)T(172)T(173)T(174)T(175)T(176)T(177)T(178)T(179)T(180)T(181)T(182)T(183)T(184)T(185)T(186)T(187)T(188)T(189)T(190)T(191) \
  T(192)T(193)T(194)T(195)T(196)T(197)T(198)T(199)T(200)T(201)T(202)T(203)T(204)T(205)T(206)T(207)T(208)T(209)T(210)T(211)T(212)T(213)T(214)T(215)T(216)T(217)T(218)T(219)T(220)T(221)T(222)T(223) \
  T(224)T(225)T(226)T(227)T(228)T(229)T(230)T(231)T(232)T(233)T(234)T(235)T(236)T(237)T(238)T(239)T(240)T(241)T(242)T(243)T(244)T(245)T(246)T(247)T(248)T(249)T(250)T(251)T(252)T(253)T(254)T(255)

template <typename E, enum_subtype S, std::size_t Size, int Min, std::size_t I>
constexpr void valid_count(bool* valid, std::size_t& count) noexcept {
#define MAGIC_ENUM_V(O)                                     \
  if constexpr ((I + O) < Size) {                           \
    if constexpr (is_valid<E, ualue<E, Min, S>(I + O)>()) { \
      valid[I + O] = true;                                  \
      ++count;                                              \
    }                                                       \
  }

  MAGIC_ENUM_FOR_EACH_256(MAGIC_ENUM_V)

  if constexpr ((I + 256) < Size) {
    valid_count<E, S, Size, Min, I + 256>(valid, count);
  }
#undef MAGIC_ENUM_V
}

template <std::size_t N>
struct valid_count_t {
  std::size_t count = 0;
  bool valid[N] = {};
};

template <typename E, enum_subtype S, std::size_t Size, int Min>
constexpr auto valid_count() noexcept {
  valid_count_t<Size> vc;
  valid_count<E, S, Size, Min, 0>(vc.valid, vc.count);
  return vc;
}

template <typename E, enum_subtype S, std::size_t Size, int Min>
constexpr auto values() noexcept {
  constexpr auto vc = valid_count<E, S, Size, Min>();

  if constexpr (vc.count > 0) {
#if defined(MAGIC_ENUM_ARRAY_CONSTEXPR)
    std::array<E, vc.count> values = {};
#else
    E values[vc.count] = {};
#endif
    for (std::size_t i = 0, v = 0; v < vc.count; ++i) {
      if (vc.valid[i]) {
        values[v++] = value<E, Min, S>(i);
      }
    }
#if defined(MAGIC_ENUM_ARRAY_CONSTEXPR)
    return values;
#else
    return to_array(values, std::make_index_sequence<vc.count>{});
#endif
  } else {
    return std::array<E, 0>{};
  }
}

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr auto values() noexcept {
  constexpr auto min = reflected_min<E, S>();
  constexpr auto max = reflected_max<E, S>();
  constexpr auto range_size = max - min + 1;
  static_assert(range_size > 0, "magic_enum::enum_range requires valid size.");

  return values<E, S, range_size, min>();
}

template <typename E, typename U = std::underlying_type_t<E>>
constexpr enum_subtype subtype(std::true_type) noexcept {
  if constexpr (std::is_same_v<U, bool>) { // bool special case
    return enum_subtype::common;
  } else if constexpr (has_is_flags<E>::value) {
    return customize::enum_range<E>::is_flags ? enum_subtype::flags : enum_subtype::common;
  } else {
    return enum_subtype::common;
  }
}

template <typename T>
constexpr enum_subtype subtype(std::false_type) noexcept {
  // For non-enum type return default common subtype.
  return enum_subtype::common;
}

template <typename E, typename D = std::decay_t<E>>
inline constexpr auto subtype_v = subtype<D>(std::is_enum<D>{});

template <typename E, enum_subtype S>
inline constexpr auto values_v = values<E, S>();

template <typename E, enum_subtype S, typename D = std::decay_t<E>>
using values_t = decltype((values_v<D, S>));

template <typename E, enum_subtype S>
inline constexpr auto count_v = values_v<E, S>.size();

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
inline constexpr auto min_v = (count_v<E, S> > 0) ? static_cast<U>(values_v<E, S>.front()) : U{0};

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
inline constexpr auto max_v = (count_v<E, S> > 0) ? static_cast<U>(values_v<E, S>.back()) : U{0};

template <typename E, enum_subtype S, std::size_t... I>
constexpr auto names(std::index_sequence<I...>) noexcept {
  constexpr auto names = std::array<string_view, sizeof...(I)>{{enum_name_v<E, values_v<E, S>[I]>.str()...}};
  return names;
}

template <typename E, enum_subtype S>
inline constexpr auto names_v = names<E, S>(std::make_index_sequence<count_v<E, S>>{});

template <typename E, enum_subtype S, typename D = std::decay_t<E>>
using names_t = decltype((names_v<D, S>));

template <typename E, enum_subtype S, std::size_t... I>
constexpr auto entries(std::index_sequence<I...>) noexcept {
  constexpr auto entries = std::array<std::pair<E, string_view>, sizeof...(I)>{{{values_v<E, S>[I], enum_name_v<E, values_v<E, S>[I]>.str()}...}};
  return entries;
}

template <typename E, enum_subtype S>
inline constexpr auto entries_v = entries<E, S>(std::make_index_sequence<count_v<E, S>>{});

template <typename E, enum_subtype S, typename D = std::decay_t<E>>
using entries_t = decltype((entries_v<D, S>));

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr bool is_sparse() noexcept {
  if constexpr (count_v<E, S> == 0) {
    return false;
  } else if constexpr (std::is_same_v<U, bool>) { // bool special case
    return false;
  } else {
    constexpr auto max = (S == enum_subtype::flags) ? log2(max_v<E, S>) : max_v<E, S>;
    constexpr auto min = (S == enum_subtype::flags) ? log2(min_v<E, S>) : min_v<E, S>;
    constexpr auto range_size = max - min + 1;

    return range_size != count_v<E, S>;
  }
}

template <typename E, enum_subtype S = subtype_v<E>>
inline constexpr bool is_sparse_v = is_sparse<E, S>();

template <typename E, enum_subtype S>
struct is_reflected
#if defined(MAGIC_ENUM_NO_CHECK_REFLECTED_ENUM)
  : std::true_type {};
#else
  : std::bool_constant<std::is_enum_v<E> && (count_v<E, S> != 0)> {};
#endif

template <typename E, enum_subtype S>
inline constexpr bool is_reflected_v = is_reflected<std::decay_t<E>, S>{};

template <bool, typename R>
struct enable_if_enum {};

template <typename R>
struct enable_if_enum<true, R> {
  using type = R;
  static_assert(supported<R>::value, "magic_enum unsupported compiler (https://github.com/Neargye/magic_enum#compiler-compatibility).");
};

template <typename T, typename R, typename BinaryPredicate = std::equal_to<>, typename D = std::decay_t<T>>
using enable_if_t = typename enable_if_enum<std::is_enum_v<D> && std::is_invocable_r_v<bool, BinaryPredicate, char_type, char_type>, R>::type;

template <typename T, std::enable_if_t<std::is_enum_v<std::decay_t<T>>, int> = 0>
using enum_concept = T;

template <typename T, bool = std::is_enum_v<T>>
struct is_scoped_enum : std::false_type {};

template <typename T>
struct is_scoped_enum<T, true> : std::bool_constant<!std::is_convertible_v<T, std::underlying_type_t<T>>> {};

template <typename T, bool = std::is_enum_v<T>>
struct is_unscoped_enum : std::false_type {};

template <typename T>
struct is_unscoped_enum<T, true> : std::bool_constant<std::is_convertible_v<T, std::underlying_type_t<T>>> {};

template <typename T, bool = std::is_enum_v<std::decay_t<T>>>
struct underlying_type {};

template <typename T>
struct underlying_type<T, true> : std::underlying_type<std::decay_t<T>> {};

#if defined(MAGIC_ENUM_ENABLE_HASH) || defined(MAGIC_ENUM_ENABLE_HASH_SWITCH)

template <typename Value, typename = void>
struct constexpr_hash_t;

template <typename Value>
struct constexpr_hash_t<Value, std::enable_if_t<is_enum_v<Value>>> {
  constexpr auto operator()(Value value) const noexcept {
    using U = typename underlying_type<Value>::type;
    if constexpr (std::is_same_v<U, bool>) { // bool special case
      return static_cast<std::size_t>(value);
    } else {
      return static_cast<U>(value);
    }
  }
  using secondary_hash = constexpr_hash_t;
};

template <typename Value>
struct constexpr_hash_t<Value, std::enable_if_t<std::is_same_v<Value, string_view>>> {
  static constexpr std::uint32_t crc_table[256] {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
    0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
    0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL, 0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
    0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
    0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
    0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
    0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
    0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L, 0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
    0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
    0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
    0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
    0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
    0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L, 0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
    0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
    0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
    0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
    0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
    0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L, 0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
    0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
    0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
    0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
    0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
    0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L, 0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
    0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
    0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
    0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
    0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
    0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL, 0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
    0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
    0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
    0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
    0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL
  };
  constexpr std::uint32_t operator()(string_view value) const noexcept {
    auto crc = static_cast<std::uint32_t>(0xffffffffL);
    for (const auto c : value) {
      crc = (crc >> 8) ^ crc_table[(crc ^ static_cast<std::uint32_t>(c)) & 0xff];
    }
    return crc ^ 0xffffffffL;
  }

  struct secondary_hash {
    constexpr std::uint32_t operator()(string_view value) const noexcept {
      auto acc = static_cast<std::uint64_t>(2166136261ULL);
      for (const auto c : value) {
        acc = ((acc ^ static_cast<std::uint64_t>(c)) * static_cast<std::uint64_t>(16777619ULL)) & (std::numeric_limits<std::uint32_t>::max)();
      }
      return static_cast<std::uint32_t>(acc);
    }
  };
};

template <typename Hash>
inline constexpr Hash hash_v{};

template <auto* GlobValues, typename Hash>
constexpr auto calculate_cases(std::size_t Page) noexcept {
  constexpr std::array values = *GlobValues;
  constexpr std::size_t size = values.size();

  using switch_t = std::invoke_result_t<Hash, typename decltype(values)::value_type>;
  static_assert(std::is_integral_v<switch_t> && !std::is_same_v<switch_t, bool>);
  const std::size_t values_to = (std::min)(static_cast<std::size_t>(256), size - Page);

  std::array<switch_t, 256> result{};
  auto fill = result.begin();
  {
    auto first = values.begin() + static_cast<std::ptrdiff_t>(Page);
    auto last = values.begin() + static_cast<std::ptrdiff_t>(Page + values_to);
    while (first != last) {
      *fill++ = hash_v<Hash>(*first++);
    }
  }

  // dead cases, try to avoid case collisions
  for (switch_t last_value = result[values_to - 1]; fill != result.end() && last_value != (std::numeric_limits<switch_t>::max)(); *fill++ = ++last_value) {
  }

  {
    auto it = result.begin();
    auto last_value = (std::numeric_limits<switch_t>::min)();
    for (; fill != result.end(); *fill++ = last_value++) {
      while (last_value == *it) {
        ++last_value, ++it;
      }
    }
  }

  return result;
}

template <typename R, typename F, typename... Args>
constexpr R invoke_r(F&& f, Args&&... args) noexcept(std::is_nothrow_invocable_r_v<R, F, Args...>) {
  if constexpr (std::is_void_v<R>) {
    std::forward<F>(f)(std::forward<Args>(args)...);
  } else {
    return static_cast<R>(std::forward<F>(f)(std::forward<Args>(args)...));
  }
}

enum class case_call_t {
  index,
  value
};

template <typename T = void>
inline constexpr auto default_result_type_lambda = []() noexcept(std::is_nothrow_default_constructible_v<T>) { return T{}; };

template <>
inline constexpr auto default_result_type_lambda<void> = []() noexcept {};

template <auto* Arr, typename Hash>
constexpr bool has_duplicate() noexcept {
  using value_t = std::decay_t<decltype((*Arr)[0])>;
  using hash_value_t = std::invoke_result_t<Hash, value_t>;
  std::array<hash_value_t, Arr->size()> hashes{};
  std::size_t size = 0;
  for (auto elem : *Arr) {
    hashes[size] = hash_v<Hash>(elem);
    for (auto i = size++; i > 0; --i) {
      if (hashes[i] < hashes[i - 1]) {
        auto tmp = hashes[i];
        hashes[i] = hashes[i - 1];
        hashes[i - 1] = tmp;
      } else if (hashes[i] == hashes[i - 1]) {
        return false;
      } else {
        break;
      }
    }
  }
  return true;
}

#define MAGIC_ENUM_CASE(val)                                                                                                  \
  case cases[val]:                                                                                                            \
    if constexpr ((val) + Page < size) {                                                                                      \
      if (!pred(values[val + Page], searched)) {                                                                              \
        break;                                                                                                                \
      }                                                                                                                       \
      if constexpr (CallValue == case_call_t::index) {                                                                        \
        if constexpr (std::is_invocable_r_v<result_t, Lambda, std::integral_constant<std::size_t, val + Page>>) {             \
          return detail::invoke_r<result_t>(std::forward<Lambda>(lambda), std::integral_constant<std::size_t, val + Page>{}); \
        } else if constexpr (std::is_invocable_v<Lambda, std::integral_constant<std::size_t, val + Page>>) {                  \
          MAGIC_ENUM_ASSERT(false && "magic_enum::detail::constexpr_switch wrong result type.");                                         \
        }                                                                                                                     \
      } else if constexpr (CallValue == case_call_t::value) {                                                                 \
        if constexpr (std::is_invocable_r_v<result_t, Lambda, enum_constant<values[val + Page]>>) {                           \
          return detail::invoke_r<result_t>(std::forward<Lambda>(lambda), enum_constant<values[val + Page]>{});               \
        } else if constexpr (std::is_invocable_r_v<result_t, Lambda, enum_constant<values[val + Page]>>) {                    \
          MAGIC_ENUM_ASSERT(false && "magic_enum::detail::constexpr_switch wrong result type.");                                         \
        }                                                                                                                     \
      }                                                                                                                       \
      break;                                                                                                                  \
    } else [[fallthrough]];

template <auto* GlobValues,
          case_call_t CallValue,
          std::size_t Page = 0,
          typename Hash = constexpr_hash_t<typename std::decay_t<decltype(*GlobValues)>::value_type>,
          typename BinaryPredicate = std::equal_to<>,
          typename Lambda,
          typename ResultGetterType>
constexpr decltype(auto) constexpr_switch(
    Lambda&& lambda,
    typename std::decay_t<decltype(*GlobValues)>::value_type searched,
    ResultGetterType&& def,
    BinaryPredicate&& pred = {}) {
  using result_t = std::invoke_result_t<ResultGetterType>;
  using hash_t = std::conditional_t<has_duplicate<GlobValues, Hash>(), Hash, typename Hash::secondary_hash>;
  static_assert(has_duplicate<GlobValues, hash_t>(), "magic_enum::detail::constexpr_switch duplicated hash found, please report it: https://github.com/Neargye/magic_enum/issues.");
  constexpr std::array values = *GlobValues;
  constexpr std::size_t size = values.size();
  constexpr std::array cases = calculate_cases<GlobValues, hash_t>(Page);

  switch (hash_v<hash_t>(searched)) {
    MAGIC_ENUM_FOR_EACH_256(MAGIC_ENUM_CASE)
    default:
      if constexpr (size > 256 + Page) {
        return constexpr_switch<GlobValues, CallValue, Page + 256, Hash>(std::forward<Lambda>(lambda), searched, std::forward<ResultGetterType>(def));
      }
      break;
  }
  return def();
}

#undef MAGIC_ENUM_CASE

#endif

} // namespace magic_enum::detail

// Checks is magic_enum supported compiler.
inline constexpr bool is_magic_enum_supported = detail::supported<void>::value;

template <typename T>
using Enum = detail::enum_concept<T>;

// Returns true if T is an Unscoped enumeration type.
// Provides the member constant value which is equal to true, if T is an [Unscoped enumeration](https://en.cppreference.com/w/cpp/language/enum#Unscoped_enumeration) type. Otherwise, value is equal to false.
template <typename T>
struct is_unscoped_enum : detail::is_unscoped_enum<T> {};

template <typename T>
inline constexpr bool is_unscoped_enum_v = is_unscoped_enum<T>::value;

// Returns true if T is a Scoped enumeration type.
// Provides the member constant value which is equal to true, if T is an [Scoped enumeration](https://en.cppreference.com/w/cpp/language/enum#Scoped_enumerations) type. Otherwise, value is equal to false.
template <typename T>
struct is_scoped_enum : detail::is_scoped_enum<T> {};

template <typename T>
inline constexpr bool is_scoped_enum_v = is_scoped_enum<T>::value;

// If T is a complete enumeration type, provides a member typedef type that names the underlying type of T.
// Otherwise, if T is not an enumeration type, there is no member type. Otherwise (T is an incomplete enumeration type), the program is ill-formed.
template <typename T>
struct underlying_type : detail::underlying_type<T> {};

template <typename T>
using underlying_type_t = typename underlying_type<T>::type;

template <auto V>
using enum_constant = detail::enum_constant<V>;

// Returns type name of enum.
template <typename E>
[[nodiscard]] constexpr auto enum_type_name() noexcept -> detail::enable_if_t<E, string_view> {
  constexpr string_view name = detail::type_name_v<std::decay_t<E>>.str();
  static_assert(!name.empty(), "magic_enum::enum_type_name enum type does not have a name.");

  return name;
}

// Returns number of enum values.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_count() noexcept -> detail::enable_if_t<E, std::size_t> {
  return detail::count_v<std::decay_t<E>, S>;
}

// Returns enum value at specified index.
// No bounds checking is performed: the behavior is undefined if index >= number of enum values.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_value(std::size_t index) noexcept -> detail::enable_if_t<E, std::decay_t<E>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  if constexpr (detail::is_sparse_v<D, S>) {
    return MAGIC_ENUM_ASSERT(index < detail::count_v<D, S>), detail::values_v<D, S>[index];
  } else {
    constexpr auto min = (S == detail::enum_subtype::flags) ? detail::log2(detail::min_v<D, S>) : detail::min_v<D, S>;

    return MAGIC_ENUM_ASSERT(index < detail::count_v<D, S>), detail::value<D, min, S>(index);
  }
}

// Returns enum value at specified index.
template <typename E, std::size_t I, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_value() noexcept -> detail::enable_if_t<E, std::decay_t<E>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");
  static_assert(I < detail::count_v<D, S>, "magic_enum::enum_value out of range.");

  return enum_value<D, S>(I);
}

// Returns std::array with enum values, sorted by enum value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_values() noexcept -> detail::enable_if_t<E, detail::values_t<E, S>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  return detail::values_v<D, S>;
}

// Returns integer value from enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_integer(E value) noexcept -> underlying_type_t<E> {
  return static_cast<underlying_type_t<E>>(value);
}

// Returns underlying value from enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_underlying(E value) noexcept -> underlying_type_t<E> {
  return static_cast<underlying_type_t<E>>(value);
}

// Returns index in enum values from enum value.
// Returns optional with index.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_index(E value) noexcept -> detail::enable_if_t<E, optional<std::size_t>> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  if constexpr (detail::is_sparse_v<D, S> || (S == detail::enum_subtype::flags)) {
#if defined(MAGIC_ENUM_ENABLE_HASH)
    return detail::constexpr_switch<&detail::values_v<D, S>, detail::case_call_t::index>(
        [](std::size_t i) { return optional<std::size_t>{i}; },
        value,
        detail::default_result_type_lambda<optional<std::size_t>>);
#else
    for (std::size_t i = 0; i < detail::count_v<D, S>; ++i) {
      if (enum_value<D, S>(i) == value) {
        return i;
      }
    }
    return {}; // Invalid value or out of range.
#endif
  } else {
    const auto v = static_cast<U>(value);
    if (v >= detail::min_v<D, S> && v <= detail::max_v<D, S>) {
      return static_cast<std::size_t>(v - detail::min_v<D, S>);
    }
    return {}; // Invalid value or out of range.
  }
}

// Returns index in enum values from enum value.
// Returns optional with index.
template <detail::enum_subtype S, typename E>
[[nodiscard]] constexpr auto enum_index(E value) noexcept -> detail::enable_if_t<E, optional<std::size_t>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  return enum_index<D, S>(value);
}

// Returns index in enum values from static storage enum variable.
template <auto V, detail::enum_subtype S = detail::subtype_v<std::decay_t<decltype(V)>>>
[[nodiscard]] constexpr auto enum_index() noexcept -> detail::enable_if_t<decltype(V), std::size_t> {\
  using D = std::decay_t<decltype(V)>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");
  constexpr auto index = enum_index<D, S>(V);
  static_assert(index, "magic_enum::enum_index enum value does not have a index.");

  return *index;
}

// Returns name from static storage enum variable.
// This version is much lighter on the compile times and is not restricted to the enum_range limitation.
template <auto V>
[[nodiscard]] constexpr auto enum_name() noexcept -> detail::enable_if_t<decltype(V), string_view> {
  constexpr string_view name = detail::enum_name_v<std::decay_t<decltype(V)>, V>.str();
  static_assert(!name.empty(), "magic_enum::enum_name enum value does not have a name.");

  return name;
}

// Returns name from enum value.
// If enum value does not have name or value out of range, returns empty string.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_name(E value) noexcept -> detail::enable_if_t<E, string_view> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  if (const auto i = enum_index<D, S>(value)) {
    return detail::names_v<D, S>[*i];
  }
  return detail::static_str<0>{}.str();
}

// Returns name from enum value.
// If enum value does not have name or value out of range, returns empty string.
template <detail::enum_subtype S, typename E>
[[nodiscard]] constexpr auto enum_name(E value) -> detail::enable_if_t<E, string_view> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  return enum_name<D, S>(value);
}

// Returns std::array with names, sorted by enum value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_names() noexcept -> detail::enable_if_t<E, detail::names_t<E, S>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  return detail::names_v<D, S>;
}

// Returns std::array with pairs (value, name), sorted by enum value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_entries() noexcept -> detail::enable_if_t<E, detail::entries_t<E, S>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  return detail::entries_v<D, S>;
}

// Allows you to write magic_enum::enum_cast<foo>("bar", magic_enum::case_insensitive);
inline constexpr auto case_insensitive = detail::case_insensitive<>{};

// Returns enum value from integer value.
// Returns optional with enum value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_cast(underlying_type_t<E> value) noexcept -> detail::enable_if_t<E, optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  if constexpr (detail::is_sparse_v<D, S> || (S == detail::enum_subtype::flags)) {
#if defined(MAGIC_ENUM_ENABLE_HASH)
    return detail::constexpr_switch<&detail::values_v<D, S>, detail::case_call_t::value>(
        [](D v) { return optional<D>{v}; },
        static_cast<D>(value),
        detail::default_result_type_lambda<optional<D>>);
#else
    for (std::size_t i = 0; i < detail::count_v<D, S>; ++i) {
      if (value == static_cast<underlying_type_t<D>>(enum_value<D, S>(i))) {
        return static_cast<D>(value);
      }
    }
    return {}; // Invalid value or out of range.
#endif
  } else {
    if (value >= detail::min_v<D, S> && value <= detail::max_v<D, S>) {
      return static_cast<D>(value);
    }
    return {}; // Invalid value or out of range.
  }
}

// Returns enum value from name.
// Returns optional with enum value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>, typename BinaryPredicate = std::equal_to<>>
[[nodiscard]] constexpr auto enum_cast(string_view value, [[maybe_unused]] BinaryPredicate p = {}) noexcept(detail::is_nothrow_invocable_v<BinaryPredicate>) -> detail::enable_if_t<E, optional<std::decay_t<E>>, BinaryPredicate> {
  using D = std::decay_t<E>;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

#if defined(MAGIC_ENUM_ENABLE_HASH)
  if constexpr (detail::is_default_predicate_v<BinaryPredicate>) {
    return detail::constexpr_switch<&detail::names_v<D, S>, detail::case_call_t::index>(
        [](std::size_t i) { return optional<D>{detail::values_v<D, S>[i]}; },
        value,
        detail::default_result_type_lambda<optional<D>>,
        [&p](string_view lhs, string_view rhs) { return detail::cmp_equal(lhs, rhs, p); });
  }
#endif
  for (std::size_t i = 0; i < detail::count_v<D, S>; ++i) {
    if (detail::cmp_equal(value, detail::names_v<D, S>[i], p)) {
      return enum_value<D, S>(i);
    }
  }
  return {}; // Invalid value or out of range.
}

// Returns true if enum contains value with such value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_contains(E value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;

  return static_cast<bool>(enum_cast<D, S>(static_cast<U>(value)));
}

// Returns true if enum contains value with such value.
template <detail::enum_subtype S, typename E>
[[nodiscard]] constexpr auto enum_contains(E value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;

  return static_cast<bool>(enum_cast<D, S>(static_cast<U>(value)));
}

// Returns true if enum contains value with such integer value.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_contains(underlying_type_t<E> value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;

  return static_cast<bool>(enum_cast<D, S>(value));
}

// Returns true if enum contains enumerator with such name.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>, typename BinaryPredicate = std::equal_to<>>
[[nodiscard]] constexpr auto enum_contains(string_view value, BinaryPredicate p = {}) noexcept(detail::is_nothrow_invocable_v<BinaryPredicate>) -> detail::enable_if_t<E, bool, BinaryPredicate> {
  using D = std::decay_t<E>;

  return static_cast<bool>(enum_cast<D, S>(value, std::move(p)));
}

// Returns true if the enum integer value is in the range of values that can be reflected.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_reflected(underlying_type_t<E> value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;

  if constexpr (detail::is_reflected_v<D, S>) {
    constexpr auto min = detail::reflected_min<E, S>();
    constexpr auto max = detail::reflected_max<E, S>();
    return value >= min && value <= max;
  } else {
    return false;
  }
}

// Returns true if the enum value is in the range of values that can be reflected.
template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_reflected(E value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;

  return enum_reflected<D, S>(static_cast<underlying_type_t<D>>(value));
}

// Returns true if the enum value is in the range of values that can be reflected.
template <detail::enum_subtype S, typename E>
[[nodiscard]] constexpr auto enum_reflected(E value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;

  return enum_reflected<D, S>(value);
}

template <bool AsFlags = true>
inline constexpr auto as_flags = AsFlags ? detail::enum_subtype::flags : detail::enum_subtype::common;

template <bool AsCommon = true>
inline constexpr auto as_common = AsCommon ? detail::enum_subtype::common : detail::enum_subtype::flags;

namespace bitwise_operators {

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E operator~(E rhs) noexcept {
  return static_cast<E>(~static_cast<underlying_type_t<E>>(rhs));
}

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E operator|(E lhs, E rhs) noexcept {
  return static_cast<E>(static_cast<underlying_type_t<E>>(lhs) | static_cast<underlying_type_t<E>>(rhs));
}

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E operator&(E lhs, E rhs) noexcept {
  return static_cast<E>(static_cast<underlying_type_t<E>>(lhs) & static_cast<underlying_type_t<E>>(rhs));
}

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E operator^(E lhs, E rhs) noexcept {
  return static_cast<E>(static_cast<underlying_type_t<E>>(lhs) ^ static_cast<underlying_type_t<E>>(rhs));
}

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E& operator|=(E& lhs, E rhs) noexcept {
  return lhs = (lhs | rhs);
}

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E& operator&=(E& lhs, E rhs) noexcept {
  return lhs = (lhs & rhs);
}

template <typename E, detail::enable_if_t<E, int> = 0>
constexpr E& operator^=(E& lhs, E rhs) noexcept {
  return lhs = (lhs ^ rhs);
}

} // namespace magic_enum::bitwise_operators

} // namespace magic_enum

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif

#undef MAGIC_ENUM_GET_ENUM_NAME_BUILTIN
#undef MAGIC_ENUM_GET_TYPE_NAME_BUILTIN
#undef MAGIC_ENUM_VS_2017_WORKAROUND
#undef MAGIC_ENUM_ARRAY_CONSTEXPR
#undef MAGIC_ENUM_FOR_EACH_256

#endif // NEARGYE_MAGIC_ENUM_HPP
// Embedded upstream header: magic_enum_containers.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
// Copyright (c) 2022 - 2023 Bela Schaum <schaumb@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_CONTAINERS_HPP
#define NEARGYE_MAGIC_ENUM_CONTAINERS_HPP

// magic_enum.hpp is already embedded above.

#if !defined(MAGIC_ENUM_NO_EXCEPTION) && (defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND))
#ifndef MAGIC_ENUM_USE_STD_MODULE
#  include <stdexcept>
#endif
#  define MAGIC_ENUM_THROW(...) throw (__VA_ARGS__)
#else
#ifndef MAGIC_ENUM_USE_STD_MODULE
#  include <cstdlib>
#endif
#  define MAGIC_ENUM_THROW(...) std::abort()
#endif

namespace magic_enum::containers {

namespace detail {

template <typename T, typename = void>
inline constexpr bool is_transparent_v{};

template <typename T>
inline constexpr bool is_transparent_v<T, std::void_t<typename T::is_transparent>>{true};

template <typename Eq = std::equal_to<>, typename T1, typename T2>
constexpr bool equal(T1&& t1, T2&& t2, Eq&& eq = {}) {
  auto first1 = t1.begin();
  auto last1 = t1.end();
  auto first2 = t2.begin();
  auto last2 = t2.end();

  for (; first1 != last1; ++first1, ++first2) {
    if (first2 == last2 || !eq(*first1, *first2)) {
      return false;
    }
  }
  return first2 == last2;
}

template <typename Cmp = std::less<>, typename T1, typename T2>
constexpr bool lexicographical_compare(T1&& t1, T2&& t2, Cmp&& cmp = {}) noexcept {
  auto first1 = t1.begin();
  auto last1 = t1.end();
  auto first2 = t2.begin();
  auto last2 = t2.end();

  // copied from std::lexicographical_compare
  for (; (first1 != last1) && (first2 != last2); ++first1, (void)++first2) {
    if (cmp(*first1, *first2)) {
      return true;
    }
    if (cmp(*first2, *first1)) {
      return false;
    }
  }
  return (first1 == last1) && (first2 != last2);
}

template <typename T>
constexpr std::size_t popcount(T x) noexcept {
  std::size_t c = 0;
  while (x > 0) {
    c += x & 1;
    x >>= 1;
  }
  return c;
}

template <typename Cmp = std::less<>, typename ForwardIt, typename E>
constexpr ForwardIt lower_bound(ForwardIt first, ForwardIt last, E&& e, Cmp&& comp = {}) {
  auto count = std::distance(first, last);
  for (auto it = first; count > 0;) {
    auto step = count / 2;
    std::advance(it, step);
    if (comp(*it, e)) {
      first = ++it;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return first;
}

template <typename Cmp = std::less<>, typename BidirIt, typename E>
constexpr auto equal_range(BidirIt begin, BidirIt end, E&& e, Cmp&& comp = {}) {
  const auto first = lower_bound(begin, end, e, comp);
  return std::pair{first, lower_bound(std::make_reverse_iterator(end), std::make_reverse_iterator(first), e, [&comp](auto&& lhs, auto&& rhs) { return comp(rhs, lhs); }).base()};
}

template <typename E = void, typename Cmp = std::less<E>, typename = void>
class indexing {
  [[nodiscard]] static constexpr auto get_indices() noexcept {
    // reverse result index mapping
    std::array<std::size_t, enum_count<E>()> rev_res{};

    // std::iota
    for (std::size_t i = 0; i < enum_count<E>(); ++i) {
      rev_res[i] = i;
    }

    constexpr auto orig_values = enum_values<E>();
    constexpr Cmp cmp{};

    // ~std::sort
    for (std::size_t i = 0; i < enum_count<E>(); ++i) {
      for (std::size_t j = i + 1; j < enum_count<E>(); ++j) {
        if (cmp(orig_values[rev_res[j]], orig_values[rev_res[i]])) {
          auto tmp = rev_res[i];
          rev_res[i] = rev_res[j];
          rev_res[j] = tmp;
        }
      }
    }

    std::array<E, enum_count<E>()> sorted_values{};
    // reverse the sorted indices
    std::array<std::size_t, enum_count<E>()> res{};
    for (std::size_t i = 0; i < enum_count<E>(); ++i) {
      res[rev_res[i]] = i;
      sorted_values[i] = orig_values[rev_res[i]];
    }

    return std::pair{sorted_values, res};
  }

  static constexpr auto indices = get_indices();

 public:
  [[nodiscard]] static constexpr const E* begin() noexcept { return indices.first.data(); }

  [[nodiscard]] static constexpr const E* end() noexcept { return indices.first.data() + indices.first.size(); }

  [[nodiscard]] static constexpr const E* it(std::size_t i) noexcept { return indices.first.data() + i; }

  [[nodiscard]] static constexpr optional<std::size_t> at(E val) noexcept {
    if (auto i = enum_index(val)) {
      return indices.second[*i];
    }
    return {};
  }
};

template <typename E, typename Cmp>
class indexing<E, Cmp, std::enable_if_t<std::is_enum_v<std::decay_t<E>> && (std::is_same_v<Cmp, std::less<E>> || std::is_same_v<Cmp, std::less<>>)>> {
   static constexpr auto& values = enum_values<E>();

 public:
   [[nodiscard]] static constexpr const E* begin() noexcept { return values.data(); }

   [[nodiscard]] static constexpr const E* end() noexcept { return values.data() + values.size(); }

  [[nodiscard]] static constexpr const E* it(std::size_t i) noexcept { return values.data() + i; }

  [[nodiscard]] static constexpr optional<std::size_t> at(E val) noexcept { return enum_index(val); }
};

template <typename Cmp>
struct indexing<void, Cmp, void> {
  using is_transparent = std::true_type;

  template <typename E>
  [[nodiscard]] static constexpr optional<std::size_t> at(E val) noexcept {
    return indexing<E, Cmp>::at(val);
  }
};

template <typename E = void, typename Cmp = std::less<>, typename = void>
struct name_sort_impl {
  [[nodiscard]] constexpr bool operator()(E e1, E e2) const noexcept { return Cmp{}(enum_name(e1), enum_name(e2)); }
};

template <typename Cmp>
struct name_sort_impl<void, Cmp> {
  using is_transparent = std::true_type;

  template <typename C = Cmp, typename = void>
  struct FullCmp : C {};

  template <typename C>
  struct FullCmp<C, std::enable_if_t<!std::is_invocable_v<C, string_view, string_view> && std::is_invocable_v<C, char_type, char_type>>> {
    [[nodiscard]] constexpr bool operator()(string_view s1, string_view s2) const noexcept { return lexicographical_compare<C>(s1, s2); }
  };

  template <typename E1, typename E2>
  [[nodiscard]] constexpr std::enable_if_t<
      // at least one of need to be an enum type
      (std::is_enum_v<std::decay_t<E1>> || std::is_enum_v<std::decay_t<E2>>) &&
      // if both is enum, only accept if the same enum
      (!std::is_enum_v<std::decay_t<E1>> || !std::is_enum_v<std::decay_t<E2>> || std::is_same_v<E1, E2>) &&
      // is invocable with comparator
      (std::is_invocable_r_v<bool, FullCmp<>, std::conditional_t<std::is_enum_v<std::decay_t<E1>>, string_view, E1>, std::conditional_t<std::is_enum_v<std::decay_t<E2>>, string_view, E2>>),
      bool>
  operator()(E1 e1, E2 e2) const noexcept {
    using D1 = std::decay_t<E1>;
    using D2 = std::decay_t<E2>;
    constexpr FullCmp<> cmp{};

    if constexpr (std::is_enum_v<D1> && std::is_enum_v<D2>) {
      return cmp(enum_name(e1), enum_name(e2));
    } else if constexpr (std::is_enum_v<D1>) {
      return cmp(enum_name(e1), e2);
    } else /* if constexpr (std::is_enum_v<D2>) */ {
      return cmp(e1, enum_name(e2));
    }
  }
};

struct raw_access_t {};

template <typename Parent, typename Iterator, typename Getter, typename Predicate>
struct FilteredIterator {
  Parent parent;
  Iterator first;
  Iterator last;
  Iterator current;
  Getter getter;
  Predicate predicate;

  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = std::remove_reference_t<std::invoke_result_t<Getter, Parent, Iterator>>;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  constexpr FilteredIterator() noexcept = default;
  constexpr FilteredIterator(const FilteredIterator&) = default;
  constexpr FilteredIterator& operator=(const FilteredIterator&) = default;
  constexpr FilteredIterator(FilteredIterator&&) noexcept = default;
  constexpr FilteredIterator& operator=(FilteredIterator&&) noexcept = default;

  template <typename OtherParent, typename OtherIterator, typename = std::enable_if_t<std::is_convertible_v<OtherParent, Parent> && std::is_convertible_v<OtherIterator, Iterator>>*>
  constexpr explicit FilteredIterator(const FilteredIterator<OtherParent, OtherIterator, Getter, Predicate>& other)
      : parent(other.parent), first(other.first), last(other.last), current(other.current), getter(other.getter), predicate(other.predicate) {}

  constexpr FilteredIterator(Parent p, Iterator begin, Iterator end, Iterator curr, Getter get = {}, Predicate pred = {})
      : parent(p), first(std::move(begin)), last(std::move(end)), current(std::move(curr)), getter{std::move(get)}, predicate{std::move(pred)} {
    if (current == first && !predicate(parent, current)) {
      ++*this;
    }
  }

  [[nodiscard]] constexpr reference operator*() const { return getter(parent, current); }

  [[nodiscard]] constexpr pointer operator->() const { return std::addressof(**this); }

  constexpr FilteredIterator& operator++() {
    do {
      ++current;
    } while (current != last && !predicate(parent, current));
    return *this;
  }

  [[nodiscard]] constexpr FilteredIterator operator++(int) {
    FilteredIterator cp = *this;
    ++*this;
    return cp;
  }

  constexpr FilteredIterator& operator--() {
    do {
      --current;
    } while (current != first && !predicate(parent, current));
    return *this;
  }

  [[nodiscard]] constexpr FilteredIterator operator--(int) {
    FilteredIterator cp = *this;
    --*this;
    return cp;
  }

  [[nodiscard]] friend constexpr bool operator==(const FilteredIterator& lhs, const FilteredIterator& rhs) { return lhs.current == rhs.current; }

  [[nodiscard]] friend constexpr bool operator!=(const FilteredIterator& lhs, const FilteredIterator& rhs) { return lhs.current != rhs.current; }
};

} // namespace detail

template <typename E = void>
using name_less = detail::name_sort_impl<E>;

template <typename E = void>
using name_greater = detail::name_sort_impl<E, std::greater<>>;

using name_less_case_insensitive = detail::name_sort_impl<void, magic_enum::detail::case_insensitive<std::less<>>>;

using name_greater_case_insensitive = detail::name_sort_impl<void, magic_enum::detail::case_insensitive<std::greater<>>>;

template <typename E = void>
using default_indexing = detail::indexing<E>;

template <typename Cmp = std::less<>>
using comparator_indexing = detail::indexing<void, Cmp>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                          ARRAY                                                            //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename E, typename V, typename Index = default_indexing<E>>
struct array {
  static_assert(std::is_enum_v<E>);
  static_assert(std::is_trivially_constructible_v<Index>);
  static_assert(enum_count<E>() > 0 && Index::at(enum_values<E>().front()));

  using index_type = Index;
  using container_type = std::array<V, enum_count<E>()>;

  using value_type = typename container_type::value_type;
  using size_type = typename container_type::size_type;
  using difference_type = typename container_type::difference_type;
  using reference = typename container_type::reference;
  using const_reference = typename container_type::const_reference;
  using pointer = typename container_type::pointer;
  using const_pointer = typename container_type::const_pointer;
  using iterator = typename container_type::iterator;
  using const_iterator = typename container_type::const_iterator;
  using reverse_iterator = typename container_type::reverse_iterator;
  using const_reverse_iterator = typename container_type::const_reverse_iterator;

  constexpr reference at(E pos) {
    if (auto index = index_type::at(pos)) {
      return a[*index];
    }
    MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::array::at Unrecognized position"));
  }

  constexpr const_reference at(E pos) const {
    if (auto index = index_type::at(pos)) {
      return a[*index];
    }
    MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::array::at: Unrecognized position"));
  }

  [[nodiscard]] constexpr reference operator[](E pos) {
    auto i = index_type::at(pos);
    return MAGIC_ENUM_ASSERT(i), a[*i];
  }

  [[nodiscard]] constexpr const_reference operator[](E pos) const {
    auto i = index_type::at(pos);
    return MAGIC_ENUM_ASSERT(i), a[*i];
  }

  [[nodiscard]] constexpr reference front() noexcept { return a.front(); }

  [[nodiscard]] constexpr const_reference front() const noexcept { return a.front(); }

  [[nodiscard]] constexpr reference back() noexcept { return a.back(); }

  [[nodiscard]] constexpr const_reference back() const noexcept { return a.back(); }

  [[nodiscard]] constexpr pointer data() noexcept { return a.data(); }

  [[nodiscard]] constexpr const_pointer data() const noexcept { return a.data(); }

  [[nodiscard]] constexpr iterator begin() noexcept { return a.begin(); }

  [[nodiscard]] constexpr const_iterator begin() const noexcept { return a.begin(); }

  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return a.cbegin(); }

  [[nodiscard]] constexpr iterator end() noexcept { return a.end(); }

  [[nodiscard]] constexpr const_iterator end() const noexcept { return a.end(); }

  [[nodiscard]] constexpr const_iterator cend() const noexcept { return a.cend(); }

  [[nodiscard]] constexpr iterator rbegin() noexcept { return a.rbegin(); }

  [[nodiscard]] constexpr const_iterator rbegin() const noexcept { return a.rbegin(); }

  [[nodiscard]] constexpr const_iterator crbegin() const noexcept { return a.crbegin(); }

  [[nodiscard]] constexpr iterator rend() noexcept { return a.rend(); }

  [[nodiscard]] constexpr const_iterator rend() const noexcept { return a.rend(); }

  [[nodiscard]] constexpr const_iterator crend() const noexcept { return a.crend(); }

  [[nodiscard]] constexpr bool empty() const noexcept { return a.empty(); }

  [[nodiscard]] constexpr size_type size() const noexcept { return a.size(); }

  [[nodiscard]] constexpr size_type max_size() const noexcept { return a.max_size(); }

  constexpr void fill(const V& value) {
    for (auto& v : a) {
      v = value;
    }
  }

  constexpr void swap(array& other) noexcept(std::is_nothrow_swappable_v<V>) {
    for (std::size_t i = 0; i < a.size(); ++i) {
      auto v = std::move(other.a[i]);
      other.a[i] = std::move(a[i]);
      a[i] = std::move(v);
    }
  }

  [[nodiscard]] friend constexpr bool operator==(const array& a1, const array& a2) { return detail::equal(a1, a2); }

  [[nodiscard]] friend constexpr bool operator!=(const array& a1, const array& a2) { return !detail::equal(a1, a2); }

  [[nodiscard]] friend constexpr bool operator<(const array& a1, const array& a2) { return detail::lexicographical_compare(a1, a2); }

  [[nodiscard]] friend constexpr bool operator<=(const array& a1, const array& a2) { return !detail::lexicographical_compare(a2, a1); }

  [[nodiscard]] friend constexpr bool operator>(const array& a1, const array& a2) { return detail::lexicographical_compare(a2, a1); }

  [[nodiscard]] friend constexpr bool operator>=(const array& a1, const array& a2) { return !detail::lexicographical_compare(a1, a2); }

  container_type a;
};

namespace detail {

template <typename E, typename T, std::size_t N, std::size_t... I>
constexpr array<E, std::remove_cv_t<T>> to_array_impl(T (&a)[N], std::index_sequence<I...>) {
  return {{a[I]...}};
}

template <typename E, typename T, std::size_t N, std::size_t... I>
constexpr array<E, std::remove_cv_t<T>> to_array_impl(T(&&a)[N], std::index_sequence<I...>) {
  return {{std::move(a[I])...}};
}

} // namespace detail

template <typename E, typename T, std::size_t N>
constexpr std::enable_if_t<(enum_count<E>() == N), array<E, std::remove_cv_t<T>>> to_array(T (&a)[N]) {
  return detail::to_array_impl<E>(a, std::make_index_sequence<N>{});
}

template <typename E, typename T, std::size_t N>
constexpr std::enable_if_t<(enum_count<E>() == N), array<E, std::remove_cv_t<T>>> to_array(T(&&a)[N]) {
  return detail::to_array_impl<E>(std::move(a), std::make_index_sequence<N>{});
}

template <typename E, typename... Ts>
constexpr std::enable_if_t<(enum_count<E>() == sizeof...(Ts)), array<E, std::remove_cv_t<std::common_type_t<Ts...>>>> make_array(Ts&&... ts) {
  return {{std::forward<Ts>(ts)...}};
}

inline constexpr detail::raw_access_t raw_access{};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                         BITSET                                                            //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename E, typename Index = default_indexing<E>>
class bitset {
  // DSLtk: keep optional string facilities dependent on E.
  using string = std::conditional_t<std::is_enum_v<E>, magic_enum::string, void>;
  using string_view = std::conditional_t<std::is_enum_v<E>, magic_enum::string_view, void>;
  static_assert(std::is_enum_v<E>);
  static_assert(std::is_trivially_constructible_v<Index>);
  static_assert(enum_count<E>() > 0 && Index::at(enum_values<E>().front()));

  using base_type = std::conditional_t<enum_count<E>() <= 8,  std::uint_least8_t,
                    std::conditional_t<enum_count<E>() <= 16, std::uint_least16_t,
                    std::conditional_t<enum_count<E>() <= 32, std::uint_least32_t,
                                                              std::uint_least64_t>>>;

  static constexpr std::size_t bits_per_base = sizeof(base_type) * 8;
  static constexpr std::size_t base_type_count = (enum_count<E>() > 0 ? (enum_count<E>() - 1) / bits_per_base + 1 : 0);
  static constexpr std::size_t not_interested = base_type_count * bits_per_base - enum_count<E>();
  static constexpr base_type last_value_max = (base_type{1} << (bits_per_base - not_interested)) - 1;

  template <typename parent_t = bitset*>
  class reference_impl {
    friend class bitset;

    parent_t parent;
    std::size_t num_index;
    base_type bit_index;

    constexpr reference_impl(parent_t p, std::size_t i) noexcept : reference_impl(p, std::pair{i / bits_per_base, base_type{1} << (i % bits_per_base)}) {}

    constexpr reference_impl(parent_t p, std::pair<std::size_t, base_type> i) noexcept : parent(p), num_index(std::get<0>(i)), bit_index(std::get<1>(i)) {}

   public:
    constexpr reference_impl& operator=(bool v) noexcept {
      if (v) {
        parent->a[num_index] |= bit_index;
      } else {
        parent->a[num_index] &= ~bit_index;
      }
      return *this;
    }

    constexpr reference_impl& operator=(const reference_impl& v) noexcept {
      if (this == &v) {
        return *this;
      }
      *this = static_cast<bool>(v);
      return *this;
    }

    [[nodiscard]] constexpr operator bool() const noexcept { return (parent->a[num_index] & bit_index) > 0; }

    [[nodiscard]] constexpr bool operator~() const noexcept { return !static_cast<bool>(*this); }

    constexpr reference_impl& flip() noexcept {
      *this = ~*this;
      return *this;
    }
  };

  template <typename T>
  [[nodiscard]] constexpr T to_(detail::raw_access_t) const {
    T res{};
    T flag{1};
    for (std::size_t i = 0; i < size(); ++i, flag <<= 1) {
      if (const_reference{this, i}) {
        if (i >= sizeof(T) * 8) {
          MAGIC_ENUM_THROW(std::overflow_error("magic_enum::containers::bitset::to: Cannot represent enum in this type"));
        }
        res |= flag;
      }
    }
    return res;
  }

 public:
  using index_type = Index;
  using container_type = std::array<base_type, base_type_count>;
  using reference = reference_impl<>;
  using const_reference = reference_impl<const bitset*>;

  constexpr explicit bitset(detail::raw_access_t = raw_access) noexcept : a{{}} {}

  constexpr explicit bitset(detail::raw_access_t, unsigned long long val) : a{{}} {
    unsigned long long bit{1};
    for (std::size_t i = 0; i < (sizeof(val) * 8); ++i, bit <<= 1) {
      if ((val & bit) > 0) {
        if (i >= enum_count<E>()) {
          MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::bitset::constructor: Upper bit set in raw number"));
        }

        reference{this, i} = true;
      }
    }
  }

  constexpr explicit bitset(detail::raw_access_t, string_view sv, std::size_t pos = 0, std::size_t n = string_view::npos, char_type zero = static_cast<char_type>('0'), char_type one = static_cast<char_type>('1'))
      : a{{}} {
    std::size_t i = 0;
    for (auto c : sv.substr(pos, n)) {
      if (c == one) {
        if (i >= enum_count<E>()) {
          MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::bitset::constructor: Upper bit set in raw string"));
        }
        reference{this, i} = true;
      } else if (c != zero) {
        MAGIC_ENUM_THROW(std::invalid_argument("magic_enum::containers::bitset::constructor: Unrecognized character in raw string"));
      }
      ++i;
    }
  }

  constexpr explicit bitset(detail::raw_access_t, const char_type* str, std::size_t n = ~std::size_t{0}, char_type zero = static_cast<char_type>('0'), char_type one = static_cast<char_type>('1'))
      : bitset(string_view{str, (std::min)(std::char_traits<char_type>::length(str), n)}, 0, n, zero, one) {}

  constexpr bitset(std::initializer_list<E> starters) : a{{}} {
    if constexpr (magic_enum::detail::subtype_v<E> == magic_enum::detail::enum_subtype::flags) {
      for (auto& f : starters) {
        *this |= bitset(f);
      }
    } else {
      for (auto& f : starters) {
        set(f);
      }
    }
  }
  template <typename V, std::enable_if_t<std::is_same_v<V, E> && magic_enum::detail::subtype_v<V> == magic_enum::detail::enum_subtype::flags, int> = 0>
  constexpr explicit bitset(V starter) : a{{}} {
    auto u = enum_underlying(starter);
    for (E v : enum_values<E>()) {
      if (auto ul = enum_underlying(v); (ul & u) != 0) {
        u &= ~ul;
        (*this)[v] = true;
      }
    }
    if (u != 0) {
      MAGIC_ENUM_THROW(std::invalid_argument("magic_enum::containers::bitset::constructor: Unrecognized enum value in flag"));
    }
  }

  template <typename Cmp = std::equal_to<>>
  constexpr explicit bitset(string_view sv, Cmp&& cmp = {}, char_type sep = static_cast<char_type>('|')) {
    for (std::size_t to = 0; (to = magic_enum::detail::find(sv, sep)) != string_view::npos; sv.remove_prefix(to + 1)) {
      if (auto v = enum_cast<E>(sv.substr(0, to), cmp)) {
        set(*v);
      } else {
        MAGIC_ENUM_THROW(std::invalid_argument("magic_enum::containers::bitset::constructor: Unrecognized enum value in string"));
      }
    }
    if (!sv.empty()) {
      if (auto v = enum_cast<E>(sv, cmp)) {
        set(*v);
      } else {
        MAGIC_ENUM_THROW(std::invalid_argument("magic_enum::containers::bitset::constructor: Unrecognized enum value in string"));
      }
    }
  }

  [[nodiscard]] friend constexpr bool operator==(const bitset& lhs, const bitset& rhs) noexcept { return detail::equal(lhs.a, rhs.a); }

  [[nodiscard]] friend constexpr bool operator!=(const bitset& lhs, const bitset& rhs) noexcept { return !detail::equal(lhs.a, rhs.a); }

  [[nodiscard]] constexpr bool operator[](E pos) const {
    auto i = index_type::at(pos);
    return MAGIC_ENUM_ASSERT(i), static_cast<bool>(const_reference(this, *i));
  }

  [[nodiscard]] constexpr reference operator[](E pos) {
    auto i = index_type::at(pos);
    return MAGIC_ENUM_ASSERT(i), reference{this, *i};
  }

  constexpr bool test(E pos) const {
    if (auto i = index_type::at(pos)) {
      return static_cast<bool>(const_reference(this, *i));
    }
    MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::bitset::test: Unrecognized position"));
  }

  [[nodiscard]] constexpr bool all() const noexcept {
    if constexpr (base_type_count == 0) {
      return true;
    }

    for (std::size_t i = 0; i < base_type_count - (not_interested > 0); ++i) {
      auto check = ~a[i];
      if (check) {
        return false;
      }
    }

    if constexpr (not_interested > 0) {
      return a[base_type_count - 1] == last_value_max;
    }
  }

  [[nodiscard]] constexpr bool any() const noexcept {
    for (auto& v : a) {
      if (v > 0) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] constexpr bool none() const noexcept { return !any(); }

  [[nodiscard]] constexpr std::size_t count() const noexcept {
    std::size_t c = 0;
    for (auto& v : a) {
      c += detail::popcount(v);
    }
    return c;
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return enum_count<E>(); }

  [[nodiscard]] constexpr std::size_t max_size() const noexcept { return enum_count<E>(); }

  constexpr bitset& operator&=(const bitset& other) noexcept {
    for (std::size_t i = 0; i < base_type_count; ++i) {
      a[i] &= other.a[i];
    }
    return *this;
  }

  constexpr bitset& operator|=(const bitset& other) noexcept {
    for (std::size_t i = 0; i < base_type_count; ++i) {
      a[i] |= other.a[i];
    }
    return *this;
  }

  constexpr bitset& operator^=(const bitset& other) noexcept {
    for (std::size_t i = 0; i < base_type_count; ++i) {
      a[i] ^= other.a[i];
    }
    return *this;
  }

  [[nodiscard]] constexpr bitset operator~() const noexcept {
    bitset res;
    for (std::size_t i = 0; i < base_type_count - (not_interested > 0); ++i) {
      res.a[i] = ~a[i];
    }

    if constexpr (not_interested > 0) {
      res.a[base_type_count - 1] = ~a[base_type_count - 1] & last_value_max;
    }
    return res;
  }

  constexpr bitset& set() noexcept {
    for (std::size_t i = 0; i < base_type_count - (not_interested > 0); ++i) {
      a[i] = ~base_type{0};
    }

    if constexpr (not_interested > 0) {
      a[base_type_count - 1] = last_value_max;
    }
    return *this;
  }

  constexpr bitset& set(E pos, bool value = true) {
    if (auto i = index_type::at(pos)) {
      reference{this, *i} = value;
      return *this;
    }
    MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::bitset::set: Unrecognized position"));
  }

  constexpr bitset& reset() noexcept { return *this = bitset{}; }

  constexpr bitset& reset(E pos) {
    if (auto i = index_type::at(pos)) {
      reference{this, *i} = false;
      return *this;
    }
    MAGIC_ENUM_THROW(std::out_of_range("magic_enum::containers::bitset::reset: Unrecognized position"));
  }

  constexpr bitset& flip() noexcept { return *this = ~*this; }

  [[nodiscard]] friend constexpr bitset operator&(const bitset& lhs, const bitset& rhs) noexcept {
    bitset cp = lhs;
    cp &= rhs;
    return cp;
  }

  [[nodiscard]] friend constexpr bitset operator|(const bitset& lhs, const bitset& rhs) noexcept {
    bitset cp = lhs;
    cp |= rhs;
    return cp;
  }

  [[nodiscard]] friend constexpr bitset operator^(const bitset& lhs, const bitset& rhs) noexcept {
    bitset cp = lhs;
    cp ^= rhs;
    return cp;
  }

  template <typename V = E>
  [[nodiscard]] constexpr explicit operator std::enable_if_t<magic_enum::detail::subtype_v<V> == magic_enum::detail::enum_subtype::flags, E>() const {
    E res{};
    for (const auto& e : enum_values<E>()) {
      if (test(e)) {
        res |= e;
      }
    }
    return res;
  }

  [[nodiscard]] string to_string(char_type sep = static_cast<char_type>('|')) const {
    string name;

    for (const auto& e : enum_values<E>()) {
      if (test(e)) {
        if (!name.empty()) {
          name.append(1, sep);
        }
        auto n = enum_name(e);
        name.append(n.data(), n.size());
      }
    }
    return name;
  }

  [[nodiscard]] string to_string(detail::raw_access_t, char_type zero = static_cast<char_type>('0'), char_type one = static_cast<char_type>('1')) const {
    string name;
    name.reserve(size());
    for (std::size_t i = 0; i < size(); ++i) {
      name.append(1, const_reference{this, i} ? one : zero);
    }
    return name;
  }

  [[nodiscard]] constexpr unsigned long long to_ullong(detail::raw_access_t raw) const { return to_<unsigned long long>(raw); }

  [[nodiscard]] constexpr unsigned long long to_ulong(detail::raw_access_t raw) const { return to_<unsigned long>(raw); }

  friend std::ostream& operator<<(std::ostream& o, const bitset& bs) { return o << bs.to_string(); }

  friend std::istream& operator>>(std::istream& i, bitset& bs) {
    string s;
    if (i >> s; !s.empty()) {
      bs = bitset(string_view{s});
    }
    return i;
  }

 private:
  container_type a;
};

template <typename V, int = 0>
explicit bitset(V starter) -> bitset<V>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                           SET                                                             //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename E, typename Cmp = std::less<E>>
class set {
  using index_type = detail::indexing<E, Cmp>;
  struct Getter {
    constexpr const E& operator()(const set*, const E* p) const noexcept { return *p; }
  };
  struct Predicate {
    constexpr bool operator()(const set* h, const E* e) const noexcept { return h->a[*e]; }
  };

 public:
  using container_type = bitset<E, index_type>;
  using key_type = E;
  using value_type = E;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using key_compare = Cmp;
  using value_compare = Cmp;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using iterator = detail::FilteredIterator<const set*, const E*, Getter, Predicate>;
  using const_iterator = detail::FilteredIterator<const set*, const E*, Getter, Predicate>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  constexpr set() noexcept = default;

  template <typename InputIt>
  constexpr set(InputIt first, InputIt last) {
    while (first != last) {
      insert(*first++);
    }
  }

  constexpr set(std::initializer_list<E> ilist) {
    for (auto e : ilist) {
      insert(e);
    }
  }
  template <typename V, std::enable_if_t<std::is_same_v<V, E> && magic_enum::detail::subtype_v<V> == magic_enum::detail::enum_subtype::flags, int> = 0>
  constexpr explicit set(V starter) {
    auto u = enum_underlying(starter);
    for (E v : enum_values<E>()) {
      if ((enum_underlying(v) & u) != 0) {
        insert(v);
      }
    }
  }

  constexpr set(const set&) noexcept = default;
  constexpr set(set&&) noexcept = default;

  constexpr set& operator=(const set&) noexcept = default;
  constexpr set& operator=(set&&) noexcept = default;
  constexpr set& operator=(std::initializer_list<E> ilist) {
    for (auto e : ilist) {
      insert(e);
    }
  }

  constexpr const_iterator begin() const noexcept {
    return const_iterator{this, index_type::begin(), index_type::end(), index_type::begin()};
  }

  constexpr const_iterator end() const noexcept {
    return const_iterator{this, index_type::begin(), index_type::end(), index_type::end()};
  }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr const_iterator cend() const noexcept { return end(); }

  constexpr const_reverse_iterator rbegin() const noexcept { return {end()}; }

  constexpr const_reverse_iterator rend() const noexcept { return {begin()}; }

  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  [[nodiscard]] constexpr bool empty() const noexcept { return s == 0; }

  [[nodiscard]] constexpr size_type size() const noexcept { return s; }

  [[nodiscard]] constexpr size_type max_size() const noexcept { return a.max_size(); }

  constexpr void clear() noexcept {
    a.reset();
    s = 0;
  }

  constexpr std::pair<iterator, bool> insert(const value_type& value) noexcept {
    if (auto i = index_type::at(value)) {
      typename container_type::reference ref = a[value];
      bool r = !ref;
      if (r) {
        ref = true;
        ++s;
      }

      return {iterator{this, index_type::begin(), index_type::end(), index_type::it(*i)}, r};
    }
    return {end(), false};
  }

  constexpr std::pair<iterator, bool> insert(value_type&& value) noexcept { return insert(value); }

  constexpr iterator insert(const_iterator, const value_type& value) noexcept { return insert(value).first; }

  constexpr iterator insert(const_iterator hint, value_type&& value) noexcept { return insert(hint, value); }

  template <typename InputIt>
  constexpr void insert(InputIt first, InputIt last) noexcept {
    while (first != last) {
      insert(*first++);
    }
  }

  constexpr void insert(std::initializer_list<value_type> ilist) noexcept {
    for (auto v : ilist) {
      insert(v);
    }
  }

  template <typename... Args>
  constexpr std::pair<iterator, bool> emplace(Args&&... args) noexcept {
    return insert({std::forward<Args>(args)...});
  }

  template <typename... Args>
  constexpr iterator emplace_hint(const_iterator, Args&&... args) noexcept {
    return emplace(std::forward<Args>(args)...).first;
  }

  constexpr iterator erase(const_iterator pos) noexcept {
    erase(*pos++);
    return pos;
  }

  constexpr iterator erase(const_iterator first, const_iterator last) noexcept {
    while ((first = erase(first)) != last) {
    }
    return first;
  }

  constexpr size_type erase(const key_type& key) noexcept {
    typename container_type::reference ref = a[key];
    bool res = ref;
    if (res) {
      --s;
    }
    ref = false;
    return res;
  }

  template <typename K, typename KC = key_compare>
  constexpr std::enable_if_t<detail::is_transparent_v<KC>, size_type> erase(K&& x) noexcept {
    size_type c = 0;
    for (auto [first, last] = detail::equal_range(index_type::begin(), index_type::end(), x, key_compare{}); first != last;) {
      c += erase(*first++);
    }
    return c;
  }

  void swap(set& other) noexcept {
    set cp = *this;
    *this = other;
    other = cp;
  }

  [[nodiscard]] constexpr size_type count(const key_type& key) const noexcept { return index_type::at(key) && a[key]; }

  template <typename K, typename KC = key_compare>
  [[nodiscard]] constexpr std::enable_if_t<detail::is_transparent_v<KC>, size_type> count(const K& x) const {
    size_type c = 0;
    for (auto [first, last] = detail::equal_range(index_type::begin(), index_type::end(), x, key_compare{}); first != last; ++first) {
      c += count(*first);
    }
    return c;
  }

  [[nodiscard]] constexpr const_iterator find(const key_type& key) const noexcept {
    if (auto i = index_type::at(key); i && a.test(key)) {
      return const_iterator{this, index_type::begin(), index_type::end(), index_type::it(*i)};
    }
    return end();
  }

  template <typename K, typename KC = key_compare>
  [[nodiscard]] constexpr std::enable_if_t<detail::is_transparent_v<KC>, const_iterator> find(const K& x) const {
    for (auto [first, last] = detail::equal_range(index_type::begin(), index_type::end(), x, key_compare{}); first != last; ++first) {
      if (a.test(*first)) {
        return find(*first);
      }
    }
    return end();
  }

  [[nodiscard]] constexpr bool contains(const key_type& key) const noexcept { return count(key); }

  template <typename K, typename KC = key_compare>
  [[nodiscard]] constexpr std::enable_if_t<detail::is_transparent_v<KC>, bool> contains(const K& x) const noexcept {
    return count(x) > 0;
  }

  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const noexcept { return {lower_bound(key), upper_bound(key)}; }

  template <typename K, typename KC = key_compare>
  [[nodiscard]] constexpr std::enable_if_t<detail::is_transparent_v<KC>, std::pair<const_iterator, const_iterator>> equal_range(const K& x) const noexcept {
    return {lower_bound(x), upper_bound(x)};
  }

  [[nodiscard]] constexpr const_iterator lower_bound(const key_type& key) const noexcept {
    if (auto i = index_type::at(key)) {
      auto it = const_iterator{this, index_type::begin(), index_type::end(), index_type::it(*i)};
      return a.test(key) ? it : std::next(it);
    }
    return end();
  }

  template <typename K, typename KC = key_compare>
  [[nodiscard]] constexpr std::enable_if_t<detail::is_transparent_v<KC>, const_iterator> lower_bound(const K& x) const noexcept {
    auto [first, last] = detail::equal_range(index_type::begin(), index_type::end(), x, key_compare{});
    return first != last ? lower_bound(*first) : end();
  }

  [[nodiscard]] constexpr const_iterator upper_bound(const key_type& key) const noexcept {
    if (auto i = index_type::at(key)) {
      return std::next(const_iterator{this, index_type::begin(), index_type::end(), index_type::it(*i)});
    }
    return end();
  }

  template <typename K, typename KC = key_compare>
  [[nodiscard]] constexpr std::enable_if_t<detail::is_transparent_v<KC>, const_iterator> upper_bound(const K& x) const noexcept {
    auto [first, last] = detail::equal_range(index_type::begin(), index_type::end(), x, key_compare{});
    return first != last ? upper_bound(*std::prev(last)) : end();
  }

  [[nodiscard]] constexpr key_compare key_comp() const { return {}; }

  [[nodiscard]] constexpr value_compare value_comp() const { return {}; }

  [[nodiscard]] constexpr friend bool operator==(const set& lhs, const set& rhs) noexcept { return lhs.a == rhs.a; }

  [[nodiscard]] constexpr friend bool operator!=(const set& lhs, const set& rhs) noexcept { return lhs.a != rhs.a; }

  [[nodiscard]] constexpr friend bool operator<(const set& lhs, const set& rhs) noexcept {
    if (lhs.s < rhs.s) {
      return true;
    }
    if (rhs.s < lhs.s) {
      return false;
    }

    for (auto it = index_type::begin(); it != index_type::end(); ++it) {
      if (auto c = rhs.contains(*it); c != lhs.contains(*it)) {
        return c;
      }
    }
    return false;
  }

  [[nodiscard]] constexpr friend bool operator<=(const set& lhs, const set& rhs) noexcept { return !(rhs < lhs); }

  [[nodiscard]] constexpr friend bool operator>(const set& lhs, const set& rhs) noexcept { return rhs < lhs; }

  [[nodiscard]] constexpr friend bool operator>=(const set& lhs, const set& rhs) noexcept { return !(lhs < rhs); }

  template <typename Pred>
  size_type erase_if(Pred pred) {
    auto old_size = size();
    for (auto i = begin(), last = end(); i != last;) {
      if (pred(*i)) {
        i = erase(i);
      } else {
        ++i;
      }
    }
    return old_size - size();
  }

 private:
  container_type a;
  std::size_t s = 0;
};

template <typename V, int = 0>
explicit set(V starter) -> set<V>;

template <auto I, typename E, typename V, typename Index>
constexpr std::enable_if_t<(std::is_integral_v<decltype(I)> && I < enum_count<E>()), V&> get(array<E, V, Index>& a) noexcept {
  return a.a[I];
}

template <auto I, typename E, typename V, typename Index>
constexpr std::enable_if_t<(std::is_integral_v<decltype(I)> && I < enum_count<E>()), V&&> get(array<E, V, Index>&& a) noexcept {
  return std::move(a.a[I]);
}

template <auto I, typename E, typename V, typename Index>
constexpr std::enable_if_t<(std::is_integral_v<decltype(I)> && I < enum_count<E>()), const V&> get(const array<E, V, Index>& a) noexcept {
  return a.a[I];
}

template <auto I, typename E, typename V, typename Index>
constexpr std::enable_if_t<(std::is_integral_v<decltype(I)> && I < enum_count<E>()), const V&&> get(const array<E, V, Index>&& a) noexcept {
  return std::move(a.a[I]);
}

template <auto Enum, typename E, typename V, typename Index>
constexpr std::enable_if_t<std::is_same_v<decltype(Enum), E> && enum_contains(Enum), V&> get(array<E, V, Index>& a) {
  return a[Enum];
}

template <auto Enum, typename E, typename V, typename Index>
constexpr std::enable_if_t<std::is_same_v<decltype(Enum), E> && enum_contains(Enum), V&&> get(array<E, V, Index>&& a) {
  return std::move(a[Enum]);
}

template <auto Enum, typename E, typename V, typename Index>
constexpr std::enable_if_t<std::is_same_v<decltype(Enum), E> && enum_contains(Enum), const V&> get(const array<E, V, Index>& a) {
  return a[Enum];
}

template <auto Enum, typename E, typename V, typename Index>
constexpr std::enable_if_t<std::is_same_v<decltype(Enum), E> && enum_contains(Enum), const V&&> get(const array<E, V, Index>&& a) {
  return std::move(a[Enum]);
}

} // namespace magic_enum::containers

#endif // NEARGYE_MAGIC_ENUM_CONTAINERS_HPP
// Embedded upstream header: magic_enum_flags.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_FLAGS_HPP
#define NEARGYE_MAGIC_ENUM_FLAGS_HPP

// magic_enum.hpp is already embedded above.

#if defined(__clang__)
#  pragma clang diagnostic push
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized" // May be used uninitialized 'return {};'.
#elif defined(_MSC_VER)
#  pragma warning(push)
#endif

namespace magic_enum {

namespace detail {

template <typename E, enum_subtype S, typename U = std::underlying_type_t<E>>
constexpr U values_ors() noexcept {
  static_assert(S == enum_subtype::flags, "magic_enum::detail::values_ors requires valid subtype.");

  auto ors = U{0};
  for (std::size_t i = 0; i < count_v<E, S>; ++i) {
    ors |= static_cast<U>(values_v<E, S>[i]);
  }

  return ors;
}

} // namespace magic_enum::detail

// Returns name from enum-flags value.
// If enum-flags value does not have name or value out of range, returns empty string.
template <typename E>
[[nodiscard]] auto enum_flags_name(E value, char_type sep = static_cast<char_type>('|')) -> detail::enable_if_t<E, string> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;
  constexpr auto S = detail::enum_subtype::flags;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  string name;
  auto check_value = U{0};
  for (std::size_t i = 0; i < detail::count_v<D, S>; ++i) {
    if (const auto v = static_cast<U>(enum_value<D, S>(i)); (static_cast<U>(value) & v) != 0) {
      if (const auto n = detail::names_v<D, S>[i]; !n.empty()) {
        check_value |= v;
        if (!name.empty()) {
          name.append(1, sep);
        }
        name.append(n.data(), n.size());
      } else {
        return {}; // Value out of range.
      }
    }
  }

  if (check_value != 0 && check_value == static_cast<U>(value)) {
    return name;
  }
  return {}; // Invalid value or out of range.
}

// Returns enum-flags value from integer value.
// Returns optional with enum-flags value.
template <typename E>
[[nodiscard]] constexpr auto enum_flags_cast(underlying_type_t<E> value) noexcept -> detail::enable_if_t<E, optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;
  constexpr auto S = detail::enum_subtype::flags;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  if constexpr (detail::count_v<D, S> == 0) {
    static_cast<void>(value);
    return {}; // Empty enum.
  } else {
    if constexpr (detail::is_sparse_v<D, S>) {
      auto check_value = U{0};
      for (std::size_t i = 0; i < detail::count_v<D, S>; ++i) {
        if (const auto v = static_cast<U>(enum_value<D, S>(i)); (value & v) != 0) {
          check_value |= v;
        }
      }

      if (check_value != 0 && check_value == value) {
        return static_cast<D>(value);
      }
    } else {
      constexpr auto min = detail::min_v<D, S>;
      constexpr auto max = detail::values_ors<D, S>();

      if (value >= min && value <= max) {
        return static_cast<D>(value);
      }
    }
    return {}; // Invalid value or out of range.
  }
}

// Returns enum-flags value from name.
// Returns optional with enum-flags value.
template <typename E, typename BinaryPredicate = std::equal_to<>>
[[nodiscard]] constexpr auto enum_flags_cast(string_view value, [[maybe_unused]] char_type sep = static_cast<char_type>('|'), [[maybe_unused]] BinaryPredicate p = {}) noexcept(detail::is_nothrow_invocable_v<BinaryPredicate>) -> detail::enable_if_t<E, optional<std::decay_t<E>>, BinaryPredicate> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;
  constexpr auto S = detail::enum_subtype::flags;
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

  if constexpr (detail::count_v<D, S> == 0) {
    static_cast<void>(value);
    return {}; // Empty enum.
  } else {
    auto result = U{0};
    while (!value.empty()) {
      const auto d = detail::find(value, sep);
      const auto s = (d == string_view::npos) ? value : value.substr(0, d);
      auto f = U{0};
      for (std::size_t i = 0; i < detail::count_v<D, S>; ++i) {
        if (detail::cmp_equal(s, detail::names_v<D, S>[i], p)) {
          f = static_cast<U>(enum_value<D, S>(i));
          result |= f;
          break;
        }
      }
      if (f == U{0}) {
        return {}; // Invalid value or out of range.
      }
      value.remove_prefix((d == string_view::npos) ? value.size() : d + 1);
    }

    if (result != U{0}) {
      return static_cast<D>(result);
    }
    return {}; // Invalid value or out of range.
  }
}

// Returns true if enum-flags contains value with such value.
template <typename E>
[[nodiscard]] constexpr auto enum_flags_contains(E value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;

  return static_cast<bool>(enum_flags_cast<D>(static_cast<U>(value)));
}

// Returns true if enum-flags contains value with such integer value.
template <typename E>
[[nodiscard]] constexpr auto enum_flags_contains(underlying_type_t<E> value) noexcept -> detail::enable_if_t<E, bool> {
  using D = std::decay_t<E>;

  return static_cast<bool>(enum_flags_cast<D>(value));
}

// Returns true if enum-flags contains enumerator with such name.
template <typename E, typename BinaryPredicate = std::equal_to<>>
[[nodiscard]] constexpr auto enum_flags_contains(string_view value, char_type sep = static_cast<char_type>('|'), BinaryPredicate p = {}) noexcept(detail::is_nothrow_invocable_v<BinaryPredicate>) -> detail::enable_if_t<E, bool, BinaryPredicate> {
  using D = std::decay_t<E>;

  return static_cast<bool>(enum_flags_cast<D>(value, sep, std::move(p)));
}

// Returns true if `flags set` contains `flag`.
// Note: If `flag` equals 0, it returns false, as 0 is not a flag.
template <typename E>
constexpr auto enum_flags_test(E flags, E flag) noexcept -> detail::enable_if_t<E, bool> {
  using U = underlying_type_t<E>;

  return static_cast<U>(flag) && ((static_cast<U>(flags) & static_cast<U>(flag)) == static_cast<U>(flag));
}

// Returns true if `lhs flags set` and `rhs flags set` have common flags.
// Note: If `lhs flags set` or `rhs flags set` equals 0, it returns false, as 0 is not a flag, and therfore cannot have any matching flag.
template <typename E>
constexpr auto enum_flags_test_any(E lhs, E rhs) noexcept -> detail::enable_if_t<E, bool> {
  using U = underlying_type_t<E>;

  return (static_cast<U>(lhs) & static_cast<U>(rhs)) != 0;
}

} // namespace magic_enum

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif

#endif // NEARGYE_MAGIC_ENUM_FLAGS_HPP
// Embedded upstream header: magic_enum_format.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_FORMAT_HPP
#define NEARGYE_MAGIC_ENUM_FORMAT_HPP

// magic_enum.hpp is already embedded above.
// magic_enum_flags.hpp is already embedded above.

namespace magic_enum::detail {

template <typename E, std::enable_if_t<std::is_enum_v<std::decay_t<E>>, int> = 0>
std::string format_as(E e) {
  using D = std::decay_t<E>;
  static_assert(std::is_same_v<char, magic_enum::string_view::value_type>, "magic_enum::formatter requires string_view::value_type type same as char.");
  if constexpr (magic_enum::detail::supported<D>::value) {
    if constexpr (magic_enum::detail::subtype_v<D> == magic_enum::detail::enum_subtype::flags) {
      if (const auto name = magic_enum::enum_flags_name<D>(e); !name.empty()) {
        return {name.data(), name.size()};
      }
    } else {
      if (const auto name = magic_enum::enum_name<D>(e); !name.empty()) {
        return {name.data(), name.size()};
      }
    }
  }
  return std::to_string(magic_enum::enum_integer<D>(e));
}

} // namespace magic_enum::format

// DSLtk: formatting is optional on C++20 standard libraries.
#ifndef MAGIC_ENUM_USE_STD_MODULE
#if __has_include(<format>)
#include <format>
#endif
#endif

#if defined(__cpp_lib_format)

#ifndef MAGIC_ENUM_USE_STD_MODULE
#include <format>
#endif

template <typename E>
struct std::formatter<E, std::enable_if_t<std::is_enum_v<std::decay_t<E>>, char>> : std::formatter<std::string_view, char> {
  template <class FormatContext>
  auto format(E e, FormatContext& ctx) const {
    return std::formatter<std::string_view, char>::format(magic_enum::detail::format_as<E>(e), ctx);
  }
};

#endif

#if defined(FMT_VERSION)

template <typename E>
struct fmt::formatter<E, std::enable_if_t<std::is_enum_v<std::decay_t<E>>, char>> : fmt::formatter<std::string_view, char> {
  template <class FormatContext>
  auto format(E e, FormatContext& ctx) const {
    return fmt::formatter<std::string_view, char>::format(magic_enum::detail::format_as<E>(e), ctx);
  }
};

#endif

#endif // NEARGYE_MAGIC_ENUM_FORMAT_HPP
// Embedded upstream header: magic_enum_fuse.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_FUSE_HPP
#define NEARGYE_MAGIC_ENUM_FUSE_HPP

// magic_enum.hpp is already embedded above.

namespace magic_enum {

namespace detail {

template <typename E>
constexpr optional<std::uintmax_t> fuse_one_enum(optional<std::uintmax_t> hash, E value) noexcept {
  if (hash) {
    if (const auto index = enum_index(value)) {
      return (*hash << log2((enum_count<E>() << 1) - 1)) | *index;
    }
  }
  return {};
}

template <typename E>
constexpr optional<std::uintmax_t> fuse_enum(E value) noexcept {
  return fuse_one_enum(0, value);
}

template <typename E, typename... Es>
constexpr optional<std::uintmax_t> fuse_enum(E head, Es... tail) noexcept {
  return fuse_one_enum(fuse_enum(tail...), head);
}

template <typename... Es>
constexpr auto typesafe_fuse_enum(Es... values) noexcept {
  enum class enum_fuse_t : std::uintmax_t;
  const auto fuse = fuse_enum(values...);
  if (fuse) {
    return optional<enum_fuse_t>{static_cast<enum_fuse_t>(*fuse)};
  }
  return optional<enum_fuse_t>{};
}

} // namespace magic_enum::detail

// Returns a bijective mix of several enum values. This can be used to emulate 2D switch/case statements.
template <typename... Es>
[[nodiscard]] constexpr auto enum_fuse(Es... values) noexcept {
  static_assert((std::is_enum_v<std::decay_t<Es>> && ...), "magic_enum::enum_fuse requires enum type.");
  static_assert(sizeof...(Es) >= 2, "magic_enum::enum_fuse requires at least 2 values.");
  static_assert((detail::log2(enum_count<std::decay_t<Es>>() + 1) + ...) <= (sizeof(std::uintmax_t) * 8), "magic_enum::enum_fuse does not work for large enums");
#if defined(MAGIC_ENUM_NO_TYPESAFE_ENUM_FUSE)
  const auto fuse = detail::fuse_enum<std::decay_t<Es>...>(values...);
#else
  const auto fuse = detail::typesafe_fuse_enum<std::decay_t<Es>...>(values...);
#endif
  return MAGIC_ENUM_ASSERT(fuse), fuse;
}

} // namespace magic_enum

#endif // NEARGYE_MAGIC_ENUM_FUSE_HPP
// Embedded upstream header: magic_enum_iostream.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_IOSTREAM_HPP
#define NEARGYE_MAGIC_ENUM_IOSTREAM_HPP

// magic_enum.hpp is already embedded above.
// magic_enum_flags.hpp is already embedded above.

#ifndef MAGIC_ENUM_USE_STD_MODULE
#include <iosfwd>
#endif

namespace magic_enum {

namespace ostream_operators {

template <typename Char, typename Traits, typename E, detail::enable_if_t<E, int> = 0>
std::basic_ostream<Char, Traits>& operator<<(std::basic_ostream<Char, Traits>& os, E value) {
  using D = std::decay_t<E>;
  using U = underlying_type_t<D>;

  if constexpr (detail::supported<D>::value) {
    if constexpr (detail::subtype_v<D> == detail::enum_subtype::flags) {
      if (const auto name = enum_flags_name<D>(value); !name.empty()) {
        for (const auto c : name) {
          os.put(c);
        }
        return os;
      }
    } else {
      if (const auto name = enum_name<D>(value); !name.empty()) {
        for (const auto c : name) {
          os.put(c);
        }
        return os;
      }
    }
  }
  return (os << static_cast<U>(value));
}

template <typename Char, typename Traits, typename E, detail::enable_if_t<E, int> = 0>
std::basic_ostream<Char, Traits>& operator<<(std::basic_ostream<Char, Traits>& os, optional<E> value) {
  return value ? (os << *value) : os;
}

} // namespace magic_enum::ostream_operators

namespace istream_operators {

template <typename Char, typename Traits, typename E, detail::enable_if_t<E, int> = 0>
std::basic_istream<Char, Traits>& operator>>(std::basic_istream<Char, Traits>& is, E& value) {
  using D = std::decay_t<E>;

  std::basic_string<Char, Traits> s;
  is >> s;
  if constexpr (detail::supported<D>::value) {
    if constexpr (detail::subtype_v<D> == detail::enum_subtype::flags) {
      if (const auto v = enum_flags_cast<D>(s)) {
        value = *v;
      } else {
        is.setstate(std::basic_ios<Char>::failbit);
      }
    } else {
      if (const auto v = enum_cast<D>(s)) {
        value = *v;
      } else {
        is.setstate(std::basic_ios<Char>::failbit);
      }
    }
  } else {
    is.setstate(std::basic_ios<Char>::failbit);
  }
  return is;
}

} // namespace magic_enum::istream_operators

namespace iostream_operators {

using magic_enum::ostream_operators::operator<<;
using magic_enum::istream_operators::operator>>;

} // namespace magic_enum::iostream_operators

} // namespace magic_enum

#endif // NEARGYE_MAGIC_ENUM_IOSTREAM_HPP
// Embedded upstream header: magic_enum_switch.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_SWITCH_HPP
#define NEARGYE_MAGIC_ENUM_SWITCH_HPP

// magic_enum.hpp is already embedded above.

namespace magic_enum {

namespace detail {

struct default_result_type {};

template <typename T>
struct identity {
  using type = T;
};

struct nonesuch {};

template <typename F, typename V, bool = std::is_invocable_v<F, V>>
struct invoke_result : identity<nonesuch> {};

template <typename F, typename V>
struct invoke_result<F, V, true> : std::invoke_result<F, V> {};

template <typename F, typename V>
using invoke_result_t = typename invoke_result<F, V>::type;

template <typename E, enum_subtype S, typename F, std::size_t... I>
constexpr auto common_invocable(std::index_sequence<I...>) noexcept {
  static_assert(std::is_enum_v<E>, "magic_enum::detail::invocable_index requires enum type.");

  if constexpr (count_v<E, S> == 0) {
    return identity<nonesuch>{};
  } else {
    return std::common_type<invoke_result_t<F, enum_constant<values_v<E, S>[I]>>...>{};
  }
}

template <typename E, enum_subtype S, typename Result, typename F>
constexpr auto result_type() noexcept {
  static_assert(std::is_enum_v<E>, "magic_enum::detail::result_type requires enum type.");

  constexpr auto seq = std::make_index_sequence<count_v<E, S>>{};
  using R = typename decltype(common_invocable<E, S, F>(seq))::type;
  if constexpr (std::is_same_v<Result, default_result_type>) {
    if constexpr (std::is_same_v<R, nonesuch>) {
      return identity<void>{};
    } else {
      return identity<R>{};
    }
  } else {
    if constexpr (std::is_convertible_v<R, Result>) {
      return identity<Result>{};
    } else if constexpr (std::is_convertible_v<Result, R>) {
      return identity<R>{};
    } else {
      return identity<nonesuch>{};
    }
  }
}

template <typename E, enum_subtype S, typename Result, typename F, typename D = std::decay_t<E>, typename R = typename decltype(result_type<D, S, Result, F>())::type>
using result_t = std::enable_if_t<std::is_enum_v<D> && !std::is_same_v<R, nonesuch>, R>;

#if !defined(MAGIC_ENUM_ENABLE_HASH) && !defined(MAGIC_ENUM_ENABLE_HASH_SWITCH)

template <typename T = void>
inline constexpr auto default_result_type_lambda = []() noexcept(std::is_nothrow_default_constructible_v<T>) { return T{}; };

template <>
inline constexpr auto default_result_type_lambda<void> = []() noexcept {};

template <std::size_t I, std::size_t End, typename R, typename E, enum_subtype S, typename F, typename Def>
constexpr decltype(auto) constexpr_switch_impl(F&& f, E value, Def&& def) {
  if constexpr(I < End) {
    constexpr auto v = enum_constant<enum_value<E, I, S>()>{};
    if (value == v) {
      if constexpr (std::is_invocable_r_v<R, F, decltype(v)>) {
        return static_cast<R>(std::forward<F>(f)(v));
      } else {
        return def();
      }
    } else {
      return constexpr_switch_impl<I + 1, End, R, E, S>(std::forward<F>(f), value, std::forward<Def>(def));
    }
  } else {
    return def();
  }
}

template <typename R, typename E, enum_subtype S, typename F, typename Def>
constexpr decltype(auto) constexpr_switch(F&& f, E value, Def&& def) {
  static_assert(is_enum_v<E>, "magic_enum::detail::constexpr_switch requires enum type.");

  if constexpr (count_v<E, S> == 0) {
    return def();
  } else {
    return constexpr_switch_impl<0, count_v<E, S>, R, E, S>(std::forward<F>(f), value, std::forward<Def>(def));
  }
}
#endif

} // namespace magic_enum::detail

template <typename Result = detail::default_result_type, typename E, detail::enum_subtype S = detail::subtype_v<E>, typename F, typename R = detail::result_t<E, S, Result, F>>
constexpr decltype(auto) enum_switch(F&& f, E value) {
  using D = std::decay_t<E>;
  static_assert(std::is_enum_v<D>, "magic_enum::enum_switch requires enum type.");
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

#if defined(MAGIC_ENUM_ENABLE_HASH) || defined(MAGIC_ENUM_ENABLE_HASH_SWITCH)
  return detail::constexpr_switch<&detail::values_v<D, S>, detail::case_call_t::value>(
      std::forward<F>(f),
      value,
      detail::default_result_type_lambda<R>);
#else
  return detail::constexpr_switch<R, D, S>(
      std::forward<F>(f),
      value,
      detail::default_result_type_lambda<R>);
#endif
}

template <typename Result = detail::default_result_type, detail::enum_subtype S, typename E, typename F, typename R = detail::result_t<E, S, Result, F>>
constexpr decltype(auto) enum_switch(F&& f, E value) {
  return enum_switch<Result, E, S>(std::forward<F>(f), value);
}

template <typename Result, typename E, detail::enum_subtype S = detail::subtype_v<E>, typename F, typename R = detail::result_t<E, S, Result, F>>
constexpr decltype(auto) enum_switch(F&& f, E value, Result&& result) {
  using D = std::decay_t<E>;
  static_assert(std::is_enum_v<D>, "magic_enum::enum_switch requires enum type.");
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");

#if defined(MAGIC_ENUM_ENABLE_HASH) || defined(MAGIC_ENUM_ENABLE_HASH_SWITCH)
  return detail::constexpr_switch<&detail::values_v<D, S>, detail::case_call_t::value>(
      std::forward<F>(f),
      value,
      [&result]() -> R { return std::forward<Result>(result); });
#else
  return detail::constexpr_switch<R, D, S>(
      std::forward<F>(f),
      value,
      [&result]() -> R { return std::forward<Result>(result); });
#endif
}

template <typename Result, detail::enum_subtype S, typename E, typename F, typename R = detail::result_t<E, S, Result, F>>
constexpr decltype(auto) enum_switch(F&& f, E value, Result&& result) {
  return enum_switch<Result, E, S>(std::forward<F>(f), value, std::forward<Result>(result));
}

} // namespace magic_enum

template <>
struct std::common_type<magic_enum::detail::nonesuch, magic_enum::detail::nonesuch> : magic_enum::detail::identity<magic_enum::detail::nonesuch> {};

template <typename T>
struct std::common_type<T, magic_enum::detail::nonesuch> : magic_enum::detail::identity<T> {};

template <typename T>
struct std::common_type<magic_enum::detail::nonesuch, T> : magic_enum::detail::identity<T> {};

#endif // NEARGYE_MAGIC_ENUM_SWITCH_HPP
// Embedded upstream header: magic_enum_utility.hpp
//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.8
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_UTILITY_HPP
#define NEARGYE_MAGIC_ENUM_UTILITY_HPP

// magic_enum.hpp is already embedded above.

namespace magic_enum {

namespace detail {

template <typename E, enum_subtype S, typename F, std::size_t... I>
constexpr auto for_each(F&& f, std::index_sequence<I...>) {
  constexpr bool has_void_return = (std::is_void_v<std::invoke_result_t<F, enum_constant<values_v<E, S>[I]>>> || ...);
  constexpr bool all_same_return = (std::is_same_v<std::invoke_result_t<F, enum_constant<values_v<E, S>[0]>>, std::invoke_result_t<F, enum_constant<values_v<E, S>[I]>>> && ...);

  if constexpr (has_void_return) {
    (f(enum_constant<values_v<E, S>[I]>{}), ...);
  } else if constexpr (all_same_return) {
    return std::array{f(enum_constant<values_v<E, S>[I]>{})...};
  } else {
    return std::tuple{f(enum_constant<values_v<E, S>[I]>{})...};
  }
}

template <typename E, enum_subtype S, typename F,std::size_t... I>
constexpr bool all_invocable(std::index_sequence<I...>) {
  if constexpr (count_v<E, S> == 0) {
    return false;
  } else {
    return (std::is_invocable_v<F, enum_constant<values_v<E, S>[I]>> && ...);
  }
}

} // namespace magic_enum::detail

template <typename E, detail::enum_subtype S = detail::subtype_v<E>, typename F, detail::enable_if_t<E, int> = 0>
constexpr auto enum_for_each(F&& f) {
  using D = std::decay_t<E>;
  static_assert(std::is_enum_v<D>, "magic_enum::enum_for_each requires enum type.");
  static_assert(detail::is_reflected_v<D, S>, "magic_enum requires enum implementation and valid max and min.");
  constexpr auto sep = std::make_index_sequence<detail::count_v<D, S>>{};

  if constexpr (detail::all_invocable<D, S, F>(sep)) {
    return detail::for_each<D, S>(std::forward<F>(f), sep);
  } else {
    static_assert(detail::always_false_v<D>, "magic_enum::enum_for_each requires invocable of all enum value.");
  }
}

template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_next_value(E value, std::ptrdiff_t n = 1) noexcept -> detail::enable_if_t<E, optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  constexpr std::ptrdiff_t count = detail::count_v<D, S>;

  if (const auto i = enum_index<D, S>(value)) {
    const std::ptrdiff_t index = (static_cast<std::ptrdiff_t>(*i) + n);
    if (index >= 0 && index < count) {
      return enum_value<D, S>(static_cast<std::size_t>(index));
    }
  }
  return {};
}

template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_next_value_circular(E value, std::ptrdiff_t n = 1) noexcept -> detail::enable_if_t<E, std::decay_t<E>> {
  using D = std::decay_t<E>;
  constexpr std::ptrdiff_t count = detail::count_v<D, S>;

  if (const auto i = enum_index<D, S>(value)) {
    const std::ptrdiff_t index = ((((static_cast<std::ptrdiff_t>(*i) + n) % count) + count) % count);
    if (index >= 0 && index < count) {
      return enum_value<D, S>(static_cast<std::size_t>(index));
    }
  }
  return MAGIC_ENUM_ASSERT(false), value;
}

template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_prev_value(E value, std::ptrdiff_t n = 1) noexcept -> detail::enable_if_t<E, optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  constexpr std::ptrdiff_t count = detail::count_v<D, S>;

  if (const auto i = enum_index<D, S>(value)) {
    const std::ptrdiff_t index = (static_cast<std::ptrdiff_t>(*i) - n);
    if (index >= 0 && index < count) {
      return enum_value<D, S>(static_cast<std::size_t>(index));
    }
  }
  return {};
}

template <typename E, detail::enum_subtype S = detail::subtype_v<E>>
[[nodiscard]] constexpr auto enum_prev_value_circular(E value, std::ptrdiff_t n = 1) noexcept -> detail::enable_if_t<E, std::decay_t<E>> {
  using D = std::decay_t<E>;
  constexpr std::ptrdiff_t count = detail::count_v<D, S>;

  if (const auto i = enum_index<D, S>(value)) {
    const std::ptrdiff_t index = ((((static_cast<std::ptrdiff_t>(*i) - n) % count) + count) % count);
    if (index >= 0 && index < count) {
      return enum_value<D, S>(static_cast<std::size_t>(index));
    }
  }
  return MAGIC_ENUM_ASSERT(false), value;
}

} // namespace magic_enum

#endif // NEARGYE_MAGIC_ENUM_UTILITY_HPP

#endif // NEARGYE_MAGIC_ENUM_ALL_HPP
// END DSLTK EMBEDDED MAGIC_ENUM

namespace dsl
{

// ============================================================
//  Enum reflection: magic_enum-compatible public API
// ============================================================

/// Complete magic_enum API, also available directly through ::magic_enum.
namespace enum_reflection = ::magic_enum;
namespace customize = ::magic_enum::customize;
namespace containers = ::magic_enum::containers;
namespace bitwise_operators = ::magic_enum::bitwise_operators;
namespace iostream_operators = ::magic_enum::iostream_operators;
namespace istream_operators = ::magic_enum::istream_operators;
namespace ostream_operators = ::magic_enum::ostream_operators;

using ::magic_enum::Enum;
using ::magic_enum::as_common;
using ::magic_enum::as_flags;
using ::magic_enum::case_insensitive;
using ::magic_enum::enum_cast;
using ::magic_enum::enum_constant;
using ::magic_enum::enum_contains;
using ::magic_enum::enum_count;
using ::magic_enum::enum_entries;
using ::magic_enum::enum_flags_cast;
using ::magic_enum::enum_flags_contains;
using ::magic_enum::enum_flags_name;
using ::magic_enum::enum_flags_test;
using ::magic_enum::enum_flags_test_any;
using ::magic_enum::enum_for_each;
using ::magic_enum::enum_fuse;
using ::magic_enum::enum_index;
using ::magic_enum::enum_integer;
using ::magic_enum::enum_name;
using ::magic_enum::enum_names;
using ::magic_enum::enum_next_value;
using ::magic_enum::enum_next_value_circular;
using ::magic_enum::enum_prev_value;
using ::magic_enum::enum_prev_value_circular;
using ::magic_enum::enum_reflected;
using ::magic_enum::enum_switch;
using ::magic_enum::enum_type_name;
using ::magic_enum::enum_underlying;
using ::magic_enum::enum_value;
using ::magic_enum::enum_values;
using ::magic_enum::is_magic_enum_supported;
using ::magic_enum::is_scoped_enum;
using ::magic_enum::is_scoped_enum_v;
using ::magic_enum::is_unscoped_enum;
using ::magic_enum::is_unscoped_enum_v;
using ::magic_enum::underlying_type;
using ::magic_enum::underlying_type_t;

/// Whether an enum uses flags reflection (false for non-enum types).
template <typename T>
struct is_flags : std::bool_constant<[] {
  if constexpr (std::is_enum_v<std::decay_t<T>>)
    return ::magic_enum::detail::subtype_v<std::decay_t<T>> == as_flags<>;
  else
    return false;
} ()>
{
};

template <typename T>
inline constexpr bool is_flags_v = is_flags<T>::value;

/**
 * @brief Feature tag for enum-reflection integrations.
 *
 * Like PatternMatch, the feature uses free functions in namespace dsl.
 * All reflection helpers are also usable without a DSL base class.
 *
 *   struct Tokens : dsl::DSL<Tokens, dsl::EnumReflection> {};
 */
struct EnumReflection
{
  template <typename Derived> struct Mixin
  {
  };
};

// ============================================================
//  Utility: fixed-length string for use as NTTP
// ============================================================

/**
 * @brief Compile-time fixed-length string, usable as a non-type template
 * parameter.
 *
 * Allows string literals to be used as template arguments:
 *   dsl::leaf<"myTag">(value)
 *   dsl::pattern<"[0-9]+">()
 *
 * @tparam N Length of the string including null terminator.
 */
template <std::size_t N> struct FixedString
{
  char data[N]{};
  constexpr FixedString (const char (&s)[N]) { std::copy_n (s, N, data); }
  constexpr std::string_view
  view () const
  {
    return { data, N - 1 };
  }
  constexpr bool operator== (const FixedString &) const = default;
};

// ============================================================
//  Forward declarations
// ============================================================

struct Pipeline;
struct Operators;
struct PatternMatch;
struct AST;
struct Rewrite;
struct ExprTemplates;
struct CustomLiterals;
struct Memoization;
struct LazyFeature;
struct Monadic;
struct ResultFeature;
struct CombinatorParser;
struct TaskPipeline;

// ============================================================
//  CRTP base: DSL<Derived, Features...>
// ============================================================

/**
 * @brief CRTP base class for all user-defined DSLs.
 *
 * Inherit from this to compose features:
 *
 *   struct MyDSL : dsl::DSL<MyDSL, dsl::Pipeline, dsl::Operators> { ... };
 *
 * The Derived type gets access to all methods provided by each Feature.
 * Features are mixed in via multiple inheritance through FeatureMixin<>.
 *
 * @tparam Derived  The user's DSL class (CRTP pattern).
 * @tparam Features Zero or more feature tags (Pipeline, Operators, etc.).
 */
template <typename Derived, typename... Features>
struct DSL : Features::template Mixin<Derived>...
{
protected:
  constexpr Derived &
  self () noexcept
  {
    return static_cast<Derived &> (*this);
  }
  constexpr const Derived &
  self () const noexcept
  {
    return static_cast<const Derived &> (*this);
  }
};

// ============================================================
//  Predicate wrapper (used by Operators)
// ============================================================

/**
 * @brief Wraps a callable as a composable predicate.
 *
 * Supports &, |, ! composition.
 *
 * @tparam F Callable type: F(auto) -> bool
 *
 * Example:
 *   auto pos  = dsl::predicate([](int x){ return x > 0; });
 *   auto even = dsl::predicate([](int x){ return x % 2 == 0; });
 *   auto rule = pos & even;
 *   rule(4);   // true
 *   rule(3);   // false (odd)
 *   rule(-2);  // false (negative)
 */
template <typename F> struct Predicate
{
  F fn;
  explicit constexpr Predicate (F f) : fn (std::move (f)) {}

  template <typename T>
  constexpr bool
  operator() (T &&v) const
  {
    return fn (std::forward<T> (v));
  }

  /// Conjunction: both predicates must pass.
  template <typename G>
  constexpr auto
  operator& (Predicate<G> other) const
  {
    return Predicate{ [a = fn, b = other.fn] (auto &&v)
                        { return a (v) && b (v); } };
  }

  /// Disjunction: at least one predicate must pass.
  template <typename G>
  constexpr auto
  operator| (Predicate<G> other) const
  {
    return Predicate{ [a = fn, b = other.fn] (auto &&v)
                        { return a (v) || b (v); } };
  }

  /// Negation.
  constexpr auto
  operator!() const
  {
    return Predicate{ [a = fn] (auto &&v) { return !a (v); } };
  }
};

/**
 * @brief Factory for Predicate<F>.
 *
 * Example:
 *   auto is_even = dsl::predicate([](int x){ return x % 2 == 0; });
 */
template <typename F>
constexpr auto
predicate (F &&f)
{
  return Predicate<std::decay_t<F>>{ std::forward<F> (f) };
}

// ============================================================
//  Pipeline stage wrapper
// ============================================================

/**
 * @brief Wraps a callable as a pipeline stage for use with operator|.
 *
 * @tparam F Callable: F(T) -> U
 *
 * Example:
 *   auto double_it = dsl::pipe([](int x){ return x * 2; });
 *   auto result = 5 | double_it;  // 10
 */
template <typename F> struct PipeStage
{
  F fn;
  explicit constexpr PipeStage (F f) : fn (std::move (f)) {}
};

/**
 * @brief Factory for PipeStage<F>.
 *
 * Example:
 *   auto stage = dsl::pipe([](int x){ return x + 1; });
 *   auto r = 4 | stage;  // 5
 */
template <typename F>
constexpr auto
pipe (F &&f)
{
  return PipeStage<std::decay_t<F>>{ std::forward<F> (f) };
}

/// operator| for any value and a PipeStage.
template <typename T, typename F>
constexpr auto
operator| (T &&val, PipeStage<F> stage)
{
  return stage.fn (std::forward<T> (val));
}

// ============================================================
//  Feature: Pipeline
// ============================================================

/**
 * @brief Feature tag: adds pipeline (operator|) support to a DSL.
 *
 * Provides a wrap() method to lift a value into the pipeline.
 *
 * Usage:
 *   struct MyDSL : dsl::DSL<MyDSL, dsl::Pipeline> {};
 *   MyDSL m;
 *   auto r = m.wrap(10)
 *            | dsl::pipe([](int x){ return x * 2; })
 *            | dsl::pipe([](int x){ return x - 1; });
 *   // r == 19
 */
struct Pipeline
{
  template <typename Derived> struct Mixin
  {
    /**
     * @brief Lifts a value into the pipeline.
     * @param v Value to wrap.
     * @return v unchanged; subsequent | stages transform it.
     */
    template <typename T>
    static constexpr T
    wrap (T &&v)
    {
      return std::forward<T> (v);
    }
  };
};

// ============================================================
//  Feature: Operators
// ============================================================

/**
 * @brief Feature tag: adds predicate composition helpers to a DSL.
 *
 * Provides make_pred() as a named factory inside the DSL.
 *
 * Usage:
 *   struct MyDSL : dsl::DSL<MyDSL, dsl::Operators> {};
 *   auto rule = MyDSL::make_pred([](int x){ return x > 0; })
 *             & MyDSL::make_pred([](int x){ return x < 100; });
 *   rule(42);  // true
 *   rule(200); // false
 */
struct Operators
{
  template <typename Derived> struct Mixin
  {
    /**
     * @brief Creates a composable Predicate from a callable.
     * @param f Callable: f(T) -> bool
     */
    template <typename F>
    static constexpr auto
    make_pred (F &&f)
    {
      return dsl::predicate (std::forward<F> (f));
    }
  };
};

// ============================================================
//  PatternMatch: when / otherwise / match
// ============================================================

/**
 * @brief Compile-time string pattern tag for use with when<>.
 *
 * Wraps a FixedString so it can be used as a template argument
 * to distinguish pattern-based dispatch from value-based dispatch.
 *
 * @tparam S The pattern string (e.g., "[0-9]+").
 *
 * Example:
 *   dsl::when<dsl::pattern<"[0-9]+">>(handler)
 */
template <FixedString S> struct pattern
{
  static constexpr std::string_view value = S.view ();

  /**
   * @brief Minimal regex-like match: anchored prefix/suffix, literal chars,
   * [class], *, +.
   *
   * Supported syntax (subset):
   *   .       any character
   *   [abc]   character class
   *   [a-z]   range in class
   *   *       zero or more of previous
   *   +       one or more of previous
   *   ^       (ignored, match is always anchored at start)
   *   $       (ignored, match is always anchored at end)
   *
   * @param input String to test.
   * @return true if the entire input matches the pattern.
   */
  static constexpr bool
  matches (std::string_view input)
  {
    return match_here (S.view (), input);
  }

private:
  static constexpr bool
  match_class (std::string_view cls, char c)
  {
    for (std::size_t i = 0; i < cls.size ();)
      {
        if (i + 2 < cls.size () && cls[i + 1] == '-')
          {
            if (c >= cls[i] && c <= cls[i + 2])
              return true;
            i += 3;
          }
        else
          {
            if (cls[i] == c)
              return true;
            ++i;
          }
      }
    return false;
  }

  static constexpr bool
  match_one (char pat, std::string_view cls, char c)
  {
    if (pat == '[')
      return match_class (cls, c);
    if (pat == '.')
      return true;
    return pat == c;
  }

  static constexpr bool
  match_here (std::string_view pat, std::string_view str)
  {
    if (pat.empty ())
      return str.empty ();
    if (pat[0] == '^')
      return match_here (pat.substr (1), str);

    // Parse class [...]
    std::string_view cls;
    std::size_t pat_advance = 1;
    char first = pat[0];
    if (first == '[')
      {
        auto end = pat.find (']', 1);
        if (end == std::string_view::npos)
          return false;
        cls = pat.substr (1, end - 1);
        pat_advance = end + 1;
      }

    // Check for quantifier
    char quant = 0;
    if (pat_advance < pat.size ()
        && (pat[pat_advance] == '*' || pat[pat_advance] == '+'))
      quant = pat[pat_advance++];

    auto rest = pat.substr (pat_advance);

    if (quant == '*')
      {
        // Try matching zero occurrences first, then one or more
        if (match_here (rest, str))
          return true;
        while (!str.empty () && match_one (first, cls, str[0]))
          {
            str.remove_prefix (1);
            if (match_here (rest, str))
              return true;
          }
        return false;
      }
    if (quant == '+')
      {
        // One or more
        if (str.empty () || !match_one (first, cls, str[0]))
          return false;
        str.remove_prefix (1);
        while (!str.empty () && match_one (first, cls, str[0]))
          str.remove_prefix (1);
        return match_here (rest, str);
      }

    // Exactly one
    if (str.empty () || !match_one (first, cls, str[0]))
      return false;
    return match_here (rest, str.substr (1));
  }
};

// --- when<Key> clause ---

/**
 * @brief A single dispatch clause for dsl::match().
 *
 * Two specializations:
 *   when<EnumOrIntegralValue>(handler)  — value-based dispatch
 *   when<dsl::pattern<"...">>(handler)  — pattern-based dispatch
 *
 * @tparam Key  The value or pattern to match against.
 * @tparam F    Handler callable.
 */
template <auto Key, typename F> struct WhenClause
{
  F handler;
  using handler_type_or_void = F;
  explicit constexpr WhenClause (F f) : handler (std::move (f)) {}

  template <typename K, typename... Args>
  constexpr bool
  try_invoke (K key, std::optional<std::invoke_result_t<F, K, Args...>> &out,
              Args &&...args) const
  {
    using KeyT = std::remove_cvref_t<decltype (Key)>;
    if constexpr (requires { KeyT::matches (std::string_view{}); })
      {
        if constexpr (std::convertible_to<K, std::string_view>)
          {
            if (KeyT::matches (std::string_view (key)))
              {
                out = handler (key, std::forward<Args> (args)...);
                return true;
              }
          }
      }
    else if constexpr (std::equality_comparable_with<K, decltype (Key)>)
      {
        if (key == Key)
          {
            out = handler (key, std::forward<Args> (args)...);
            return true;
          }
      }
    return false;
  }
};

/**
 * @brief Factory for a when<Key> clause.
 *
 * Example (value-based):
 *   dsl::when<Color::Red>([](auto key){ return "red"; })
 *
 * Example (pattern-based):
 *   dsl::when<dsl::pattern<"[0-9]+">>(handler)
 */
template <auto Key, typename F>
constexpr auto
when (F &&f)
{
  return WhenClause<Key, std::decay_t<F>>{ std::forward<F> (f) };
}

// --- otherwise clause ---

/**
 * @brief Fallback clause for dsl::match() when no when<> matches.
 *
 * @tparam F Handler callable.
 *
 * Example:
 *   dsl::otherwise([](auto key){ return "unknown"; })
 */
template <typename F> struct OtherwiseClause
{
  F handler;
  using handler_type_or_void = F;
  explicit constexpr OtherwiseClause (F f) : handler (std::move (f)) {}

  template <typename K, typename... Args>
  constexpr bool
  try_invoke (K, std::optional<std::invoke_result_t<F, K, Args...>> &, Args &&...)
    const
  {
    return false;
  }
};

/**
 * @brief Factory for an otherwise clause.
 */
template <typename F>
constexpr auto
otherwise (F &&f)
{
  return OtherwiseClause<std::decay_t<F>>{ std::forward<F> (f) };
}

// --- MatchTable ---

/**
 * @brief Dispatch table built from when<>/otherwise clauses.
 *
 * Created by dsl::match(). Call operator()(key, args...) to dispatch.
 *
 * @tparam Clauses  Tuple of WhenClause and OtherwiseClause types.
 *
 * Example:
 *   auto table = dsl::match(
 *       dsl::when<1>([](auto){ return "one"; }),
 *       dsl::when<2>([](auto){ return "two"; }),
 *       dsl::otherwise([](auto){ return "other"; })
 *   );
 *   table(1, 0);  // "one"
 *   table(99, 0); // "other"
 */
template <typename... Clauses> struct MatchTable
{
  std::tuple<Clauses...> clauses;
  explicit constexpr MatchTable (Clauses... cs) : clauses (std::move (cs)...)
  {
  }

  template <typename Key, typename... Args>
  constexpr auto
  operator() (Key key, Args &&...args) const
  {
    using RetT = std::common_type_t<std::invoke_result_t<
        typename Clauses::handler_type_or_void, Key, Args...>...>;
    // Try each when<> clause in order
    std::optional<RetT> result;
    bool has_otherwise = false;
    bool matched = std::apply (
        [&] (const auto &...c)
          {
            return (c.try_invoke (key, result, std::forward<Args> (args)...)
                    || ...);
          },
        clauses);
    if (!matched)
      {
        // Find and invoke the otherwise clause
        std::apply (
            [&] (const auto &...c)
              {
                (([&] {
                   using Cls = std::remove_cvref_t<decltype (c)>;
                   if constexpr (std::is_same_v<
                                     Cls,
                                     OtherwiseClause<typename Cls::handler_type_or_void>>)
                     {
                       has_otherwise = true;
                       if (!result)
                         result = c.handler (key, std::forward<Args> (args)...);
                     }
                 }()),
                 ...);
              },
            clauses);
      }
    if (!result && !has_otherwise)
      throw std::runtime_error (
          "dsl::match: no clause matched and no otherwise");
    return *result;
  }
};

// Deduction guide
template <typename... Cs> MatchTable (Cs...) -> MatchTable<Cs...>;

/**
 * @brief Builds a MatchTable from when<>/otherwise clauses.
 *
 * Example:
 *   auto t = dsl::match(
 *       dsl::when<0>([](auto){ return "zero"; }),
 *       dsl::otherwise([](auto){ return "nonzero"; })
 *   );
 *   t(0, 0);  // "zero"
 *   t(5, 0);  // "nonzero"
 */
template <typename... Clauses>
constexpr auto
match (Clauses &&...cs)
{
  return MatchTable<std::decay_t<Clauses>...>{ std::forward<Clauses> (cs)... };
}

// ============================================================
//  Feature: PatternMatch
// ============================================================

/**
 * @brief Feature tag: adds pattern matching helpers to a DSL.
 *
 * Exposes dsl::match / dsl::when / dsl::otherwise as static methods
 * on the derived class for convenience.
 *
 * Usage:
 *   struct MyDSL : dsl::DSL<MyDSL, dsl::PatternMatch> {
 *       static constexpr auto table = dsl::match(
 *           dsl::when<1>([](auto){ return "one"; }),
 *           dsl::otherwise([](auto){ return "?"; })
 *       );
 *   };
 */
struct PatternMatch
{
  template <typename Derived> struct Mixin
  {
    // Feature presence marker; no additional methods needed since
    // dsl::match/when/otherwise are free functions in the dsl:: namespace.
  };
};

// ============================================================
//  AST: leaf / node / ASTNode
// ============================================================

/**
 * @brief A value-type AST node.
 *
 * Each node has a tag (compile-time string) and either:
 *   - A leaf value (string representation), or
 *   - A list of child nodes.
 *
 * Nodes are copyable, movable, and support dump() for S-expression output.
 */
class ASTNode
{
public:
  ASTNode () : tag_ (""), is_leaf_ (true) {}

  /// Construct a leaf node.
  ASTNode (std::string_view tag, std::string value)
      : tag_ (tag), value_ (std::move (value)), is_leaf_ (true)
  {
  }

  /// Construct an inner node with children.
  ASTNode (std::string_view tag, std::vector<ASTNode> children)
      : tag_ (tag), children_ (std::move (children)), is_leaf_ (false)
  {
  }

  /// @return The tag string of this node.
  std::string_view
  tag () const
  {
    return tag_;
  }

  /// @return true if this is a leaf (no children).
  bool
  is_leaf () const
  {
    return is_leaf_;
  }

  /// @return The leaf value (empty string if not a leaf).
  const std::string &
  value () const
  {
    return value_;
  }

  /// @return The children of this node (empty if leaf).
  const std::vector<ASTNode> &
  children () const
  {
    return children_;
  }

  /// @return Mutable reference to children.
  std::vector<ASTNode> &
  children ()
  {
    return children_;
  }

  /// @return The i-th child. Throws if out of range.
  const ASTNode &
  child (std::size_t i) const
  {
    return children_.at (i);
  }
  ASTNode &
  child (std::size_t i)
  {
    return children_.at (i);
  }

  /// @return Number of children.
  std::size_t
  size () const
  {
    return children_.size ();
  }

  /**
   * @brief Produces an S-expression string representation.
   *
   * Leaf:  "(tag value)"
   * Inner: "(tag child1 child2 ...)"
   *
   * Example:
   *   node<"add">(leaf<"num">(1), leaf<"num">(2)).dump()
   *   // => "(add (num 1) (num 2))"
   */
  std::string
  dump () const
  {
    std::ostringstream os;
    dump_impl (os);
    return os.str ();
  }

  bool
  operator== (const ASTNode &other) const
  {
    if (tag_ != other.tag_ || is_leaf_ != other.is_leaf_)
      return false;
    if (is_leaf_)
      return value_ == other.value_;
    return children_ == other.children_;
  }

private:
  void
  dump_impl (std::ostringstream &os) const
  {
    os << '(' << tag_;
    if (is_leaf_)
      {
        if (!value_.empty ())
          os << ' ' << value_;
      }
    else
      {
        for (auto &c : children_)
          {
            os << ' ';
            c.dump_impl (os);
          }
      }
    os << ')';
  }

  std::string tag_;
  std::string value_;
  std::vector<ASTNode> children_;
  bool is_leaf_ = true;
};

/**
 * @brief Creates a leaf AST node with the given tag and value.
 *
 * @tparam Tag Compile-time string tag.
 * @param val  The value (converted to string via ostringstream).
 * @return ASTNode leaf.
 *
 * Example:
 *   auto n = dsl::leaf<"num">(42);
 *   n.dump();  // "(num 42)"
 */
template <FixedString Tag, typename T>
ASTNode
leaf (T &&val)
{
  std::ostringstream os;
  os << std::forward<T> (val);
  return ASTNode{ Tag.view (), os.str () };
}

/// Overload for string_view values (avoids extra stream).
template <FixedString Tag>
ASTNode
leaf (std::string_view val)
{
  return ASTNode{ Tag.view (), std::string (val) };
}

/// Overload for const char* values.
template <FixedString Tag>
ASTNode
leaf (const char *val)
{
  return ASTNode{ Tag.view (), std::string (val) };
}

/**
 * @brief Creates an inner AST node with the given tag and children.
 *
 * @tparam Tag Compile-time string tag.
 * @param children  Variadic list of ASTNode children.
 * @return ASTNode inner node.
 *
 * Example:
 *   auto n = dsl::node<"add">(dsl::leaf<"x">(1), dsl::leaf<"y">(2));
 *   n.dump();  // "(add (x 1) (y 2))"
 */
template <FixedString Tag, typename... Children>
  requires (std::convertible_to<Children, ASTNode> && ...)
ASTNode
node (Children &&...children)
{
  std::vector<ASTNode> v;
  v.reserve (sizeof...(children));
  (v.emplace_back (std::forward<Children> (children)), ...);
  return ASTNode{ Tag.view (), std::move (v) };
}

// ============================================================
//  Feature: AST
// ============================================================

/**
 * @brief Feature tag: adds AST construction helpers to a DSL.
 *
 * Provides make_leaf() and make_node() as member functions.
 *
 * Usage:
 *   struct MyDSL : dsl::DSL<MyDSL, dsl::AST> {};
 *   MyDSL m;
 *   auto tree = dsl::node<"root">(dsl::leaf<"val">(42));
 */
struct AST
{
  template <typename Derived> struct Mixin
  {
    /**
     * @brief Creates a leaf node (convenience wrapper).
     */
    template <FixedString Tag, typename T>
    static ASTNode
    make_leaf (T &&val)
    {
      return dsl::leaf<Tag> (std::forward<T> (val));
    }

    /**
     * @brief Creates an inner node (convenience wrapper).
     */
    template <FixedString Tag, typename... Children>
      requires (std::convertible_to<Children, ASTNode> && ...)
    static ASTNode
    make_node (Children &&...children)
    {
      return dsl::node<Tag> (std::forward<Children> (children)...);
    }
  };
};

// ============================================================
//  Rewrite: rule / rewrite_set
// ============================================================

/**
 * @brief A single rewrite rule: predicate + transformer.
 *
 * @tparam Tag   Name of the rule (for debugging/logging).
 * @tparam Pred  Callable: Pred(const ASTNode&) -> bool
 * @tparam Trans Callable: Trans(const ASTNode&) -> ASTNode
 *
 * Example:
 *   auto r = dsl::rule<"elim-double-neg">(
 *       [](const ASTNode& n){ return n.tag()=="not" &&
 * n.child(0).tag()=="not"; },
 *       [](const ASTNode& n){ return n.child(0).child(0); }
 *   );
 */
template <FixedString Tag, typename Pred, typename Trans> struct RewriteRule
{
  Pred pred;
  Trans trans;
  static constexpr std::string_view name = Tag.view ();

  constexpr RewriteRule (Pred p, Trans t)
      : pred (std::move (p)), trans (std::move (t))
  {
  }

  /**
   * @brief Attempts to apply this rule to a node.
   * @param n The node to test and potentially transform.
   * @return The transformed node if pred matched, or std::nullopt.
   */
  std::optional<ASTNode>
  try_apply (const ASTNode &n) const
  {
    if (pred (n))
      return trans (n);
    return std::nullopt;
  }
};

/**
 * @brief Factory for RewriteRule.
 *
 * @tparam Tag  Rule name (FixedString NTTP).
 * @param pred  Predicate: pred(const ASTNode&) -> bool
 * @param trans Transformer: trans(const ASTNode&) -> ASTNode
 */
template <FixedString Tag, typename Pred, typename Trans>
constexpr auto
rule (Pred &&p, Trans &&t)
{
  return RewriteRule<Tag, std::decay_t<Pred>, std::decay_t<Trans>>{
    std::forward<Pred> (p), std::forward<Trans> (t)
  };
}

/**
 * @brief A set of rewrite rules applied repeatedly until fixpoint.
 *
 * @tparam Rules  Variadic list of RewriteRule types.
 *
 * Example:
 *   auto rules = dsl::rewrite_set(
 *       dsl::rule<"r1">(pred1, trans1),
 *       dsl::rule<"r2">(pred2, trans2)
 *   );
 *   auto optimized = rules.apply(tree);
 */
template <typename... Rules> struct RewriteSet
{
  std::tuple<Rules...> rules;
  std::size_t max_iterations = 100;

  explicit constexpr RewriteSet (Rules... rs) : rules (std::move (rs)...) {}

  /**
   * @brief Applies all rules bottom-up until no rule fires or max iterations
   * reached.
   * @param root The AST to rewrite.
   * @return The rewritten AST.
   */
  ASTNode
  apply (ASTNode root) const
  {
    for (std::size_t i = 0; i < max_iterations; ++i)
      {
        auto [changed, result] = rewrite_once (root);
        if (!changed)
          return result;
        root = std::move (result);
      }
    return root;
  }

private:
  /// Single bottom-up pass: rewrite children first, then try rules on the
  /// node.
  std::pair<bool, ASTNode>
  rewrite_once (const ASTNode &n) const
  {
    bool any_changed = false;

    // Recursively rewrite children
    std::vector<ASTNode> new_children;
    if (!n.is_leaf ())
      {
        new_children.reserve (n.children ().size ());
        for (auto &child : n.children ())
          {
            auto [changed, rewritten] = rewrite_once (child);
            any_changed |= changed;
            new_children.push_back (std::move (rewritten));
          }
      }

    ASTNode current
        = n.is_leaf () ? n : ASTNode{ n.tag (), std::move (new_children) };

    // Try each rule on the current node
    std::apply (
        [&] (const auto &...r)
          {
            (
                [&]
                  {
                    if (auto result = r.try_apply (current))
                      {
                        current = std::move (*result);
                        any_changed = true;
                      }
                  }(),
                ...);
          },
        rules);

    return { any_changed, current };
  }
};

/**
 * @brief Factory for RewriteSet.
 *
 * Example:
 *   auto rs = dsl::rewrite_set(rule1, rule2, rule3);
 *   auto tree2 = rs.apply(tree1);
 */
template <typename... Rules>
constexpr auto
rewrite_set (Rules &&...rs)
{
  return RewriteSet<std::decay_t<Rules>...>{ std::forward<Rules> (rs)... };
}

// ============================================================
//  Feature: Rewrite
// ============================================================

/**
 * @brief Feature tag: adds rewrite capabilities to a DSL.
 *
 * Provides a rewrite() convenience method.
 *
 * Usage:
 *   struct MyDSL : dsl::DSL<MyDSL, dsl::AST, dsl::Rewrite> {
 *       static inline auto rules = dsl::rewrite_set(...);
 *   };
 *   MyDSL m;
 *   auto optimized = m.rewrite(tree);
 */
struct Rewrite
{
  template <typename Derived> struct Mixin
  {
    /**
     * @brief Applies the Derived::rules rewrite set to the given tree.
     * @param tree The AST to rewrite.
     * @return Rewritten AST.
     *
     * Requires that Derived defines a static `rules` member of type
     * RewriteSet.
     */
    ASTNode
    rewrite (const ASTNode &tree) const
      requires requires { Derived::rules; }
    {
      return Derived::rules.apply (tree);
    }
  };
};

// ============================================================
//  ExprTemplates: lazy expression trees
// ============================================================

/**
 * @brief Tag type indicating a terminal (leaf) in an expression template tree.
 */
struct ExprTerminal
{
};

/**
 * @brief Binary expression node in an expression template tree.
 *
 * @tparam Op  Operation tag (e.g., std::plus<>, std::multiplies<>).
 * @tparam L   Left operand type.
 * @tparam R   Right operand type.
 *
 * Evaluation is deferred until .eval() is called.
 */
template <typename Op, typename L, typename R> struct BinExpr
{
  L lhs;
  R rhs;

  constexpr BinExpr (L l, R r) : lhs (std::move (l)), rhs (std::move (r)) {}

  /**
   * @brief Evaluates the expression tree recursively.
   *
   * For terminal nodes, returns the stored value.
   * For binary nodes, evaluates both sides and applies Op.
   */
  constexpr auto
  eval () const
  {
    auto l = eval_operand (lhs);
    auto r = eval_operand (rhs);
    return Op{}(l, r);
  }

private:
  template <typename T>
  static constexpr auto
  eval_operand (const T &operand)
  {
    if constexpr (requires { operand.eval (); })
      {
        return operand.eval ();
      }
    else if constexpr (requires { operand.expr_value (); })
      {
        return operand.expr_value ();
      }
    else
      {
        return operand;
      }
  }
};

/**
 * @brief Unary expression node.
 *
 * @tparam Op  Operation tag (e.g., std::negate<>).
 * @tparam E   Operand type.
 */
template <typename Op, typename E> struct UnaryExpr
{
  E operand;

  explicit constexpr UnaryExpr (E e) : operand (std::move (e)) {}

  constexpr auto
  eval () const
  {
    if constexpr (requires { operand.eval (); })
      {
        return Op{}(operand.eval ());
      }
    else if constexpr (requires { operand.expr_value (); })
      {
        return Op{}(operand.expr_value ());
      }
    else
      {
        return Op{}(operand);
      }
  }
};

// ============================================================
//  Feature: ExprTemplates
// ============================================================

/**
 * @brief Feature tag: adds expression template operators to a DSL.
 *
 * The derived class must provide an expr_value() method that returns
 * the underlying value for evaluation.
 *
 * Provides: operator+, operator-, operator*, operator/ (binary)
 *           operator- (unary negation)
 *
 * Usage:
 *   struct Scalar : dsl::DSL<Scalar, dsl::ExprTemplates> {
 *       double val;
 *       double expr_value() const { return val; }
 *   };
 *   Scalar a{3.0}, b{4.0};
 *   auto expr = a + b * a;   // lazy: no computation
 *   double r = expr.eval();  // 3.0 + 4.0*3.0 = 15.0
 */
struct ExprTemplates
{
  template <typename Derived> struct Mixin
  {
    /// operator+ (binary)
    template <typename R>
    friend constexpr auto
    operator+ (const Derived &lhs, const R &rhs)
    {
      return BinExpr<std::plus<>, Derived, R>{ lhs, rhs };
    }

    /// operator- (binary)
    template <typename R>
    friend constexpr auto
    operator- (const Derived &lhs, const R &rhs)
    {
      return BinExpr<std::minus<>, Derived, R>{ lhs, rhs };
    }

    /// operator* (binary)
    template <typename R>
    friend constexpr auto
    operator* (const Derived &lhs, const R &rhs)
    {
      return BinExpr<std::multiplies<>, Derived, R>{ lhs, rhs };
    }

    /// operator/ (binary)
    template <typename R>
    friend constexpr auto
    operator/ (const Derived &lhs, const R &rhs)
    {
      return BinExpr<std::divides<>, Derived, R>{ lhs, rhs };
    }

    /// Unary negation
    friend constexpr auto
    operator- (const Derived &e)
    {
      return UnaryExpr<std::negate<>, Derived>{ e };
    }
  };
};

// Also allow BinExpr on the left side of further operations:

template <typename Op1, typename L1, typename R1, typename R>
constexpr auto
operator+ (const BinExpr<Op1, L1, R1> &lhs, const R &rhs)
{
  return BinExpr<std::plus<>, BinExpr<Op1, L1, R1>, R>{ lhs, rhs };
}

template <typename Op1, typename L1, typename R1, typename R>
constexpr auto
operator- (const BinExpr<Op1, L1, R1> &lhs, const R &rhs)
{
  return BinExpr<std::minus<>, BinExpr<Op1, L1, R1>, R>{ lhs, rhs };
}

template <typename Op1, typename L1, typename R1, typename R>
constexpr auto
operator* (const BinExpr<Op1, L1, R1> &lhs, const R &rhs)
{
  return BinExpr<std::multiplies<>, BinExpr<Op1, L1, R1>, R>{ lhs, rhs };
}

template <typename Op1, typename L1, typename R1, typename R>
constexpr auto
operator/ (const BinExpr<Op1, L1, R1> &lhs, const R &rhs)
{
  return BinExpr<std::divides<>, BinExpr<Op1, L1, R1>, R>{ lhs, rhs };
}

// ============================================================
//  CustomLiterals: literal suffix registration
// ============================================================

/**
 * @brief A single literal suffix entry.
 *
 * @tparam Suffix  The suffix string (e.g., "_km").
 * @tparam F       Callable: F(long double) -> T
 */
template <FixedString Suffix, typename F> struct LiteralEntry
{
  F handler;
  static constexpr std::string_view suffix = Suffix.view ();

  explicit constexpr LiteralEntry (F f) : handler (std::move (f)) {}
};

/**
 * @brief Factory for LiteralEntry.
 *
 * @tparam Suffix  The suffix string (FixedString NTTP).
 * @param f        Handler: f(long double) -> T
 *
 * Example:
 *   dsl::lit<"_km">([](long double v){ return v * 1000.0L; })
 */
template <FixedString Suffix, typename F>
constexpr auto
lit (F &&f)
{
  return LiteralEntry<Suffix, std::decay_t<F>>{ std::forward<F> (f) };
}

/**
 * @brief A set of literal suffix entries for runtime parsing.
 *
 * @tparam Entries  Variadic list of LiteralEntry types.
 *
 * Example:
 *   constexpr auto lits = dsl::literal_set(
 *       dsl::lit<"_km">([](long double v){ return v * 1000.0L; }),
 *       dsl::lit<"_m"> ([](long double v){ return v; })
 *   );
 *   auto val = lits.parse("3.5_km");  // 3500.0L
 */
template <typename... Entries> struct LiteralSet
{
  std::tuple<Entries...> entries;

  explicit constexpr LiteralSet (Entries... es) : entries (std::move (es)...)
  {
  }

  /**
   * @brief Parses a string like "3.5_km" by finding the matching suffix.
   *
   * @param input  The full literal string (e.g., "3.5_km").
   * @return The result of applying the matched handler to the numeric part.
   * @throws std::runtime_error if no suffix matches.
   */
  long double
  parse (std::string_view input) const
  {
    long double result = 0.0L;
    bool found = false;

    std::apply (
        [&] (const auto &...e)
          {
            (
                [&]
                  {
                    if (found)
                      return;
                    auto suf = e.suffix;
                    if (input.size () > suf.size ()
                        && input.substr (input.size () - suf.size ()) == suf)
                      {
                        auto num_str
                            = input.substr (0, input.size () - suf.size ());
                        long double num = std::stold (std::string (num_str));
                        result = e.handler (num);
                        found = true;
                      }
                  }(),
                ...);
          },
        entries);

    if (!found)
      {
        throw std::runtime_error (
            std::string ("dsl::LiteralSet::parse: no suffix matched for '")
            + std::string (input) + "'");
      }
    return result;
  }
};

/**
 * @brief Factory for LiteralSet.
 *
 * Example:
 *   auto ls = dsl::literal_set(
 *       dsl::lit<"_km">([](long double v){ return v * 1000.0L; }),
 *       dsl::lit<"_cm">([](long double v){ return v / 100.0L; })
 *   );
 */
template <typename... Entries>
constexpr auto
literal_set (Entries &&...es)
{
  return LiteralSet<std::decay_t<Entries>...>{ std::forward<Entries> (es)... };
}

// ============================================================
//  Feature: CustomLiterals
// ============================================================

/**
 * @brief Feature tag: adds literal parsing support to a DSL.
 *
 * The derived class must define a static `literals` member of type LiteralSet.
 *
 * Usage:
 *   struct UnitsDSL : dsl::DSL<UnitsDSL, dsl::CustomLiterals> {
 *       static constexpr auto literals = dsl::literal_set(
 *           dsl::lit<"_km">([](long double v){ return v * 1000.0L; }),
 *           dsl::lit<"_m"> ([](long double v){ return v; })
 *       );
 *   };
 *   auto val = UnitsDSL::parse_literal("5.0_km");  // 5000.0
 */
struct CustomLiterals
{
  template <typename Derived> struct Mixin
  {
    /**
     * @brief Parses a literal string using the Derived::literals set.
     * @param input  String like "3.5_km".
     * @return Parsed value.
     */
    static long double
    parse_literal (std::string_view input)
      requires requires { Derived::literals; }
    {
      return Derived::literals.parse (input);
    }
  };
};

/**
 * @brief Feature tag: adds cache invalidation helper for memoized DSL state.
 *
 * The derived DSL is expected to define `memo_cache` (typically an
 * unordered_map). clear_cache() erases all entries. Thread-safety is
 * intentionally not provided.
 */
struct Memoization
{
  template <typename Derived> struct Mixin
  {
    /// Clears all cached entries stored in `Derived::memo_cache`.
    void
    clear_cache ()
    {
      static_cast<Derived &> (*this).memo_cache.clear ();
    }
  };
};
/** @brief Feature tag for lazy-evaluation related DSL integrations. */
struct LazyFeature
{
  template <typename Derived> struct Mixin
  {
  };
};
/** @brief Feature tag for monadic combinator integrations. */
struct Monadic
{
  template <typename Derived> struct Mixin
  {
  };
};
/** @brief Feature tag for Result-based error-flow integrations. */
struct ResultFeature
{
  template <typename Derived> struct Mixin
  {
  };
};
/** @brief Feature tag for combinatory parser integrations. */
struct CombinatorParser
{
  template <typename Derived> struct Mixin
  {
  };
};
/** @brief Feature tag for task-pipeline integrations. */
struct TaskPipeline
{
  template <typename Derived> struct Mixin
  {
  };
};

/**
 * @brief Wraps a callable with a memoization cache keyed by the first
 * argument.
 *
 * This helper is suitable for expensive pure functions where repeated calls
 * with the same key should return cached results. The key type must be
 * hashable by `std::hash<Key>`. This helper is intentionally not thread-safe.
 *
 * @tparam Key Cache key type.
 * @tparam Value Cached value type.
 * @tparam Fn Callable type with signature compatible with `Value(const Key&)`.
 * @param fn Callable to memoize.
 * @return Callable object that performs memoized lookup.
 *
 * Example:
 *   auto fib = dsl::memoize<int, long long>([](int n){ return n < 2 ? n : 1LL;
 * });
 */
template <typename Key, typename Value, typename Fn> class MemoizedCallable
{
public:
  /// Constructs a memoized callable wrapper.
  explicit MemoizedCallable (Fn fn) : fn_ (std::move (fn)) {}
  /// Evaluates or returns cached value.
  Value
  operator() (const Key &key)
  {
    if (auto it = cache_.find (key); it != cache_.end ())
      return it->second;
    auto value = fn_ (key);
    cache_.emplace (key, value);
    return value;
  }
  /// Clears the memoization cache.
  void
  clear_cache ()
  {
    cache_.clear ();
  }

private:
  Fn fn_;
  std::unordered_map<Key, Value> cache_{};
};
template <typename Key, typename Value, typename Fn>
auto
memoize (Fn &&fn)
{
  return MemoizedCallable<Key, Value, std::decay_t<Fn>>{ std::forward<Fn> (
      fn) };
}

/**
 * @brief Deferred single-shot computation wrapper.
 *
 * Lazy<T> stores a callable and evaluates it only on first access through
 * get()/force(). The computed value is cached for subsequent reads.
 * Unlike ExprTemplates, this models one deferred computation, not an
 * expression tree. This type is not thread-safe by default.
 *
 * @tparam T Value produced by the deferred computation.
 *
 * Example:
 *   dsl::Lazy<std::string> cfg([]{ return std::string("loaded"); });
 *   auto& v = cfg.get();
 */
template <typename T> class Lazy
{
public:
  /// Creates lazy value from producer callable.
  explicit Lazy (std::function<T ()> producer)
      : producer_ (std::move (producer))
  {
  }
  /// Computes the value on first access and returns cached reference.
  T &
  get ()
  {
    if (!value_)
      value_ = producer_ ();
    return *value_;
  }
  /// Alias of get().
  T &
  force ()
  {
    return get ();
  }
  /// Indicates whether value has already been computed.
  bool
  is_computed () const noexcept
  {
    return value_.has_value ();
  }

private:
  std::function<T ()> producer_;
  std::optional<T> value_{};
};

/**
 * @brief Optional-like wrapper with monadic method chaining.
 *
 * This type wraps std::optional semantics and adds fluent
 * map/flat_map/filter/or_else. Monadic laws (left identity/right
 * identity/associativity) apply when map/flat_map functions are pure and
 * side-effect free.
 *
 * @tparam T Contained value type.
 *
 * Example:
 *   auto out = dsl::Maybe<int>(4).map([](int x){ return x + 1; }).or_else(0);
 */
template <typename T> class Maybe
{
public:
  /// Constructs empty Maybe.
  Maybe () = default;
  /// Constructs Maybe with a value.
  Maybe (T value) : value_ (std::move (value)) {}
  /// Constructs Maybe from std::optional.
  Maybe (std::optional<T> value) : value_ (std::move (value)) {}
  /// Checks whether a value is present.
  bool
  has_value () const noexcept
  {
    return value_.has_value ();
  }
  /// Returns underlying std::optional.
  const std::optional<T> &
  as_optional () const
  {
    return value_;
  }
  /// Applies function to contained value, if present.
  /// Applies function returning Maybe-like type, if value is present.
  template <typename Fn>
  auto
  map (Fn &&fn) const -> Maybe<decltype (fn (std::declval<T> ()))>
  {
    using U = decltype (fn (*value_));
    if (!value_)
      return Maybe<U>{};
    return Maybe<U>{ fn (*value_) };
  }
  template <typename Fn>
  auto
  flat_map (Fn &&fn) const -> decltype (fn (std::declval<T> ()))
  {
    using R = decltype (fn (*value_));
    if (!value_)
      return R{};
    return fn (*value_);
  }
  /// Retains value only if predicate passes.
  template <typename Pred>
  Maybe
  filter (Pred &&pred) const
  {
    if (value_ && pred (*value_))
      return *this;
    return Maybe{};
  }
  /// Returns contained value or fallback.
  T
  or_else (T fallback) const
  {
    return value_.value_or (std::move (fallback));
  }

private:
  std::optional<T> value_{};
};
template <typename Opt, typename Fn>
auto
map (const Opt &opt, Fn &&fn) -> std::optional<decltype (fn (*opt))>
{
  if (!opt)
    return std::nullopt;
  return fn (*opt);
}
template <typename Opt, typename Fn>
auto
flat_map (const Opt &opt, Fn &&fn) -> decltype (fn (*opt))
{
  using R = decltype (fn (*opt));
  if (!opt)
    return R{};
  return fn (*opt);
}
template <typename Opt, typename Pred>
auto
filter (const Opt &opt, Pred &&pred) -> Opt
{
  if (opt && pred (*opt))
    return opt;
  return std::nullopt;
}
template <typename Opt, typename T>
T
or_else (const Opt &opt, T fallback)
{
  return opt.value_or (std::move (fallback));
}

template <typename T, typename E> class Result
{
public:
  /// Constructs an Ok result.
  Result (T value) : data_ (std::in_place_index<0>, std::move (value)) {}
  /// Constructs an Err result (disabled when T == E to avoid ambiguity).
  Result (E error)
    requires (!std::same_as<T, E>)
      : data_ (std::in_place_index<1>, std::move (error))
  {
  }
  /// True when value branch is active.
  bool
  is_ok () const
  {
    return std::holds_alternative<T> (data_);
  }
  /// True when error branch is active.
  bool
  is_err () const
  {
    return std::holds_alternative<E> (data_);
  }
  /// Returns value or throws if Err.
  T
  unwrap () const
  {
    if (!is_ok ())
      throw std::runtime_error ("dsl::Result::unwrap called on Err");
    return std::get<T> (data_);
  }
  /// Returns value or fallback when Err.
  T
  unwrap_or (T fallback) const
  {
    return is_ok () ? std::get<T> (data_) : std::move (fallback);
  }
  /// Maps Ok value with function.
  template <typename Fn>
  auto
  map (Fn &&fn) const -> Result<decltype (fn (std::declval<T> ())), E>
  {
    using U = decltype (fn (std::declval<T> ()));
    if (is_ok ())
      return Result<U, E>{ fn (std::get<T> (data_)) };
    return Result<U, E>::from_err (std::get<E> (data_));
  }
  /// Chains operation producing another Result.
  template <typename Fn>
  auto
  and_then (Fn &&fn) const -> decltype (fn (std::declval<T> ()))
  {
    using R = decltype (fn (std::declval<T> ()));
    if (is_ok ())
      return fn (std::get<T> (data_));
    return R::from_err (std::get<E> (data_));
  }
  /**
   * @brief Transforms the error payload while keeping success values
   * unchanged.
   *
   * @tparam Fn Error transformer callable.
   * @param fn Function applied only when this is an Err.
   * @return Result<T, E2> with transformed error type.
   */
  template <typename Fn>
  auto
  map_err (Fn &&fn) const -> Result<T, decltype (fn (std::declval<E> ()))>
  {
    using E2 = decltype (fn (std::declval<E> ()));
    if (is_ok ())
      return Result<T, E2>::from_ok (std::get<T> (data_));
    return Result<T, E2>::from_err (fn (std::get<E> (data_)));
  }
  /// Factory for Ok value.
  static Result
  from_ok (T value)
  {
    return Result{ std::move (value) };
  }
  /// Factory for Err value.
  static Result
  from_err (E error)
  {
    return Result (std::in_place_index<1>, std::move (error));
  }

private:
  Result (std::in_place_index_t<1>, E error)
      : data_ (std::in_place_index<1>, std::move (error))
  {
  }
  std::variant<T, E> data_;
};
template <typename T> struct OkWrap
{
  T value;
};
template <typename E> struct ErrWrap
{
  E error;
};
template <typename T>
OkWrap<T>
Ok (T value)
{
  return { std::move (value) };
}
template <typename E>
ErrWrap<E>
Err (E error)
{
  return { std::move (error) };
}

/**
 * @brief Mutable parser input stream for combinatory parsers.
 *
 * ParsecInput tracks source text and cursor position and provides primitive
 * operations used by low-level parser callables.
 */
struct ParsecInput
{
  std::string_view source{};
  std::size_t pos{ 0 };
  char
  peek () const
  {
    return pos < source.size () ? source[pos] : '\0';
  }
  bool
  eof () const
  {
    return pos >= source.size ();
  }
  char
  consume ()
  {
    return eof () ? '\0' : source[pos++];
  }
  std::string_view
  get_span (std::size_t start) const
  {
    return source.substr (start, pos - start);
  }
};

inline Result<std::string, std::string>
load_parse_input_file (const std::string &path)
{
  std::ifstream in (path, std::ios::in | std::ios::binary);
  if (!in)
    return Result<std::string, std::string>::from_err (
        "dsl::load_parse_input_file: cannot open '" + path + "'");
  std::ostringstream ss;
  ss << in.rdbuf ();
  if (!in.good () && !in.eof ())
    return Result<std::string, std::string>::from_err (
        "dsl::load_parse_input_file: read failed for '" + path + "'");
  return Result<std::string, std::string>::from_ok (ss.str ());
}
enum class ParseFailureKind
{
  Soft,
  Committed
};

struct ParseError
{
  std::size_t pos{ 0 };
  ParseFailureKind kind{ ParseFailureKind::Soft };
  std::vector<std::string> expected{};
};

template <typename T> struct ExpectedResult
{
  using value_type = T;

  std::optional<T> value{};
  ParseError error{};

  ExpectedResult () = default;
  ExpectedResult (std::nullopt_t) : value (std::nullopt) {}
  ExpectedResult (const T &v) : value (v) {}
  ExpectedResult (T &&v) : value (std::move (v)) {}

  static ExpectedResult
  failure (std::size_t pos, ParseFailureKind kind = ParseFailureKind::Soft,
           std::vector<std::string> expected = {})
  {
    ExpectedResult out{};
    out.error = ParseError{ pos, kind, std::move (expected) };
    return out;
  }

  explicit operator bool () const { return value.has_value (); }
  T &operator* () { return *value; }
  const T &operator* () const { return *value; }
};

/**
 * @brief Functional parser wrapper around `ExpectedResult<T>(ParsecInput&)`.
 *
 * @tparam T Parse output type.
 */
template <typename T> struct Parser
{
  std::function<ExpectedResult<T> (ParsecInput &)> parse;
  ExpectedResult<T>
  operator() (ParsecInput &input) const
  {
    return parse (input);
  }
};

inline void
merge_error (ParseError &dst, const ParseError &src)
{
  if (src.pos > dst.pos)
    {
      dst = src;
      return;
    }
  if (src.pos < dst.pos)
    return;
  if (src.kind == ParseFailureKind::Committed)
    dst.kind = ParseFailureKind::Committed;
  for (const auto &label : src.expected)
    {
      if (std::find (dst.expected.begin (), dst.expected.end (), label)
          == dst.expected.end ())
        dst.expected.push_back (label);
    }
}

template <typename T>
ExpectedResult<T>
fail_expected (const ParsecInput &in, std::string label,
               ParseFailureKind kind = ParseFailureKind::Soft)
{
  return ExpectedResult<T>::failure (in.pos, kind, { std::move (label) });
}

template <typename Fn>
auto
parser (Fn &&fn)
{
  using Ret = decltype (fn (std::declval<ParsecInput &> ()));
  using T = typename Ret::value_type;
  return Parser<T>{ std::forward<Fn> (fn) };
}

template <typename T>
Parser<std::vector<T>>
operator* (const Parser<T> &p)
{
  return parser (
      [p] (ParsecInput &in) -> ExpectedResult<std::vector<T>>
        {
          std::vector<T> out;
          ParseError best_err{};
          while (true)
            {
              std::size_t save = in.pos;
              auto r = p (in);
              if (!r)
                {
                  merge_error (best_err, r.error);
                  if (r.error.kind == ParseFailureKind::Committed)
                    return ExpectedResult<std::vector<T>>::failure (
                        best_err.pos, best_err.kind, best_err.expected);
                  in.pos = save;
                  break;
                }
              out.push_back (*r);
            }
          return out;
        });
}
template <typename A, typename B>
Parser<std::pair<A, B>>
operator& (const Parser<A> &a, const Parser<B> &b)
{
  return parser (
      [a, b] (ParsecInput &in) -> ExpectedResult<std::pair<A, B>>
        {
          auto save = in.pos;
          auto ra = a (in);
          if (!ra)
            return ExpectedResult<std::pair<A, B>>::failure (
                ra.error.pos, ra.error.kind, ra.error.expected);
          auto rb = b (in);
          if (!rb)
            {
              bool consumed = in.pos != save;
              in.pos = save;
              auto kind = rb.error.kind;
              if (consumed)
                kind = ParseFailureKind::Committed;
              return ExpectedResult<std::pair<A, B>>::failure (
                  rb.error.pos, kind, rb.error.expected);
            }
          return std::make_pair (*ra, *rb);
        });
}
template <typename T>
Parser<T>
operator| (const Parser<T> &a, const Parser<T> &b)
{
  return parser (
      [a, b] (ParsecInput &in) -> ExpectedResult<T>
        {
          auto save = in.pos;
          auto ra = a (in);
          if (ra)
            return ra;
          if (ra.error.kind == ParseFailureKind::Committed)
            return ra;
          in.pos = save;
          auto rb = b (in);
          if (!rb)
            merge_error (rb.error, ra.error);
          return rb;
        });
}
template <typename T>
Parser<std::optional<T>>
optional (const Parser<T> &p)
{
  return parser (
      [p] (ParsecInput &in) -> ExpectedResult<std::optional<T>>
        {
          auto save = in.pos;
          auto r = p (in);
          if (!r)
            {
              if (r.error.kind == ParseFailureKind::Committed)
                return ExpectedResult<std::optional<T>>::failure (
                    r.error.pos, r.error.kind, r.error.expected);
              in.pos = save;
              return std::optional<T>{};
            }
          return std::optional<T>{ *r };
        });
}

template <typename T>
Parser<T>
try_parse (const Parser<T> &p)
{
  return parser ([p] (ParsecInput &in) -> ExpectedResult<T> {
    auto save = in.pos;
    auto r = p (in);
    if (!r)
      {
        in.pos = save;
        r.error.kind = ParseFailureKind::Soft;
      }
    return r;
  });
}

template <typename T>
Parser<T>
labeled (const Parser<T> &p, std::string expected)
{
  return parser ([p, expected = std::move (expected)] (
                     ParsecInput &in) -> ExpectedResult<T> {
    auto r = p (in);
    if (!r)
      r.error.expected = { expected };
    return r;
  });
}

template <typename T>
struct ParseOutcome
{
  std::optional<T> value{};
  ParseError error{};
};

template <typename T>
ParseOutcome<T>
run_parser (const Parser<T> &p, std::string_view source)
{
  ParsecInput in{ source, 0 };
  auto r = p (in);
  if (!r)
    return { std::nullopt, r.error };
  if (!in.eof ())
    {
      ParseError trailing{ in.pos, ParseFailureKind::Committed,
                           { "<end-of-input>" } };
      return { std::nullopt, trailing };
    }
  return { std::move (r.value), {} };
}

inline Parser<char>
ch (char c)
{
  return labeled (
      parser ([c] (ParsecInput &in) -> ExpectedResult<char> {
        if (in.peek () == c)
          return in.consume ();
        return fail_expected<char> (in, std::string (1, c));
      }),
      std::string (1, c));
}

inline Parser<char>
satisfy (std::function<bool (char)> pred, std::string label)
{
  return labeled (
      parser ([pred = std::move (pred), label] (
                  ParsecInput &in) -> ExpectedResult<char> {
        if (!in.eof () && pred (in.peek ()))
          return in.consume ();
        return fail_expected<char> (in, label);
      }),
      std::move (label));
}

enum class TaskPolicy
{
  StopOnError,
  ContinueOnError
};
/**
 * @brief Shared mutable state for task-pipeline execution.
 */
struct TaskState
{
  std::unordered_map<std::string, Result<std::string, std::string>> results{};
  bool suspended{ false };
};
/**
 * @brief Pipeline task unit, either internal callable or external command
 * adapter.
 */
struct Task
{
  std::string name{};
  std::function<Result<std::string, std::string> (TaskState &)> run_fn{};
  Result<std::string, std::string>
  run (TaskState &state) const
  {
    return run_fn (state);
  }
};
/**
 * @brief Sequence of tasks composed by operator|.
 */
struct TaskChain
{
  std::vector<Task> tasks{};
  TaskPolicy policy{ TaskPolicy::StopOnError };
};
inline TaskChain
operator| (Task lhs, Task rhs)
{
  return TaskChain{ { std::move (lhs), std::move (rhs) },
                    TaskPolicy::StopOnError };
}
inline TaskChain
operator| (TaskChain lhs, Task rhs)
{
  lhs.tasks.push_back (std::move (rhs));
  return lhs;
}
template <typename... Args>
TaskState &
run (TaskState &state, const TaskChain &chain, Args &&...)
{
  for (const auto &task : chain.tasks)
    {
      auto result = task.run (state);
    state.results.insert_or_assign (task.name, result);
      if (result.is_err () && chain.policy == TaskPolicy::StopOnError)
        break;
      if (state.suspended)
        break;
    }
  return state;
}
inline void
suspend (TaskState &state)
{
  state.suspended = true;
}
template <typename EventFn>
void
upon (TaskState &state, EventFn &&event, Task task)
{
  if (event ())
    state.results[task.name] = task.run (state);
}

/**
 * @brief Parser stage metadata passed to semantic hooks.
 */
struct ParserStage
{
  std::size_t begin{};
  std::size_t end{};
  std::string_view span{};
};

/**
 * @brief Runs a parser and invokes semantic action with matched span.
 *
 * @tparam T Parser output type.
 * @tparam ActionFn Action callable type.
 * @param p Parser to execute.
 * @param input Parse input stream.
 * @param semantics Map of production id to matched content.
 * @param action Semantic action callback.
 * @return Optional action result; empty when parse fails.
 */
template <typename T, typename ActionFn>
auto
parse_with_action (
    const Parser<T> &p, ParsecInput &input,
    const std::unordered_map<std::string, std::string> &semantics,
    ActionFn &&action)
    -> std::optional<decltype (action (semantics, ParserStage{}))>
{
  std::size_t begin = input.pos;
  auto r = p (input);
  if (!r)
    return std::nullopt;
  ParserStage stage{ begin, input.pos, input.get_span (begin) };
  return action (semantics, stage);
}

/**
 * @brief Production registration entry for parser table-pass semantics.
 *
 * @tparam Id Compile-time production identifier.
 * @tparam Fn Handler callable.
 */
template <FixedString Id, typename Fn> struct ProductionHandler
{
  Fn fn;
};

/**
 * @brief Registers a production handler keyed by compile-time string ID.
 *
 * @tparam Id Compile-time production identifier.
 * @tparam Fn Handler callable type.
 * @param fn Production handler.
 * @return ProductionHandler object.
 */
template <FixedString Id, typename Fn>
auto
production (Fn &&fn)
{
  return ProductionHandler<Id, std::decay_t<Fn>>{ std::forward<Fn> (fn) };
}

// ============================================================
//  PEG: definitions, rules, channels, matching, combinators
// ============================================================
//
// This section is a purely additive extension: it layers a PEG
// (Parsing Expression Grammar) subsystem on top of the existing
// pattern-matching (`dsl::pattern`) and parser-combinator
// (`dsl::Parser` / `ParsecInput`) machinery above. No existing type,
// function, or layout is modified.

struct PEGDefinition;
struct PEGRule;
struct PEGMatch;
struct PEGParseResult;
struct PEGMatcher;

/**
 * @brief A named logical stream that PEG matches are routed into.
 *
 * Channels suppress or categorize matches without changing the grammar:
 * the built-in @IGNORE channel is used for whitespace/comments, while
 * user-defined channels (created with dsl::new_peg_channel) group matches
 * by category and enable filtering in the PEG matcher.
 */
struct PEGChannel
{
  std::string name{ "@DEFAULT" };

  constexpr bool
  operator== (const PEGChannel &) const
      = default;

  /// @return true if this is the built-in @IGNORE channel.
  bool
  is_ignore () const noexcept
  {
    return name == "@IGNORE";
  }
};

/// Built-in channel for ignorable input (whitespace, comments, ...).
inline const PEGChannel PEGIgnoreChannel{ "@IGNORE" };

/// Built-in channel for ordinary (semantic) input.
inline const PEGChannel PEGDefaultChannel{ "@DEFAULT" };

/**
 * @brief Creates a custom PEG channel.
 *
 * Custom channel names must begin with '@'.
 *
 * @param name Channel name, e.g. "@COMMENTS".
 * @throws std::invalid_argument if the name is empty or lacks the '@'
 * prefix.
 */
inline PEGChannel
new_peg_channel (std::string_view name)
{
  if (name.empty () || name.front () != '@')
    throw std::invalid_argument (
        "dsl::new_peg_channel: channel names must begin with '@'");
  return PEGChannel{ std::string (name) };
}

/**
 * @brief Match record handed to PEG semantic actions.
 *
 * `value` is a view into the parsed source text and is only valid for the
 * duration of the parse that produced it.
 */
struct PEGMatch
{
  std::string_view value{};   ///< matched text
  std::size_t begin{ 0 };     ///< start offset in source
  std::size_t end{ 0 };       ///< one-past-end offset in source
  PEGRule *rule{ nullptr };   ///< rule that produced this match
  PEGChannel channel{};       ///< resolved channel of the rule
  void *user_data{ nullptr }; ///< optional extension point

  std::size_t line{ 1 };      ///< 1-based line of `begin`
  std::size_t column{ 1 };    ///< 1-based column of `begin`
  bool ignored{ false };      ///< true if routed to @IGNORE

  /// @return length of the matched text (end - begin).
  std::size_t
  length () const noexcept
  {
    return end - begin;
  }
};

/// Signature of a PEG semantic action.
using PEGSemanticAction = std::function<void (PEGMatch &)>;

namespace peg_detail
{

/// Runtime-compiled matcher for the dsl::pattern regex subset:
/// literals, '.', [class] (with ranges and ^ negation), and */+/?/?!
/// quantifiers. Matching is an unanchored greedy prefix match.
struct CompiledPattern
{
  struct Atom
  {
    bool any{ false };        ///< '.' wildcard
    bool is_class{ false };   ///< [...]
    bool negate{ false };     ///< [^...]
    char ch{ 0 };             ///< literal character
    std::string cls{};        ///< class body, e.g. "a-z0-9"
    char quant{ 0 };          ///< 0, '*', '+', '?', '!'
  };

  std::vector<Atom> atoms{};
  bool valid{ true };
  std::string error{};

  static bool
  class_test (std::string_view cls, bool negate, char c)
  {
    bool hit = false;
    for (std::size_t i = 0; i < cls.size ();)
      {
        if (i + 2 < cls.size () && cls[i + 1] == '-')
          {
            if (c >= cls[i] && c <= cls[i + 2])
              hit = true;
            i += 3;
          }
        else
          {
            if (cls[i] == c)
              hit = true;
            ++i;
          }
      }
    return negate ? !hit : hit;
  }

  static bool
  atom_test (const Atom &a, char c)
  {
    if (a.any)
      return true;
    if (a.is_class)
      return class_test (a.cls, a.negate, c);
    return a.ch == c;
  }

  static CompiledPattern
  compile (std::string_view pat)
  {
    CompiledPattern out;
    std::size_t i = 0;
    while (i < pat.size ())
      {
        // '^'/'$' anchors are accepted and ignored: PEG matching is
        // implicitly anchored at the current position.
        if (pat[i] == '^' || pat[i] == '$')
          {
            ++i;
            continue;
          }
        Atom a;
        if (pat[i] == '.')
          {
            a.any = true;
            ++i;
          }
        else if (pat[i] == '[')
          {
            auto close = pat.find (']', i + 1);
            if (close == std::string_view::npos)
              {
                out.valid = false;
                out.error = "unterminated character class";
                return out;
              }
            a.is_class = true;
            std::string body{ pat.substr (i + 1, close - i - 1) };
            if (!body.empty () && body.front () == '^')
              {
                a.negate = true;
                body.erase (body.begin ());
              }
            a.cls = std::move (body);
            i = close + 1;
          }
        else
          {
            a.ch = pat[i];
            ++i;
          }
        if (i < pat.size ()
            && (pat[i] == '*' || pat[i] == '+' || pat[i] == '?'
                || pat[i] == '!'))
          a.quant = pat[i++];
        out.atoms.push_back (std::move (a));
      }
    return out;
  }

  /// Greedy prefix match starting at `start`; returns match end offset.
  std::optional<std::size_t>
  match_at (std::string_view text, std::size_t start) const
  {
    if (!valid)
      return std::nullopt;
    std::size_t pos = start;
    for (const auto &a : atoms)
      {
        auto avail = [&] { return pos < text.size (); };
        switch (a.quant)
          {
          case '*':
            while (avail () && atom_test (a, text[pos]))
              ++pos;
            break;
          case '+': {
            std::size_t n = 0;
            while (avail () && atom_test (a, text[pos]))
              {
                ++pos;
                ++n;
              }
            if (n == 0)
              return std::nullopt;
            break;
          }
          case '?':
            if (avail () && atom_test (a, text[pos]))
              ++pos;
            break;
          case '!':
            // Must-fail atom: zero-width negative lookahead.
            if (avail () && atom_test (a, text[pos]))
              return std::nullopt;
            break;
          default:
            if (!avail () || !atom_test (a, text[pos]))
              return std::nullopt;
            ++pos;
            break;
          }
      }
    return pos;
  }
};

inline void
line_col_at (std::string_view text, std::size_t pos, std::size_t &line,
             std::size_t &col)
{
  line = 1;
  col = 1;
  for (std::size_t i = 0; i < pos && i < text.size (); ++i)
    {
      if (text[i] == '\n')
        {
          ++line;
          col = 1;
        }
      else
        ++col;
    }
}

/// A lightweight match-arm consumed by PEGMatcher::operator<<.
struct PEGArm
{
  PEGRule *rule{ nullptr };      ///< nullptr => wildcard
  PEGSemanticAction action{};
  bool is_wildcard{ false };
  bool is_channel{ false };      ///< match any rule on `channel`
  PEGChannel channel{};
};

} // namespace peg_detail

/**
 * @brief A single PEG rule: a compiled pattern plus mutable behavior.
 *
 * Rules are created via PEGDefinition::add_rule<"...">() and remain
 * mutable afterwards: the semantic action, channel, and the guarded
 * (must_fail / if_succeed) behavior may be adjusted at any time.
 *
 * The rule is invocable with a callback to build a matcher arm:
 *   matcher << rule([](dsl::PEGMatch& m){ ... });
 */
struct PEGRule
{
  std::string pattern_text{};
  peg_detail::CompiledPattern compiled{};
  PEGSemanticAction semantic_action{};
  PEGChannel channel{ "@DEFAULT" };
  bool must_fail{ false };    ///< succeed only when the pattern fails
  bool fire_on_neg{ false };  ///< run the action even on negated success
  PEGRule *if_succeed{ nullptr }; ///< rule to attempt when pattern matches
  std::size_t order{ 0 };     ///< declaration order (PEG precedence)

  /// Attempts this rule at `pos`. Returns the match end offset, or
  /// std::nullopt on failure. For must_fail rules, success consumes one
  /// character so tokenizing parses always make progress.
  std::optional<std::size_t>
  try_match (std::string_view text, std::size_t pos) const
  {
    if (must_fail)
      {
        if (compiled.match_at (text, pos))
          return std::nullopt;
        return pos < text.size () ? std::optional<std::size_t> (pos + 1)
                                  : std::nullopt;
      }
    return compiled.match_at (text, pos);
  }

  /// Builds a match-arm binding this rule to a callback.
  template <typename Fn>
  peg_detail::PEGArm
  operator() (Fn &&fn) const
  {
    peg_detail::PEGArm arm;
    arm.rule = const_cast<PEGRule *> (this);
    arm.action = std::forward<Fn> (fn);
    return arm;
  }
};

/**
 * @brief Diagnostics produced by PEGDefinition::parse().
 */
struct PEGParseResult
{
  bool success{ true };
  std::size_t offset{ 0 };
  std::size_t line{ 1 };
  std::size_t column{ 1 };
  std::string message{};

  bool
  ok () const noexcept
  {
    return success;
  }
  bool
  failed () const noexcept
  {
    return !success;
  }
  std::size_t
  error_offset () const noexcept
  {
    return offset;
  }
  std::size_t
  error_line () const noexcept
  {
    return line;
  }
  std::size_t
  error_column () const noexcept
  {
    return column;
  }
  /// @return human-readable failure description (empty on success).
  std::string_view
  error_message () const noexcept
  {
    return message;
  }
};

/**
 * @brief A PEG grammar: an ordered set of rules plus channels and
 * optional inheritance metadata.
 *
 * Create a root grammar with dsl::create_peg_definition(), or extend an
 * existing one with dsl::derive_peg_definition(parent). Derived grammars
 * see parent rules first (parent rules precede child rules at any
 * decision point), and may add new rules or shadow parent behavior
 * without mutating the parent.
 */
struct PEGDefinition
{
  const PEGDefinition *parent{ nullptr };

  /// Adds a rule from a compile-time pattern string, with an optional
  /// semantic action. Returns a reference to the stored rule; the
  /// reference stays valid as long as rules are only appended (and this
  /// definition is alive).
  template <FixedString S, typename Fn>
  PEGRule &
  add_rule (Fn &&action)
  {
    return emplace_rule (S.view (),
                         PEGSemanticAction (std::forward<Fn> (action)));
  }

  template <FixedString S>
  PEGRule &
  add_rule ()
  {
    return emplace_rule (S.view (), PEGSemanticAction{});
  }

  /// @return all rules visible at this level: parent rules first, then
  /// local rules, preserving PEG ordered-choice precedence.
  std::vector<const PEGRule *>
  rules () const
  {
    std::vector<const PEGRule *> out;
    if (parent)
      out = parent->rules ();
    for (const auto &r : local_rules_)
      out.push_back (&r);
    return out;
  }

  /**
   * @brief Parses `text` token by token using ordered choice over all
   * visible rules, invoking semantic actions as matches occur.
   *
   * Matches routed to @IGNORE are recognized and skipped silently.
   * Parsing succeeds when the whole input is consumed.
   */
  PEGParseResult
  parse (std::string_view text) const
  {
    PEGParseResult result;
    auto all = rules ();
    std::size_t pos = 0;
    while (pos < text.size ())
      {
        bool matched = false;
        for (const PEGRule *r : all)
          {
            auto end = r->try_match (text, pos);
            if (!end)
              continue;
            const bool negated = r->must_fail;
            if (!negated && *end == pos)
              {
                // Zero-width positive match would loop forever.
                result.success = false;
                result.offset = pos;
                peg_detail::line_col_at (text, pos, result.line,
                                         result.column);
                result.message
                    = "PEG rule matched empty input; refusing to loop";
                return result;
              }
            matched = true;
            if (!negated || r->fire_on_neg)
              {
                PEGMatch m;
                m.value = text.substr (pos, *end - pos);
                m.begin = pos;
                m.end = *end;
                m.rule = const_cast<PEGRule *> (r);
                m.channel = r->channel;
                m.ignored = r->channel.is_ignore ();
                peg_detail::line_col_at (text, pos, m.line, m.column);
                if (r->semantic_action)
                  r->semantic_action (m);
              }
            if (r->if_succeed)
              {
                if (auto e2 = r->if_succeed->try_match (text, pos))
                  {
                    PEGMatch m;
                    m.value = text.substr (pos, *e2 - pos);
                    m.begin = pos;
                    m.end = *e2;
                    m.rule = r->if_succeed;
                    m.channel = r->if_succeed->channel;
                    m.ignored = r->if_succeed->channel.is_ignore ();
                    peg_detail::line_col_at (text, pos, m.line, m.column);
                    if (r->if_succeed->semantic_action)
                      r->if_succeed->semantic_action (m);
                  }
              }
            pos = *end;
            break; // ordered choice: first success wins
          }
        if (!matched)
          {
            result.success = false;
            result.offset = pos;
            peg_detail::line_col_at (text, pos, result.line, result.column);
            result.message = "no PEG rule matched at offset "
                             + std::to_string (pos);
            return result;
          }
      }
    result.offset = pos;
    return result;
  }

private:
  PEGRule &
  emplace_rule (std::string_view pat, PEGSemanticAction action)
  {
    PEGRule r;
    r.pattern_text = std::string (pat);
    r.compiled = peg_detail::CompiledPattern::compile (pat);
    if (!r.compiled.valid)
      throw std::invalid_argument ("dsl::PEGDefinition::add_rule: "
                                   + r.compiled.error + " in pattern '"
                                   + r.pattern_text + "'");
    r.semantic_action = std::move (action);
    r.order = next_order_++;
    local_rules_.push_back (std::move (r));
    return local_rules_.back ();
  }

  std::vector<PEGRule> local_rules_{};
  std::size_t next_order_{ 0 };
};

/// Creates a root PEG definition (parent == nullptr).
inline PEGDefinition
create_peg_definition ()
{
  return PEGDefinition{};
}

/// Derives a PEG definition from `parent`; parent rules stay visible.
inline PEGDefinition
derive_peg_definition (const PEGDefinition &parent)
{
  PEGDefinition d;
  d.parent = &parent;
  return d;
}

/**
 * @brief Fluent, stream-style PEG pattern matcher.
 *
 * Created with dsl::peg_new_matcher(peg, text). Arms are consumed with
 * operator<<; the first arm whose rule matches at the current position
 * wins (PEG ordered choice). The wildcard arm fires when no preceding
 * arm matches, consuming one character by default.
 *
 * Channel filters (only/include/exclude) restrict which rules the rule
 * arms will consider; ignored-channel matches are skipped automatically
 * before any arm is consulted.
 *
 *   auto matcher = dsl::peg_new_matcher(peg, text);
 *   while (!matcher.is_spent())
 *       matcher << ident(cb) << number(cb) << matcher.wildcard(cb);
 *   matcher.close();
 */
struct PEGMatcher
{
  const PEGDefinition *def{ nullptr };
  std::string_view text{};
  std::size_t pos{ 0 };

  bool
  is_spent () const noexcept
  {
    return pos >= text.size ();
  }
  std::size_t
  position () const noexcept
  {
    return pos;
  }
  /// No-op for API symmetry; releases nothing (nothing is held).
  void
  close () noexcept
  {
  }

  /// Restricts matching to a single channel.
  PEGMatcher &
  only (std::string_view channel)
  {
    mode_ = FilterMode::Only;
    channels_.clear ();
    channels_.push_back (std::string (channel));
    return *this;
  }
  /// Adds a channel to the accepted set.
  PEGMatcher &
  include (std::string_view channel)
  {
    if (mode_ == FilterMode::Exclude)
      mode_ = FilterMode::Only;
    channels_.push_back (std::string (channel));
    return *this;
  }
  /// Rejects rules on the given channel.
  PEGMatcher &
  exclude (std::string_view channel)
  {
    if (mode_ != FilterMode::Exclude)
      {
        mode_ = FilterMode::Exclude;
        channels_.clear ();
      }
    channels_.push_back (std::string (channel));
    return *this;
  }
  bool
  channel_allowed (std::string_view name) const
  {
    if (mode_ == FilterMode::All)
      return true;
    bool found = std::find (channels_.begin (), channels_.end (), name)
                 != channels_.end ();
    return mode_ == FilterMode::Only ? found : !found;
  }

  /// Wildcard arm: fires when no rule arm matches at the position.
  template <typename Fn>
  peg_detail::PEGArm
  wildcard (Fn &&fn)
  {
    peg_detail::PEGArm arm;
    arm.is_wildcard = true;
    arm.action = std::forward<Fn> (fn);
    return arm;
  }

  /// Enqueues one match arm. Arms accumulate across chained << calls
  /// until one of them fires at the current position; the first arm
  /// whose rule matches wins (ordered choice). @IGNORE matches are
  /// skipped automatically before arms are consulted.
  PEGMatcher &
  operator<< (const peg_detail::PEGArm &arm)
  {
    arms_.push_back (arm);
    step ();
    return *this;
  }

private:
  /// Tries the accumulated arms once at the current position.
  void
  step ()
  {
    if (arms_.empty () || is_spent ())
      return;
    skip_ignored ();
    if (is_spent ())
      {
        arms_.clear ();
        return;
      }
    // Non-wildcard arms in order: first match wins.
    for (std::size_t i = 0; i < arms_.size (); ++i)
      {
        const auto &arm = arms_[i];
        if (arm.is_wildcard)
          continue;
        if (arm.is_channel)
          {
            for (const PEGRule *r : def->rules ())
              {
                if (r->channel.name != arm.channel.name)
                  continue;
                if (auto end = r->try_match (text, pos))
                  {
                    fire (arm.action, r, pos, *end);
                    arms_.clear ();
                    return;
                  }
              }
            continue;
          }
        if (!arm.rule || !channel_allowed (arm.rule->channel.name))
          continue;
        if (auto end = arm.rule->try_match (text, pos))
          {
            fire (arm.action, arm.rule, pos, *end);
            arms_.clear ();
            return;
          }
      }
    // No rule arm matched: fire the first wildcard arm, if any.
    for (std::size_t i = 0; i < arms_.size (); ++i)
      {
        if (!arms_[i].is_wildcard)
          continue;
        PEGMatch m;
        m.value = text.substr (pos, 1);
        m.begin = pos;
        m.end = pos + 1;
        m.rule = nullptr;
        peg_detail::line_col_at (text, pos, m.line, m.column);
        if (arms_[i].action)
          arms_[i].action (m);
        ++pos; // wildcard consumes one character
        arms_.clear ();
        return;
      }
    // Nothing fired yet: keep the accumulated arms; the next << may
    // provide one that matches at this position.
  }

  void
  skip_ignored ()
  {
    bool progressed = true;
    while (progressed && !is_spent ())
      {
        progressed = false;
        for (const PEGRule *r : def->rules ())
          {
            if (!r->channel.is_ignore ())
              continue;
            if (auto end = r->try_match (text, pos))
              {
                if (*end == pos)
                  continue;
                pos = *end;
                progressed = true;
                break;
              }
          }
      }
  }

  void
  fire (const PEGSemanticAction &action, const PEGRule *rule,
        std::size_t begin, std::size_t end)
  {
    PEGMatch m;
    m.value = text.substr (begin, end - begin);
    m.begin = begin;
    m.end = end;
    m.rule = const_cast<PEGRule *> (rule);
    m.channel = rule->channel;
    m.ignored = rule->channel.is_ignore ();
    peg_detail::line_col_at (text, begin, m.line, m.column);
    pos = end;
    if (action)
      action (m);
    else if (rule->semantic_action)
      rule->semantic_action (m);
  }

  enum class FilterMode
  {
    All,
    Only,
    Exclude
  };
  FilterMode mode_{ FilterMode::All };
  std::vector<std::string> channels_{};
  std::vector<peg_detail::PEGArm> arms_{};
  static constexpr std::size_t npos
      = static_cast<std::size_t> (-1);
};

/// Creates a fluent PEG matcher over `text` for the grammar `peg`.
inline PEGMatcher
peg_new_matcher (const PEGDefinition &peg, std::string_view text)
{
  PEGMatcher m;
  m.def = &peg;
  m.text = text;
  return m;
}

// ============================================================
//  PEG parser combinators
// ============================================================
//
// Thin, additive wrappers over the existing dsl::Parser machinery with
// strict PEG semantics: ordered choice (operator| already behaves
// ordered), repetition that never commits on partial input, and
// lookahead operators.

namespace peg_detail
{

/// Converts any dsl::Parser<T> into a Parser<PEGMatch> that reports the
/// consumed span. The output value is discarded; the span is kept.
template <typename T>
Parser<PEGMatch>
as_span_parser (const Parser<T> &p)
{
  return parser ([p] (ParsecInput &in) -> ExpectedResult<PEGMatch> {
    std::size_t begin = in.pos;
    auto r = p (in);
    if (!r)
      return ExpectedResult<PEGMatch>::failure (r.error.pos, r.error.kind,
                                                r.error.expected);
    PEGMatch m;
    m.begin = begin;
    m.end = in.pos;
    m.value = in.get_span (begin);
    return m;
  });
}

} // namespace peg_detail

/// PEG sequence: parse a, then b, then c... Spans concatenate.
template <typename A, typename B, typename... Rest>
auto
peg_seq (const Parser<A> &a, const Parser<B> &b, const Rest &...rest)
{
  auto ab = parser ([a, b] (ParsecInput &in) -> ExpectedResult<PEGMatch> {
    std::size_t begin = in.pos;
    auto ra = a (in);
    if (!ra)
      return ExpectedResult<PEGMatch>::failure (ra.error.pos, ra.error.kind,
                                                ra.error.expected);
    auto rb = b (in);
    if (!rb)
      {
        bool consumed = in.pos != begin;
        in.pos = begin;
        auto kind = rb.error.kind;
        if (consumed)
          kind = ParseFailureKind::Committed;
        return ExpectedResult<PEGMatch>::failure (rb.error.pos, kind,
                                                  rb.error.expected);
      }
    PEGMatch m;
    m.begin = begin;
    m.end = in.pos;
    m.value = in.get_span (begin);
    return m;
  });
  if constexpr (sizeof...(rest) == 0)
    return ab;
  else
    return peg_seq (ab, rest...);
}

/// PEG ordered choice: first successful alternative wins; later
/// alternatives are not tried at that decision point.
template <typename A, typename B, typename... Rest>
auto
peg_choice (const Parser<A> &a, const Parser<B> &b, const Rest &...rest)
{
  auto ab = try_parse (a) | try_parse (b);
  if constexpr (sizeof...(rest) == 0)
    return ab;
  else
    return peg_choice (ab, rest...);
}

/// PEG optional: a? (never fails, never commits).
template <typename T>
auto
peg_opt (const Parser<T> &p)
{
  return optional (try_parse (p));
}

/// PEG zero-or-more: a* (greedy; partial failures backtrack fully).
template <typename T>
auto
peg_many (const Parser<T> &p)
{
  return *try_parse (p);
}

/// PEG one-or-more: a+ (greedy; fails if the first iteration fails).
template <typename T>
auto
peg_many1 (const Parser<T> &p)
{
  return p & *try_parse (p);
}

/// PEG positive lookahead: &a — succeeds if a matches, consumes nothing.
template <typename T>
auto
peg_and (const Parser<T> &p)
{
  return parser ([p] (ParsecInput &in) -> ExpectedResult<bool> {
    auto save = in.pos;
    auto r = p (in);
    in.pos = save;
    if (!r)
      return ExpectedResult<bool>::failure (r.error.pos, ParseFailureKind::Soft,
                                            r.error.expected);
    return true;
  });
}

/// PEG negative lookahead: !a — succeeds if a fails, consumes nothing.
template <typename T>
auto
peg_not (const Parser<T> &p)
{
  return parser ([p] (ParsecInput &in) -> ExpectedResult<bool> {
    auto save = in.pos;
    auto r = p (in);
    in.pos = save;
    if (r)
      return fail_expected<bool> (in, "negative lookahead");
    return true;
  });
}

/// Adapts a PEGRule into a Parser<PEGMatch> honoring must_fail.
inline Parser<PEGMatch>
peg_rule (const PEGRule &rule)
{
  return parser ([&rule] (ParsecInput &in) -> ExpectedResult<PEGMatch> {
    std::size_t begin = in.pos;
    auto end = rule.try_match (in.source, begin);
    if (!end)
      return fail_expected<PEGMatch> (in, rule.pattern_text);
    in.pos = *end;
    PEGMatch m;
    m.begin = begin;
    m.end = *end;
    m.value = in.source.substr (begin, *end - begin);
    m.rule = const_cast<PEGRule *> (&rule);
    m.channel = rule.channel;
    m.ignored = rule.channel.is_ignore ();
    peg_detail::line_col_at (in.source, begin, m.line, m.column);
    return m;
  });
}

/// Assigns a channel to a parser's PEGMatch result.
template <typename T>
auto
peg_channel (PEGChannel channel, const Parser<T> &p)
{
  return parser ([channel = std::move (channel), p] (
                     ParsecInput &in) -> ExpectedResult<PEGMatch> {
    std::size_t begin = in.pos;
    auto r = p (in);
    if (!r)
      return ExpectedResult<PEGMatch>::failure (r.error.pos, r.error.kind,
                                                r.error.expected);
    PEGMatch m;
    m.begin = begin;
    m.end = in.pos;
    m.value = in.get_span (begin);
    m.channel = channel;
    m.ignored = channel.is_ignore ();
    peg_detail::line_col_at (in.source, begin, m.line, m.column);
    return m;
  });
}

/// Overload accepting the channel name directly (must begin with '@').
template <typename T>
auto
peg_channel (std::string_view name, const Parser<T> &p)
{
  return peg_channel (new_peg_channel (name), p);
}

// ============================================================
//  Utility concepts
// ============================================================

/// Concept: T has a feature F mixed in.
template <typename T, typename F>
concept HasFeature = std::derived_from<T, typename F::template Mixin<T>>;

/// Concept: T is a DSL with Pipeline support.
template <typename T>
concept Pipeable = HasFeature<T, Pipeline>;

/// Concept: T is a DSL with ExprTemplates support.
template <typename T>
concept ExprLike = HasFeature<T, ExprTemplates>
                   && requires (const T &t) { t.expr_value (); };

/// Concept: T is a DSL with CustomLiterals support.
template <typename T>
concept HasLiterals
    = HasFeature<T, CustomLiterals> && requires { T::literals; };

/// Concept: T is a DSL with Rewrite support.
template <typename T>
concept Rewritable = HasFeature<T, Rewrite> && requires { T::rules; };

} // namespace dsl
