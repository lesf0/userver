#pragma once

#include <type_traits>
#include <unordered_map>

#include <cache/cache_statistics.hpp>
#include <cache/caching_component_base.hpp>

#include <storages/postgres/cluster.hpp>
#include <storages/postgres/component.hpp>

#include <utils/strlen.hpp>
#include <utils/void_t.hpp>

namespace components {

/// @page pg_cache Caching Component for PostgreSQL
///
/// @par Configuration
///
/// PostgreSQL component name must be specified in `pgcomponent` configuration
/// parameter.
///
/// @par Cache policy
///
/// Cache policy is the template argument of component. Please see the following
/// code snippet for documentation.
///
/// @snippet cache/postgres_cache_test.cpp Pg Cache Policy Example

namespace pg_cache::detail {

template <typename T>
using ValueType = typename T::ValueType;

template <typename T, typename = ::utils::void_t<>>
struct HasValueType : std::false_type {};
template <typename T>
struct HasValueType<T, ::utils::void_t<typename T::ValueType>>
    : std::true_type {};
template <typename T>
constexpr bool kHasValueType = HasValueType<T>::value;

// Component name in policy
template <typename T, typename = ::utils::void_t<>>
struct HasName : std::false_type {};
template <typename T>
struct HasName<T, ::utils::void_t<decltype(T::kName)>>
    : std::integral_constant<bool, (T::kName != nullptr) &&
                                       (::utils::StrLen(T::kName) > 0)> {};
template <typename T>
constexpr bool kHasName = HasName<T>::value;

// Component query in policy
template <typename T, typename = ::utils::void_t<>>
struct HasQuery : std::false_type {};
template <typename T>
struct HasQuery<T, ::utils::void_t<decltype(T::kQuery)>>
    : std::integral_constant<bool, (T::kQuery != nullptr) &&
                                       (::utils::StrLen(T::kQuery) > 0)> {};
template <typename T>
constexpr bool kHasQuery = HasQuery<T>::value;

// Update field
template <typename T, typename = ::utils::void_t<>>
struct HasUpdateField : std::false_type {};
template <typename T>
struct HasUpdateField<T, ::utils::void_t<decltype(T::kUpdatedField)>>
    : std::true_type {};
template <typename T>
constexpr bool kHasUpdateField = HasUpdateField<T>::value;

template <typename T, typename = ::utils::void_t<>>
struct WantIncrementalUpdates : std::false_type {};
template <typename T>
struct WantIncrementalUpdates<T, ::utils::void_t<decltype(T::kUpdatedField)>>
    : std::integral_constant<bool,
                             (T::kUpdatedField != nullptr) &&
                                 (::utils::StrLen(T::kUpdatedField) > 0)> {};
template <typename T>
constexpr bool kWantIncrementalUpdates = WantIncrementalUpdates<T>::value;

// Key member in policy
template <typename T, typename = ::utils::void_t<>>
struct HasKeyMember : std::false_type {};
template <typename T>
struct HasKeyMember<T, ::utils::void_t<decltype(T::kKeyMember)>>
    : std::true_type {};
template <typename T>
constexpr bool kHasKeyMember = HasKeyMember<T>::value;

template <typename T>
constexpr auto GetKeyValue(const ValueType<T>& v) {
  if constexpr (std::is_member_function_pointer<decltype(T::kKeyMember)>{}) {
    return (v.*T::kKeyMember)();
  } else {
    return v.*T::kKeyMember;
  }
}

template <typename T>
struct KeyMember {
  using type =
      std::decay_t<decltype(GetKeyValue<T>(std::declval<ValueType<T>>()))>;
};
template <typename T>
using KeyMemberType = typename KeyMember<T>::type;

// Data container for cache
template <typename T, typename = ::utils::void_t<>>
struct DataCacheContainer {
  using type = std::unordered_map<KeyMemberType<T>, ValueType<T>>;
};

template <typename T>
struct DataCacheContainer<T, ::utils::void_t<typename T::CacheContainer>> {
  using type = typename T::CacheContainer;
  // TODO Checks that the type is some sort of a map
};

template <typename T>
using DataCacheContainerType = typename DataCacheContainer<T>::type;

// Cluster host type policy
template <typename T, typename = ::utils::void_t<>>
struct PostgresClusterType
    : std::integral_constant<storages::postgres::ClusterHostType,
                             storages::postgres::ClusterHostType::kSlave> {};
template <typename T>
struct PostgresClusterType<T, ::utils::void_t<decltype(T::kClusterHostType)>>
    : std::integral_constant<storages::postgres::ClusterHostType,
                             T::kClusterHostType> {};

template <typename T>
constexpr storages::postgres::ClusterHostType kPostgresClusterType =
    PostgresClusterType<T>::value;

template <typename PostgreCachePolicy>
struct PolicyChecker {
  // Static assertions for cache traits
  static_assert(
      kHasName<PostgreCachePolicy>,
      "The PosgreSQL cache policy must contain a static member `kName`");
  static_assert(
      kHasValueType<PostgreCachePolicy>,
      "The PosgreSQL cache policy must define a type alias `ValueType`");
  static_assert(
      kHasKeyMember<PostgreCachePolicy>,
      "The PostgreSQL cache policy must contain a static member `kKeyMember` "
      "with a pointer to a data or a function member with the object's key");
  static_assert(kHasQuery<PostgreCachePolicy>,
                "The PosgreSQL cache policy must contain a static member "
                "`kQuery` with a select statement");
  static_assert(
      kHasUpdateField<PostgreCachePolicy>,
      "The PosgreSQL cache policy must contain a static member "
      "`kUpdatedField`. If you don't want to use incremental updates, "
      "please set its value to `nullptr`");

