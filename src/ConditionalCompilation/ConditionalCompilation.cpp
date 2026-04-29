// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements macro conditional compilation related apis for compiler.
 */

#include <algorithm>
#include <functional>
#include <optional>
#include <regex>
#include <utility>

#include "ConditionalCompilationImpl.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/ConditionalCompilation/ConditionalCompilation.h"

using namespace Cangjie::FileUtil;
using namespace Cangjie::Triple;

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
const std::string BACKEND_STR = "backend";
const std::string ARCH_STR = "arch";
const std::string OS_STR = "os";
const std::string CJC_VERSION_STR = "cjc_version";
const std::string DEBUG_STR = "debug";
const std::string TEST_STR = "test";
const std::string ENV_STR = "env";
const std::string CONDITION_TRUE = "1"; // Used in debug and test condition variable.
constexpr size_t CACHE_SEPARATOR_SIZE = sizeof('\0');
constexpr size_t JUDGE_CONDITION_CACHE_KEY_EXTRA_SIZE = CACHE_SEPARATOR_SIZE + sizeof(char) + CACHE_SEPARATOR_SIZE;
// Condition value map.
const std::unordered_set<std::string> TARGET_CONDITION = {
    BACKEND_STR,
    ARCH_STR,
    OS_STR,
    CJC_VERSION_STR,
    DEBUG_STR,
    TEST_STR,
    ENV_STR,
};
// Condition supported op map.
const std::unordered_map<std::string, std::vector<TokenKind>> CONDITION_OP = {
    {
        BACKEND_STR,
        {
            TokenKind::EQUAL,
            TokenKind::NOTEQ,
        },
    },
    {
        ARCH_STR,
        {
            TokenKind::EQUAL,
            TokenKind::NOTEQ,
        },
    },
    {
        OS_STR,
        {
            TokenKind::EQUAL,
            TokenKind::NOTEQ,
        },
    },
    {
        CJC_VERSION_STR,
        {
            TokenKind::EQUAL,
            TokenKind::NOTEQ,
            TokenKind::GT,
            TokenKind::LT,
            TokenKind::GE,
            TokenKind::LE,
        },
    },
    {
        DEBUG_STR,
        {},
    },
    {
        TEST_STR,
        {},
    },
    {
        ENV_STR,
        {
            TokenKind::EQUAL,
            TokenKind::NOTEQ,
        }
    }
};
// Condition supported values map.
const std::map<std::string, std::vector<std::string>> CONDITION_VALUES = {
    {
        BACKEND_STR,
        {
            "cjnative",
        },
    },
    {
        ARCH_STR,
        {
            "x86_64",
            "aarch64",
            "arm",
        },
    },
    {
        OS_STR, // consistent with the official style.
        {
            "Windows",
            "Linux",
            "macOS",
            "iOS",
        },
    },
    {
        ENV_STR,
        {
            "",
            "gnu",
            "ohos",
            "simulator",
            "android",
        }
    },
};

template <typename T, typename Pred> void EraseConditionalIf(T& container, Pred pred)
{
    container.erase(std::remove_if(container.begin(), container.end(), pred), container.end());
}

std::string MakeJudgeConditionCacheKey(const std::string& condition, TokenKind op, const std::string& right)
{
    std::string key;
    key.reserve(condition.size() + right.size() + JUDGE_CONDITION_CACHE_KEY_EXTRA_SIZE);
    key.append(condition);
    key.push_back('\0');
    key.push_back(static_cast<char>(static_cast<unsigned char>(op)));
    key.push_back('\0');
    key.append(right);
    return key;
}

void AppendCacheField(std::string& key, const std::string& field)
{
    key.append(field);
    key.push_back('\0');
}

template <typename T> void AppendEnumCacheField(std::string& key, T value)
{
    key.append(std::to_string(static_cast<unsigned>(value)));
    key.push_back('\0');
}

