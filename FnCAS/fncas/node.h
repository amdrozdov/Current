/*******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * *******************************************************************************/

#ifndef FNCAS_FNCAS_NODE_H
#define FNCAS_FNCAS_NODE_H

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <stack>
#include <string>
#include <vector>
#include <exception>

#include "base.h"
#include "exceptions.h"

#include "../../Bricks/exception.h"
#include "../../Bricks/util/singleton.h"

namespace fncas {
// Non-standard functions useful in data science.
inline double_t sqr(double_t x) { return x * x; }
inline double_t unit_step(double_t x) { return x >= 0 ? 1 : 0; }
inline double_t ramp(double_t x) { return x > 0 ? x : 0; }
// TODO(dkorolev): Sigmoid, its derivative, normal distribution, ERF, etc.

template <typename T>
T apply_function(::fncas::impl::function_t function, T argument) {
  static std::function<T(T)> evaluator[static_cast<size_t>(::fncas::impl::function_t::end)] = {
      sqr, sqrt, exp, log, sin, cos, tan, asin, acos, atan, unit_step, ramp};
  return function < ::fncas::impl::function_t::end ? evaluator[static_cast<size_t>(function)](argument)
                                                   : std::numeric_limits<T>::quiet_NaN();
}
}  // namespace fncas

namespace fncas {
namespace impl {

// Parsed expressions are stored in an array of node_impl objects.
// Instances of `node_impl` take 10 bytes each and are packed.
// Each node_impl refers to a value, an input variable, an operation or math function invokation.
// The `ThreadLocalSingleton` containing `vector<node_impl>` is the allocator, thus
// at most one expression per thread (at most one scope of `fncas::impl::X`) can be "recorded" at a time.

inline const char* operation_as_string(operation_t operation) {
  static const char* representation[static_cast<size_t>(operation_t::end)] = {"+", "-", "*", "/"};
  return operation < operation_t::end ? representation[static_cast<size_t>(operation)] : "?";
}

inline const char* function_as_string(function_t function) {
  static const char* representation[static_cast<size_t>(function_t::end)] = {
      "sqr", "sqrt", "exp", "log", "sin", "cos", "tan", "asin", "acos", "atan", "unit_step", "ramp"};
  return function < function_t::end ? representation[static_cast<size_t>(function)] : "?";
}

template <typename T>
T apply_operation(operation_t operation, T lhs, T rhs) {
  static std::function<T(T, T)> evaluator[static_cast<size_t>(operation_t::end)] = {
      std::plus<T>(), std::minus<T>(), std::multiplies<T>(), std::divides<T>(),
  };
  return operation < operation_t::end ? evaluator[static_cast<size_t>(operation)](lhs, rhs)
                                      : std::numeric_limits<T>::quiet_NaN();
}

struct node_impl;
struct X;
struct internals_impl {
  // The dimensionality of the function that is currently being worked with.
  size_t dim_;

  // The pointer to a thread-local vector of "variables" used for expression parsing.
  X* x_ptr_;

  // All expression nodes created so far, with fixed indexes.
  std::vector<node_impl> node_vector_;

  // Values per node computed so far.
  std::vector<double_t> node_value_;
  std::vector<int8_t> node_computed_;

  // df_[var_index][node_index] => node index for d (node[node_index]) / d (x[variable_index]), -1 if unknown.
  std::vector<std::vector<node_index_type>> df_;

  // A block of RAM to be used as the buffer for externally compiled functions.
  std::vector<double_t> heap_for_compiled_evaluations_;

