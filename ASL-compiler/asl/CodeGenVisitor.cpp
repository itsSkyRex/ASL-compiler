//////////////////////////////////////////////////////////////////////
//
//    CodeGenVisitor - Walk the parser tree to do
//                     the generation of code
//
//////////////////////////////////////////////////////////////////////

#include "CodeGenVisitor.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/code.h"

#include <string>
#include <cstddef>    // std::size_t

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

//using namespace std;

#define UNIT  "1"

// Constructor
CodeGenVisitor::CodeGenVisitor(TypesMgr       & Types,
                               SymTable       & Symbols,
                               TreeDecoration & Decorations) :
  Types{Types},
  Symbols{Symbols},
  Decorations{Decorations} {
}

// Methods to visit each kind of node:
//
antlrcpp::Any CodeGenVisitor::visitProgram(AslParser::ProgramContext *ctx) {
  DEBUG_ENTER();
  code my_code;
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  for (auto ctxFunc : ctx->function()) { 
    subroutine subr = visit(ctxFunc);
    my_code.add_subroutine(subr);
  }
  Symbols.popScope();
  DEBUG_EXIT();
  return my_code;
}

antlrcpp::Any CodeGenVisitor::visitFunction(AslParser::FunctionContext *ctx) {
  DEBUG_ENTER();
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  subroutine subr(ctx->ID()->getText());
  codeCounters.reset();
  std::vector<var> && lvars = visit(ctx->declarations());
  // Local variables subroutine
  for (auto & onevar : lvars)
    subr.add_var(onevar);
  // Return parameter
  if (ctx->basic_type())
    subr.add_param("_result");
  // Parameters subroutine
  if (ctx->parameters()) {
    for (auto & param: ctx->parameters()->ID())
      subr.add_param(param->getText());
  }
  instructionList && code = visit(ctx->statements());
  code = code || instruction::RETURN();
  subr.set_instructions(code);
  Symbols.popScope();
  DEBUG_EXIT();
  return subr;
}

antlrcpp::Any CodeGenVisitor::visitDeclarations(AslParser::DeclarationsContext *ctx) {
  DEBUG_ENTER();
  std::vector<var> lvars;
  for (auto & varDeclCtx : ctx->variable_decl()) {
    std::vector<var> varline = visit(varDeclCtx);
    for (auto & onevar : varline)
      lvars.push_back(onevar);
  }
  DEBUG_EXIT();
  return lvars;
}

antlrcpp::Any CodeGenVisitor::visitVariable_decl(AslParser::Variable_declContext *ctx) {
  DEBUG_ENTER();
  std::vector<var> lvars;
  TypesMgr::TypeId t = getTypeDecor(ctx->type());
  std::size_t size = Types.getSizeOfType(t);
  for (auto & idCtx : ctx->ID())
    lvars.push_back(var{idCtx->getText(), size});
  DEBUG_EXIT();
  return lvars;
}