bool AppendConditionExprCacheKey(const Expr& condition, std::string& key)
{
    AppendEnumCacheField(key, condition.astKind);
    switch (condition.astKind) {
        case ASTKind::BINARY_EXPR: {
            auto& be = static_cast<const BinaryExpr&>(condition);
            if (be.leftExpr == nullptr || be.rightExpr == nullptr) {
                return false;
            }
            AppendEnumCacheField(key, be.op);
            return AppendConditionExprCacheKey(*be.leftExpr, key) && AppendConditionExprCacheKey(*be.rightExpr, key);
        }
        case ASTKind::UNARY_EXPR: {
            auto& ue = static_cast<const UnaryExpr&>(condition);
            if (ue.expr == nullptr) {
                return false;
            }
            AppendEnumCacheField(key, ue.op);
            return AppendConditionExprCacheKey(*ue.expr, key);
        }
        case ASTKind::REF_EXPR: {
            auto& re = static_cast<const RefExpr&>(condition);
            AppendCacheField(key, re.ref.identifier.Val());
            return true;
        }
        case ASTKind::PAREN_EXPR: {
            auto& pe = static_cast<const ParenExpr&>(condition);
            if (pe.expr == nullptr) {
                return false;
            }
            return AppendConditionExprCacheKey(*pe.expr, key);
        }
        case ASTKind::LIT_CONST_EXPR: {
            auto& lit = static_cast<const LitConstExpr&>(condition);
            AppendEnumCacheField(key, lit.kind);
            key.push_back(lit.siExpr != nullptr ? '1' : '0');
            key.push_back('\0');
            AppendCacheField(key, lit.stringValue);
            return true;
        }
        default:
            return false;
    }
}

std::optional<std::string> MakeConditionExprCacheKey(const Expr& condition)
{
    switch (condition.astKind) {
        case ASTKind::BINARY_EXPR:
        case ASTKind::UNARY_EXPR:
        case ASTKind::REF_EXPR:
        case ASTKind::PAREN_EXPR:
            break;
        default:
            return std::nullopt;
    }
    std::string key;
    if (!AppendConditionExprCacheKey(condition, key)) {
        return std::nullopt;
    }
    return key;
}
} // namespace

static auto GetVersionUInt(const std::string& version) -> uint32_t
{
    std::string res = "";
    const int digitaSize = 2;
    auto digitals = Utils::SplitString(version, ".");
    for (const auto& it : digitals) {
        if (it.size() == digitaSize) {
            res += it;
        } else if (it.size() == 1) {
            res += "0" + it;
        }
    }
    int32_t iRes = std::stoi(res);
    CJC_ASSERT(iRes >= 0);
    return static_cast<uint32_t>(iRes);
}

ConditionalCompilationImpl::ConditionalCompilationImpl(CompilerInstance* c)
    : ci(c),
      backendType(ci->invocation.globalOptions.backend),
      triple(ci->invocation.globalOptions.target),
      cjcVersion(GetVersionUInt(CANGJIE_VERSION)),
      debug(ci->invocation.globalOptions.enableCompileDebug),
      test(ci->invocation.globalOptions.enableCompileTest),
      passedCondition(ci->invocation.globalOptions.passedWhenKeyValue)
{
    InitBuiltinConditionCache();
    // in cjc cfg has been parsed, in lsp need parse and check cfg here.
    if (ci->invocation.globalOptions.enableMacroInLSP) {
        if (!passedCondition.empty()) {
            for (auto it : passedCondition) {
                if (TARGET_CONDITION.count(it.first) != 0) {
                    ci->diag.DiagnoseRefactor(DiagKindRefactor::dirver_cfg_same_with_builtin, DEFAULT_POSITION);
                }
            }
            if (!ci->invocation.globalOptions.passedWhenCfgPaths.empty()) {
                ci->diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_path_ignored, DEFAULT_POSITION);
            }
        } else {
            const std::string cfgFileName("cfg.toml");
            for (auto& path : ci->invocation.globalOptions.passedWhenCfgPaths) {
                std::string filePath = JoinPath(path, cfgFileName);
                if (!FileExist(filePath)) {
                    ci->diag.DiagnoseRefactor(
                        DiagKindRefactor::driver_warning_no_such_file, DEFAULT_POSITION, filePath);
                    continue;
                }
                (void)SetupConditionalCompilationCfgFromFile(filePath, passedCondition, ci->diag);
                return;
            }
            // parse cfg.toml in default path
            std::string defaultCfgFilePath = ci->invocation.globalOptions.packagePaths.empty()
                ? cfgFileName
                : JoinPath(ci->invocation.globalOptions.packagePaths[0], cfgFileName);
            if (!FileExist(defaultCfgFilePath)) {
                return;
            }
            (void)SetupConditionalCompilationCfgFromFile(defaultCfgFilePath, passedCondition, ci->diag);
        }
    }
}

