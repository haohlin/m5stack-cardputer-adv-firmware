#include "orca/protocol.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace orca {
namespace {

struct JsonValue {
  enum class Type { Null, Boolean, Number, String, Object, Array };

  Type type = Type::Null;
  bool boolean = false;
  std::uint64_t number = 0;
  std::string string;
  std::map<std::string, JsonValue> object;
  std::vector<JsonValue> array;
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& input) : input_(input) {}

  bool parse(JsonValue& output) {
    skipWhitespace();
    if (!parseValue(output)) return false;
    skipWhitespace();
    return offset_ == input_.size();
  }

 private:
  bool parseValue(JsonValue& output) {
    if (offset_ >= input_.size()) return false;
    switch (input_[offset_]) {
      case '{': return parseObject(output);
      case '[': return parseArray(output);
      case '"': {
        output.type = JsonValue::Type::String;
        return parseString(output.string);
      }
      case 't':
        if (!consume("true")) return false;
        output.type = JsonValue::Type::Boolean;
        output.boolean = true;
        return true;
      case 'f':
        if (!consume("false")) return false;
        output.type = JsonValue::Type::Boolean;
        output.boolean = false;
        return true;
      case 'n':
        if (!consume("null")) return false;
        output.type = JsonValue::Type::Null;
        return true;
      default: return parseNumber(output);
    }
  }

  bool parseObject(JsonValue& output) {
    if (!take('{')) return false;
    output.type = JsonValue::Type::Object;
    output.object.clear();
    skipWhitespace();
    if (take('}')) return true;
    for (;;) {
      std::string key;
      if (!parseString(key)) return false;
      skipWhitespace();
      if (!take(':')) return false;
      skipWhitespace();
      JsonValue value;
      if (!parseValue(value) || !output.object.emplace(std::move(key), std::move(value)).second) {
        return false;
      }
      skipWhitespace();
      if (take('}')) return true;
      if (!take(',')) return false;
      skipWhitespace();
    }
  }

  bool parseArray(JsonValue& output) {
    if (!take('[')) return false;
    output.type = JsonValue::Type::Array;
    output.array.clear();
    skipWhitespace();
    if (take(']')) return true;
    for (;;) {
      JsonValue value;
      if (!parseValue(value)) return false;
      output.array.push_back(std::move(value));
      skipWhitespace();
      if (take(']')) return true;
      if (!take(',')) return false;
      skipWhitespace();
    }
  }

  static bool appendCodepoint(std::string& output, std::uint32_t codepoint) {
    if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
      return false;
    }
    if (codepoint <= 0x7fu) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
      output.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
      output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
      output.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
      output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
      output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
      output.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
      output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
      output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
      output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
    return true;
  }

  bool readHex4(std::uint32_t& output) {
    if (offset_ + 4 > input_.size()) return false;
    output = 0;
    for (int i = 0; i < 4; ++i) {
      const unsigned char c = static_cast<unsigned char>(input_[offset_++]);
      output <<= 4u;
      if (c >= '0' && c <= '9') output |= c - '0';
      else if (c >= 'a' && c <= 'f') output |= c - 'a' + 10u;
      else if (c >= 'A' && c <= 'F') output |= c - 'A' + 10u;
      else return false;
    }
    return true;
  }

  bool parseString(std::string& output) {
    if (!take('"')) return false;
    output.clear();
    while (offset_ < input_.size()) {
      const unsigned char c = static_cast<unsigned char>(input_[offset_++]);
      if (c == '"') return true;
      if (c < 0x20u) return false;
      if (c != '\\') {
        output.push_back(static_cast<char>(c));
        continue;
      }
      if (offset_ >= input_.size()) return false;
      const char escaped = input_[offset_++];
      switch (escaped) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          std::uint32_t codepoint = 0;
          if (!readHex4(codepoint)) return false;
          if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
            if (!consume("\\u")) return false;
            std::uint32_t low = 0;
            if (!readHex4(low) || low < 0xdc00u || low > 0xdfffu) return false;
            codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) + (low - 0xdc00u);
          }
          if (!appendCodepoint(output, codepoint)) return false;
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool parseNumber(JsonValue& output) {
    const std::size_t start = offset_;
    if (offset_ < input_.size() && input_[offset_] == '-') return false;
    if (offset_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[offset_]))) {
      return false;
    }
    if (input_[offset_] == '0') {
      ++offset_;
      if (offset_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[offset_]))) {
        return false;
      }
    } else {
      while (offset_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[offset_]))) {
        ++offset_;
      }
    }
    if (offset_ < input_.size() &&
        (input_[offset_] == '.' || input_[offset_] == 'e' || input_[offset_] == 'E')) {
      return false;
    }
    std::uint64_t value = 0;
    for (std::size_t i = start; i < offset_; ++i) {
      const std::uint64_t digit = static_cast<unsigned char>(input_[i]) - '0';
      if (value > (UINT64_MAX - digit) / 10u) return false;
      value = value * 10u + digit;
    }
    output.type = JsonValue::Type::Number;
    output.number = value;
    return true;
  }

  bool take(char expected) {
    if (offset_ >= input_.size() || input_[offset_] != expected) return false;
    ++offset_;
    return true;
  }

  bool consume(const char* expected) {
    const std::size_t length = std::char_traits<char>::length(expected);
    if (input_.compare(offset_, length, expected) != 0) return false;
    offset_ += length;
    return true;
  }

  void skipWhitespace() {
    while (offset_ < input_.size() &&
           (input_[offset_] == ' ' || input_[offset_] == '\t' ||
            input_[offset_] == '\r' || input_[offset_] == '\n')) {
      ++offset_;
    }
  }

  const std::string& input_;
  std::size_t offset_ = 0;
};

