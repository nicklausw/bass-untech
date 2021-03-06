#include "evaluate.cpp"
#include "analyze.cpp"
#include "execute.cpp"
#include "assemble.cpp"
#include "utility.cpp"

auto Bass::target(const string& filename, bool create) -> bool {
  if(targetFile.open()) targetFile.close();
  if(!filename) return true;

  //cannot modify a file unless it exists
  if(!file::exists(filename)) create = true;

  if(!targetFile.open(filename, create ? file::mode::write : file::mode::modify)) {
    print(stderr, "warning: unable to open target file: ", filename, "\n");
    return false;
  }

  return true;
}

auto Bass::symFile(const string& filename, bool symSNES) -> bool {
  if(symbolFile.open()) symbolFile.close();
  if(!filename) return true;

  if(!symbolFile.open(filename, file::mode::write)) {
    print(stderr, "warning: unable to open symbol file: ", filename, "\n");
    return false;
  }

  if(symSNES) {
    symbolFile.print("#SNES65816\n");
    symbolFile.print("\n[SYMBOL]\n");
  }

  for(auto c : symbols) {
    symbolFile.print(hex(c.offset >> 16, 2), ':', hex(c.offset, 4), ' ', c.name, symSNES ? " ANY 1\n" : "\n");
  }

  if(symSNES)
  for(auto c : comments) {
    symbolFile.print("\n[COMMENT]\n");
    symbolFile.print(hex(c.offset >> 16, 2), ':', hex(c.offset, 4), " \"", c.name, "\"\n");
  }

  return true;
}

auto Bass::source(const string& filename) -> bool {
  if(!file::exists(filename)) {
    print(stderr, "warning: source file not found: ", filename, "\n");
    return false;
  }

  uint fileNumber = sourceFilenames.size();
  sourceFilenames.append(filename);

  string data = file::read(filename);
  data.transform("\t\r", "  ");

  auto lines = data.split("\n");
  for(uint lineNumber : range(lines)) {
    string comment = "";
    if(auto position = lines[lineNumber].qfind("//")) {
      comment = lines[lineNumber].qsplit("//", 1)(1).strip();
      lines[lineNumber].resize(position());  //remove comments
    }

    auto blocks = lines[lineNumber].qsplit(";").strip();
    for(uint blockNumber : range(blocks)) {
      string statement = blocks[blockNumber];
      strip(statement);
      if(!statement) continue;

      if(statement.match("include \"?*\"")) {
        statement.trim("include \"", "\"", 1L);
        source({Location::path(filename), statement});
      } else {
        Instruction instruction;
        instruction.statement = statement;
        instruction.comment = comment;
        instruction.fileNumber = fileNumber;
        instruction.lineNumber = 1 + lineNumber;
        instruction.blockNumber = 1 + blockNumber;
        program.append(instruction);
      }
    }
  }

  return true;
}

auto Bass::define(const string& name, const string& value) -> void {
  defines.insert({name, {}, value});
}

auto Bass::constant(const string& name, const string& value) -> void {
  try {
    constantNames.insert(name);
    constants.insert({name, evaluate(value)});
  } catch(...) {
  }
}

auto Bass::assemble(bool strict) -> bool {
  this->strict = strict;

  try {
    phase = Phase::Analyze;
    analyze();

    phase = Phase::Query;
    architecture = new Architecture{*this};
    execute();

    phase = Phase::Write;
    architecture = new Architecture{*this};
    execute();
  } catch(...) {
    return false;
  }

  return true;
}

//internal

auto Bass::pc() const -> uint {
  return origin + base;
}

auto Bass::seek(uint offset) -> void {
  if(!targetFile.open()) return;
  if(writePhase()) targetFile.seek(offset);
}

auto Bass::write(uint64_t data, uint length) -> void {
  if(writePhase()) {
    if(targetFile.open()) {
      if(endian == Endian::LSB) targetFile.writel(data, length);
      if(endian == Endian::MSB) targetFile.writem(data, length);
    } else if(!isatty(fileno(stdout))) {
      if(endian == Endian::LSB) for(auto n :  range(length)) fputc(data >> n * 8, stdout);
      if(endian == Endian::MSB) for(auto n : rrange(length)) fputc(data >> n * 8, stdout);
    }
  }
  origin += length;
}

auto Bass::writeComment(int64_t value, const string& comment) -> void {
  if(writePhase() && comment != "") {
    comments.append({value, comment});
  }
}

auto Bass::writeSymbolLabel(int64_t value, const string& name) -> void {
  if(writePhase()) {
    string scopedName = {scope.merge("."), scope ? "." : "", name};
    symbols.append({value, scopedName});
  }
}

auto Bass::printInstruction() -> void {
  if(activeInstruction) {
    auto& i = *activeInstruction;
    print(stderr, sourceFilenames[i.fileNumber], ":", i.lineNumber, ":", i.blockNumber, ": ", i.statement, "\n");
  }
}

auto Bass::printInstructionStack() -> void {
  printInstruction();

  for(unsigned s : rrange(frames)) {
    if(frames[s].invokedBy) {
      auto& i = *frames[s].invokedBy;
      print(stderr, "   ", sourceFilenames[i.fileNumber], ":", i.lineNumber, ":", i.blockNumber, ": ", i.statement, "\n");
    }
  }
}

template<typename... P> auto Bass::notice(P&&... p) -> void {
  string s{forward<P>(p)...};
  print(stderr, "notice: ", s, "\n");
  printInstructionStack();
}

template<typename... P> auto Bass::warning(P&&... p) -> void {
  string s{forward<P>(p)...};
  print(stderr, "warning: ", s, "\n");
  printInstructionStack();

  if(!strict) return;
  struct BassWarning {};
  throw BassWarning();
}

template<typename... P> auto Bass::error(P&&... p) -> void {
  string s{forward<P>(p)...};
  print(stderr, "error: ", s, "\n");
  printInstructionStack();

  struct BassError {};
  throw BassError();
}