void ConditionalCompilationImpl::InitBuiltinConditionCache()
{
    builtinConditionCache.emplace(ARCH_STR, GetArchType());
    builtinConditionCache.emplace(BACKEND_STR, GetBackendType());
    builtinConditionCache.emplace(CJC_VERSION_STR, GetCJCVersion());
    builtinConditionCache.emplace(DEBUG_STR, GetDebug());
    builtinConditionCache.emplace(ENV_STR, GetEnv());
    builtinConditionCache.emplace(TEST_STR, GetTest());
    builtinConditionCache.emplace(OS_STR, GetOSType());
}

static inline auto IsLogicBinaryExpr(const BinaryExpr& expr) -> bool
{
    return expr.op >= TokenKind::AND && expr.op <= TokenKind::OR;
}

static inline auto IsJudgeBinaryExpr(const BinaryExpr& expr) -> bool
{
    return expr.op >= TokenKind::NOTEQ && expr.op <= TokenKind::EQUAL;
}

static inline auto IsRangeBinaryExpr(const BinaryExpr& expr) -> bool
{
    return expr.op >= TokenKind::LT && expr.op <= TokenKind::GE;
}

std::string ConditionalCompilationImpl::GetOSType() const
{
    switch (triple.os) {
        case Triple::OSType::DARWIN:
            return "macOS";
        case Triple::OSType::IOS:
            return "iOS";
        case Triple::OSType::WINDOWS:
            return "Windows";
        case Triple::OSType::LINUX:
            return "Linux";
        case Triple::OSType::UNKNOWN:
        default:
            break;
    }
    return triple.OSToString();
}

const std::string* ConditionalCompilationImpl::GetUserDefinedInfoByName(const std::string& name) const
{
    auto found = passedCondition.find(name);
    if (found == passedCondition.end()) {
        return nullptr; // "" is one of env options.
    }

    return &found->second;
}

bool ConditionalCompilationImpl::EvalLogicBinaryExpr(const BinaryExpr& be)
{
    if (be.op == TokenKind::AND) {
        return EvalConditionExpr(be.leftExpr.operator*()) && EvalConditionExpr(be.rightExpr.operator*());
    } else {
        return EvalConditionExpr(be.leftExpr.operator*()) || EvalConditionExpr(be.rightExpr.operator*());
    }
}

bool ConditionalCompilationImpl::ConditionCheck(
    const std::string& conditionStr, const Position& begin, const std::string& right)
{ // If condition is not builtin and not user defined, break it.
    auto isBuiltin = TARGET_CONDITION.count(conditionStr) > 0;
    auto isUserDefined = !isBuiltin && passedCondition.count(conditionStr) == 1;
    if (!isBuiltin && !isUserDefined) {
        (void)ci->diag.DiagnoseRefactor(
            DiagKindRefactor::conditional_compilation_not_support_this_condition, begin, conditionStr);
        return false;
    }
    // Check `arch`, `env`, `backend` and `os` condition value.
    auto conditionValues = CONDITION_VALUES.find(conditionStr);
    if (conditionValues != CONDITION_VALUES.end() && !Utils::In(right, conditionValues->second)) {
        std::string supportedValues = "";
        for (const auto& it : conditionValues->second) {
            supportedValues += it + " ";
        }
        supportedValues.pop_back();
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_not_support_builtin_value, begin,
            conditionStr, right, supportedValues);
        return false;
    }
    // Check `cjc_version`.
    if (conditionStr == CJC_VERSION_STR) {
        static const std::regex cjcVersionRegex("[0-9]{1,2}[.][0-9]{1,2}[.][0-9]{1,2}");
        if (!std::regex_match(right, cjcVersionRegex)) {
            (void)ci->diag.DiagnoseRefactor(
                DiagKindRefactor::conditional_compilation_not_support_cjc_version_format, begin);
            return false;
        }
    }

    return true;
}

