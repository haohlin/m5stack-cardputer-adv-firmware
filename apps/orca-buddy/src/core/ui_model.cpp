#include "orca/ui_model.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace orca {
namespace {

std::string safeDisplayText(const std::string& input, std::size_t maximum) {
  std::string output;
  output.reserve(std::min(input.size(), maximum));
  for (unsigned char c : input) {
    if (output.size() >= maximum) break;
    output.push_back((c < 32 || c == 127) ? ' ' : static_cast<char>(c));
  }
  while (!output.empty() &&
         (static_cast<unsigned char>(output.back()) & 0xc0u) == 0x80u) {
    output.pop_back();
  }
  if (!output.empty()) {
    const unsigned char lead = static_cast<unsigned char>(output.back());
    if ((lead & 0xe0u) == 0xc0u || (lead & 0xf0u) == 0xe0u ||
        (lead & 0xf8u) == 0xf0u) {
      output.pop_back();
    }
  }
  return output;
}

}  // namespace

void ConsoleModel::apply(const ServerEvent& event) {
  switch (event.type) {
    case ServerEventType::Snapshot:
      connected_ = event.connected;
      worktreeCount_ = event.worktreeCount;
      rows_.clear();
      for (std::size_t i = 0;
           i < event.rows.size() && rows_.size() < kMaximumRows; ++i) {
        rows_.push_back({safeDisplayText(event.rows[i].displayName, kMaximumRowBytes),
                         safeDisplayText(event.rows[i].workspaceStatus, kMaximumRowBytes),
                         safeDisplayText(event.rows[i].agentState, kMaximumRowBytes)});
      }
      break;
    case ServerEventType::Notify:
      notice_ = safeDisplayText(event.text, kMaximumNoticeBytes);
      break;
    case ServerEventType::Question:
      questionId_ = safeDisplayText(event.questionId, kMaximumRowBytes);
      questionText_ = safeDisplayText(event.text, kMaximumNoticeBytes);
      questionLabels_.clear();
      for (std::size_t i = 0;
           i < event.labels.size() && questionLabels_.size() < kMaximumLabels;
           ++i) {
        questionLabels_.push_back(
            safeDisplayText(event.labels[i], kMaximumRowBytes));
      }
      selectedLabel_ = 0;
      break;
  }
}

bool ConsoleModel::connected() const { return connected_; }
std::size_t ConsoleModel::worktreeCount() const { return worktreeCount_; }
const std::vector<SnapshotRow>& ConsoleModel::rows() const { return rows_; }
const std::string& ConsoleModel::notice() const { return notice_; }

bool ConsoleModel::hasQuestion() const {
  return !questionId_.empty() && !questionLabels_.empty();
}
const std::string& ConsoleModel::questionId() const { return questionId_; }
const std::string& ConsoleModel::questionText() const { return questionText_; }
const std::vector<std::string>& ConsoleModel::questionLabels() const {
  return questionLabels_;
}
std::size_t ConsoleModel::selectedLabel() const { return selectedLabel_; }

void ConsoleModel::moveQuestionSelection(int delta) {
  if (questionLabels_.empty()) return;
  const int count = static_cast<int>(questionLabels_.size());
  const int current = static_cast<int>(selectedLabel_);
  selectedLabel_ = static_cast<std::size_t>((current + (delta % count) + count) % count);
}

void ConsoleModel::clearQuestion() {
  questionId_.clear();
  questionText_.clear();
  questionLabels_.clear();
  selectedLabel_ = 0;
}

const std::string& ConsoleModel::prompt() const { return prompt_; }

bool ConsoleModel::appendPrompt(char value) {
  const unsigned char c = static_cast<unsigned char>(value);
  if (c < 32 || c == 127 || prompt_.size() >= kMaximumPromptBytes) return false;
  prompt_.push_back(value);
  return true;
}

void ConsoleModel::backspacePrompt() {
  if (prompt_.empty()) return;
  prompt_.pop_back();
  while (!prompt_.empty() &&
         (static_cast<unsigned char>(prompt_.back()) & 0xc0u) == 0x80u) {
    prompt_.pop_back();
  }
}

void ConsoleModel::clearPrompt() { prompt_.clear(); }

}  // namespace orca