  void reset() {
    dim_ = 0;
    x_ptr_ = nullptr;
    node_vector_.clear();
    df_.clear();
    heap_for_compiled_evaluations_.clear();
  }
};

inline internals_impl& internals_singleton() { return current::ThreadLocalSingleton<internals_impl>(); }

inline std::vector<node_impl>& node_vector_singleton() { return internals_singleton().node_vector_; }

struct node_impl {
  uint8_t data_[18];
  type_t& type() { return *reinterpret_cast<type_t*>(&data_[0]); }
  int32_t& variable() {
    CURRENT_ASSERT(type() == type_t::variable);
    return *reinterpret_cast<int32_t*>(&data_[2]);
  }
  double_t& value() {
    CURRENT_ASSERT(type() == type_t::value);
    return *reinterpret_cast<double_t*>(&data_[2]);
  }
  operation_t& operation() {
    CURRENT_ASSERT(type() == type_t::operation);
    return *reinterpret_cast<operation_t*>(&data_[1]);
  }
  node_index_type& lhs_index() {
    CURRENT_ASSERT(type() == type_t::operation);
    return *reinterpret_cast<node_index_type*>(&data_[2]);
  }
  node_index_type& rhs_index() {
    CURRENT_ASSERT(type() == type_t::operation);
    return *reinterpret_cast<node_index_type*>(&data_[10]);
  }
  function_t& function() {
    CURRENT_ASSERT(type() == type_t::function);
    return *reinterpret_cast<function_t*>(&data_[1]);
  }
  node_index_type& argument_index() {
    CURRENT_ASSERT(type() == type_t::function);
    return *reinterpret_cast<node_index_type*>(&data_[2]);
  }
};
static_assert(sizeof(node_impl) == 18, "sizeof(node_impl) should be 18. Check struct alignment compilation flags.");

// eval_node() should use manual stack implementation to avoid SEGFAULT. Using plain recursion
// will overflow the stack for every formula containing repeated operation on the top level.
enum class reuse_cache : int8_t { invalidate = 0, reuse = 1 };
inline double_t eval_node(node_index_type index,
                          const std::vector<double_t>& x,
                          reuse_cache reuse = reuse_cache::invalidate) {
  std::vector<double_t>& V = internals_singleton().node_value_;
  std::vector<int8_t>& B = internals_singleton().node_computed_;
  if (reuse == reuse_cache::invalidate) {
    B.clear();
  }
  std::stack<node_index_type> stack;
  stack.push(index);
  while (!stack.empty()) {
    const node_index_type i = stack.top();
    stack.pop();
    const node_index_type dependent_i = ~i;
    if (i > dependent_i) {
      if (!growing_vector_access(B, i, static_cast<int8_t>(false))) {
        node_impl& f = node_vector_singleton()[i];
        if (f.type() == type_t::variable) {
          const int32_t v = f.variable();
          CURRENT_ASSERT(v >= 0 && v < static_cast<int32_t>(x.size()));
          growing_vector_access(V, i, 0.0) = x[v];
          growing_vector_access(B, i, static_cast<int8_t>(false)) = true;

        } else if (f.type() == type_t::value) {
          growing_vector_access(V, i, 0.0) = f.value();
          growing_vector_access(B, i, static_cast<int8_t>(false)) = true;
        } else if (f.type() == type_t::operation) {
          stack.push(~i);
          stack.push(f.lhs_index());
          stack.push(f.rhs_index());
        } else if (f.type() == type_t::function) {
          stack.push(~i);
          stack.push(f.argument_index());
        } else {
          CURRENT_ASSERT(false);
          return std::numeric_limits<double_t>::quiet_NaN();
        }
      }
    } else {
      node_impl& f = node_vector_singleton()[dependent_i];
      if (f.type() == type_t::operation) {
        growing_vector_access(V, dependent_i, 0.0) =
            apply_operation<double_t>(f.operation(), V[f.lhs_index()], V[f.rhs_index()]);
        growing_vector_access(B, dependent_i, static_cast<int8_t>(false)) = true;
      } else if (f.type() == type_t::function) {
        growing_vector_access(V, dependent_i, 0.0) =
            ::fncas::apply_function<double_t>(f.function(), V[f.argument_index()]);
        growing_vector_access(B, dependent_i, static_cast<int8_t>(false)) = true;
      } else {
        CURRENT_ASSERT(false);
        return std::numeric_limits<double_t>::quiet_NaN();
      }
    }
  }
  CURRENT_ASSERT(B[index]);
  return V[index];
}

// The code that deals with nodes directly uses class V as a wrapper to node_impl.
// Since the storage for node_impl-s is global, class V just holds an index of node_impl.
// User code that defines the function to work with is effectively dealing with class V objects:
// arithmetical and mathematical operations are overloaded for class V.

struct allocate_new {};
enum class from_index : node_index_type;

struct node_index_allocator {
  node_index_type index_;  // non-const since `V` objects can be modified.
  explicit inline node_index_allocator(from_index i) : index_(static_cast<node_index_type>(i)) {}
  explicit inline node_index_allocator(allocate_new) : index_(node_vector_singleton().size()) {
    node_vector_singleton().resize(index_ + 1);
  }
  node_index_type index() const { return index_; }
  node_index_allocator() = delete;
};

// Template, used as a header-only way to move implementation to another source or header file.
template <typename T>
struct node_differentiate_impl {};

// Need `IS_BOXED`, as `std::vector<V>` can be either a special helper type, or a real `std::vector<V>`, hence two `V`s.
template <bool IS_BOXED>
struct GenericV : node_index_allocator {
 private:
  friend class ::std::vector<GenericV<IS_BOXED>>;
  GenericV(const node_index_allocator& instance) : node_index_allocator(instance) {}