bool ConditionalCompilationImpl::Eval(const BinaryExpr& expr, const std::string& left, const std::string& right) const
{
    // Eval value.
    switch (expr.op) {
        case TokenKind::EQUAL: {
            return left == right;
        }
        case TokenKind::NOTEQ: {
            return left != right;
        }
        case TokenKind::GT:
            return left > right;
        case TokenKind::LT:
            return left < right;
        case TokenKind::GE:
            return left >= right;
        default:
            return left <= right;
    }
}

bool ConditionalCompilationImpl::CheckJudgeBinaryExpr(const BinaryExpr& be) const
{
    if (be.leftExpr == nullptr || be.rightExpr == nullptr) {
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, be.begin);
        return false;
    }
    if (be.leftExpr->astKind != ASTKind::REF_EXPR) {
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, be.begin);
        return false;
    }
    if (be.rightExpr->astKind != ASTKind::LIT_CONST_EXPR) {
        (void)ci->diag.DiagnoseRefactor(
            DiagKindRefactor::conditional_compilation_invalid_condition_value, be.rightExpr->begin);
        return false;
    }
    auto right = RawStaticCast<LitConstExpr*>(be.rightExpr.get());
    if (right->kind != LitConstKind::STRING || right->siExpr) {
        (void)ci->diag.DiagnoseRefactor(
            DiagKindRefactor::conditional_compilation_invalid_condition_value, be.rightExpr->begin);
        return false;
    }
    return true;
}

bool ConditionalCompilationImpl::EvalJudgeBinaryExpr(const BinaryExpr& be)
{
    if (!CheckJudgeBinaryExpr(be)) {
        return false;
    }
    auto right = RawStaticCast<LitConstExpr*>(be.rightExpr.get());
    auto left = RawStaticCast<RefExpr*>(be.leftExpr.get());
    const auto& conditionStr = left->ref.identifier.Val();
    const auto& rightValue = right->stringValue;
    auto cacheKey = MakeJudgeConditionCacheKey(conditionStr, be.op, rightValue);
    auto cached = judgeConditionCache.find(cacheKey);
    if (cached != judgeConditionCache.end()) {
        return cached->second;
    }
    if (!ConditionCheck(conditionStr, be.begin, rightValue)) {
        return false;
    }
    auto relatedInfo = GetRelatedInfo(conditionStr);
    if (relatedInfo == nullptr) {
        return false;
    }
    // Filter not support op.
    auto conditionOps = CONDITION_OP.find(conditionStr);
    if (conditionOps != CONDITION_OP.end() && Utils::NotIn(be.op, conditionOps->second)) {
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_not_support_op, be.begin,
            conditionStr, TOKENS[static_cast<int>(be.op)]);
        return false;
    }
    // Decode cjc version to judge.
    auto evalRightValue = &rightValue;
    std::string refreshedRightValue;
    if (conditionStr == CJC_VERSION_STR) {
        refreshedRightValue = std::to_string(GetVersionUInt(rightValue));
        refreshedRightValue = RefreshVersionStr(refreshedRightValue);
        evalRightValue = &refreshedRightValue;
    }
    auto evalResult = Eval(be, *relatedInfo, *evalRightValue);
    judgeConditionCache.emplace(std::move(cacheKey), evalResult);
    return evalResult;
}

bool ConditionalCompilationImpl::EvalBinaryExpr(const BinaryExpr& be)
{
    if (IsLogicBinaryExpr(be)) {
        return EvalLogicBinaryExpr(be);
    }
    if (IsJudgeBinaryExpr(be) || IsRangeBinaryExpr(be)) {
        return EvalJudgeBinaryExpr(be);
    }
    (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, be.begin);
    return false;
}

bool ConditionalCompilationImpl::EvalParenExpr(const ParenExpr& pe)
{
    if (pe.expr == nullptr) {
        return false;
    }
    return EvalConditionExpr(pe.expr.operator*());
}

