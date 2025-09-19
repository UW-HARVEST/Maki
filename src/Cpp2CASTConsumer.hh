#pragma once

#include "json.hpp"

#include "MacroForest.hh"
#include "IncludeCollector.hh"
#include "DefinitionInfoCollector.hh"

#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"

namespace cpp2c
{
    struct CodeRangeAnalysisTask
    {
        int beginLine;
        int beginCol;
        int endLine;
        int endCol;
        nlohmann::json extraInfo;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(CodeRangeAnalysisTask, beginLine, beginCol, endLine, endCol, extraInfo)

        clang::SourceRange getSourceRange(clang::SourceManager & SM) const
        {
            clang::SourceLocation BeginLoc = SM.translateLineCol(SM.getMainFileID(), beginLine, beginCol);
            clang::SourceLocation EndLoc = SM.translateLineCol(SM.getMainFileID(), endLine, endCol);
            return clang::SourceRange(BeginLoc, EndLoc);
        }

        std::string toString() const
        {
            std::ostringstream oss;
            oss << "CodeRangeAnalysisTask: "
                << "  Begin: " << beginLine << ":" << beginCol << "\n"
                << "  End: " << endLine << ":" << endCol << "\n"
                << "  Extra Info: " << extraInfo.dump(4) << "\n";
            return oss.str();
        }
    };

    class Cpp2CASTConsumer : public clang::ASTConsumer
    {
    public:
        Cpp2CASTConsumer
        (
            clang::CompilerInstance &CI,
            std::vector<CodeRangeAnalysisTask> codeRangeAnalysisTasks
        );
        void HandleTranslationUnit(clang::ASTContext &Ctx) override;

    private:
        cpp2c::MacroForest *MF;
        cpp2c::IncludeCollector *IC;
        cpp2c::DefinitionInfoCollector *DC;

        std::vector<CodeRangeAnalysisTask> codeRangeAnalysisTasks;
    };

    template <typename T>
    inline std::function<bool(const clang::Stmt *)> stmtIsA()
    {
        return [](const clang::Stmt *ST)
        { return llvm::isa_and_nonnull<T>(ST); };
    }
} // namespace cpp2c
