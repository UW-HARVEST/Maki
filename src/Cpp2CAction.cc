#include <filesystem>
#include <fstream>
#include <string>

#include "Cpp2CAction.hh"
#include "Cpp2CASTConsumer.hh"

#include "json.hpp"

namespace cpp2c
{
    std::unique_ptr<clang::ASTConsumer>
    Cpp2CAction::CreateASTConsumer(clang::CompilerInstance &CI,
                                   llvm::StringRef InFile)
    {
        return std::make_unique<cpp2c::Cpp2CASTConsumer>(CI, std::move(codeIntervalAnalysisTasks));
    }

    bool Cpp2CAction::ParseArgs(const clang::CompilerInstance &CI,
                                const std::vector<std::string> &arg)
    {
        // Allow an optional argument "<code_interval_analysis_tasks_json_path>"
        static std::string optionName = "code_interval_analysis_tasks_json_path";
        codeIntervalAnalysisTasks = {};
        for (int i = 0; i < arg.size(); ++i)
        {
            if (arg[i].find(optionName + "=") != 0)
                continue; // Not the option we are looking for
            // Extract the path from the argument
            std::string pathStr = arg[i].substr(optionName.size() + 1);
            if (pathStr.empty())
            {
                CI.getDiagnostics().Report(clang::diag::err_cannot_open_file)
                    << "Empty path provided for " << optionName;
                return false;
            }
            // Store the path in a static variable or use it as needed
            std::filesystem::path path(pathStr);
            // Read the JSON file and process it as needed
            try
            {
                std::ifstream file(path);
                if (!file.is_open())
                {
                    CI.getDiagnostics().Report(clang::diag::err_cannot_open_file)
                        << "Failed to open file: " << pathStr;
                    return false;
                }
                nlohmann::json jsonData;
                file >> jsonData;
                file.close();
                codeIntervalAnalysisTasks = jsonData.get<std::vector<Cpp2CASTConsumer::CodeIntervalAnalysisTask>>();
            }
            catch (const std::exception &e)
            {
                CI.getDiagnostics().Report(clang::diag::err_cannot_open_file)
                    << "Error reading JSON file: " << e.what();
                return false;
            }
            break;
        }
        return true;
    }

    clang::PluginASTAction::ActionType Cpp2CAction::getActionType()
    {
        return clang::PluginASTAction::ActionType::AddBeforeMainAction;
    }

    static clang::FrontendPluginRegistry::Add<Cpp2CAction>
        X("cpp2c", "Extract macro typing information");
} // namespace cpp2c