bool ConditionalCompilationImpl::EvalUnaryExpr(const UnaryExpr& ue) const
{
    if (ue.expr == nullptr || ue.expr->astKind != ASTKind::REF_EXPR) {
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, ue.begin);
        return false;
    }
    auto conditionExpr = RawStaticCast<RefExpr*>(ue.expr.get());
    if (conditionExpr == nullptr || (conditionExpr->ref.identifier != DEBUG_STR &&
        conditionExpr->ref.identifier != TEST_STR)) {
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, ue.begin);
        return false;
    }
    if (ue.op != TokenKind::NOT) { // -debug or -test should compile error
        auto builder = ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, ue.begin);
        builder.AddNote("debug and test builtin conditions only support the logic ! operator");
        return false;
    }
    auto relatedInfo = GetRelatedInfo(conditionExpr->ref.identifier);
    if (relatedInfo == nullptr) {
        return false;
    }
    return *relatedInfo != CONDITION_TRUE; // !debug or !test
}

bool ConditionalCompilationImpl::EvalRefExpr(const RefExpr& re) const
{
    if (re.ref.identifier != DEBUG_STR && re.ref.identifier != TEST_STR) {
        (void)ci->diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_invalid_condition_expr, re.begin);
        return false;
    }
    auto relatedInfo = GetRelatedInfo(re.ref.identifier);
    if (relatedInfo == nullptr) {
        return false;
    }
    return *relatedInfo == CONDITION_TRUE;
}

bool ConditionalCompilationImpl::EvalConditionExpr(const Expr& condition)
{
    switch (condition.astKind) {
        case ASTKind::BINARY_EXPR:
            return EvalBinaryExpr(static_cast<const BinaryExpr&>(condition));
        case ASTKind::UNARY_EXPR:
            return EvalUnaryExpr(static_cast<const UnaryExpr&>(condition));
        case ASTKind::REF_EXPR:
            return EvalRefExpr(static_cast<const RefExpr&>(condition));
        case ASTKind::PAREN_EXPR:
            return EvalParenExpr(static_cast<const ParenExpr&>(condition));
        default: {
            (void)ci->diag.DiagnoseRefactor(
                DiagKindRefactor::conditional_compilation_invalid_condition_expr, condition.begin);
            return false;
        }
    }
}

bool ConditionalCompilationImpl::EvalCachedConditionExpr(const Expr& condition)
{
    auto cacheKey = MakeConditionExprCacheKey(condition);
    if (!cacheKey.has_value()) {
        return EvalConditionExpr(condition);
    }
    auto cached = conditionExprCache.find(*cacheKey);
    if (cached != conditionExprCache.end()) {
        return cached->second;
    }
    auto beforeErrCnt = ci->diag.GetErrorCount();
    auto result = EvalConditionExpr(condition);
    if (beforeErrCnt == ci->diag.GetErrorCount()) {
        conditionExprCache.emplace(std::move(*cacheKey), result);
    }
    return result;
}

void ConditionalCompilationImpl::HandleConditionalCompilation(const Package& root)
{
    for (auto& file : root.files) {
        HandleFileConditionalCompilation(*file.get());
        if (file->hasMacro) {
            // update hasMacro flag
            file->hasMacro = false;
            auto action = [&file](Ptr<Node> curNode) -> VisitAction {
                if (curNode->astKind == ASTKind::MACRO_EXPAND_DECL || curNode->astKind == ASTKind::MACRO_EXPAND_EXPR ||
                    curNode->astKind == ASTKind::MACRO_EXPAND_PARAM) {
                    file->hasMacro = true;
                    return VisitAction::STOP_NOW;
                }
                return VisitAction::WALK_CHILDREN;
            };
            Walker astWalker(file, action);
            astWalker.Walk();
        }
    }
}

template <typename T> bool ConditionalCompilationImpl::EvalNodeCondition(Ptr<T> node)
{
    // Eval condition.
    auto evalResult = true;
    auto filterPred = [this, &evalResult](const auto& anno) -> bool {
        if (anno->kind != AnnotationKind::WHEN) {
            return false;
        }
        if (anno->condExpr == nullptr) {
            (void)ci->diag.DiagnoseRefactor(
                DiagKindRefactor::conditional_compilation_not_have_condition_expr, anno->begin);
            return true;
        }
        evalResult = EvalCachedConditionExpr(anno->condExpr.operator*());
        return true;
    };
    Utils::EraseIf(node->annotations, filterPred);
    return evalResult;
};

