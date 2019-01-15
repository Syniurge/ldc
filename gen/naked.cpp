//===-- naked.cpp ---------------------------------------------------------===//
//
//                         LDC – the LLVM D compiler
//
// This file is distributed under the BSD-style LDC license. See the LICENSE
// file for details.
//
//===----------------------------------------------------------------------===//

#include "expression.h"
#include "declaration.h"
#include "statement.h"
#include "template.h"
#include "gen/dvalue.h"
#include "gen/funcgenstate.h"
#include "gen/irstate.h"
#include "gen/llvm.h"
#include "gen/llvmhelpers.h"
#include "gen/logger.h"
#include "gen/tollvm.h"
#include "ir/irfunction.h"
#include "llvm/IR/InlineAsm.h"
#include <cassert>

////////////////////////////////////////////////////////////////////////////////
// FIXME: Integrate these functions
void AsmStatement_toNakedIR(AsmStatement *stmt, IRState *irs);

////////////////////////////////////////////////////////////////////////////////

class ToNakedIRVisitor : public Visitor {
  IRState *irs;

public:
  explicit ToNakedIRVisitor(IRState *irs) : irs(irs) {}

  //////////////////////////////////////////////////////////////////////////

  // Import all functions from class Visitor
  using Visitor::visit;

  //////////////////////////////////////////////////////////////////////////

  void visit(Statement *stmt) override {
    error(Loc(), "Statement not allowed in naked function");
  }

  //////////////////////////////////////////////////////////////////////////

  void visit(AsmStatement *stmt) override {
    AsmStatement_toNakedIR(stmt, irs);
  }

  //////////////////////////////////////////////////////////////////////////

