#pragma once

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/ASTTypeTraits.h"

#include <optional>

namespace cpp2c
{
    class DeclStmtTypeLoc
    {
    private:
        inline void assertOneNonNull();
        inline void syncTypeLocPointer();

    public:
        const clang::Decl *D = nullptr;
        const clang::Stmt *ST = nullptr;
        const clang::TypeLoc *TL = nullptr;

    private:
        std::optional<clang::TypeLoc> StoredTypeLoc;

    public:
        DeclStmtTypeLoc() = default;

        DeclStmtTypeLoc(const clang::Decl *D);
        DeclStmtTypeLoc(const clang::Stmt *ST);
        DeclStmtTypeLoc(const clang::TypeLoc *TL);

        DeclStmtTypeLoc(const DeclStmtTypeLoc &Other);
        DeclStmtTypeLoc(DeclStmtTypeLoc &&Other) noexcept;
        DeclStmtTypeLoc &operator=(const DeclStmtTypeLoc &Other);
        DeclStmtTypeLoc &operator=(DeclStmtTypeLoc &&Other) noexcept;

        void dump();

        clang::SourceRange getSourceRange();

        bool operator==(const DeclStmtTypeLoc &Other) const
        {
            if (D != Other.D || ST != Other.ST)
                return false;
            if (TL == nullptr && Other.TL == nullptr)
                return true;
            if ((TL == nullptr) != (Other.TL == nullptr))
                return false;
            return TL->getOpaqueData() == Other.TL->getOpaqueData();
        }

        clang::DynTypedNode getDynTypedNode() const
        {
            if (D)
                return clang::DynTypedNode::create(*D);
            else if (ST)
                return clang::DynTypedNode::create(*ST);
            else if (TL)
                return clang::DynTypedNode::create(*TL);
            else
                return clang::DynTypedNode();
        }
    };
} // namespace cpp2c
