#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "drake/common/drake_throw.h"
#include "drake/common/unused.h"
#include "drake/systems/framework/cache_entry.h"
#include "drake/systems/framework/framework_common.h"

namespace drake {
namespace systems {

/** Provides non-templatized functionality shared by the templatized System
classes.

Terminology: in general a Drake System is a tree structure composed of
"subsystems", which are themselves System objects. The corresponding Context is
a parallel tree structure composed of "subcontexts", which are themselves
Context objects. There is a one-to-one correspondence between subsystems and
subcontexts. Within a given System (Context), its child subsystems (subcontexts)
are indexed using a SubsystemIndex; there is no separate SubcontextIndex since
the numbering must be identical. */
class SystemBase : public internal::SystemMessageInterface {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(SystemBase)

  ~SystemBase() override;

  /** Sets the name of the system. Do not use the path delimiter character '/'
  in the name. When creating a Diagram, names of sibling subsystems should be
  unique. */
  // TODO(sherm1) Enforce reasonable naming policies.
  void set_name(const std::string& name) { name_ = name; }

  /** Returns the name last supplied to set_name(), or a default name if
  set_name() was never called. Systems with an empty name that are added to a
  Diagram will have a default name automatically assigned. Systems created
  by copying with a scalar type change have the same name as the source
  system. */
  const std::string& GetSystemName() const final { return name_; }

  /** Generates and returns the full path name of this subsystem, starting from
  the root System, with '/' delimiters between parent and child subsystems. */
  std::string GetSystemPathname() const final;

  /** Returns the most-derived type of this concrete System object as a
  human-readable string suitable for use in error messages. */
  std::string GetSystemType() const final { return NiceTypeName::Get(*this); }

  /** Throws an exception with an appropriate message if the given `context` is
  not compatible with this System. Restrictions may vary for different systems;
  the error message should explain. This can be an expensive check so you may
  want to limit it to Debug builds. */
  void ThrowIfContextNotCompatible(const ContextBase& context) const final {
    CheckValidContext(context);
  }

  /** Returns a Context suitable for use with this System. Context resources
  are allocated based on resource requests that were made during System
  construction. */
  // TODO(sherm1) Split this into phases as needed for caching.
  std::unique_ptr<ContextBase> AllocateContext() const;

  /** Returns the number nc of cache entries currently allocated in this System.
  These are indexed from 0 to nc-1. */
  int num_cache_entries() const {
    return static_cast<int>(cache_entries_.size());
  }

  /** Return a reference to a CacheEntry given its `index`. */
  const CacheEntry& get_cache_entry(CacheIndex index) const {
    DRAKE_ASSERT(0 <= index && index < num_cache_entries());
    return *cache_entries_[index];
  }

