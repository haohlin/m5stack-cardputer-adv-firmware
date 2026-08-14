#pragma once

#include "orca/protocol.h"

#include <cstddef>
#include <string>
#include <vector>

namespace orca {

class ConsoleModel {
 public:
  static constexpr std::size_t kMaximumRows = 8;
  static constexpr std::size_t kMaximumRowBytes = 80;
  static constexpr std::size_t kMaximumNoticeBytes = 480;
  static constexpr std::size_t kMaximumPromptBytes = 480;
  static constexpr std::size_t kMaximumLabels = 5;

  void apply(const ServerEvent& event);

  bool connected() const;
  std::size_t worktreeCount() const;
  const std::vector<SnapshotRow>& rows() const;
  const std::string& notice() const;

  bool hasQuestion() const;
  const std::string& questionId() const;
  const std::string& questionText() const;
  const std::vector<std::string>& questionLabels() const;
  std::size_t selectedLabel() const;
  void moveQuestionSelection(int delta);
  void clearQuestion();

  const std::string& prompt() const;
  bool appendPrompt(char value);
  void backspacePrompt();
  void clearPrompt();

 private:
  bool connected_ = false;
  std::size_t worktreeCount_ = 0;
  std::vector<SnapshotRow> rows_;
  std::string notice_;
  std::string questionId_;
  std::string questionText_;
  std::vector<std::string> questionLabels_;
  std::size_t selectedLabel_ = 0;
  std::string prompt_;
};

}  // namespace orca