 public:
  GenericV() : node_index_allocator(allocate_new()) {}
  GenericV(double_t x) : node_index_allocator(allocate_new()) {
    type() = type_t::value;
    value() = x;
  }
  GenericV(from_index i) : node_index_allocator(i) {}
  type_t& type() const { return node_vector_singleton()[index_].type(); }
  int32_t& variable() const { return node_vector_singleton()[index_].variable(); }
  double_t& value() const { return node_vector_singleton()[index_].value(); }
  operation_t& operation() const { return node_vector_singleton()[index_].operation(); }
  node_index_type& lhs_index() const { return node_vector_singleton()[index_].lhs_index(); }
  node_index_type& rhs_index() const { return node_vector_singleton()[index_].rhs_index(); }
  GenericV lhs() const { return from_index(node_vector_singleton()[index_].lhs_index()); }
  GenericV rhs() const { return from_index(node_vector_singleton()[index_].rhs_index()); }
  function_t& function() const { return node_vector_singleton()[index_].function(); }
  node_index_type& argument_index() const { return node_vector_singleton()[index_].argument_index(); }
  GenericV argument() const { return from_index(node_vector_singleton()[index_].argument_index()); }
  static GenericV variable(node_index_type index) {
    GenericV result;
    result.type() = type_t::variable;
    result.variable() = index;
    return result;
  }
  std::string debug_as_string() const {
    if (type() == type_t::variable) {
      return "x[" + std::to_string(variable()) + "]";
    } else if (type() == type_t::value) {
      return std::to_string(value());
    } else if (type() == type_t::operation) {
      // Note: this recursive call will overflow the stack with SEGFAULT on deep functions.
      // For debugging purposes only.
      return "(" + lhs().debug_as_string() + operation_as_string(operation()) + rhs().debug_as_string() + ")";
    } else if (type() == type_t::function) {
      // Note: this recursive call will overflow the stack with SEGFAULT on deep functions.
      // For debugging purposes only.
      return std::string(function_as_string(function())) + "(" + argument().debug_as_string() + ")";
    } else {
      return "?";
    }
  }
  double_t operator()(const std::vector<double_t>& x, reuse_cache reuse = reuse_cache::invalidate) const {
    return eval_node(index_, x, reuse);
  }
  // Template is used here as a form of forward declaration.
  template <typename TX>
  GenericV differentiate(const TX& x_ref, size_t variable_index) const {
    static_assert(std::is_same<TX, X>::value, "V::differentiate(const x& x, size_t variable_index);");
    // Note: This method will not build unless `fncas_differentiate.h` is included.
    return node_differentiate_impl<TX>::differentiate(x_ref, index_, variable_index);
  }
};
using V = GenericV<false>;
static_assert(sizeof(V) == 8, "sizeof(V) should be 8, as sizeof(node_index_type).");

// Class "x" is the placeholder class an instance of which is to be passed to the user function
// to record the computation rather than perform it.

}  // namespace fncas::impl
}  // namespace fncas

// A thin wrapper replacing `std::vector<V>`, with the sole purpose of being able to inherit from it
// when defining the special "parameters" class, the lifetime of which is the lifetime of the "formula"
// being dealt with.
namespace std {

// NOTE(dkorolev): This is by no means a complete `vector<>` implementation.
template <>
class vector<::fncas::impl::V> {
 public:
  ~vector() = default;  // Not a final class.

  using value_type = ::fncas::impl::V;
  using contained_value_type = ::fncas::impl::GenericV<true>;

  std::vector<contained_value_type> v;

  struct iterator {
    value_type* v;
    size_t i;
    iterator(value_type* v, size_t i) : v(v), i(i) {}
    value_type& operator*() const { return v[i]; }
    bool operator==(const iterator& rhs) const { return i == rhs.i; }
    bool operator!=(const iterator& rhs) const { return i != rhs.i; }
    void operator++() { ++i; }
  };

  struct const_iterator {
    const value_type* v;
    size_t i;
    const_iterator(const value_type* v, size_t i) : v(v), i(i) {}
    value_type operator*() const { return v[i]; }
    bool operator==(const const_iterator& rhs) const { return i == rhs.i; }
    bool operator!=(const const_iterator& rhs) const { return i != rhs.i; }
    void operator++() { ++i; }
  };

  vector(size_t n = 0u) : v(n) {}
  vector(size_t n, value_type default_value) : v(n) {
    for (auto& value : v) {
      value = reinterpret_cast<contained_value_type&>(default_value);
    }
  }