const JsonValue* member(const JsonValue& value, const char* key,
                        JsonValue::Type type) {
  if (value.type != JsonValue::Type::Object) return nullptr;
  const auto found = value.object.find(key);
  if (found == value.object.end() || found->second.type != type) return nullptr;
  return &found->second;
}

bool validUtf8(const std::string& value) {
  for (std::size_t offset = 0; offset < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[offset++]);
    if (first <= 0x7f) continue;
    if (first < 0xc2 || first > 0xf4 || offset >= value.size()) return false;

    const unsigned char second = static_cast<unsigned char>(value[offset++]);
    if (second < 0x80 || second > 0xbf ||
        (first == 0xe0 && second < 0xa0) ||
        (first == 0xed && second > 0x9f) ||
        (first == 0xf0 && second < 0x90) ||
        (first == 0xf4 && second > 0x8f)) {
      return false;
    }
    const std::size_t remaining = first < 0xe0 ? 0 : (first < 0xf0 ? 1 : 2);
    if (value.size() - offset < remaining) return false;
    for (std::size_t index = 0; index < remaining; ++index) {
      const unsigned char continuation =
          static_cast<unsigned char>(value[offset++]);
      if (continuation < 0x80 || continuation > 0xbf) return false;
    }
  }
  return true;
}

bool boundedText(const std::string& value, std::size_t maximum,
                 bool allowEmpty = false) {
  if ((!allowEmpty && value.empty()) || value.size() > maximum) return false;
  for (unsigned char c : value) {
    if (c < 32 || c == 127) return false;
  }
  return validUtf8(value);
}

bool optionalBoundedString(const JsonValue& object, const char* key,
                           std::string& output) {
  const auto found = object.object.find(key);
  if (found == object.object.end()) {
    output.clear();
    return true;
  }
  if (found->second.type != JsonValue::Type::String ||
      !boundedText(found->second.string, kMaximumLabelBytes, true)) {
    return false;
  }
  output = found->second.string;
  return true;
}

std::string escapeJson(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20u) return {};
        result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

}  // namespace

bool parseProvisioningCommand(const std::string& command,
                              PairingConfig& output) {
  constexpr const char* kPrefix = "orca-pair ";
  constexpr std::size_t kMaximumProvisioningBytes = 12288;
  const std::size_t prefixLength = std::char_traits<char>::length(kPrefix);
  if (command.size() <= prefixLength || command.size() > kMaximumProvisioningBytes ||
      command.rfind(kPrefix, 0) != 0) {
    return false;
  }
  JsonValue root;
  if (!JsonParser(command.substr(prefixLength)).parse(root) ||
      root.type != JsonValue::Type::Object || root.object.size() != 4) {
    return false;
  }
  const JsonValue* version = member(root, "version", JsonValue::Type::String);
  const JsonValue* endpoint = member(root, "endpoint", JsonValue::Type::String);
  const JsonValue* bearer = member(root, "bearer", JsonValue::Type::String);
  const JsonValue* ca = member(root, "ca", JsonValue::Type::String);
  if (version == nullptr || endpoint == nullptr || bearer == nullptr || ca == nullptr ||
      version->string != "orca-cardputer/v1") {
    return false;
  }
  PairingConfig candidate{endpoint->string, bearer->string, ca->string};
  if (!validatePairing(candidate)) return false;
  output = std::move(candidate);
  return true;
}

