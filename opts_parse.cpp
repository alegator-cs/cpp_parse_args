#include <type_traits>
#include <algorithm>
#include <array>
#include <memory>
#include <tuple>
#include <variant>
#include <any>
#include <utility>
#include <type_traits>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <fstream>

/*
 * util
*/
template <typename T, typename U>
std::unique_ptr<T[]> make_uptr_arr(const U *const raw, const size_t len) {
  std::unique_ptr<T[]> arr{new T[len]};
  for (size_t i = 0, max = len; i < max; i++) {
    arr[i] = T{raw[i]};
  }
  return arr;
}

/*
 * 'T_string' accepts a variadic template param 'chars' corresponding to a compile time string
 * so that this compile time string can be referred to by an instance of T_string, T_string
 * has the member 'arr' of std::array type
 * since 'arr' is a static constexpr const member, instances of T_string can be compile time
 * instantiated
*/
template <char... chars>
struct T_string {
  /* static constexpr size_t size = sizeof...(chars);*/
  /* static constexpr const char *const arr = {chars...}; */
  static constexpr const std::array<char, sizeof...(chars)> arr{chars...};
  constexpr operator std::string_view() { return std::string_view(arr.data(), sizeof...(chars)); }
};

/*
 * operator""_opt() is a user defined literal that returns a compile time T_string from a
 * string literal like "-f"_opt
 */
template <typename T, T... chars>
constexpr T_string<chars...> operator"" _opt() { return T_string<chars...>{}; }

/*
 * 'option' is forward declared so it can have a specialized template declaration
 */
template <typename...>
struct option;

/*
 * 'option' defines types for the brief and full option labels, as well as the
 * underlying type of the option
 * i.e. option<int, "-f", "-file">
 */
template <typename T,
          char... brief,
          char... full>
struct option<T, T_string<brief...>, T_string<full...>> {
  using value_t = T;
  using brief_name_t = T_string<brief...>;
  using full_name_t = T_string<full...>;
};

/*
 * 'getter' uses CRTP to inject operator[] definitions into the derived
 * class; this is a way of defining member functions by expanding a param
 * pack (two functions for each option)
 *
 * this should not be thought of as an independent class
*/

template<typename Derived, typename Opt>
struct getter {
  using brief_opt_t = typename Opt::brief_name_t;
  using full_opt_t = typename Opt::full_name_t;
  using T = typename Opt::value_t;

  T operator[](brief_opt_t opt) const {
    auto self = static_cast<const Derived&>(*this);
    const std::any& a = self.res.at(opt);
    return std::any_cast<T>(a);
  }

  T operator[](full_opt_t opt) const {
    auto self = static_cast<const Derived&>(*this);
    const std::any& a = self.res.at(opt);
    return std::any_cast<T>(a);
  }
};

/*
 * 'parsed_opts' inherits overloaded getters for each option
 * the params that select overloads are the T_strings for each
 * option
 */

template <typename... Opts>
class parsed_opts : public getter<parsed_opts<Opts...>, Opts>... {
private:
  using OptsMap = std::unordered_map<std::string_view, std::any>;
public:
  using getter<parsed_opts<Opts...>, Opts>::operator[]...;
  const OptsMap res;
  parsed_opts(const OptsMap& res) : res{res} {}
};

/*
 * 'decl_opts' allows the user to declare a typed parameter list with
 * long and short names, parsed in the style of a command line parser
 *
 * params are added using chained 'add'
 *
 * the result object is obtained with the 'parse' method
 */

template <typename... TsOptions>
struct decl_opts {
  template <typename T>
  struct select_type {};

  using ts_tup_t = std::tuple<typename TsOptions::value_t...>;
  using opts_tup_t = std::tuple<TsOptions...>;
  using sequence_t = std::make_index_sequence<sizeof...(TsOptions)>;
  using parse_fn_t = std::any (*)(const decl_opts<TsOptions...>&, const std::string_view);
  using parse_arr_t = std::array<parse_fn_t, sizeof...(TsOptions)>;
  using opts_map_t = std::unordered_map<std::string_view, std::any>;

  // using cmp_fn_t = bool (*)(const std::string_view opt);
  // using cmp_arr_t = std::array<cmp_fn_t, sizeof...(TsOptions)>;

  parse_arr_t parse_arr;