antlrcpp::Any CodeGenVisitor::visitStatements(AslParser::StatementsContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  for (auto stCtx : ctx->statement()) {
    instructionList && codeS = visit(stCtx);
    code = code || codeS;
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitAssignStmt(AslParser::AssignStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE1 = visit(ctx->left_expr());
  std::string           addr1 = codAtsE1.addr;
  std::string           offs1 = codAtsE1.offs;
  instructionList &     code1 = codAtsE1.code;
  TypesMgr::TypeId tid1 = getTypeDecor(ctx->left_expr());
  CodeAttribs     && codAtsE2 = visit(ctx->expr());
  std::string           addr2 = codAtsE2.addr;
  std::string           offs2 = codAtsE2.offs;
  instructionList &     code2 = codAtsE2.code;
  TypesMgr::TypeId tid2 = getTypeDecor(ctx->expr());

  // a = b  Array: a,b
  if (Types.isArrayTy(tid1) && Types.isArrayTy(tid2)) {
    std::string temp1 = "%"+codeCounters.newTEMP();
    std::string temp2 = "%"+codeCounters.newTEMP();

    if (!Symbols.isLocalVarClass(addr1))
      code = code || instruction::LOAD(temp1, addr1);
    if (!Symbols.isLocalVarClass(addr2))
      code = code || instruction::LOAD(temp2, addr2);
    
    // Creació temporals
    std::string index      = "%"+codeCounters.newTEMP();  
    std::string size       = "%"+codeCounters.newTEMP();
    std::string offset     = "%"+codeCounters.newTEMP();
    std::string value      = "%"+codeCounters.newTEMP();
    std::string comparison = "%"+codeCounters.newTEMP();
    std::string address    = "%"+codeCounters.newTEMP();
    std::string increase   = "%"+codeCounters.newTEMP();
    std::string labelWhile = "while"+codeCounters.newLabelWHILE();
    std::string labelEndWhile = "end"+labelWhile;

    code = code || instruction::ILOAD(index, "0");
    code = code || instruction::ILOAD(increase, UNIT);
    code = code || instruction::ILOAD(size, std::to_string(Types.getArraySize(Symbols.getType(addr1))));
    code = code || instruction::ILOAD(offset, UNIT);

    code = code || instruction::LABEL(labelWhile);
    code = code || instruction::LT(comparison, index, size);
    code = code || instruction::FJUMP(comparison, labelEndWhile);
    code = code || instruction::MUL(address, offset, index);
    code = code || instruction::LOADX(value, Symbols.isLocalVarClass(addr2) ? addr2 : temp2, address);
    code = code || instruction::XLOAD(Symbols.isLocalVarClass(addr1) ? addr1 : temp1, address, value);
    code = code || instruction::ADD(index, index, increase);
    code = code || instruction::UJUMP(labelWhile);
    code = code || instruction::LABEL(labelEndWhile);
  }
  // coercion int -> float
  if (Types.isFloatTy(tid1) && Types.isIntegerTy(tid2)) {
    std::string temp = "%"+codeCounters.newTEMP();
    code = code || instruction::FLOAT(temp, addr2);
    addr2 = temp;
  }
  // Array assignement
  if (ctx->left_expr()->expr())
    code = code || instruction::XLOAD(addr1, offs1, addr2);
  else
    code = code || instruction::LOAD(addr1,addr2);

  code = code1 || code2 || code;    
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitIfStmt(AslParser::IfStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE = visit(ctx->expr());
  std::string          addr1 = codAtsE.addr;
  instructionList &    code1 = codAtsE.code;
  instructionList &&   code2 = visit(ctx->statements());
  std::string label = codeCounters.newLabelIF();
  std::string labelEndIf = "endif"+label;
  if (!ctx->elseStat())
    code = code1 || instruction::FJUMP(addr1, labelEndIf) ||
           code2 || instruction::LABEL(labelEndIf);
  else {
    std::string labelElse = "else"+label;
    instructionList && code3 = visit(ctx->elseStat()->statements());
    code = code1 || instruction::FJUMP(addr1, labelElse) ||
           code2 || instruction::UJUMP(labelEndIf) || instruction::LABEL(labelElse) ||
           code3 || instruction::LABEL(labelEndIf);
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWhileStmt(AslParser::WhileStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAt = visit(ctx->expr());
  std::string        addr1 = codAt.addr;
  instructionList &  code1 = codAt.code;
  instructionList && code2 = visit(ctx->statements());
  std::string label = codeCounters.newLabelWHILE();
  std::string labelWhile = "while" + label;
  std::string labelEndWhile = "endWhile" + label;
  code = instruction::LABEL(labelWhile) || code1 || 
         instruction::FJUMP(addr1, labelEndWhile) || code2 || 
         instruction::UJUMP(labelWhile) || instruction::LABEL(labelEndWhile);
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitProcCall(AslParser::ProcCallContext *ctx) {
  DEBUG_ENTER();
  instructionList  code = instructionList();
  CodeAttribs && codAts = visit(ctx->ident());
  std::string      addr = codAts.addr;
  auto parameters = Types.getFuncParamsTypes(getTypeDecor(ctx->ident()));

  // Add space for the result
  if (!Types.isVoidFunction(getTypeDecor(ctx->ident())))
    code = code || instruction::PUSH();
  // If it has parameters stack them
  if (ctx->expr().size() >= 1)  {
    int i = 0;
    for (auto ctxParam : ctx->expr()) {
      CodeAttribs      && codAt = visit(ctxParam);
      std::string         addrP = codAt.addr;
      instructionList &   codeP = codAt.code;
      if (Types.isIntegerTy(getTypeDecor(ctxParam)) && Types.isFloatTy(parameters[i])) {
        std::string temp = "%"+codeCounters.newTEMP();
        codeP = codeP || instruction::FLOAT(temp, addrP);
        addrP = temp;
      }
      if (Types.isArrayTy(getTypeDecor(ctxParam))) {
        std::string temp = "%"+codeCounters.newTEMP();
        codeP = codeP || instruction::ALOAD(temp, addrP);
        addrP = temp;
      }
      code = code || codeP || instruction::PUSH(addrP);
      ++i;
    }
    code = code || instruction::CALL(addr);
    // Removed passed parameters
    for (std::size_t j = 0; j < (ctx->expr()).size(); ++j)
      code = code || instruction::POP();
  }
  else 
    code = code || instruction::CALL(ctx->ident()->ID()->getText());

  // Discard the result
  if (!Types.isVoidFunction(getTypeDecor(ctx->ident())))
    code = code || instruction::POP();

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitRetStmt(AslParser::RetStmtContext *ctx) {
  DEBUG_ENTER();
  if (ctx->expr()) {
    CodeAttribs     && codAt = visit(ctx->expr());
    std::string        addr1 = codAt.addr;
    instructionList &   code = codAt.code;
    code = code || instruction::LOAD("_result", addr1);
    return code;
  }
  DEBUG_EXIT();
  return instructionList();
}

antlrcpp::Any CodeGenVisitor::visitReadStmt(AslParser::ReadStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAt = visit(ctx->left_expr());
  std::string        addr1 = codAt.addr;
  std::string        offs1 = codAt.offs;
  instructionList &  code1 = codAt.code;
  TypesMgr::TypeId tid1 = getTypeDecor(ctx->left_expr());
  // Array
  if (ctx->left_expr()->expr()) {
    std::string temp = "%"+codeCounters.newTEMP();
    if (Types.isIntegerTy(tid1) || Types.isBooleanTy(tid1))
      code = code || instruction::READI(temp);
    else if (Types.isFloatTy(tid1))
      code = code || instruction::READF(temp);
    else
      code = code || instruction::READC(temp);
    code = code || instruction::XLOAD(addr1, offs1, temp);
  }
  else {
    if (Types.isIntegerTy(tid1) || Types.isBooleanTy(tid1))
      code = code || instruction::READI(addr1);
    else if (Types.isFloatTy(tid1))
      code = code || instruction::READF(addr1);
    else
      code = code || instruction::READC(addr1);
  }
  code = code1 || code;
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWriteExpr(AslParser::WriteExprContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt = visit(ctx->expr());
  std::string         addr = codAt.addr;
  instructionList &   code = codAt.code;
  TypesMgr::TypeId t = getTypeDecor(ctx->expr());
  if (Types.isIntegerTy(t) || Types.isBooleanTy(t))
    code = code || instruction::WRITEI(addr);
  else if (Types.isCharacterTy(t))
    code = code || instruction::WRITEC(addr);
  else 
    code = code || instruction::WRITEF(addr);
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWriteString(AslParser::WriteStringContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string s = ctx->STRING()->getText();
  std::string temp = "%"+codeCounters.newTEMP();
  int i = 1;
  while (i < int(s.size())-1) {
    if (s[i] != '\\') {
      code = code || instruction::CHLOAD(temp, s.substr(i,1)) ||
	           instruction::WRITEC(temp);
      i += 1;
    }
    else {
      assert(i < int(s.size())-2);
      if (s[i+1] == 'n') {
        code = code || instruction::WRITELN();
        i += 2;
      }
      else if (s[i+1] == 't' || s[i+1] == '"' || s[i+1] == '\\') {
        code = code || instruction::CHLOAD(temp, s.substr(i,2)) ||
	             instruction::WRITEC(temp);
        i += 2;
      }
      else {
        code = code || instruction::CHLOAD(temp, s.substr(i,1)) ||
	             instruction::WRITEC(temp);
        i += 1;
      }
    }
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitLeft_expr(AslParser::Left_exprContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs &&  codAtsI = visit(ctx->ident());
  std::string       addr1 = codAtsI.addr;
  instructionList &  code = codAtsI.code;
  std::string offset = "";
  // Array
  if (ctx->expr()) {
    std::string temp = "%"+codeCounters.newTEMP();
    CodeAttribs     && codAtsE = visit(ctx->expr());
    instructionList &    codeE = codAtsE.code;
    offset = codAtsE.addr;
    // Local array
    if (Symbols.isLocalVarClass(addr1)) {
      code = code || codeE || instruction::LOAD(temp, UNIT) ||
             instruction::MUL(temp, offset, temp);
    }
    else {  // Reference array
      std::string temp2 = "%"+codeCounters.newTEMP();
      code = code || codeE || instruction::LOAD(temp2, addr1) ||
             instruction::LOAD(temp, UNIT) || instruction::MUL(temp, offset, temp);
      addr1 = temp2;
    }
    offset = temp;
  }
  CodeAttribs codAtts(addr1, offset, code);
  DEBUG_EXIT();
  return codAtts;
}

antlrcpp::Any CodeGenVisitor::visitParenthesis(AslParser::ParenthesisContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->expr());
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitIndexer(AslParser::IndexerContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     &&  codAtsI = visit(ctx->ident());
  std::string           addr1 = codAtsI.addr;
  CodeAttribs     &&  codAtsE = visit(ctx->expr());
  std::string           offs2 = codAtsE.addr;
  instructionList &     code = codAtsE.code;
  std::string temp1 = "%"+codeCounters.newTEMP();
  std::string temp2 = "%"+codeCounters.newTEMP();

  code = code || instruction::LOAD(temp2, UNIT) || 
                 instruction::MUL(temp2, offs2, temp2);

  if (Symbols.isLocalVarClass(addr1)) 
    code = code || instruction::LOADX(temp1, addr1, temp2);
  else {
    std::string temp = "%"+codeCounters.newTEMP();
    code = code || instruction::LOAD(temp, addr1) || 
                   instruction::LOADX(temp1, temp, temp2);
  }
  CodeAttribs codAtts(temp1, "", code);
  DEBUG_EXIT();
  return codAtts;
}

antlrcpp::Any CodeGenVisitor::visitFunctional(AslParser::FunctionalContext *ctx) {
  DEBUG_ENTER();
  instructionList  code = instructionList();
  CodeAttribs && codAts = visit(ctx->ident());
  std::string      addr = codAts.addr;
  std::string temp = "%"+codeCounters.newTEMP();
  auto parameters = Types.getFuncParamsTypes(getTypeDecor(ctx->ident()));

  // Make space for function result
  code = code || instruction::PUSH();
  // If it has parameters stack them
  if (ctx->expr().size() >= 1)  {
    int i = 0;
    for (auto ctxParam : ctx->expr()) {
      CodeAttribs     && codAt = visit(ctxParam);
      std::string         addrP = codAt.addr;
      instructionList &   codeP = codAt.code;
      if (Types.isIntegerTy(getTypeDecor(ctxParam)) && Types.isFloatTy(parameters[i])) {
        std::string temp = "%"+codeCounters.newTEMP();
        codeP = codeP || instruction::FLOAT(temp, addrP);
        addrP = temp;
      }
      if (Types.isArrayTy(getTypeDecor(ctxParam))) {
        std::string temp = "%"+codeCounters.newTEMP();
        codeP = codeP || instruction::ALOAD(temp, addrP);
        addrP = temp;
      }
      code = code || codeP || instruction::PUSH(addrP);
      ++i;
    }
    code = code || instruction::CALL(addr);
    // Removed passed parameters
    for (std::size_t j = 0; j < (ctx->expr()).size(); ++j)
      code = code || instruction::POP();

    code = code || instruction::POP(temp);
  }
  else 
    code = code || instruction::CALL(ctx->ident()->ID()->getText());

  CodeAttribs codAtts(temp, "", code);
  DEBUG_EXIT();
  return codAtts;
}

antlrcpp::Any CodeGenVisitor::visitUnary(AslParser::UnaryContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt = visit(ctx->expr());
  std::string         addr = codAt.addr;
  instructionList &   code = codAt.code;
  std::string temp = "%"+codeCounters.newTEMP();
  TypesMgr::TypeId t = getTypeDecor(ctx);
  
  if (ctx->SUB())
    code = code || (Types.isIntegerTy(t) ? instruction::NEG (temp, addr) :
                                           instruction::FNEG(temp, addr));
  else if (ctx->NOT())
    code = code || instruction::NOT(temp, addr);
  else  // ctx->ADD()
    temp = addr;
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitArithmetic(AslParser::ArithmeticContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();

  if (Types.isIntegerTy(t)) {
    if (ctx->MUL())
      code = code || instruction::MUL(temp, addr1, addr2);
    else if (ctx->ADD())
      code = code || instruction::ADD(temp, addr1, addr2);
    else if (ctx->DIV())
      code = code || instruction::DIV(temp, addr1, addr2);
    else if (ctx->SUB())
      code = code || instruction::SUB(temp, addr1, addr2);
    else if (ctx->MOD()) {
      std::string temp1 = "%"+codeCounters.newTEMP();
      std::string temp2 = "%"+codeCounters.newTEMP();
      code = code || instruction::DIV(temp1, addr1, addr2) || 
                     instruction::MUL(temp2, temp1, addr2) || 
                     instruction::SUB(temp , addr1, temp2);
    }
  }
  else {
    std::string addr1f = addr1;
    std::string addr2f = addr2;
    // Arithmetic operators with coercion int -> float
    if (Types.isIntegerTy(t1)) {
      addr1f = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(addr1f, addr1);
    }
    else if (Types.isIntegerTy(t2)) {
      addr2f = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(addr2f, addr2);
    }
    // Float operations
    if (ctx->MUL())
      code = code || instruction::FMUL(temp, addr1f, addr2f);
    else if (ctx->ADD())
      code = code || instruction::FADD(temp, addr1f, addr2f);
    else if (ctx->DIV())
      code = code || instruction::FDIV(temp, addr1f, addr2f);
    else if (ctx->SUB())
      code = code || instruction::FSUB(temp, addr1f, addr2f);
  }
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitRelational(AslParser::RelationalContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  std::string temp = "%"+codeCounters.newTEMP();

  if ((Types.isIntegerTy(t1)   && Types.isIntegerTy(t2)) ||
      (Types.isCharacterTy(t1) && Types.isCharacterTy(t2))) {
    if (ctx->SEQ()) 
      code = code || instruction::EQ(temp, addr1, addr2);
    else if (ctx->SNEQ()) {
      std::string temp1 = "%"+codeCounters.newTEMP();
      code = code || instruction::EQ(temp1, addr1, addr2) ||
                     instruction::NOT(temp, temp1);
    }
    else if (ctx->SLE())
      code = code || instruction::LE(temp, addr1, addr2);
    else if (ctx->SLT())
      code = code || instruction::LT(temp, addr1, addr2);
    else if (ctx->SGT()) {
      std::string temp1 = "%"+codeCounters.newTEMP();
      code = code || instruction::LE(temp1, addr1, addr2) || 
                     instruction::NOT(temp, temp1);
    }
    else {  // ctx->SGE()
      std::string temp1 = "%"+codeCounters.newTEMP();
      code = code || instruction::LT(temp1, addr1, addr2) ||
                     instruction::NOT(temp, temp1);
    }
  }
  else {
    std::string addr1f = addr1;
    std::string addr2f = addr2;
    // Relational operators with coercion int -> float
    if (Types.isIntegerTy(t1)) {
      addr1f = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(addr1f, addr1);
    }
    else if (Types.isIntegerTy(t2)) {
      addr2f = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(addr2f, addr2);
    }
    // Float relations
    if (ctx->SEQ()) 
      code = code || instruction::FEQ(temp, addr1f, addr2f);
    else if (ctx->SNEQ()) {
      std::string temp1 = "%"+codeCounters.newTEMP();
      code = code || instruction::FEQ(temp1, addr1f, addr2f) || 
                     instruction::NOT(temp, temp1);
    }
    else if (ctx->SLE())
      code = code || instruction::FLE(temp, addr1f, addr2f);
    else if (ctx->SLT())
      code = code || instruction::FLT(temp, addr1f, addr2f);
    else if (ctx->SGT()) {
      std::string temp1 = "%"+codeCounters.newTEMP();
      code = code || instruction::FLE(temp1, addr1f, addr2f) ||
                     instruction::NOT(temp, temp1);
    }
    else {  // ctx->SGE()
      std::string temp1 = "%"+codeCounters.newTEMP();
      code = code || instruction::FLT(temp1, addr1f, addr2f) || 
                     instruction::NOT(temp, temp1);
    }
  }
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitLogical(AslParser::LogicalContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  std::string temp = "%"+codeCounters.newTEMP();
  if (ctx->AND())
    code = code || instruction::AND(temp, addr1, addr2);
  else
    code = code || instruction::OR(temp, addr1, addr2);
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitValue(AslParser::ValueContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string temp = "%"+codeCounters.newTEMP();
  if (ctx->INTVAL())
    code = instruction::ILOAD(temp, ctx->getText());
  else if (ctx->CHARVAL()) {
    std::string c = ctx->getText();
    code = instruction::CHLOAD(temp, c.substr(1,c.size()-2));
  }
  else if (ctx->FLOATVAL())
    code = instruction::FLOAD(temp, ctx->getText());
  else   // ctx->BOOLVAL
    code = instruction::ILOAD(temp, ctx->getText() == "true" ? "1" : "0");
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitExprIdent(AslParser::ExprIdentContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->ident());
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitIdent(AslParser::IdentContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs codAts(ctx->ID()->getText(), "", instructionList());
  DEBUG_EXIT();
  return codAts;
}


// Getters for the necessary tree node atributes:
//   Scope and Type
SymTable::ScopeId CodeGenVisitor::getScopeDecor(antlr4::ParserRuleContext *ctx) const {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId CodeGenVisitor::getTypeDecor(antlr4::ParserRuleContext *ctx) const {
  return Decorations.getType(ctx);
}


// Constructors of the class CodeAttribs:
//
CodeGenVisitor::CodeAttribs::CodeAttribs(const std::string & addr,
					 const std::string & offs,
					 instructionList & code) :
  addr{addr}, offs{offs}, code{code} {
}

CodeGenVisitor::CodeAttribs::CodeAttribs(const std::string & addr,
					 const std::string & offs,
					 instructionList && code) :
  addr{addr}, offs{offs}, code{code} {
}