  // TODO(sherm1) Consider whether to make DeclareCacheEntry methods protected.
  //============================================================================
  /** @name                    Declare cache entries
  @anchor DeclareCacheEntry_documentation

  Methods in this section are used by derived classes to declare cache entries
  for their own internal computations. (Other cache entries are provided
  automatically for well-known computations such as output ports and time
  derivatives.) Cache entries may contain values of any type, however the type
  for any particular cache entry is fixed after first allocation. Every cache
  entry must have an _allocator_ function `Alloc()` and a _calculator_ function
  `Calc()`. `Alloc()` returns an object suitable for holding a value of the
  cache entry. `Calc()` uses the contents of a given Context to produce the
  cache entry's value, which is placed in an object of the type returned by
  `Alloc()`.

  <h4>Prerequisites</h4>

  Correct runtime caching behavior depends critically on understanding the
  dependencies of the cache entry's `Calc()` function (we call those
  "prerequisites"). If none of the prerequisites has changed since the last
  time `Calc()` was invoked to set the cache entry's value, then we don't need
  to perform a potentially expensive recalculation. On the other hand, if any
  of the prerequisites has changed then the current value is invalid and must
  not be used without first recomputing.

  Currently it is not possible for Drake to infer prerequisites accurately and
  automatically from inspection of the `Calc()` implementation. Therefore,
  if you don't say otherwise, Drake will assume `Calc()` is dependent
  on all value sources in the Context, including time, state, input ports,
  parameters, and accuracy. That means the cache entry's value will be
  considered invalid if _any_ of those sources has changed since the last time
  the value was calculated. That is safe, but can result in more computation
  than necessary. If you know that your `Calc()` method has fewer prerequisites,
  you may say so by providing an explicit list in the `prerequisites_of_calc`
  parameter. Every possible prerequisite has a DependencyTicket ("ticket"), and
  the list should consist of tickets. For example, if your calculator depends
  only on time (e.g. `Calc(context)` is `sin(context.get_time())`) then you
  would specify `prerequisites_of_calc={time_ticket()}` here. See
  @ref DependencyTicket_documentation "Dependency tickets" for a list of the
  possible tickets and what they mean.

  @warning It is critical that the prerequisite list you supply be accurate, or
  at least conservative, for correct functioning of the caching system. Drake
  cannot currently detect that a `Calc()` function accesses an undeclared
  prerequisite. Even assuming you have correctly listed the prerequisites, you
  should include a prominent comment in every `Calc()` implementation noting
  that if the implementation is changed then the prerequisite list must be
  updated correspondingly.

  A technique you can use to ensure that prerequisites have been properly
  specified is to make use of the Context's
  @ref drake::systems::ContextBase::DisableCaching "DisableCaching()"
  method, which causes cache values to be recalculated unconditionally. You
  should get identical results with caching enabled or disabled, with speed
  being the only difference.
  @see drake::systems::ContextBase::DisableCaching()

  <h4>Which signature to use?</h4>

  Although the allocator and calculator functions ultimately satisfy generic
  function signatures defined in CacheEntry, we provide a variety
  of `DeclareCacheEntry()` signatures here for convenient specification,
  with mapping to the generic form handled invisibly. In particular,
  allocators are most easily defined by providing a model value that can be
  used to construct an allocator that copies the model when a new value
  object is needed. Alternatively a method can be provided that constructs
  a value object when invoked (those methods are conventionally, but not
  necessarily, named `MakeSomething()` where `Something` is replaced by the
  cache entry value type).

  Because cache entry values are ultimately stored in AbstractValue objects,
  the underlying types must be suitable. That means the type must be copy
  constructible or cloneable. For methods below that are not given an explicit
  model value or construction ("make") method, the underlying type must also be
  default constructible.
  @see drake::systems::Value for more about abstract values. */
  //@{

  /** Declares a new %CacheEntry in this System using the least-restrictive
  definitions for the associated functions. Prefer one of the more-convenient
  signatures below if you can. The new cache entry is assigned a unique
  CacheIndex and DependencyTicket, which can be obtained from the returned
  %CacheEntry. The function signatures here are:
  @code
    std::unique_ptr<AbstractValue> Alloc(const ContextBase&);
    void Calc(const ContextBase&, AbstractValue*);
  @endcode
  where the AbstractValue objects must resolve to the same concrete type.

  @param[in] description
    A human-readable description of this cache entry, most useful for debugging
    and documentation. Not interpreted in any way by Drake; it is retained
    by the cache entry and used to generate the description for the
    corresponding CacheEntryValue in the Context.
  @param[in] alloc_function
    Given a Context, returns a heap-allocated AbstractValue object suitable for
    holding a value for this cache entry.
  @param[in] calc_function
    Provides the computation that maps from a given Context to the current
    value that this cache entry should have, and writes that value to a given
    object of the type returned by `alloc_function`.
  @param[in] prerequisites_of_calc
    Provides the DependencyTicket list containing a ticket for _every_ Context
    value on which `calc_function` may depend when it computes its result.
    Defaults to `{all_sources_ticket()}` if unspecified. If the cache value
    is truly independent of the Context (rare!) say so explicitly by providing
    the list `{nothing_ticket()}`; an explicitly empty list `{}` is forbidden.
  @returns a const reference to the newly-created %CacheEntry.
  @throws std::logic_error if given an explicitly empty prerequisite list. */
  const CacheEntry& DeclareCacheEntry(
      std::string description, CacheEntry::AllocCallback alloc_function,
      CacheEntry::CalcCallback calc_function,
      std::vector<DependencyTicket> prerequisites_of_calc = {
          all_sources_ticket()});