  constexpr decl_opts() : parse_arr{make_parse_arr()} {}
  constexpr decl_opts(const decl_opts& from) = default;

  template <typename T, char... brief, char... full>
  constexpr auto add(const T_string<brief...>, const T_string<full...>) const {
    return decl_opts<TsOptions..., option<T, T_string<brief...>, T_string<full...>>>{};
  }

  template <size_t... Is>
  constexpr parse_arr_t make_parse_arr_impl(std::index_sequence<Is...>) const {
    return {
      +[](const decl_opts<TsOptions...>& opts, const std::string_view raw_opt) {
        typename TsOptions::value_t v = opts.parse_opt<typename TsOptions::value_t>(raw_opt);
        return std::any{v};
      }...
    };
  }

  constexpr parse_arr_t make_parse_arr() const {
    return make_parse_arr_impl(sequence_t{});
  }

  /*
  template <size_t... Is>
  constexpr cmp_arr_t make_cmp_arr_impl(const std::index_sequence<Is...>) {
    return {
      +[](const std::string_view opt) {
        return opt == typename TsOptions::brief_name_t{} || opt == typename TsOptions::full_name_t{};
      }...
    };
  }

  constexpr cmp_arr_t make_cmp_arr() const {
    return make_cmp_arr_impl(sequence_t{});
  }
  */

  // should prooobably make this less horrific
  template <size_t... Is>
  constexpr size_t index_impl(const std::index_sequence<Is...>, const std::string_view opt) const {
    size_t idx {sizeof...(Is)};
    (..., ((opt == typename TsOptions::brief_name_t{} || opt == typename TsOptions::full_name_t{}) ? (void)(idx = Is) : (void)0));
    return idx;
  }

  constexpr size_t index(const std::string_view opt) const {
    return index_impl(sequence_t{}, opt.substr(0, opt.find('=')));
  }

  constexpr void print_bad_opt(const std::string_view bad_opt) const {
    std::cout << "command line option " << bad_opt << " not recognized" << std::endl;
  }

  constexpr void print_bad_value(const std::string_view opt) const {
    std::cout << "command line option " << opt << " provided invalid value" << std::endl;
  }

  template <typename T>
  constexpr T parse_opt_impl(const std::string_view opt, const select_type<T>) const {
    const size_t rhs_start = opt.find('=') + 1;
    const std::string_view rhs{opt.data() + rhs_start};
    std::istringstream ss{std::string{rhs}};
    T value;
    ss >> value;
    if (!ss) {
      print_bad_value(opt);
    }
    return value;
  }

  constexpr std::string_view parse_opt_impl(const std::string_view opt, const select_type<std::string_view>) const {
    const size_t rhs_start = opt.find('=') + 1;
    return std::string_view{opt.data() + rhs_start};
  }

  constexpr bool parse_opt_impl(const std::string_view opt, const select_type<bool>) const {
    return true;
  }

  template <typename T>
  constexpr T parse_opt(const std::string_view opt) const {
    return parse_opt_impl<T>(opt, select_type<T>{});
  }

  constexpr auto parse(const std::string_view raw_opt) const {
    const parse_fn_t parse = parse_arr[index(raw_opt)];
    std::any a = (*parse)(*this, raw_opt);
    return a;
  }

  constexpr auto parse(const int argc, const char *const *const argv) const {
    const char *const *const raw_input = argv + 1;
    const size_t input_len = argc - 1;
    const std::unique_ptr<const std::string_view[]> sv_input = make_uptr_arr<std::string_view>(raw_input, input_len);
    const std::string_view *const input = sv_input.get();

    opts_map_t res;
    for (size_t i = 0; i < input_len; i++) {
      const std::string_view raw_opt = input[i];
      const std::string_view lhs(raw_opt.data(), raw_opt.find('='));
      res[lhs] = parse(raw_opt);
    }
    return parsed_opts<TsOptions...>{res};
  }

};

// example program usage: ./a.out -first=2 -second=3.14
int main(int argc, char** argv) {
  constexpr const auto opts = decl_opts{}
    .add<int>("-f"_opt, "-first"_opt)
    .add<double>("-s"_opt, "-second"_opt);
  const auto parsed = opts.parse(argc, argv);
  std::cout << parsed["-f"_opt] << parsed["-s"_opt] << std::endl;
}
