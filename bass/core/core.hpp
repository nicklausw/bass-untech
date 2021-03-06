struct Architecture;

struct Bass {
  auto target(const string& filename, bool create) -> bool;
  auto symFile(const string& filename, bool symSNES) -> bool;
  auto source(const string& filename) -> bool;
  auto define(const string& name, const string& value) -> void;
  auto constant(const string& name, const string& value) -> void;
  auto assemble(bool strict = false) -> bool;

  bool requireModifier;

  enum class Phase : uint { Analyze, Query, Write };
  enum class Endian : uint { LSB, MSB };
  enum class Evaluation : uint { Strict = 0, Lax = 1 };  //strict mode disallows forward-declaration of constants

  struct Instruction {
    string statement;
    string comment;
    uint ip;

    uint fileNumber;
    uint lineNumber;
    uint blockNumber;
  };

  struct Macro {
    Macro() {}
    Macro(const string& name) : name(name) {}
    Macro(const string& name, const string_vector& parameters, uint ip, bool inlined) : name(name), parameters(parameters), ip(ip), inlined(inlined) {}

    auto hash() const -> uint { return name.hash(); }
    auto operator==(const Macro& source) const -> bool { return name == source.name; }
    auto operator< (const Macro& source) const -> bool { return name <  source.name; }

    string name;
    string_vector parameters;
    uint ip;
    bool inlined;
  };

  struct Define {
    Define() {}
    Define(const string& name) : name(name) {}
    Define(const string& name, const string_vector& parameters, const string& value) : name(name), parameters(parameters), value(value) {}

    auto hash() const -> uint { return name.hash(); }
    auto operator==(const Define& source) const -> bool { return name == source.name; }
    auto operator< (const Define& source) const -> bool { return name <  source.name; }

    string name;
    string_vector parameters;
    string value;
  };

  using Expression = Define;  //Define and Expression structures are identical

  struct Variable {
    Variable() {}
    Variable(const string& name) : name(name) {}
    Variable(const string& name, int64_t value) : name(name), value(value) {}

    auto hash() const -> uint { return name.hash(); }
    auto operator==(const Variable& source) const -> bool { return name == source.name; }
    auto operator< (const Variable& source) const -> bool { return name <  source.name; }

    string name;
    int64_t value;
  };

  using Constant = Variable;  //Variable and Constant structures are identical

  struct File {
    File(const string& name) : name(name) {}
    File(const string& name, const string& fileName, Bass* self) : name(name), fileName(fileName), self(self) {
      if(!file::exists(fileName)) self->error("file doesn't exist");
      data = file::read(fileName);
      size = file::size(fileName);
      lastByte = 0;
    }

    auto read() -> uint8_t {
      lastByte++;
      return data[lastByte-1];
    }
    auto read(const uint offset) -> uint8_t {
      lastByte = offset + 1;
      return data[offset];
    }
    auto eof() -> uint {
      return lastByte == size ? 1 : 0;
    }

    auto hash() const -> uint { return name.hash(); }
    auto operator==(const File& source) const -> bool { return name == source.name; }

    string name, fileName;
    vector<uint8_t> data;
    uint lastByte, size;
    Bass* self;
  };

  struct Frame {
    enum class Level : uint {
      Inline,  //use deepest frame (eg for parameters)
      Active,  //use deepest non-inline frame
      Parent,  //use second-deepest non-inline frame
      Global,  //use root frame
    };

    uint ip;
    bool inlined;

    Instruction* invokedBy = nullptr;
    hashset<Macro> macros;
    hashset<Define> defines;
    hashset<Expression> expressions;
    hashset<Variable> variables;
  };

  struct Block {
    uint ip;
    string type;
  };

  struct Line {
    string statement;
    string comment;
  };

  struct Symbol {
    int64_t offset;
    string name;
  };

protected:
  auto analyzePhase() const -> bool { return phase == Phase::Analyze; }
  auto queryPhase() const -> bool { return phase == Phase::Query; }
  auto writePhase() const -> bool { return phase == Phase::Write; }

  //core.cpp
  auto pc() const -> uint;
  auto seek(uint offset) -> void;
  auto write(uint64_t data, uint length = 1) -> void;
  auto writeComment(int64_t value, const string& comment) -> void;
  auto writeSymbolLabel(int64_t value, const string& name) -> void;