  bool empty() const { return v.empty(); }
  size_t size() const { return v.size(); }
  void resize(size_t n) { v.resize(n); }
  value_type& operator[](size_t i) { return reinterpret_cast<value_type&>(v[i]); }
  value_type operator[](size_t i) const { return reinterpret_cast<const value_type&>(v[i]); }

  value_type* data() { return v.empty() ? nullptr : reinterpret_cast<value_type*>(&v[0]); }
  const value_type* data() const { return v.empty() ? nullptr : reinterpret_cast<const value_type*>(&v[0]); }

  iterator begin() { return iterator(data(), 0u); }
  iterator end() { return iterator(data(), v.size()); }
  const_iterator begin() const { return const_iterator(data(), 0u); }
  const_iterator end() const { return const_iterator(data(), v.size()); }
};

}  // namespace std

namespace fncas {
namespace impl {

struct X : std::vector<V>, noncopyable {
  using super_t = std::vector<V>;

  explicit X(size_t dim) {
    CURRENT_ASSERT(dim > 0);
    auto& meta = internals_singleton();
    if (meta.x_ptr_) {
      CURRENT_THROW(exceptions::FnCASConcurrentEvaluationAttemptException());
    }
    CURRENT_ASSERT(!meta.dim_);
    // Invalidates cached functions, resets temp nodes enumeration from zero and frees cache memory.
    meta.reset();
    meta.x_ptr_ = this;
    meta.dim_ = dim;

    // Initialize the actual `vector<V>`.
    super_t::resize(internals_singleton().dim_);
    for (size_t i = 0; i < super_t::size(); ++i) {
      super_t::operator[](i) = V::variable(i);
    }
  }

  ~X() {
    auto& meta = internals_singleton();
    if (meta.x_ptr_ == this) {
      // The condition is required to correctly handle the case when the constructor did `throw`.
      meta.x_ptr_ = nullptr;
      meta.dim_ = 0;
    }
  }
};

// Class "f" is the placeholder for function evaluators.
// One implementation -- f_intermediate -- is provided by default.
// Compiled implementations using the same interface are defined in fncas_jit.h.

struct f : noncopyable {
  virtual ~f() = default;
  // The evaluator of the function.
  virtual double_t operator()(const std::vector<double_t>& x) const = 0;
  // The dimensionality of the parameters vector for the function.
  virtual size_t dim() const = 0;
  // The number of external `double_t` "registers" required to compute it, for compiled versions.
  virtual size_t heap_size() const { return 0; }
};

struct f_native final : f {
  std::function<double_t(const std::vector<double_t>&)> f_;
  size_t dim_;
  f_native(std::function<double_t(std::vector<double_t>)> f, size_t d) : f_(f), dim_(d) {}
  double_t operator()(const std::vector<double_t>& x) const override { return f_(x); }
  size_t dim() const override { return dim_; }
};

struct f_intermediate final : f {
  const V f_;
  f_intermediate(const V& f) : f_(f) {}
  f_intermediate(f_intermediate&& rhs) : f_(rhs.f_) {}
  double_t operator()(const std::vector<double_t>& x) const override {
    CURRENT_ASSERT(x.size() == dim());
    return f_(x);
  }
  std::string debug_as_string() const { return f_.debug_as_string(); }
  // Template is used here as a form of forward declaration.
  template <typename TX>
  V differentiate(const TX& x_ref, size_t variable_index) const {
    static_assert(std::is_same<TX, X>::value, "f_intermediate::differentiate(const x& x, size_t variable_index);");
    CURRENT_ASSERT(&x_ref == internals_singleton().x_ptr_);
    CURRENT_ASSERT(variable_index >= 0);
    CURRENT_ASSERT(variable_index < dim());
    return f_.template differentiate<X>(x_ref, variable_index);
  }
  size_t dim() const override { return internals_singleton().dim_; }
};

// Helper code to allow writing polymorphic functions that can be both evaluated and recorded.
// Type `V` describes one value (`double`), type `X` describes an array of values (`std::vector<double>`),
// although `double` is in fact `double_t`.
// Synopsis: `fncas::impl::X2V<X> f(const X& x)` or `V f(const fncas::impl::V2X<V>& x);`.

template <typename T>
struct x2v_impl {};

template <typename T>
struct x2v_impl<std::vector<T>> {
  typedef T type;
};
template <>
struct x2v_impl<X> {
  typedef V type;
};

template <typename T>
struct v2x_impl {
  typedef std::vector<T> type;
};
template <>
struct v2x_impl<V> {
  typedef X type;
};

template <typename X>
using X2V = typename x2v_impl<X>::type;
template <typename V>
using V2X = typename v2x_impl<V>::type;

}  // namespace fncas::impl
}  // namespace fncas

