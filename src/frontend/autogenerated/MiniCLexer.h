
// Generated from MiniC.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  MiniCLexer : public antlr4::Lexer {
public:
  enum {
    T_L_PAREN = 1, T_R_PAREN = 2, T_SEMICOLON = 3, T_L_BRACE = 4, T_R_BRACE = 5,
    T_L_BRACKET = 6, T_R_BRACKET = 7, T_ASSIGN = 8, T_COMMA = 9, T_INC = 10,
    T_DEC = 11, T_EQ = 12, T_NE = 13, T_LE = 14, T_GE = 15, T_LT = 16, T_GT = 17,
    T_LAND = 18, T_LOR = 19, T_ADD = 20, T_SUB = 21, T_MUL = 22, T_DIV = 23,
    T_MOD = 24, T_NOT = 25, T_IF = 26, T_ELSE = 27, T_WHILE = 28, T_FOR = 29,
    T_BREAK = 30, T_CONTINUE = 31, T_RETURN = 32, T_INT = 33, T_FLOAT = 34,
    T_CONST = 35, T_STATIC = 36, T_VOID = 37, T_ID = 38, T_STRING_LITERAL = 39,
    T_FLOAT_LITERAL = 40, T_DIGIT = 41, LINE_COMMENT = 42, BLOCK_COMMENT = 43,
    WS = 44
  };

  explicit MiniCLexer(antlr4::CharStream *input);

  ~MiniCLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

