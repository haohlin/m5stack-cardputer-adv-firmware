#pragma once

#include "orca/config.h"

#include <cstddef>
#include <string>
#include <vector>

namespace orca {

constexpr std::size_t kMaximumFrameBytes = 4096;
constexpr std::size_t kMaximumTextBytes = 480;
constexpr std::size_t kMaximumLabelBytes = 80;

struct SnapshotRow {
  std::string displayName;
  std::string workspaceStatus;
  std::string agentState;
};

enum class ServerEventType { Snapshot, Notify, Question };

struct ServerEvent {
  ServerEventType type = ServerEventType::Notify;
  bool connected = false;
  std::size_t worktreeCount = 0;
  std::vector<SnapshotRow> rows;
  std::string text;
  std::string questionId;
  std::vector<std::string> labels;
};

bool parseProvisioningCommand(const std::string& command,
                              PairingConfig& output);
bool parseServerFrame(const std::string& frame, ServerEvent& output);

std::string encodeHello();
std::string encodeState();
std::string encodeQuestionAnswer(const std::string& questionId,
                                 const std::string& answer);
std::string encodePromptDraft(const std::string& text);

}  // namespace orca