  /** Declares a cache entry by specifying member functions to use both for the
  allocator and calculator. The signatures are: @code
    ValueType MySystem::MakeValueType(const MyContext&) const;
    void MySystem::CalcCacheValue(const MyContext&, ValueType*) const;
  @endcode
  where `MySystem` is a class derived from `SystemBase`, `MyContext` is a class
  derived from `ContextBase`, and `ValueType` is any concrete type such that
  `Value<ValueType>` is permitted. (The method names are arbitrary.) Template
  arguments will be deduced and do not need to be specified. See the first
  DeclareCacheEntry() signature above for more information about the parameters
  and behavior.
  @see drake::systems::Value */
  template <class MySystem, class MyContext, typename ValueType>
  const CacheEntry& DeclareCacheEntry(
      std::string description,
      ValueType (MySystem::*make)(const MyContext&) const,
      void (MySystem::*calc)(const MyContext&, ValueType*) const,
      std::vector<DependencyTicket> prerequisites_of_calc = {
          all_sources_ticket()});

  /** Declares a cache entry by specifying a model value of concrete type
  `ValueType` and a calculator function that is a class member function (method)
  with signature: @code
    void MySystem::CalcCacheValue(const MyContext&, ValueType*) const;
  @endcode
  where `MySystem` is a class derived from `SystemBase`, `MyContext` is a class
  derived from `ContextBase`, and `ValueType` is any concrete type such that
  `Value<ValueType>` is permitted. (The method names are arbitrary.) Template
  arguments will be deduced and do not need to be specified.
  See the first DeclareCacheEntry() signature above for more information about
  the parameters and behavior.
  @see drake::systems::Value */
  template <class MySystem, class MyContext, typename ValueType>
  const CacheEntry& DeclareCacheEntry(
      std::string description, const ValueType& model_value,
      void (MySystem::*calc)(const MyContext&, ValueType*) const,
      std::vector<DependencyTicket> prerequisites_of_calc = {
          all_sources_ticket()});

  /** Declares a cache entry by specifying only a calculator function that is a
  class member function (method) with signature:
  @code
    void MySystem::CalcCacheValue(const MyContext&, ValueType*) const;
  @endcode
  where `MySystem` is a class derived from `SystemBase` and `MyContext` is a
  class derived from `ContextBase`. `ValueType` is a concrete type such that
  (a) `Value<ValueType>` is permitted, and (b) `ValueType` is default
  constructible. That allows us to create a model value using
  `Value<ValueType>{}` (value initialized so numerical types will be zeroed in
  the model). (The method name is arbitrary.) Template arguments will be
  deduced and do not need to be specified. See the first DeclareCacheEntry()
  signature above for more information about the parameters and behavior.

  @note The default constructor will be called once immediately to create a
  model value, and subsequent allocations will just copy the model value without
  invoking the constructor again. If you want the constructor invoked again at
  each allocation (not common), use one of the other signatures to explicitly
  provide a method for the allocator to call; that method can then invoke
  the `ValueType` default constructor each time it is called.
  @see drake::systems::Value */
  template <class MySystem, class MyContext, typename ValueType>
  const CacheEntry& DeclareCacheEntry(
      std::string description,
      void (MySystem::*calc)(const MyContext&, ValueType*) const,
      std::vector<DependencyTicket> prerequisites_of_calc = {
          all_sources_ticket()});
  //@}