  static_assert(kPostgresClusterType<PostgreCachePolicy> !=
                    storages::postgres::ClusterHostType::kAny,
                "`Any` cluster host type cannot be used for caching component, "
                "please be more specific");

  using BaseType =
      CachingComponentBase<DataCacheContainerType<PostgreCachePolicy>>;
};

}  // namespace pg_cache::detail

template <typename PostgreCachePolicy>
class PostgreCache
    : public pg_cache::detail::PolicyChecker<PostgreCachePolicy>::BaseType {
 public:
  // Type aliases
  using PolicyType = PostgreCachePolicy;
  using ValueType = pg_cache::detail::ValueType<PolicyType>;
  using DataType = pg_cache::detail::DataCacheContainerType<PolicyType>;
  using BaseType =
      typename pg_cache::detail::PolicyChecker<PostgreCachePolicy>::BaseType;

  // Calculated constants
  constexpr static bool kIncrementalUpdates =
      pg_cache::detail::kWantIncrementalUpdates<PolicyType>;
  constexpr static storages::postgres::ClusterHostType kClusterHostType =
      pg_cache::detail::kPostgresClusterType<PolicyType>;
  constexpr static auto kName = PolicyType::kName;

  PostgreCache(const ComponentConfig&, const ComponentContext&);
  ~PostgreCache();

 private:
  using CachedData = std::shared_ptr<DataType>;

  void Update(cache::UpdateType type,
              const std::chrono::system_clock::time_point& last_update,
              const std::chrono::system_clock::time_point& now,
              cache::UpdateStatisticsScope& stats_scope) override;

  CachedData GetData(cache::UpdateType type);
  void CacheResults(storages::postgres::ResultSet res, CachedData data_cache);

  static std::string GetDeltaQuery();

  static inline const std::string kAllQuery = PolicyType::kQuery;
  static inline const std::string kDeltaQuery = GetDeltaQuery();

  std::vector<storages::postgres::ClusterPtr> clusters_;
};

template <typename PostgreCachePolicy>
PostgreCache<PostgreCachePolicy>::PostgreCache(const ComponentConfig& config,
                                               const ComponentContext& context)
    : BaseType{config, context, kName} {
  const auto pg_alias = config.ParseString("pgcomponent", {});
  if (pg_alias.empty()) {
    throw storages::postgres::InvalidConfig{
        "No `pgcomponent` entry in configuration"};
  }
  auto& pg_cluster_comp = context.FindComponent<components::Postgres>(pg_alias);
  const auto shard_count = pg_cluster_comp.GetShardCount();
  clusters_.resize(shard_count);
  for (size_t i = 0; i < shard_count; ++i) {
    clusters_[i] = pg_cluster_comp.GetClusterForShard(i);
  }

  LOG_INFO() << "Cache " << kName << " full update query `" << kAllQuery
             << "` incremental update query `" << kDeltaQuery << "`";

  this->StartPeriodicUpdates();
}

template <typename PostgreCachePolicy>
PostgreCache<PostgreCachePolicy>::~PostgreCache() {
  this->StopPeriodicUpdates();
}

template <typename PostgreCachePolicy>
std::string PostgreCache<PostgreCachePolicy>::GetDeltaQuery() {
  using namespace std::string_literals;
  if constexpr (kIncrementalUpdates) {
    return PolicyType::kQuery + " where "s + PolicyType::kUpdatedField +
           " >= $1";
  } else {
    return PolicyType::kQuery;
  }
}

template <typename PostgreCachePolicy>
void PostgreCache<PostgreCachePolicy>::Update(
    cache::UpdateType type,
    const std::chrono::system_clock::time_point& last_update,
    const std::chrono::system_clock::time_point& /*now*/,
    cache::UpdateStatisticsScope& stats_scope) {
  if constexpr (!kIncrementalUpdates) {
    type = cache::UpdateType::kFull;
  }
  const std::string& query =
      (type == cache::UpdateType::kFull) ? kAllQuery : kDeltaQuery;
  // COPY current cached data
  auto data_cache = GetData(type);
  size_t changes = 0;
  // Iterate clusters
  for (auto cluster : clusters_) {
    try {
      auto res = cluster->Execute(kClusterHostType, query, last_update);
      stats_scope.IncreaseDocumentsParseFailures(res.Size());
      CacheResults(res, data_cache);
      changes += res.Size();
    } catch (const std::exception& e) {
      stats_scope.IncreaseDocumentsParseFailures(1);
    }
  }
  if (changes > 0) {
    // Set current cache
    stats_scope.Finish(data_cache->size());
    this->Set(std::move(data_cache));
  } else {
    stats_scope.FinishNoChanges();
  }
}

template <typename PostgreCachePolicy>
void PostgreCache<PostgreCachePolicy>::CacheResults(
    storages::postgres::ResultSet res, CachedData data_cache) {
  auto values = res.AsSetOf<ValueType>();
  for (auto value : values) {
    auto key = pg_cache::detail::GetKeyValue<PolicyType>(value);
    data_cache->insert({std::move(key), std::move(value)});
  }
}

template <typename PostgreCachePolicy>
typename PostgreCache<PostgreCachePolicy>::CachedData
PostgreCache<PostgreCachePolicy>::GetData(cache::UpdateType type) {
  if (type == cache::UpdateType::kIncremental) {
    auto data = this->Get();
    if (data) {
      return std::make_shared<DataType>(*data);
    }
  }
  return std::make_shared<DataType>();
}

}  // namespace components