void ConditionalCompilationImpl::HandleFileConditionalCompilation(File& file)
{
    auto pred = [this](Ptr<Node> node) -> bool {
        if (auto decl = DynamicCast<Decl*>(node); decl) {
            return EvalNodeCondition<Decl>(decl);
        } else if (auto import = DynamicCast<ImportSpec*>(node); import) {
            return EvalNodeCondition<ImportSpec>(import);
        } else if (auto mc = DynamicCast<MacroExpandExpr*>(node); mc) {
            return EvalNodeCondition<MacroExpandExpr>(mc);
        }
        return true;
    };
    // Remove Nodes.
    auto conditionalAction = [&pred](Ptr<Node> curNode) -> VisitAction {
        switch (curNode->astKind) {
            case ASTKind::FILE: {
                auto file = StaticAs<ASTKind::FILE>(curNode);
                EraseConditionalIf(file->imports, std::not_fn(pred));
                EraseConditionalIf(file->decls, std::not_fn(pred));
                break;
            }
            case ASTKind::INTERFACE_BODY: {
                auto ib = StaticAs<ASTKind::INTERFACE_BODY>(curNode);
                EraseConditionalIf(ib->decls, std::not_fn(pred));
                break;
            }
            case ASTKind::CLASS_BODY: {
                auto cb = StaticAs<ASTKind::CLASS_BODY>(curNode);
                EraseConditionalIf(cb->decls, std::not_fn(pred));
                break;
            }
            case ASTKind::STRUCT_BODY: {
                auto sb = StaticAs<ASTKind::STRUCT_BODY>(curNode);
                EraseConditionalIf(sb->decls, std::not_fn(pred));
                break;
            }
            case ASTKind::ENUM_DECL: {
                auto ed = StaticAs<ASTKind::ENUM_DECL>(curNode);
                EraseConditionalIf(ed->members, std::not_fn(pred));
                EraseConditionalIf(ed->constructors, std::not_fn(pred));
                break;
            }
            case ASTKind::EXTEND_DECL: {
                auto ed = StaticAs<ASTKind::EXTEND_DECL>(curNode);
                EraseConditionalIf(ed->members, std::not_fn(pred));
                break;
            }
            case ASTKind::PROP_DECL: {
                auto pd = StaticAs<ASTKind::PROP_DECL>(curNode);
                EraseConditionalIf(pd->setters, std::not_fn(pred));
                EraseConditionalIf(pd->getters, std::not_fn(pred));
                break;
            }
            case ASTKind::FUNC_PARAM_LIST: {
                auto fpl = StaticAs<ASTKind::FUNC_PARAM_LIST>(curNode);
                EraseConditionalIf(fpl->params, std::not_fn(pred));
                break;
            }
            case ASTKind::BLOCK: {
                auto block = StaticAs<ASTKind::BLOCK>(curNode);
                EraseConditionalIf(block->body, std::not_fn(pred));
                break;
            }
            case ASTKind::MACRO_EXPAND_DECL:
            case ASTKind::MACRO_EXPAND_EXPR:
            case ASTKind::MACRO_EXPAND_PARAM:
                return VisitAction::SKIP_CHILDREN;
            default:
                break;
        }
        return VisitAction::WALK_CHILDREN;
    };

    Walker astWalker(&file, conditionalAction);
    astWalker.Walk();
}

ConditionalCompilation::ConditionalCompilation(CompilerInstance* ci) : impl{new ConditionalCompilationImpl{ci}} {}
ConditionalCompilation::~ConditionalCompilation()
{
    delete impl;
}
void ConditionalCompilation::HandleConditionalCompilation(const Package& root) const
{
    impl->HandleConditionalCompilation(root);
}
void ConditionalCompilation::HandleFileConditionalCompilation(File& file) const
{
    impl->HandleFileConditionalCompilation(file);
}

const std::string* ConditionalCompilationImpl::GetRelatedInfo(const std::string& target) const
{
    auto builtin = builtinConditionCache.find(target);
    if (builtin != builtinConditionCache.end()) {
        return &builtin->second;
    }
    return GetUserDefinedInfoByName(target);
}