  auto printInstruction() -> void;
  auto printInstructionStack() -> void;
  template<typename... P> auto notice(P&&... p) -> void;
  template<typename... P> auto warning(P&&... p) -> void;
  template<typename... P> auto error(P&&... p) -> void;

  //evaluate.cpp
  auto evaluate(const string& expression, Evaluation mode = Evaluation::Strict) -> int64_t;
  auto evaluate(Eval::Node* node, Evaluation mode) -> int64_t;
  auto evaluateParameters(Eval::Node* node, Evaluation mode) -> vector<int64_t>;
  auto evaluateExpression(Eval::Node* node, Evaluation mode) -> int64_t;
  auto evaluateLiteral(Eval::Node* node, Evaluation mode) -> int64_t;
  auto evaluateAssign(Eval::Node* node, Evaluation mode) -> int64_t;

  //analyze.cpp
  auto analyze() -> bool;
  auto analyzeInstruction(Instruction& instruction) -> bool;

  //execute.cpp
  auto execute() -> bool;
  auto executeInstruction(Instruction& instruction) -> bool;

  //assemble.cpp
  auto initialize() -> void;
  auto assemble(const Line line) -> bool;

  //utility.cpp
  auto setMacro(const string& name, const string_vector& parameters, uint ip, bool inlined, Frame::Level level) -> void;
  auto findMacro(const string& name) -> maybe<Macro&>;

  auto setDefine(const string& name, const string_vector& parameters, const string& value, Frame::Level level) -> void;
  auto findDefine(const string& name) -> maybe<Define&>;

  auto setExpression(const string& name, const string_vector& parameters, const string& value, Frame::Level level) -> void;
  auto findExpression(const string& name) -> maybe<Expression&>;

  auto setVariable(const string& name, int64_t value, Frame::Level level) -> void;
  auto findVariable(const string& name) -> maybe<Variable&>;

  auto setUnknownConstant(const string& name) -> void;
  auto setConstant(const string& name, int64_t value) -> void;
  auto findConstant(const string& name) -> maybe<Constant&>;
  auto findConstantName(const string& name) -> maybe<string>;

  auto setFile(const string& name, const string& fileName, Bass* self) -> void;
  auto findFile(const string& name) -> maybe<File&>;

  auto evaluateDefines(string& statement) -> void;

  auto filepath() -> string;
  auto split(const string& s) -> string_vector;
  auto strip(string& s) -> void;
  auto validate(const string& s) -> bool;
  auto text(string s) -> string;
  auto character(const string& s) -> int64_t;

  //internal state
  Instruction* activeInstruction = nullptr;  //used by notice, warning, error
  vector<Symbol> comments;        //comments for symbol file
  vector<Symbol> symbols;         //symbols for symbol file
  vector<Instruction> program;    //parsed source code statements
  vector<Block> blocks;           //track the start and end of blocks
  set<Define> defines;            //defines specified on the terminal
  hashset<string> constantNames;  //set of constant names, including those with unknown values
  hashset<Constant> constants;    //constants support forward-declaration
  vector<File> files;             //files opened
  vector<Frame> frames;           //macros, defines and variables do not
  vector<bool> conditionals;      //track conditional matching
  string_vector queue;            //track enqueue, dequeue directives
  string_vector scope;            //track scope recursion
  int64_t stringTable[256];       //overrides for d[bwldq] text strings
  Phase phase;                    //phase of assembly
  Endian endian = Endian::LSB;    //used for multi-byte writes (d[bwldq], etc)
  uint macroInvocationCounter;    //used for {#} support
  uint ip = 0;                    //instruction pointer into program
  uint origin = 0;                //file offset
  int base = 0;                   //file offset to memory map displacement
  uint lastLabelCounter = 1;      //- instance counter
  uint nextLabelCounter = 1;      //+ instance counter
  bool strict = false;            //upgrade warnings to errors when true

  bool forwardReference = false;  //true if the last evaluate(string) call contained a forward reference

  file targetFile;
  file symbolFile;
  string_vector sourceFilenames;

  shared_pointer<Architecture> architecture;
  friend class Architecture;
};
