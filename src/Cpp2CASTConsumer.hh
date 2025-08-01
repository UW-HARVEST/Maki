#pragma once

#include "json.hpp"

#include "MacroForest.hh"
#include "IncludeCollector.hh"
#include "DefinitionInfoCollector.hh"

#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"

namespace cpp2c
{
    class Cpp2CASTConsumer : public clang::ASTConsumer
    {
    public:
        struct CodeIntervalAnalysisTask
        {
            std::string name;
            int beginLine;
            int beginCol;
            int endLine;
            int endCol;
            std::string extraInfo;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE(CodeIntervalAnalysisTask, name, beginLine, beginCol, endLine, endCol, extraInfo)
        };

        Cpp2CASTConsumer
        (
            clang::CompilerInstance &CI,
            std::vector<CodeIntervalAnalysisTask> codeIntervalAnalysisTasks
        );
        void HandleTranslationUnit(clang::ASTContext &Ctx) override;

    private:
        cpp2c::MacroForest *MF;
        cpp2c::IncludeCollector *IC;
        cpp2c::DefinitionInfoCollector *DC;

        std::vector<CodeIntervalAnalysisTask> codeIntervalAnalysisTasks;
    };

    template <typename T>
    inline std::function<bool(const clang::Stmt *)> stmtIsA()
    {
        return [](const clang::Stmt *ST)
        { return llvm::isa_and_nonnull<T>(ST); };
    }
} // namespace cpp2c
