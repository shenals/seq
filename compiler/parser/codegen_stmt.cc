#include <fmt/format.h>
#include <fmt/ostream.h>
#include <memory>
#include <ostream>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "parser/codegen.h"
#include "parser/common.h"
#include "parser/context.h"
#include "parser/expr.h"
#include "parser/stmt.h"
#include "parser/visitor.h"
#include "seq/seq.h"

using fmt::format;
using std::get;
using std::make_pair;
using std::make_unique;
using std::move;
using std::ostream;
using std::stack;
using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;
using std::vector;

#define RETURN(T, ...) (this->result = new T(__VA_ARGS__)); return
#define ERROR(...) error(stmt->getSrcInfo(), __VA_ARGS__)

CodegenStmtVisitor::CodegenStmtVisitor(Context &ctx)
    : ctx(ctx), result(nullptr) {}
void CodegenStmtVisitor::apply(Context &ctx, const StmtPtr &stmts) {
  auto tv = CodegenStmtVisitor(ctx);
  tv.transform(stmts);
}

seq::Stmt *CodegenStmtVisitor::transform(const StmtPtr &stmt) {
  fmt::print("<codegen> {} :pos {}\n", *stmt, stmt->getSrcInfo());
  CodegenStmtVisitor v(ctx);
  stmt->accept(v);
  if (v.result) {
    v.result->setSrcInfo(stmt->getSrcInfo());
    v.result->setBase(ctx.getBase());
    ctx.getBlock()->add(v.result);
  }
  return v.result;
}

seq::Expr *CodegenStmtVisitor::transform(const ExprPtr &expr) {
  CodegenExprVisitor v(ctx, *this);
  expr->accept(v);
  return v.result;
}

seq::types::Type *CodegenStmtVisitor::transformType(const ExprPtr &expr) {
  return CodegenExprVisitor(ctx, *this).transformType(expr);
}

