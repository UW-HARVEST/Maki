#include "IncludeCollector.hh"

namespace cpp2c
{
    void IncludeCollector::InclusionDirective(
        clang::SourceLocation HashLoc,
        const clang::Token &IncludeTok,
        llvm::StringRef FileName,
        bool IsAngled,
        clang::CharSourceRange FilenameRange,
        clang::OptionalFileEntryRef File,
        llvm::StringRef SearchPath,
        llvm::StringRef RelativePath,
        const clang::Module *Imported,
        clang::SrcMgr::CharacteristicKind FileType)
    {
        if (File.has_value())
        {
            // Implicit conversion from FileEntryRef to const clang::FileEntry * is used here.
            IncludeEntriesLocs.emplace_back(*File, HashLoc);
        }
        else
        {
            IncludeEntriesLocs.emplace_back(nullptr, HashLoc);
        }
    }

} // namespace cpp2c