  /** Checks whether the given context is valid for this System and throws
  an exception with a helpful message if not. This is *very* expensive and
  should generally be done only in Debug builds, like this:
  @code
     DRAKE_ASSERT_VOID(CheckValidContext(context));
  @endcode */
  void CheckValidContext(const ContextBase& context) const {
    // TODO(sherm1) Add base class checks.

    // Let derived classes have their say.
    DoCheckValidContext(context);
  }

  //============================================================================
  /** @name                     Dependency tickets
  @anchor DependencyTicket_documentation

  Use these tickets to declare well-known sources as prerequisites of a
  downstream computation such as an output port, derivative, update, or cache
  entry. The ticket numbers for these sources are the same for all subsystems.
  For time and accuracy they refer to the same global resource; otherwise they
  refer to the specified sources within the referencing subsystem.

  A dependency ticket for a more specific resource (a particular input or
  output port, a discrete variable group, abstract state variable, a parameter,
  or a cache entry) is allocated and stored with the resource when it is
  declared. Usually the tickets are obtained directly from the resource but
  you can recover them with methods here knowing only the resource index. */
  //@{

  /** Returns a ticket indicating dependence on every possible independent
  source value, including time, state, input ports, parameters, and the accuracy
  setting (but not cache entries). This is the default dependency for
  computations that have not specified anything more refined. */
  static DependencyTicket all_sources_ticket() {
    return DependencyTicket(internal::kAllSourcesTicket);
  }

  /** Returns a ticket indicating that a computation does not depend on *any*
  source value; that is, it is a constant. If this appears in a prerequisite
  list, it must be the only entry. */
  static DependencyTicket nothing_ticket() {
    return DependencyTicket(internal::kNothingTicket);
  }

  /** Returns a ticket indicating dependence on time. This is the same ticket
  for all subsystems and refers to the same time value. */
  static DependencyTicket time_ticket() {
    return DependencyTicket(internal::kTimeTicket);
  }

  /** Returns a ticket indicating dependence on the accuracy setting in the
  Context. This is the same ticket for all subsystems and refers to the same
  accuracy value. */
  static DependencyTicket accuracy_ticket() {
    return DependencyTicket(internal::kAccuracyTicket);
  }

  /** Returns a ticket indicating that a computation depends on configuration
  state variables q. */
  static DependencyTicket q_ticket() {
    return DependencyTicket(internal::kQTicket);
  }

  /** Returns a ticket indicating dependence on velocity state variables v. This
  does _not_ also indicate a dependence on configuration variables q -- you must
  list that explicitly or use kinematics_ticket() instead. */
  static DependencyTicket v_ticket() {
    return DependencyTicket(internal::kVTicket);
  }

  /** Returns a ticket indicating dependence on all of the miscellaneous
  continuous state variables z. */
  static DependencyTicket z_ticket() {
    return DependencyTicket(internal::kZTicket);
  }

  /** Returns a ticket indicating dependence on all of the continuous
  state variables q, v, or z. */
  static DependencyTicket xc_ticket() {
    return DependencyTicket(internal::kXcTicket);
  }

  /** Returns a ticket indicating dependence on all of the numerical
  discrete state variables, in any discrete variable group. */
  static DependencyTicket xd_ticket() {
    return DependencyTicket(internal::kXdTicket);
  }

  /** Returns a ticket indicating dependence on all of the abstract
  state variables in the current Context. */
  static DependencyTicket xa_ticket() {
    return DependencyTicket(internal::kXaTicket);
  }

  /** Returns a ticket indicating dependence on _all_ state variables x in this
  subsystem, including continuous variables xc, discrete (numeric) variables xd,
  and abstract state variables xa. This does not imply dependence on time,
  parameters, or inputs; those must be specified separately. If you mean to
  express dependence on all possible value sources, use all_sources_ticket()
  instead. */
  static DependencyTicket all_state_ticket() {
    return DependencyTicket(internal::kXTicket);
  }

  /** Returns a ticket for the cache entry that holds time derivatives of
  the continuous variables. */
  static DependencyTicket xcdot_ticket() {
    return DependencyTicket(internal::kXcdotTicket);
  }