void CodegenStmtVisitor::visit(const SuiteStmt *stmt) {
  for (auto &s : stmt->stmts) {
    transform(s);
  }
}
void CodegenStmtVisitor::visit(const PassStmt *stmt) {}
void CodegenStmtVisitor::visit(const BreakStmt *stmt) {
  RETURN(seq::Break, );
}
void CodegenStmtVisitor::visit(const ContinueStmt *stmt) {
  RETURN(seq::Continue, );
}
void CodegenStmtVisitor::visit(const ExprStmt *stmt) {
  RETURN(seq::ExprStmt, transform(stmt->expr));
}
void CodegenStmtVisitor::visit(const AssignStmt *stmt) {
  // TODO: JIT
  if (auto i = dynamic_cast<IdExpr *>(stmt->lhs.get())) {
    auto var = i->value;
    if (auto v =
            dynamic_cast<VarContextItem *>(ctx.find(var).get())) { // assignment
      RETURN(seq::Assign, v->getVar(), transform(stmt->rhs));
    } else {
      auto varStmt =
          new seq::VarStmt(transform(stmt->rhs),
                           stmt->type ? transformType(stmt->type) : nullptr);
      ctx.add(var, varStmt->getVar());
      this->result = varStmt;
    }
  } else if (auto i = dynamic_cast<DotExpr *>(stmt->lhs.get())) {
    RETURN(seq::AssignMember, transform(i->expr), i->member,
           transform(stmt->rhs));
  } else {
    ERROR("invalid assignment");
  }
}
void CodegenStmtVisitor::visit(const DelStmt *stmt) {
  if (auto expr = dynamic_cast<IdExpr *>(stmt->expr.get())) {
    if (auto v = dynamic_cast<VarContextItem *>(ctx.find(expr->value).get())) {
      ctx.remove(expr->value);
      RETURN(seq::Del, v->getVar());
    }
  }
  ERROR("cannot delete non-variable");
}
void CodegenStmtVisitor::visit(const PrintStmt *stmt) {
  RETURN(seq::Print, transform(stmt->expr));
}
void CodegenStmtVisitor::visit(const ReturnStmt *stmt) {
  if (!stmt->expr) {
    RETURN(seq::Return, nullptr);
  } else if (auto f = dynamic_cast<seq::Func *>(ctx.getBase())) {
    auto ret = new seq::Return(transform(stmt->expr));
    f->sawReturn(ret);
    this->result = ret;
  } else {
    ERROR("return outside function");
  }
}
void CodegenStmtVisitor::visit(const YieldStmt *stmt) {
  if (!stmt->expr) {
    RETURN(seq::Yield, nullptr);
  } else if (auto f = dynamic_cast<seq::Func *>(ctx.getBase())) {
    auto ret = new seq::Yield(transform(stmt->expr));
    f->sawYield(ret);
    this->result = ret;
  } else {
    ERROR("yield outside function");
  }
}
void CodegenStmtVisitor::visit(const AssertStmt *stmt) {
  RETURN(seq::Assert, transform(stmt->expr));
}
void CodegenStmtVisitor::visit(const TypeAliasStmt *stmt) {
  ctx.add(stmt->name, transformType(stmt->expr));
}
void CodegenStmtVisitor::visit(const WhileStmt *stmt) {
  auto r = new seq::While(transform(stmt->cond));
  ctx.addBlock(r->getBlock());
  transform(stmt->suite);
  ctx.popBlock();
  this->result = r;
}
void CodegenStmtVisitor::visit(const ForStmt *stmt) {
  auto r = new seq::For(transform(stmt->iter));
  string forVar;
  if (auto expr = dynamic_cast<IdExpr*>(stmt->var.get())) {
    forVar = expr->value;
  } else {
    error("expected valid assignment statement");
  }
  ctx.addBlock(r->getBlock());
  ctx.add(forVar, r->getVar());
  transform(stmt->suite);
  ctx.popBlock();
  this->result = r;
}
void CodegenStmtVisitor::visit(const IfStmt *stmt) {
  auto r = new seq::If();
  for (auto &i: stmt->ifs) {
    auto b = i.cond ? r->addCond(transform(i.cond)) : r->addElse();
    ctx.addBlock(b);
    transform(i.suite);
    ctx.popBlock();
  }
  this->result = r;
}
void CodegenStmtVisitor::visit(const MatchStmt *stmt) { ERROR("TODO"); }
void CodegenStmtVisitor::visit(const ImportStmt *stmt) { ERROR("TODO"); }
void CodegenStmtVisitor::visit(const ExternImportStmt *stmt) { ERROR("TODO"); }
void CodegenStmtVisitor::visit(const TryStmt *stmt) {
  auto r = new seq::TryCatch();
  ctx.addBlock(r->getBlock());
  transform(stmt->suite);
  ctx.popBlock();
  int varIdx = 0;
  for (auto &c: stmt->catches) {
    ctx.addBlock(r->addCatch(c.exc ? transformType(c.exc) : nullptr));
    ctx.add(c.var, r->getVar(varIdx++));
    transform(c.suite);
    ctx.popBlock();
  }
  if (stmt->finally) {
    ctx.addBlock(r->getFinally());
    transform(stmt->finally);
    ctx.popBlock();
  }
  this->result = r;
}
void CodegenStmtVisitor::visit(const GlobalStmt *stmt) {
  if (auto var = dynamic_cast<VarContextItem*>(ctx.find(stmt->var).get())) {
    if (var->isGlobal()) {
      if (var->getBase() != ctx.getBase()) {
        ctx.add(stmt->var, var->getVar(), true);
      }
      return;
    }
  }
  error("identifier '{}' not found", stmt->var);
}
void CodegenStmtVisitor::visit(const ThrowStmt *stmt) {
  RETURN(seq::Throw, transform(stmt->expr));
}
void CodegenStmtVisitor::visit(const PrefetchStmt *stmt) {
  if (auto e = dynamic_cast<IndexExpr *>(stmt->expr.get())) {
    if (auto f = dynamic_cast<seq::Func *>(ctx.getBase())) {
      auto r = new seq::Prefetch({transform(e->expr)}, {transform(e->index)});
      f->sawPrefetch(r);
      this->result = r;
    } else {
      ERROR("prefetch outside of function");
    }
  } else {
    ERROR("prefetch needs index expression");
  }
}
void CodegenStmtVisitor::visit(const FunctionStmt *stmt) {
  auto f = new seq::Func();
  f->setName(stmt->name);
  if (auto c = ctx.getEnclosingType()) {
    c->addMethod(stmt->name, f, false);
  } else {
    if (!ctx.isToplevel()) {
      f->setEnclosingFunc(dynamic_cast<seq::Func *>(ctx.getBase()));
    }
    vector<string> names;
    for (auto &n : stmt->args) {
      names.push_back(n.name);
    }
    ctx.add(stmt->name, f, names);
  }
  ctx.addBlock(f->getBlock(), f);

  unordered_set<string> seen;
  unordered_set<string> generics(stmt->generics.begin(), stmt->generics.end());
  bool hasDefault = false;
  for (auto &arg : stmt->args) {
    if (!arg.type) {
      string typName = format("'{}", arg.name);
      generics.insert(typName);
    }
    if (seen.find(arg.name) != seen.end()) {
      ERROR("argument '{}' already specified", arg.name);
    }
    seen.insert(arg.name);
    if (arg.deflt) {
      hasDefault = true;
    } else if (hasDefault) {
      ERROR("argument '{}' has no default value", arg.name);
    }
  }
  f->addGenerics(generics.size());
  int gc = 0;
  for (auto &g : generics) {
    f->getGeneric(gc)->setName(g);
    ctx.add(g, f->getGeneric(gc++));
  }
  vector<seq::types::Type *> types;
  vector<string> names;
  vector<seq::Expr *> defaults;
  for (auto &arg : stmt->args) {
    if (!arg.type) {
      types.push_back(
          transformType(make_unique<IdExpr>(format("'{}", arg.name))));
    } else {
      types.push_back(transformType(arg.type));
    }
    names.push_back(arg.name);
    defaults.push_back(arg.deflt ? transform(arg.deflt) : nullptr);
  }
  f->setIns(types);
  f->setArgNames(names);
  f->setDefaults(defaults);

  if (stmt->ret) {
    f->setOut(transformType(stmt->ret));
  }
  for (auto a : stmt->attributes) {
    f->addAttribute(a);
    if (a == "atomic") {
      ctx.setFlag("atomic");
    }
  }
  for (auto &arg : stmt->args) {
    ctx.add(arg.name, f->getArgVar(arg.name));
  }
  transform(stmt->suite);
  ctx.popBlock();
  RETURN(seq::FuncStmt, f);
}
void CodegenStmtVisitor::visit(const ClassStmt *stmt) {
  auto getMembers = [&]() {
    vector<seq::types::Type *> types;
    vector<string> names;
    if (stmt->isType && !stmt->args.size()) {
      ERROR("types need at least one member");
    } else
      for (auto &arg : stmt->args) {
        if (!arg.type) {
          ERROR("type information needed for '{}'", arg.name);
        }
        types.push_back(transformType(arg.type));
        names.push_back(arg.name);
      }
    return make_pair(types, names);
  };

  if (stmt->isType) {
    auto t = seq::types::RecordType::get({}, {}, stmt->name);
    ctx.add(stmt->name, t);
    ctx.setEnclosingType(t);
    ctx.addBlock();
    if (stmt->generics.size()) {
      ERROR("types cannot be generic");
    }
    auto tn = getMembers();
    t->setContents(tn.first, tn.second);
  } else {
    auto t = seq::types::RefType::get(stmt->name);
    ctx.add(stmt->name, t);
    ctx.setEnclosingType(t);
    ctx.addBlock();
    unordered_set<string> generics(stmt->generics.begin(),
                                   stmt->generics.end());
    t->addGenerics(generics.size());
    int gc = 0;
    for (auto &g : generics) {
      t->getGeneric(gc)->setName(g);
      ctx.add(g, t->getGeneric(gc++));
    }
    auto tn = getMembers();
    t->setContents(seq::types::RecordType::get(tn.first, tn.second, ""));
  }
  transform(stmt->suite);
  ctx.popBlock();
  ctx.setEnclosingType(nullptr);
}
void CodegenStmtVisitor::visit(const ExtendStmt *stmt) {
  seq::types::Type *type;
  vector<string> generics;
  if (auto w = dynamic_cast<IdExpr *>(stmt->what.get())) {
    type = transformType(stmt->what);
  } if (auto w = dynamic_cast<IndexExpr *>(stmt->what.get())) {
    type = transformType(w->expr);
    if (auto t = dynamic_cast<TupleExpr *>(w->index.get())) {
      for (auto &ti: t->items) {
        if (auto l = dynamic_cast<IdExpr *>(ti.get())) {
          generics.push_back(l->value);
        } else {
          ERROR("invalid generic variable");
        }
      }
    } else if (auto l = dynamic_cast<IdExpr *>(w->index.get())) {
      generics.push_back(l->value);
    } else {
      ERROR("invalid generic variable");
    }
  } else {
    ERROR("cannot extend non-type");
  }
  ctx.setEnclosingType(transformType(stmt->what));
  ctx.addBlock();
  int count = 0;
  if (auto g = dynamic_cast<seq::types::RefType *>(type)) {
    if (g->numGenerics() != generics.size()) {
      ERROR("generic count mismatch");
    }
    for (int i = 0; i < g->numGenerics(); i++) {
      ctx.add(generics[i], g->getGeneric(i));
    }
  } else if (count) {
    ERROR("unexpected generics");
  }
  transform(stmt->suite);
  ctx.popBlock();
  ctx.setEnclosingType(nullptr);
}