// Arithmetic operations and mathematical functions are defined outside namespace fncas::impl.

#define DECLARE_OP(OP, OP2, NAME)                                                             \
  inline fncas::impl::V operator OP(const fncas::impl::V& lhs, const fncas::impl::V& rhs) {   \
    fncas::impl::V result;                                                                    \
    result.type() = fncas::impl::type_t::operation;                                           \
    result.operation() = fncas::impl::operation_t::NAME;                                      \
    result.lhs_index() = lhs.index_;                                                          \
    result.rhs_index() = rhs.index_;                                                          \
    return result;                                                                            \
  }                                                                                           \
  inline const fncas::impl::V& operator OP2(fncas::impl::V& lhs, const fncas::impl::V& rhs) { \
    lhs = lhs OP rhs;                                                                         \
    return lhs;                                                                               \
  }

DECLARE_OP(+, +=, add);
DECLARE_OP(-, -=, subtract);
DECLARE_OP(*, *=, multiply);
DECLARE_OP(/, /=, divide);

// NOTE(dkorolev): This `using namespace std` declaration within `fncas` should be here, not above.
namespace fncas {
using namespace std;
}  // namespace fncas

#ifndef INJECT_FNCAS_INTO_NAMESPACE_STD
// Put the "compile-as-you-execute" implementations of math functions into `fncas::impl::functions::`.
#define DECLARE_FUNCTION(F)                                 \
  namespace fncas {                                         \
  inline fncas::impl::V F(const fncas::impl::V& argument) { \
    fncas::impl::V result;                                  \
    result.type() = fncas::impl::type_t::function;          \
    result.function() = fncas::impl::function_t::F;         \
    result.argument_index() = argument.index_;              \
    return result;                                          \
  }                                                         \
  }                                                         \
  namespace fncas {                                         \
  namespace impl {                                          \
  using ::fncas::F;                                         \
  }                                                         \
  }
#else
// Expose math functions into `std::` as well.
// NOTE: This is in violation of `C++11: 17.6.4.2.1/1`, and hence guarded. CC @dkorolev, @mzhurovich.
#define DECLARE_FUNCTION(F)                                 \
  namespace fncas {                                         \
  inline fncas::impl::V F(const fncas::impl::V& argument) { \
    fncas::impl::V result;                                  \
    result.type() = fncas::impl::type_t::function;          \
    result.function() = fncas::impl::function_t::F;         \
    result.argument_index() = argument.index_;              \
    return result;                                          \
  }                                                         \
  }                                                         \
  namespace std {                                           \
  using ::fncas::F;                                         \
  }
#endif

DECLARE_FUNCTION(sqr);
DECLARE_FUNCTION(sqrt);
DECLARE_FUNCTION(exp);
DECLARE_FUNCTION(log);
DECLARE_FUNCTION(sin);
DECLARE_FUNCTION(cos);
DECLARE_FUNCTION(tan);
DECLARE_FUNCTION(asin);
DECLARE_FUNCTION(acos);
DECLARE_FUNCTION(atan);
DECLARE_FUNCTION(unit_step);
DECLARE_FUNCTION(ramp);

// Unary plus and unary minus.
inline fncas::impl::V operator+(const fncas::impl::V& x) { return x; }
inline fncas::impl::V operator-(const fncas::impl::V& x) { return 0.0 - x; }

namespace fncas {

// A smart `double`: behaves like one, but allows for function recording, differentiation, and JIT.
using term_t = impl::V;

// A smart `vector<double>`: behaves like one, but allows for function recording, differentiation, and JIT.
using term_vector_t = std::vector<term_t>;

// Extends `term_vector_t` to be used as the "default" parameter to user functions.
// Passing a `variables_vector_t x(10)` as the parameter stands for "record the expression of this function
// assuming it takes a 10-dimensional vector as the parameter".
// The instance of the `variables_vector_t` is what maintains the state of the thread-local singleton corresponding
// to the function being recorded, and thus at most one `variables_vector_t` per thread can exist at any given
// point in time.
using variables_vector_t = impl::X;

// TODO(dkorolev): Clean up the namespaces.
using NativeFunction = impl::f_native;
using SlowFunction = impl::f_intermediate;

}  // namespace fncas

#endif  // #ifndef FNCAS_FNCAS_NODE_H
