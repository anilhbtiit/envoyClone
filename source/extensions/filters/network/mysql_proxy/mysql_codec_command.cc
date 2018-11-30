#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

Command::Cmd Command::parseCmd(Buffer::Instance& data, uint64_t& offset) {
  uint8_t cmd;
  if (BufferHelper::peekUint8(data, offset, cmd) != MYSQL_SUCCESS) {
    return Command::Cmd::COM_NULL;
  }
  return static_cast<Command::Cmd>(cmd);
}

void Command::setCmd(Command::Cmd cmd) { cmd_ = cmd; }

void Command::setDb(std::string db) { db_ = db; }

int Command::decode(Buffer::Instance& buffer, uint64_t& offset, int seq, int len) {
  setSeq(seq);

  Command::Cmd cmd = parseCmd(buffer, offset);
  setCmd(cmd);
  if (cmd == Command::Cmd::COM_NULL) {
    return MYSQL_FAILURE;
  }

  switch (cmd) {
  case Command::Cmd::COM_INIT_DB:
  case Command::Cmd::COM_CREATE_DB:
  case Command::Cmd::COM_DROP_DB: {
    std::string db = "";
    BufferHelper::peekStringBySize(buffer, offset, len - 1, db);
    setDb(db);
    break;
  }

  case Command::Cmd::COM_QUERY:
    is_query_ = true;
    // query string starts after mysql_hdr + one byte for comm type
    BufferHelper::peekStringBySize(buffer, offset, len - 1, data_);
    setDb("");
    break;

  default:
    setDb("");
    break;
  }

  return MYSQL_SUCCESS;
}

void Command::setData(std::string& data) { data_.assign(data); }

std::string Command::encode() {
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());

  BufferHelper::addUint8(*buffer, static_cast<int>(cmd_));
  BufferHelper::addString(*buffer, data_);
  std::string e_string = BufferHelper::toString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