  /** Returns a ticket for the cache entry that holds the discrete state
  update for the numerical discrete variables in the state. */
  static DependencyTicket xdhat_ticket() {
    return DependencyTicket(internal::kXdhatTicket);
  }

  /** Returns a ticket indicating dependence on all the configuration
  variables for this System. By default this is set to the continuous
  second-order state variables q, but configuration may be represented
  differently in some systems (discrete ones, for example), in which case this
  ticket should have been set to depend on that representation. */
  static DependencyTicket configuration_ticket() {
    return DependencyTicket(internal::kConfigurationTicket);
  }

  /** Returns a ticket indicating dependence on all of the velocity variables
  for this System. By default this is set to the continuous state variables v,
  but velocity may be represented differently in some systems (discrete ones,
  for example), in which case this ticket should have been set to depend on that
  representation. */
  static DependencyTicket velocity_ticket() {
    return DependencyTicket(internal::kVelocityTicket);
  }

  /** Returns a ticket indicating dependence on all of the configuration
  and velocity state variables of this System. This ticket depends on the
  configuration_ticket and the velocity_ticket.
  @see configuration_ticket(), velocity_ticket() */
  static DependencyTicket kinematics_ticket() {
    return DependencyTicket(internal::kKinematicsTicket);
  }

  /** Returns a ticket indicating dependence on _all_ parameters p in this
  subsystem, including numeric parameters pn, and abstract parameters pa. */
  static DependencyTicket all_parameters_ticket() {
    return DependencyTicket(internal::kAllParametersTicket);
  }

  /** Returns a ticket indicating dependence on _all_ input ports u of this
  subsystem. */
  static DependencyTicket all_input_ports_ticket() {
    return DependencyTicket(internal::kAllInputPortsTicket);
  }

  /** Returns a ticket indicating dependence on a particular cache entry. */
  DependencyTicket cache_entry_ticket(CacheIndex index) {
    DRAKE_DEMAND(0 <= index && index < num_cache_entries());
    return cache_entries_[index]->ticket();
  }
  //@}

 protected:
  SystemBase() = default;

  /** Derived class implementations should allocate a suitable
  default-constructed Context, with default-constructed subcontexts for
  diagrams. The base class allocates trackers for known resources and
  intra-subcontext dependencies. No inter-subcontext dependencies should be
  made in this step. */
  virtual std::unique_ptr<ContextBase> DoMakeContext() const = 0;

  /** Derived classes must implement this to verify that the supplied
  context is suitable, and throw an exception if not. */
  virtual void DoCheckValidContext(const ContextBase&) const = 0;

 private:
  // Assigns the next unused dependency ticket number, unique only within a
  // particular subsystem. Each call to this method increments the
  // ticket number.
  DependencyTicket assign_next_dependency_ticket() {
    return next_available_ticket_++;
  }

  // Ports and cache entries hold their own DependencyTickets. Note that the
  // addresses of the elements are stable even if the std::vectors are resized.

  // Indexed by CacheIndex.
  std::vector<std::unique_ptr<CacheEntry>> cache_entries_;
  // TODO(sherm1) Add input and output ports here.

  // States and parameters don't hold their own tickets so we track them here.
  // TODO(sherm1) Add state & parameter trackers here.

  // Initialize to the first ticket number available after all the well-known
  // ones. This gets incremented as tickets are handed out for the optional
  // entities above.
  DependencyTicket next_available_ticket_{internal::kNextAvailableTicket};

