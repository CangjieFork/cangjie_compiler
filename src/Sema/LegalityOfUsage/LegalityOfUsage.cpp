// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements legality of usage checking after sema type completed.
 */

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "InitializationChecker.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie {
using namespace AST;

namespace {
bool HasCallExpr(Ptr<Node> body)
{
    bool hasCall = false;
    Walker(body, [&hasCall](Ptr<Node> n) {
        if (n->astKind == ASTKind::CALL_EXPR) {
            hasCall = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return hasCall;
}

std::unordered_set<Ptr<Decl>> CollectMutableVars(Ptr<Node> body)
{
    std::unordered_set<Ptr<Decl>> mutVars;
    Walker(body, [&mutVars](Ptr<Node> n) {
        if (auto varDecl = DynamicCast<VarDecl*>(n); varDecl && varDecl->isVar) {
            mutVars.emplace(varDecl);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return mutVars;
}

Ptr<FuncBody> GetCapturedTargetFuncBody(const CallExpr& ce)
{
    if (auto refExpr = DynamicCast<RefExpr*>(ce.baseFunc.get()); refExpr) {
        auto target = DynamicCast<FuncDecl*>(refExpr->ref.target);
        bool needCollect =
            target && target->funcBody && target->funcBody->captureKind != CaptureKind::NO_CAPTURE;
        return needCollect ? target->funcBody.get() : nullptr;
    }
    if (auto lambdaExpr = DynamicCast<LambdaExpr*>(ce.baseFunc.get()); lambdaExpr) {
        CJC_ASSERT(lambdaExpr->funcBody); // Parser guarantees.
        if (lambdaExpr->funcBody->captureKind != CaptureKind::NO_CAPTURE) {
            return lambdaExpr->funcBody.get();
        }
    }
    return nullptr;
}

VisitAction SetTransitiveCaptureKind(
    FuncBody& fb, const std::unordered_set<Ptr<Decl>>& mutVars, Ptr<const Node> n)
{
    if (n->astKind != ASTKind::CALL_EXPR) {
        return VisitAction::WALK_CHILDREN;
    }
    auto targetFuncBody = GetCapturedTargetFuncBody(static_cast<const CallExpr&>(*n));
    if (!targetFuncBody) {
        return VisitAction::WALK_CHILDREN;
    }
    std::copy_if(targetFuncBody->capturedVars.begin(), targetFuncBody->capturedVars.end(),
        std::inserter(fb.capturedVars, fb.capturedVars.begin()),
        [&mutVars](Ptr<const NameReferenceExpr> varRe) {
            return mutVars.find(varRe->GetTarget()) == mutVars.cend();
        });
    if (fb.capturedVars.empty()) {
        return VisitAction::WALK_CHILDREN;
    }
    fb.captureKind = CaptureKind::TRANSITIVE_CAPTURE;
    return VisitAction::STOP_NOW;
}

void SetFuncBodyCaptureKind(FuncBody& fb)
{
    if (!fb.body || !HasCallExpr(fb.body.get())) {
        return;
    }
    auto mutVars = CollectMutableVars(fb.body.get());
    Walker(fb.body.get(), [&fb, &mutVars](Ptr<const Node> n) {
        return SetTransitiveCaptureKind(fb, mutVars, n);
    }).Walk();
}

template <typename Func> void RunUsageCheck(const std::string& subtitle, Func func)
{
    Utils::ProfileRecorder subRecorder("CheckLegalityOfUsage", subtitle);
    func();
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckLegalityOfUsage(ASTContext& ctx, AST::Package& pkg)
{
    Utils::ProfileRecorder recorder("Post TypeCheck", "CheckLegalityOfUsage");
    RunUsageCheck("CheckValueTypeRecursive", [this, &pkg]() { CheckValueTypeRecursive(pkg); });
    RunUsageCheck("CheckLegalityOfReference", [this, &ctx, &pkg]() { CheckLegalityOfReference(ctx, pkg); });
    RunUsageCheck("CheckStaticMembersWithGeneric", [this, &pkg]() { CheckStaticMembersWithGeneric(pkg); });
    RunUsageCheck("CheckUsageOfDeprecated", [this, &pkg]() { CheckUsageOfDeprecated(pkg); });
    if (!ci->invocation.globalOptions.disableSemaVic) {
        RunUsageCheck("InitializationChecker", [this, &ctx, &pkg]() { InitializationChecker::Check(*ci, ctx, &pkg); });
    }
    RunUsageCheck("CheckGlobalVarInitialization", [this, &ctx, &pkg]() { CheckGlobalVarInitialization(ctx, pkg); });
    RunUsageCheck("CheckLegalityOfUnsafeAndInout", [this, &pkg]() { CheckLegalityOfUnsafeAndInout(pkg); });
    RunUsageCheck("CheckInheritance", [this, &pkg]() { CheckInheritance(pkg); });
    RunUsageCheck("CheckClosures", [this, &ctx, &pkg]() { CheckClosures(ctx, pkg); });
    RunUsageCheck("CheckAccessLevelValidity", [this, &pkg]() { CheckAccessLevelValidity(pkg); });
    RunUsageCheck("CheckAllInvocationHasImpl", [this, &ctx, &pkg]() { CheckAllInvocationHasImpl(ctx, pkg); });
    RunUsageCheck("CheckSubscriptLegality", [this, &pkg]() { CheckSubscriptLegality(pkg); });
}

void TypeChecker::TypeCheckerImpl::CheckStaticMemberWithGeneric(
    Decl& member, const std::vector<Ptr<Ty>>& outerGenericTys)
{
    // Static member variables, properties and init func can't depend on generic parameters of outer type.
    if (!member.TestAttr(Attribute::STATIC) || outerGenericTys.empty()) {
        return;
    }
    if (member.astKind != ASTKind::VAR_DECL && member.astKind != ASTKind::PROP_DECL && !IsStaticInitializer(member)) {
        return;
    }
    auto preVisitor = [this, outerGenericTys](Ptr<Node> node) {
        if (node == nullptr || (!Is<RefExpr>(node) && !Is<RefType>(node))) {
            return VisitAction::WALK_CHILDREN;
        }
        // Because static member var and prop cannot themselves declare generic parameters, static member var and
        // prop cannot contain any outside generic types.
        if (node->GetTy() && node->GetTy()->HasGeneric() && !node->begin.IsZero()) {
            std::set<Ptr<Ty>> targetTys;
            for (auto& usedTy : node->GetTy()->GetGenericTyArgs()) {
                if (usedTy && Utils::In(usedTy, outerGenericTys)) {
                    targetTys.emplace(usedTy);
                    break;
                }
            }
            Sema::DiagForStaticVariableDependsGeneric(diag, *node, targetTys);
            return VisitAction::SKIP_CHILDREN;
        }
        // If the static var/let's initialization expression contains a static member function call of a generic class,
        // it's also not legal.
        // eg: class A<T> { static func foo() {1}; static var a = foo(); }
        //     'static var a = foo()' will be same as 'static var a = A<T>.foo()'.
        auto ref = node->astKind == ASTKind::REF_EXPR ? StaticCast<RefExpr>(node)->ref : StaticCast<RefType>(node)->ref;
        auto needDiag = ref.target &&
            (ref.target->astKind == ASTKind::FUNC_DECL || ref.target->astKind == ASTKind::PROP_DECL) &&
            ref.target->TestAttr(Attribute::STATIC) && ref.target->outerDecl && ref.target->outerDecl->GetTy() &&
            ref.target->outerDecl->GetTy()->HasGeneric();
        if (needDiag) {
            Sema::DiagForStaticVariableDependsGeneric(diag, *node, ref.target->outerDecl->GetTy()->GetGenericTyArgs());
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&member, preVisitor).Walk();
}

void TypeChecker::TypeCheckerImpl::CheckStaticMembersWithGeneric(const Package& pkg)
{
    IterateToplevelDecls(pkg, [this](auto& decl) {
        if (!decl || !decl->generic || decl->generic->typeParameters.empty()) {
            return;
        }
        for (auto& member : decl->GetMemberDecls()) {
            std::vector<Ptr<Ty>> outersideGenericParamTys;
            for (auto& tp : decl->generic->typeParameters) {
                CJC_ASSERT(tp && Ty::IsTyCorrect(tp->GetTy()));
                outersideGenericParamTys.emplace_back(tp->GetTy());
            }
            CheckStaticMemberWithGeneric(*member, outersideGenericParamTys);
        }
    });
}

void TypeChecker::TypeCheckerImpl::CheckValueTypeRecursive(const Package& pkg)
{
    IterateToplevelDecls(pkg, [this](auto& decl) {
        if (decl->astKind == ASTKind::ENUM_DECL || decl->astKind == ASTKind::STRUCT_DECL) {
            CheckValueTypeRecursiveDFS(decl.get());
        }
    });
    for (auto& instantiatedDecl : pkg.genericInstantiatedDecls) {
        if (instantiatedDecl->astKind == ASTKind::ENUM_DECL || instantiatedDecl->astKind == ASTKind::STRUCT_DECL) {
            CheckValueTypeRecursiveDFS(instantiatedDecl.get());
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckClosures(const ASTContext& ctx, Node& node) const
{
    // 1. mark all reference capture status.
    Walker(&node, [&ctx, this](auto node) {
        if (auto ma = DynamicCast<MemberAccess*>(node); ma && ma->baseExpr && IsThisOrSuper(*ma->baseExpr)) {
            MarkAndCheckRefExprVarCaptureStatus(ctx, *ma);
        } else if (auto re = DynamicCast<RefExpr*>(node); re) {
            MarkAndCheckRefExprVarCaptureStatus(ctx, *re);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    // 2. set all funcBody capture status after every direct capture has been marked.
    Walker(&node, nullptr, [](auto node) {
        if (auto fb = DynamicCast<FuncBody*>(node); fb) {
            SetFuncBodyCaptureKind(*fb);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();

    // 3. diagnose for invalid capture.

    // We produce different error messages for lambdas written by the user and lambdas introduced by the parser for
    // spawn- and try-handle-expressions.
    // To distinguish these, we maintain `lambdaSourceStack` so as to remember what kind of expression the parent of the
    // lambda expression is.
    auto asLambdaSource = [](Ptr<Node> n) {
        switch (n->astKind) {
            case ASTKind::SPAWN_EXPR:
                return LambdaSource::SPAWN;
                break;
            case ASTKind::TRY_EXPR:
                // NB: The special try-handle error message only applies to try-, handle-, and finally-blocks in
                // try-expressions with at least one handle-block.
                // Luckily, however, when there is no handle-block, the parent of any potential lambda is a block node
                // instead of a TryExpr.
                return LambdaSource::TRY_HANDLE;
                break;
            default:
                return LambdaSource::USER;
                break;
        }
    };
    std::vector<LambdaSource> lambdaSourceStack;
    lambdaSourceStack.push_back(asLambdaSource(&node));

    auto preAction = [&ctx, &lambdaSourceStack, &asLambdaSource, this](Ptr<Node> n) {
        CheckLegalUseOfClosure(ctx, *n, lambdaSourceStack.back());
        lambdaSourceStack.push_back(asLambdaSource(n));
        return VisitAction::WALK_CHILDREN;
    };
    auto postAction = [&lambdaSourceStack](Ptr<Node>) {
        lambdaSourceStack.pop_back();
        return VisitAction::WALK_CHILDREN;
    };

    Walker(&node, preAction, postAction).Walk();
}

void TypeChecker::TypeCheckerImpl::CheckSubscriptLegality(Node& node)
{
    auto postVisit = [this](Ptr<Node> node) -> VisitAction {
        if (node->astKind != ASTKind::SUBSCRIPT_EXPR) {
            return VisitAction::WALK_CHILDREN;
        }
        auto se = StaticCast<SubscriptExpr*>(node);
        // Checking the Validity of VArray Subscript Access
        if (!se->baseExpr || !Ty::IsTyCorrect(se->baseExpr->GetTy()) || !Is<VArrayTy>(se->baseExpr->GetTy())) {
            return VisitAction::WALK_CHILDREN;
        }
        auto varrTy = StaticCast<VArrayTy*>(se->baseExpr->GetTy());
        CJC_ASSERT(!se->indexExprs.empty());
        if (se->indexExprs[0]->isConst) {
            auto index = se->indexExprs[0]->constNumValue.asInt;
            if (index.IsOutOfRange()) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (index.Uint64() >= static_cast<uint64_t>(varrTy->size) && se->ShouldDiagnose(true)) {
                auto builder = diag.Diagnose(*se, DiagKind::sema_builtin_index_in_bound, VARRAY_NAME);
                std::string hint;
                if (index.Int64() < 0) {
                    hint = "'VArray' index can not be negative";
                } else {
                    hint = "'VArray' index " + index.GetValue() + " is past the end of 'VArray' (which contains " +
                        std::to_string(varrTy->size) + " elements)";
                }
                builder.AddHint(*se->indexExprs[0], hint);
                return VisitAction::SKIP_CHILDREN;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&node, nullptr, postVisit);
    walker.Walk();
}

void TypeChecker::TypeCheckerImpl::CheckAllInvocationHasImpl(const ASTContext& ctx, Node& node)
{
    auto preVisit = [this, &ctx](Ptr<Node> node) -> VisitAction {
        if (node->astKind != ASTKind::MEMBER_ACCESS) {
            return VisitAction::WALK_CHILDREN;
        }
        auto& ma = *RawStaticCast<MemberAccess*>(node);
        auto target = ma.GetTarget();
        if (!target) {
            return VisitAction::WALK_CHILDREN;
        }
        bool isInterfaceAccess = ma.baseExpr && ma.baseExpr->GetTy() && ma.baseExpr->GetTy()->IsInterface();
        bool isStaticFuncOrProp = target->TestAttr(Attribute::STATIC) && target->IsFuncOrProp() && target->outerDecl;
        if (isInterfaceAccess && isStaticFuncOrProp) {
            std::unordered_set<Ptr<AST::Decl>> traversedDecls = {};
            MultiTypeSubst typeMapping;
            if (ma.matchedParentTy && target->outerDecl->GetTy()) {
                typeMapping = promotion.GetPromoteTypeMapping(*ma.matchedParentTy, *target->outerDecl->GetTy());
            }
            auto baseDecl = Ty::GetDeclPtrOfTy(ma.baseExpr->GetTy());
            MultiTypeSubst typeMapping1;
            if (baseDecl->GetTy()) {
                typeMapping1 = promotion.GetPromoteTypeMapping(*ma.baseExpr->GetTy(), *baseDecl->GetTy());
            }
            typeMapping.merge(typeMapping1);
            auto ret = CheckInvokeTargetHasImpl(ctx, *ma.baseExpr->GetTy(), *target, typeMapping, traversedDecls);
            if (ret.first && ret.second) {
                auto retTarget = ret.second->GetTarget();
                std::string strType = retTarget->IsFunc() ? "function" : "property";
                std::string strNote = "indirect use of unimplemented " + strType + " reference here";
                auto builder = diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_interface_call_with_unimplemented_call, ma, strType, retTarget->identifier);
                builder.AddNote(MakeRange(ret.second->begin, ret.second->end), strNote);
                return VisitAction::SKIP_CHILDREN;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&node, preVisit);
    walker.Walk();
}
} // namespace Cangjie