  void visit(CompoundStatement *stmt) override {
    IF_LOG Logger::println("CompoundStatement::toNakedIR(): %s",
                           stmt->loc.toChars());
    LOG_SCOPE;

    if (stmt->statements) {
      for (auto s : *stmt->statements) {
        if (s) {
          s->accept(this);
        }
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////

  void visit(ExpStatement *stmt) override {
    IF_LOG Logger::println("ExpStatement::toNakedIR(): %s",
                           stmt->loc.toChars());
    LOG_SCOPE;

    // This happens only if there is a ; at the end:
    // asm { naked; ... };
    // Is this a legal AST?
    if (!stmt->exp) {
      return;
    }

    // only expstmt supported in declarations
    if (!stmt->exp || stmt->exp->op != TOKdeclaration) {
      visit(static_cast<Statement *>(stmt));
      return;
    }

    DeclarationExp *d = static_cast<DeclarationExp *>(stmt->exp);
    VarDeclaration *vd = d->declaration->isVarDeclaration();
    FuncDeclaration *fd = d->declaration->isFuncDeclaration();
    EnumDeclaration *ed = d->declaration->isEnumDeclaration();

    // and only static variable/function declaration
    // no locals or nested stuffies!
    if (!vd && !fd && !ed) {
      visit(static_cast<Statement *>(stmt));
      return;
    }
    if (vd && !(vd->storage_class & (STCstatic | STCmanifest))) {
      error(vd->loc, "non-static variable `%s` not allowed in naked function",
            vd->toChars());
      return;
    }
    if (fd && !fd->isStatic()) {
      error(fd->loc,
            "non-static nested function `%s` not allowed in naked function",
            fd->toChars());
      return;
    }
    // enum decls should always be safe

    // make sure the symbols gets processed
    // TODO: codegen() here is likely incorrect
    Declaration_codegen(d->declaration, irs);
  }

  //////////////////////////////////////////////////////////////////////////

  void visit(LabelStatement *stmt) override {
    IF_LOG Logger::println("LabelStatement::toNakedIR(): %s",
                           stmt->loc.toChars());
    LOG_SCOPE;

    printLabelName(irs->nakedAsm, mangleExact(irs->func()->decl),
                   stmt->ident->toChars());
    irs->nakedAsm << ":";

    if (stmt->statement) {
      stmt->statement->accept(this);
    }
  }
};

////////////////////////////////////////////////////////////////////////////////

void DtoDefineNakedFunction(FuncDeclaration *fd) {
  IF_LOG Logger::println("DtoDefineNakedFunction(%s)", mangleExact(fd));
  LOG_SCOPE;

  gIR->funcGenStates.emplace_back(new FuncGenState(*getIrFunc(fd), *gIR));

  // we need to do special processing on the body, since we only want
  // to allow actual inline asm blocks to reach the final asm output

  std::ostringstream &asmstr = gIR->nakedAsm;

  // build function header

  // FIXME: could we perhaps use llvm asmwriter to give us these details ?

  const char *mangle = mangleExact(fd);
  std::string fullmangle; // buffer only

  std::ostringstream tmpstr;

  const auto &triple = *global.params.targetTriple;
  bool const isWin = triple.isOSWindows();
  bool const isOSX = (triple.getOS() == llvm::Triple::Darwin ||
                      triple.getOS() == llvm::Triple::MacOSX);

  // osx is different
  // also mangling has an extra underscore prefixed
  if (isOSX) {
    fullmangle += '_';
    fullmangle += mangle;
    mangle = fullmangle.c_str();

    std::string section = "text";
    bool weak = false;
    if (DtoIsTemplateInstance(fd)) {
      tmpstr << "section\t__TEXT,__textcoal_nt,coalesced,pure_instructions";
      section = tmpstr.str();
      weak = true;
    }
    asmstr << "\t." << section << std::endl;
    asmstr << "\t.align\t4, 0x90" << std::endl;
    asmstr << "\t.globl\t" << mangle << std::endl;
    if (weak) {
      asmstr << "\t.weak_definition\t" << mangle << std::endl;
    }
    asmstr << mangle << ":" << std::endl;
  }
  // Windows is different
  else if (isWin) {
    // mangled names starting with '?' (MSVC++ symbols) apparently need quoting
    if (mangle[0] == '?') {
      fullmangle += '"';
      fullmangle += mangle;
      fullmangle += '"';
      mangle = fullmangle.c_str();
    } else if (triple.isArch32Bit()) {
      // prepend extra underscore for Windows x86
      fullmangle += '_';
      fullmangle += mangle;
      mangle = fullmangle.c_str();
    }

    asmstr << "\t.def\t" << mangle << ";" << std::endl;
    // hard code these two numbers for now since gas ignores .scl and llvm
    // is defaulting to .type 32 for everything I have seen
    asmstr << "\t.scl 2;" << std::endl;
    asmstr << "\t.type 32;" << std::endl;
    asmstr << "\t.endef" << std::endl;

    if (DtoIsTemplateInstance(fd)) {
      asmstr << "\t.section\t.text$" << mangle << ",\"xr\"" << std::endl;
      asmstr << "\t.linkonce\tdiscard" << std::endl;
    } else {
      asmstr << "\t.text" << std::endl;
    }
    asmstr << "\t.globl\t" << mangle << std::endl;
    asmstr << "\t.align\t16, 0x90" << std::endl;
    asmstr << mangle << ":" << std::endl;
  } else {
    if (DtoIsTemplateInstance(fd)) {
      asmstr << "\t.section\t.text." << mangle << ",\"axG\",@progbits,"
             << mangle << ",comdat" << std::endl;
      asmstr << "\t.weak\t" << mangle << std::endl;
    } else {
      asmstr << "\t.text" << std::endl;
      asmstr << "\t.globl\t" << mangle << std::endl;
    }
    asmstr << "\t.align\t16, 0x90" << std::endl;
    asmstr << "\t.type\t" << mangle << ",@function" << std::endl;
    asmstr << mangle << ":" << std::endl;
  }

  // emit body
  ToNakedIRVisitor v(gIR);
  fd->fbody->accept(&v);

  // We could have generated new errors in toNakedIR(), but we are in codegen
  // already so we have to abort here.
  if (global.errors) {
    fatal();
  }

  // emit size after body
  // llvm does this on linux, but not on osx or Win
  if (!(isWin || isOSX)) {
    asmstr << "\t.size\t" << mangle << ", .-" << mangle << std::endl
           << std::endl;
  }

  gIR->module.appendModuleInlineAsm(asmstr.str());
  asmstr.str("");

  if (global.params.targetTriple->isWindowsMSVCEnvironment() &&
      fd->isExport()) {
    // Embed a linker switch telling the MS linker to export the naked function.
    // This mimics the effect of the dllexport attribute for regular functions.
    const auto linkerSwitch = std::string("/EXPORT:") + mangle;
    auto Value = llvm::MDString::get(gIR->context(), linkerSwitch);
    gIR->LinkerMetadataArgs.push_back(llvm::MDNode::get(gIR->context(), Value));
  }

  gIR->funcGenStates.pop_back();
}

////////////////////////////////////////////////////////////////////////////////

void emitABIReturnAsmStmt(IRAsmBlock *asmblock, Loc &loc,
                          FuncDeclaration *fdecl) {
  IF_LOG Logger::println("emitABIReturnAsmStmt(%s)", mangleExact(fdecl));
  LOG_SCOPE;

  auto as = new IRAsmStmt;

  LLType *llretTy = DtoType(fdecl->type->nextOf());
  asmblock->retty = llretTy;
  asmblock->retn = 1;

  // FIXME: This should probably be handled by the TargetABI somehow.
  //        It should be able to do this for a greater variety of types.

  const auto &triple = *global.params.targetTriple;
  Type *const rt = fdecl->type->nextOf()->toBasetype();

  // x86
  if (triple.getArch() == llvm::Triple::x86) {
    if (rt->isintegral() || rt->ty == Tpointer || rt->ty == Tclass ||
        rt->ty == Taarray) {
      if (rt->size() == 8) {
        as->out_c = "=A,";
      } else {
        as->out_c = "={ax},";
      }
    } else if (rt->isfloating()) {
      if (rt->iscomplex()) {
        if (fdecl->linkage == LINKd) {
          // extern(D) always returns on the FPU stack
          as->out_c = "={st},={st(1)},";
          asmblock->retn = 2;
        } else if (rt->ty == Tcomplex32) {
          // non-extern(D) cfloat is returned as i64
          as->out_c = "=A,";
          asmblock->retty = LLType::getInt64Ty(gIR->context());
        } else {
          // non-extern(D) cdouble and creal are returned via sret
          // don't add anything!
          asmblock->retty = LLType::getVoidTy(gIR->context());
          asmblock->retn = 0;
          return;
        }
      } else {
        as->out_c = "={st},";
      }
    } else if (rt->ty == Tarray || rt->ty == Tdelegate) {
      as->out_c = "={ax},={dx},";
      asmblock->retn = 2;
#if 0
      // this is to show how to allocate a temporary for the return value
      // in case the appropriate multi register constraint isn't supported.
      // this way abi return from inline asm can still be emulated.
      // note that "$<<out0>>" etc in the asm will translate to the correct
      // numbered output when the asm block in finalized

      // generate asm
      as->out_c = "=*m,=*m,";
      LLValue* tmp = DtoRawAlloca(llretTy, 0, ".tmp_asm_ret");
      as->out.push_back( tmp );
      as->out.push_back( DtoGEPi(tmp, 0,1) );
      as->code = "movd %eax, $<<out0>>" "\n\t" "mov %edx, $<<out1>>";

      // fix asmblock
      asmblock->retn = 0;
      asmblock->retemu = true;
      asmblock->asmBlock->abiret = tmp;

      // add "ret" stmt at the end of the block
      asmblock->s.push_back(as);

      // done, we don't want anything pushed in the front of the block
      return;
#endif
    } else {
      error(loc, "unimplemented return type `%s` for implicit abi return",
            rt->toChars());
      fatal();
    }
  }

  // x86_64
  else if (triple.getArch() == llvm::Triple::x86_64) {
    if (rt->isintegral() || rt->ty == Tpointer || rt->ty == Tclass ||
        rt->ty == Taarray) {
      as->out_c = "={ax},";
    } else if (rt->isfloating()) {
      const bool isWin64 = triple.isOSWindows();

      if (rt == Type::tcomplex80 && !isWin64) {
        // On x87 stack, re=st, im=st(1)
        as->out_c = "={st},={st(1)},";
        asmblock->retn = 2;
      } else if ((rt == Type::tfloat80 || rt == Type::timaginary80) &&
                 !triple.isWindowsMSVCEnvironment()) {
        // On x87 stack
        as->out_c = "={st},";
      } else if (rt == Type::tcomplex32) {
        if (isWin64) {
          // cfloat on Win64 -> %rax
          as->out_c = "={ax},";
          asmblock->retty = LLType::getInt64Ty(gIR->context());
        } else {
          // cfloat on Posix -> %xmm0 (extract two floats)
          as->out_c = "={xmm0},";
          asmblock->retty = LLType::getDoubleTy(gIR->context());
        }
      } else if (rt->iscomplex()) {
        if (isWin64) {
          // Win64: cdouble and creal are returned via sret
          // don't add anything!
          asmblock->retty = LLType::getVoidTy(gIR->context());
          asmblock->retn = 0;
          return;
        } else {
          // cdouble on Posix -> re=%xmm0, im=%xmm1
          as->out_c = "={xmm0},={xmm1},";
          asmblock->retn = 2;
        }
      } else {
        // Plain float/double/ifloat/idouble
        as->out_c = "={xmm0},";
      }
    } else if (rt->ty == Tarray || rt->ty == Tdelegate) {
      as->out_c = "={ax},={dx},";
      asmblock->retn = 2;
    } else {
      error(loc, "unimplemented return type `%s` for implicit abi return",
            rt->toChars());
      fatal();
    }
  }

  // unsupported
  else {
    error(loc,
          "this target (%s) does not implement inline asm falling off the end "
          "of the function",
          triple.str().c_str());
    fatal();
  }

  // return values always go in the front
  asmblock->s.push_front(as);
}

////////////////////////////////////////////////////////////////////////////////

// sort of kinda related to naked ...

DValue *DtoInlineAsmExpr(Loc &loc, FuncDeclaration *fd, Expressions *arguments,
                         LLValue *sretPointer) {
  IF_LOG Logger::println("DtoInlineAsmExpr @ %s", loc.toChars());
  LOG_SCOPE;

  assert(fd->toParent()->isTemplateInstance() && "invalid inline __asm expr");
  assert(arguments->dim >= 2 && "invalid __asm call");

  // get code param
  Expression *e = (*arguments)[0];
  IF_LOG Logger::println("code exp: %s", e->toChars());
  StringExp *se = static_cast<StringExp *>(e);
  if (e->op != TOKstring || se->sz != 1) {
    e->error("`__asm` code argument is not a `char[]` string literal");
    fatal();
  }
  std::string code(se->toPtr(), se->numberOfCodeUnits());

  // get constraints param
  e = (*arguments)[1];
  IF_LOG Logger::println("constraint exp: %s", e->toChars());
  se = static_cast<StringExp *>(e);
  if (e->op != TOKstring || se->sz != 1) {
    e->error("`__asm` constraints argument is not a `char[]` string literal");
    fatal();
  }
  std::string constraints(se->toPtr(), se->numberOfCodeUnits());

  // build runtime arguments
  size_t n = arguments->dim;

  LLSmallVector<llvm::Value *, 8> args;
  args.reserve(n - 2);
  std::vector<LLType *> argtypes;
  argtypes.reserve(n - 2);

  for (size_t i = 2; i < n; i++) {
    args.push_back(DtoRVal((*arguments)[i]));
    argtypes.push_back(args.back()->getType());
  }

  // build asm function type
  Type *type = fd->type->nextOf();
  LLType *ret_type = DtoType(type->toBasetype());
  llvm::FunctionType *FT = llvm::FunctionType::get(ret_type, argtypes, false);

  // make sure the constraints are valid
  if (!llvm::InlineAsm::Verify(FT, constraints)) {
    e->error("`__asm` constraint argument is invalid");
    fatal();
  }

  // build asm call
  bool sideeffect = true;
  llvm::InlineAsm *ia = llvm::InlineAsm::get(FT, code, constraints, sideeffect);

  llvm::Value *rv = gIR->ir->CreateCall(ia, args, "");

  if (sretPointer) {
    DtoStore(rv, DtoBitCast(sretPointer, getPtrToType(ret_type)));
    return new DLValue(type, sretPointer);
  }

  // work around missing tuple support for users of the return value
  if (type->ty == Tstruct) {
    // make a copy
    llvm::Value *mem = DtoAlloca(type, ".__asm_tuple_ret");
    DtoStore(rv, DtoBitCast(mem, getPtrToType(ret_type)));
    return new DLValue(type, mem);
  }

  // return call as im value
  return new DImValue(type, rv);
}