  // Name of this subsystem.
  std::string name_;
};

// Implementations of templatized DeclareCacheEntry() methods.

// Takes make() and calc() member functions.
template <class MySystem, class MyContext, typename ValueType>
const CacheEntry& SystemBase::DeclareCacheEntry(
    std::string description,
    ValueType (MySystem::*make)(const MyContext&) const,
    void (MySystem::*calc)(const MyContext&, ValueType*) const,
    std::vector<DependencyTicket> prerequisites_of_calc) {
  static_assert(std::is_base_of<SystemBase, MySystem>::value,
                "Expected to be invoked from a SystemBase-derived System.");
  static_assert(std::is_base_of<ContextBase, MyContext>::value,
                "Expected to be invoked with a ContextBase-derived Context.");
  auto this_ptr = dynamic_cast<const MySystem*>(this);
  DRAKE_DEMAND(this_ptr != nullptr);
  auto alloc_callback = [this_ptr, make](const ContextBase& context) {
    const auto& typed_context = dynamic_cast<const MyContext&>(context);
    return AbstractValue::Make((this_ptr->*make)(typed_context));
  };
  auto calc_callback = [this_ptr, calc](const ContextBase& context,
                                        AbstractValue* result) {
    const auto& typed_context = dynamic_cast<const MyContext&>(context);
    ValueType& typed_result = result->GetMutableValue<ValueType>();
    (this_ptr->*calc)(typed_context, &typed_result);
  };
  // Invoke the general signature above.
  auto& entry = DeclareCacheEntry(
      std::move(description), std::move(alloc_callback),
      std::move(calc_callback), std::move(prerequisites_of_calc));
  return entry;
}

// Takes an initial value and calc() member function.
template <class MySystem, class MyContext, typename ValueType>
const CacheEntry& SystemBase::DeclareCacheEntry(
    std::string description, const ValueType& model_value,
    void (MySystem::*calc)(const MyContext&, ValueType*) const,
    std::vector<DependencyTicket> prerequisites_of_calc) {
  static_assert(std::is_base_of<SystemBase, MySystem>::value,
                "Expected to be invoked from a SystemBase-derived System.");
  static_assert(std::is_base_of<ContextBase, MyContext>::value,
                "Expected to be invoked with a ContextBase-derived Context.");
  auto this_ptr = dynamic_cast<const MySystem*>(this);
  DRAKE_DEMAND(this_ptr != nullptr);
  // The given model value may have *either* a copy constructor or a Clone()
  // method, since it just has to be suitable for containing in an
  // AbstractValue. We need to create a functor that is copy constructible,
  // so need to wrap the model value to give it a copy constructor. Drake's
  // copyable_unique_ptr does just that, so is suitable for capture by the
  // allocator functor here.
  copyable_unique_ptr<AbstractValue> owned_model(
      new Value<ValueType>(model_value));
  auto alloc_callback = [model = std::move(owned_model)](const ContextBase&) {
    return model->Clone();
  };
  auto calc_callback = [this_ptr, calc](const ContextBase& context,
                                        AbstractValue* result) {
    const auto& typed_context = dynamic_cast<const MyContext&>(context);
    ValueType& typed_result = result->GetMutableValue<ValueType>();
    (this_ptr->*calc)(typed_context, &typed_result);
  };
  auto& entry = DeclareCacheEntry(
      std::move(description), std::move(alloc_callback),
      std::move(calc_callback), std::move(prerequisites_of_calc));
  return entry;
}

// Takes just a calc() member function, value-initializes entry.
template <class MySystem, class MyContext, typename ValueType>
const CacheEntry& SystemBase::DeclareCacheEntry(
    std::string description,
    void (MySystem::*calc)(const MyContext&, ValueType*) const,
    std::vector<DependencyTicket> prerequisites_of_calc) {
  static_assert(std::is_base_of<SystemBase, MySystem>::value,
                "Expected to be invoked from a SystemBase-derived System.");
  static_assert(std::is_base_of<ContextBase, MyContext>::value,
                "Expected to be invoked with a ContextBase-derived Context.");
  static_assert(
      std::is_default_constructible<ValueType>::value,
      "SystemBase::DeclareCacheEntry(calc): the calc-only overload of "
      "this method requires that the output type has a default constructor");
  // Invokes the above model-value method. Note that value initialization {}
  // is required here.
  return DeclareCacheEntry(std::move(description), ValueType{}, calc,
                           std::move(prerequisites_of_calc));
}

}  // namespace systems
}  // namespace drake