bool parseServerFrame(const std::string& frame, ServerEvent& output) {
  if (frame.empty() || frame.size() > kMaximumFrameBytes) return false;
  JsonValue root;
  if (!JsonParser(frame).parse(root) || root.type != JsonValue::Type::Object) {
    return false;
  }
  const JsonValue* version = member(root, "version", JsonValue::Type::String);
  const JsonValue* type = member(root, "type", JsonValue::Type::String);
  if (version == nullptr || type == nullptr || version->string != "orca-cardputer/v1") {
    return false;
  }

  ServerEvent candidate;
  if (type->string == "snapshot") {
    const JsonValue* snapshot = member(root, "snapshot", JsonValue::Type::Object);
    if (snapshot == nullptr) return false;
    const JsonValue* connected = member(*snapshot, "connected", JsonValue::Type::Boolean);
    const JsonValue* worktreeCount = member(*snapshot, "worktreeCount", JsonValue::Type::Number);
    const JsonValue* items = member(*snapshot, "items", JsonValue::Type::Array);
    if (connected == nullptr || worktreeCount == nullptr || items == nullptr ||
        worktreeCount->number > 1000000u || items->array.size() > 12) {
      return false;
    }
    candidate.type = ServerEventType::Snapshot;
    candidate.connected = connected->boolean;
    candidate.worktreeCount = static_cast<std::size_t>(worktreeCount->number);
    for (const JsonValue& item : items->array) {
      if (item.type != JsonValue::Type::Object) return false;
      SnapshotRow row;
      if (!optionalBoundedString(item, "displayName", row.displayName) ||
          !optionalBoundedString(item, "workspaceStatus", row.workspaceStatus) ||
          !optionalBoundedString(item, "agentState", row.agentState)) {
        return false;
      }
      candidate.rows.push_back(std::move(row));
    }
  } else if (type->string == "notify") {
    const JsonValue* text = member(root, "text", JsonValue::Type::String);
    if (text == nullptr || !boundedText(text->string, kMaximumTextBytes)) return false;
    candidate.type = ServerEventType::Notify;
    candidate.text = text->string;
  } else if (type->string == "question.request") {
    const JsonValue* questionId = member(root, "questionId", JsonValue::Type::String);
    const JsonValue* question = member(root, "question", JsonValue::Type::String);
    const JsonValue* labels = member(root, "labels", JsonValue::Type::Array);
    if (questionId == nullptr || question == nullptr || labels == nullptr ||
        !boundedText(questionId->string, kMaximumLabelBytes) ||
        !boundedText(question->string, kMaximumTextBytes) ||
        labels->array.size() < 2 || labels->array.size() > 5) {
      return false;
    }
    std::set<std::string> unique;
    for (const JsonValue& label : labels->array) {
      if (label.type != JsonValue::Type::String ||
          !boundedText(label.string, kMaximumLabelBytes) ||
          !unique.insert(label.string).second) {
        return false;
      }
      candidate.labels.push_back(label.string);
    }
    candidate.type = ServerEventType::Question;
    candidate.questionId = questionId->string;
    candidate.text = question->string;
  } else {
    return false;
  }
  output = std::move(candidate);
  return true;
}

std::string encodeHello() {
  return "{\"version\":\"orca-cardputer/v1\",\"type\":\"hello\"}";
}

std::string encodeState() {
  return "{\"version\":\"orca-cardputer/v1\",\"type\":\"state\"}";
}

std::string encodeQuestionAnswer(const std::string& questionId,
                                 const std::string& answer) {
  if (!boundedText(questionId, kMaximumLabelBytes) ||
      !boundedText(answer, kMaximumLabelBytes)) {
    return {};
  }
  return "{\"version\":\"orca-cardputer/v1\",\"type\":\"question.answer\","
         "\"questionId\":\"" + escapeJson(questionId) + "\",\"answer\":\"" +
         escapeJson(answer) + "\"}";
}

std::string encodePromptDraft(const std::string& text) {
  if (!boundedText(text, kMaximumTextBytes)) return {};
  return "{\"version\":\"orca-cardputer/v1\",\"type\":\"prompt.draft\","
         "\"text\":\"" + escapeJson(text) + "\"}";
}

}  // namespace orca
