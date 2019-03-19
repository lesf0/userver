#pragma once

#include <string>
#include <unordered_map>

#include <formats/json/value.hpp>

namespace storages::mongo_ng::secdist {

class MongoSettings {
 public:
  explicit MongoSettings(const formats::json::Value& doc);

  const std::string& GetConnectionString(const std::string& dbalias) const;

 private:
  std::unordered_map<std::string, std::string> settings_;
};

}  // namespace storages::mongo_ng::secdist